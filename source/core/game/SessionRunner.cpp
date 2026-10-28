#include "game/SessionRunner.h"

#include "sim/HybridBackend.h"

#include <algorithm>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

constexpr double maximumPhysicsFrameSeconds = 0.1;
constexpr size_t viewPoolSize = 3;

std::mutex& liveThreadsMutex() {
    static std::mutex mutex;
    return mutex;
}

std::condition_variable& liveThreadsChanged() {
    static std::condition_variable changed;
    return changed;
}

int& liveThreadCount() {
    static int count = 0;
    return count;
}

double secondsBetween(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double>(end - start).count();
}

// Windows wakes a waiting thread on its 15.6 ms timer tick, so a physics frame asked for every 8.3 ms ran every 15.6
// (64 a second) and one asked for every 16.7 ms every 31; raising the timer resolution is not honoured for a game
// without a visible window. The physics thread instead sleeps in slices of at most a millisecond on a
// high-resolution waitable timer, checking for commands between them, so a command still starts the next frame
// within a millisecond.
class PreciseSleeper {
public:
    PreciseSleeper() {
#if defined(_WIN32)
        timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
#endif
    }
    ~PreciseSleeper() {
#if defined(_WIN32)
        if (timer) {
            CloseHandle(timer);
        }
#endif
    }
    PreciseSleeper(const PreciseSleeper&) = delete;
    PreciseSleeper& operator=(const PreciseSleeper&) = delete;

    void sleepFor(Clock::duration duration) {
#if defined(_WIN32)
        if (timer) {
            constexpr long long hundredNanosecondsPerSecond = 10'000'000;
            LARGE_INTEGER due{};
            due.QuadPart = -std::chrono::duration_cast<std::chrono::duration<long long, std::ratio<1, hundredNanosecondsPerSecond>>>(
                                duration)
                                .count();
            if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(timer, INFINITE);
                return;
            }
        }
#endif
        std::this_thread::sleep_for(duration);
    }

private:
#if defined(_WIN32)
    HANDLE timer = nullptr;
#endif
};

constexpr auto longestSleepSlice = std::chrono::milliseconds(1);

} // namespace

struct SessionRunner::Shared {
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false;
    PhysicsFrameInput input;
    std::vector<Command> commands;
    std::vector<SessionEvent> events;
    std::shared_ptr<const SessionView> latest;
    // Views are recycled once the UI has let go of them, so publishing a frame reuses their buffers.
    std::array<std::shared_ptr<SessionView>, viewPoolSize> pool;
};

