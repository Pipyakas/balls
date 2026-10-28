#include "game/SessionRunner.h"

#include "sim/HybridBackend.h"

#include <algorithm>
#include <thread>

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

void captureBalls(Simulation& simulation, float slotCells, FieldSize& field, BallFrame& frame) {
    PhysicsBackend& backend = simulation.backend();
    if (backend.ballCount() == 0) {
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
              float slotCells, FieldSize& field) {
    Simulation& simulation = session.simulation();
    view.mode = session.mode();
    view.state = session.state();
    view.elapsedSeconds = session.elapsedSeconds();
    view.launchers = session.launchers();
    view.pendingSpawn = session.spawner().pendingBalls();
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
    captureBalls(simulation, slotCells, field, view.balls);
}

void runPhysics(std::shared_ptr<SessionRunner::Shared> shared, GameMode mode, LevelData level, BackendKind backend) {
    auto session = std::make_unique<GameSession>(mode, std::move(level), backend);
    uint64_t frameNumber = 0;
    uint64_t boardRevision = ~0ull;
    FieldSize field;
    Clock::time_point lastUpdate = Clock::now();
    Clock::time_point rateWindowStart = lastUpdate;
    int framesInWindow = 0;
    double framesPerSecond = 0.0;
    double lastWorkSeconds = 0.0;
    bool wasPaused = false;
    const auto tickPeriod = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(session->simulation().tuning().tickSeconds));
    auto framePeriod = tickPeriod;
    for (;;) {
        std::vector<SessionRunner::Command> commands;
        PhysicsFrameInput input;
        std::shared_ptr<SessionView> view;
        std::shared_ptr<const SessionView> previous;
        {
            std::unique_lock lock(shared->mutex);
            // One frame per simulation tick: more would step nothing and only republish the same state. A command
            // wakes the thread at once.
            if (frameNumber > 0) {
                shared->wake.wait_until(lock, lastUpdate + framePeriod,
                                        [&] { return shared->stopping || !shared->commands.empty(); });
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
        std::optional<SessionEvent> event;
        if (const auto outcome = session->takeBackendSwitchOutcome()) {
            event = SessionEvent{outcome->succeeded ? SessionEventKind::BackendSwitched : SessionEventKind::BackendFailed,
                                 outcome->kind};
        }
        const double sinceLastUpdate = secondsBetween(lastUpdate, workStart);
        lastUpdate = workStart;
        const CapacityProbe& probe = session->capacityProbe();
        const bool probing = probe.isActive() && probe.targetFps() > 0.0;
        if (!input.paused && probing) {
            // The probe asks whether a frame carrying one target interval of ticks finishes within that interval,
            // so each frame advances exactly that much and reports the longer of it and the work it took. A
            // sleep's wake-up granularity (15.6 ms on Windows by default) then cannot read as a slow frame.
            const double targetSeconds = 1.0 / probe.targetFps();
            session->reportFrameSeconds(std::max({lastWorkSeconds, targetSeconds, input.uiWorkSeconds}),
                                        std::max(lastWorkSeconds, input.uiWorkSeconds));
            session->update(static_cast<float>(targetSeconds));
        } else if (!input.paused) {
            // Coming out of a pause, the paused time is not simulated.
            const double frameSeconds = wasPaused ? 0.0 : std::min(sinceLastUpdate, maximumPhysicsFrameSeconds);
            session->reportFrameSeconds(std::max(sinceLastUpdate, input.uiFrameSeconds),
                                        std::max(lastWorkSeconds, input.uiWorkSeconds));
            session->update(static_cast<float>(frameSeconds));
        }
        wasPaused = input.paused;
        // While the capacity probe runs, frames come at about its target rate, each carrying that much simulated time.
        framePeriod = probe.isActive() && probe.targetFps() > 0.0
                          ? std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / probe.targetFps()))
                          : tickPeriod;
        fillView(*session, *view, previous.get(), boardRevision, input.visibilitySlotCells, field);
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

SessionRunner::SessionRunner(GameMode mode, LevelData level, BackendKind backend) : shared(std::make_shared<Shared>()) {
    {
        std::lock_guard lock(liveThreadsMutex());
        ++liveThreadCount();
    }
    std::thread(runPhysics, shared, mode, std::move(level), backend).detach();
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