namespace {

std::shared_ptr<SessionView> freeView(std::array<std::shared_ptr<SessionView>, viewPoolSize>& pool,
                                      const std::shared_ptr<const SessionView>& latest) {
    for (std::shared_ptr<SessionView>& view : pool) {
        if (!view) {
            view = std::make_shared<SessionView>();
        }
        // One reference is the pool's own; a second is the latest view or a frame the UI is still drawing.
        if (view.use_count() == 1 && view != latest) {
            return view;
        }
    }
    return std::make_shared<SessionView>();
}

// The board is shared between views until it changes, so a frame without hits or falling bricks copies nothing.
std::shared_ptr<const BrickBoard> boardFor(const Simulation& simulation, const std::shared_ptr<const BrickBoard>& previous,
                                           uint64_t& previousRevision) {
    const BrickBoard& board = simulation.board();
    if (previous && previousRevision == board.revision() && !board.isAnimating()) {
        return previous;
    }
    previousRevision = board.revision();
    return std::make_shared<const BrickBoard>(board);
}

// The last field size the backend reported, kept across frames: the grid is sized from it, and a size that changed
// from frame to frame would make the backend rebuild its grid every frame.
struct FieldSize {
    float width = 1.0f;
    float height = 1.0f;
};

void captureBalls(Simulation& simulation, float slotCells, bool drawing, FieldSize& field, BallFrame& frame) {
    PhysicsBackend& backend = simulation.backend();
    if (backend.ballCount() == 0 || !drawing) {
        frame.packed.clear();
        return;
    }
    if (slotCells > 0.0f) {
        backend.setVisibilityGrid(visibilitySlots(field.width, slotCells), visibilitySlots(field.height, slotCells));
    }
    const BallSnapshot snapshot = backend.snapshot();
    field = {snapshot.fieldWidth, snapshot.fieldHeight};
    frame.fieldWidth = snapshot.fieldWidth;
    frame.fieldHeight = snapshot.fieldHeight;
    frame.visibilityGrid = snapshot.visibilityGrid;
    if (!snapshot.packed || snapshot.count == 0) {
        frame.packed.clear();
        return;
    }
    frame.packed.assign(snapshot.packed, snapshot.packed + snapshot.count);
}

void fillView(GameSession& session, SessionView& view, const SessionView* previous, uint64_t& boardRevision,
              float slotCells, bool drawing, FieldSize& field) {
    Simulation& simulation = session.simulation();
    view.mode = session.mode();
    view.state = session.state();
    view.elapsedSeconds = session.elapsedSeconds();
    view.launchers = session.launchers();
    view.pendingSpawn = session.spawner().pendingBalls() + session.queuedLaunches();
    view.autoReleasing = session.isAutoReleasing();
    view.spawnRateIndex = session.spawnRateIndex();
    view.ballMultiplierIndex = session.ballMultiplierIndex();
    view.ballLimitIndex = session.ballLimitIndex();
    view.ballLimit = session.ballLimit();
    view.usingMachineLimit = session.isUsingMachineBallLimit();
    view.machineLimit = session.machineBallLimit();
    view.probe = session.capacityProbe();
    view.backend = session.backend();
    view.pendingBackend = session.pendingBackend();
    view.refilling = session.ballsLeftToRefill();
    view.backendFault = session.lastBackendFault();
    view.deviceStepMs = simulation.backend().deviceStepMs();
    if (const auto* hybrid = dynamic_cast<const HybridBackend*>(&simulation.backend())) {
        view.hybrid = HybridSplit{hybrid->cpuBallCount(), hybrid->ballCount(), hybrid->cpuStepMs(), hybrid->gpuStepMs()};
    } else {
        view.hybrid.reset();
    }
    view.collisionsRequested = simulation.ballCollisionsRequested();
    view.collisionsActive = simulation.ballCollisionsActive();
    view.stats = simulation.stats();
    view.liveBalls = simulation.liveBallCount();
    view.board = boardFor(simulation, previous ? previous->board : nullptr, boardRevision);
    view.launchX = simulation.launchX();
    view.launchY = simulation.launchY();
    view.ballRadius = simulation.tuning().ballRadius;
    captureBalls(simulation, slotCells, drawing, field, view.balls);
}

void runPhysics(std::shared_ptr<SessionRunner::Shared> shared, GameMode mode, LevelData level, BackendKind backend,
                int ticksPerSecond) {
    PreciseSleeper sleeper;
    auto session = std::make_unique<GameSession>(mode, std::move(level), backend, ticksPerSecond);
    uint64_t frameNumber = 0;
    uint64_t boardRevision = ~0ull;
    FieldSize field;
    Clock::time_point lastUpdate = Clock::now();
    Clock::time_point rateWindowStart = lastUpdate;
    int framesInWindow = 0;
    double framesPerSecond = 0.0;
    double lastWorkSeconds = 0.0;
    bool wasPaused = false;
    bool probing = false;
    const double tickSeconds = session->simulation().tuning().tickSeconds;
    Clock::duration framePeriod = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(tickSeconds));
    bool unpaced = false;
    for (;;) {
        std::vector<SessionRunner::Command> commands;
        PhysicsFrameInput input;
        std::shared_ptr<SessionView> view;
        std::shared_ptr<const SessionView> previous;
        {
            std::unique_lock lock(shared->mutex);
            // One frame per simulation tick: more would step nothing and only republish the same state. A command
            // wakes the thread at once. While the capacity probe runs, frames follow each other unpaced, so the
            // probe measures what the physics thread can do rather than the tick rate.
            const int framesPerSecond = shared->input.framesPerSecond;
            unpaced = framesPerSecond < 0;
            framePeriod = std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double>(framesPerSecond > 0 ? 1.0 / framesPerSecond : tickSeconds));
            if (frameNumber > 0 && !probing && !unpaced) {
                const Clock::time_point due = lastUpdate + framePeriod;
                for (Clock::time_point now = Clock::now();
                     now < due && !shared->stopping && shared->commands.empty(); now = Clock::now()) {
                    lock.unlock();
                    sleeper.sleepFor(std::min<Clock::duration>(due - now, longestSleepSlice));
                    lock.lock();
                }
            }
            if (shared->stopping) {
                break;
            }
            commands.swap(shared->commands);
            input = shared->input;
            previous = shared->latest;
            view = freeView(shared->pool, shared->latest);
        }
        const Clock::time_point workStart = Clock::now();
        for (SessionRunner::Command& command : commands) {
            command(*session);
        }
        session->pollBackgroundWork();
        session->simulation().backend().setDrawing(input.drawing);
        std::optional<SessionEvent> event;
        if (const auto outcome = session->takeBackendSwitchOutcome()) {
            event = SessionEvent{outcome->succeeded ? SessionEventKind::BackendSwitched : SessionEventKind::BackendFailed,
                                 outcome->kind};
        }
        const double sinceLastUpdate = secondsBetween(lastUpdate, workStart);
        lastUpdate = workStart;
        if (!input.paused) {
            // Coming out of a pause, the paused time is not simulated.
            const double frameSeconds = wasPaused ? 0.0 : std::min(sinceLastUpdate, maximumPhysicsFrameSeconds);
            session->reportFrameSeconds(std::max(sinceLastUpdate, input.uiFrameSeconds),
                                        std::max(lastWorkSeconds, input.uiWorkSeconds));
            session->update(static_cast<float>(frameSeconds));
        }
        wasPaused = input.paused;
        probing = session->capacityProbe().isActive();
        fillView(*session, *view, previous.get(), boardRevision, input.visibilitySlotCells, input.drawing, field);
        const Clock::time_point workEnd = Clock::now();
        lastWorkSeconds = secondsBetween(workStart, workEnd);
        ++framesInWindow;
        if (const double windowSeconds = secondsBetween(rateWindowStart, workEnd); windowSeconds >= 0.5) {
            framesPerSecond = static_cast<double>(framesInWindow) / windowSeconds;
            framesInWindow = 0;
            rateWindowStart = workEnd;
        }
        view->frameNumber = ++frameNumber;
        view->physicsFrameMs = lastWorkSeconds * 1000.0;
        view->physicsFramesPerSecond = framesPerSecond;
        {
            std::lock_guard lock(shared->mutex);
            shared->latest = view;
            if (event) {
                shared->events.push_back(*event);
            }
        }
    }
    session.reset();
    {
        std::lock_guard lock(liveThreadsMutex());
        --liveThreadCount();
    }
    liveThreadsChanged().notify_all();
}

} // namespace

SessionRunner::SessionRunner(GameMode mode, LevelData level, BackendKind backend, int ticksPerSecond)
    : shared(std::make_shared<Shared>()) {
    {
        std::lock_guard lock(liveThreadsMutex());
        ++liveThreadCount();
    }
    std::thread(runPhysics, shared, mode, std::move(level), backend, ticksPerSecond).detach();
}

SessionRunner::~SessionRunner() {
    {
        std::lock_guard lock(shared->mutex);
        shared->stopping = true;
    }
    shared->wake.notify_one();
}

void SessionRunner::post(Command command) {
    {
        std::lock_guard lock(shared->mutex);
        shared->commands.push_back(std::move(command));
    }
    shared->wake.notify_one();
}

void SessionRunner::tick(const PhysicsFrameInput& input) {
    std::lock_guard lock(shared->mutex);
    shared->input = input;
}

std::shared_ptr<const SessionView> SessionRunner::view() const {
    std::lock_guard lock(shared->mutex);
    return shared->latest;
}

std::vector<SessionEvent> SessionRunner::takeEvents() {
    std::lock_guard lock(shared->mutex);
    return std::exchange(shared->events, {});
}

bool SessionRunner::waitForStoppedRunners(std::chrono::milliseconds timeout) {
    std::unique_lock lock(liveThreadsMutex());
    return liveThreadsChanged().wait_for(lock, timeout, [] { return liveThreadCount() == 0; });
}
