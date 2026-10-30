#include "game/SessionRunner.h"

#include "game/PhysicsRate.h"

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

// Everything but the board and the balls, which the physics thread shares and recycles between views.
void describeSessionState(const GameSession& session, SessionView& view) {
    const Simulation& simulation = session.simulation();
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
    view.launchX = simulation.launchX();
    view.launchY = simulation.launchY();
    view.ballRadius = simulation.tuning().ballRadius;
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

void fillView(GameSession& session, SessionView& view, const SessionView* previous, BoardSnapshots& boards,
              float slotCells, FieldSize& field) {
    describeSessionState(session, view);
    const Clock::time_point publishStart = Clock::now();
    view.board = boards.publish(session.simulation().board(), previous ? previous->board : nullptr);
    view.boardPublishMs = std::chrono::duration<double, std::milli>(Clock::now() - publishStart).count();
    captureBalls(session.simulation(), slotCells, field, view.balls);
}

void runPhysics(std::shared_ptr<SessionRunner::Shared> shared, GameMode mode, LevelData level, BackendKind backend,
                int ticksPerSecond) {
    auto session = std::make_unique<GameSession>(mode, std::move(level), backend, ticksPerSecond);
    const Clock::duration tickPeriod =
        std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(session->simulation().tuning().tickSeconds));
    Clock::duration framePeriod = tickPeriod;
    PhysicsRate rate;
    uint64_t frameNumber = 0;
    BoardSnapshots boards;
    FieldSize field;
    bool wasPaused = false;
    bool drawing = true;
    Clock::time_point lastUpdate = Clock::now();
    Clock::time_point nextTick = lastUpdate;

    while (true) {
        PhysicsFrameInput input;
        std::vector<SessionRunner::Command> commands;
        std::shared_ptr<SessionView> view;
        std::shared_ptr<const SessionView> previous;
        {
            std::unique_lock lock(shared->mutex);
            while (!shared->stopping && Clock::now() < nextTick && shared->commands.empty()) {
                shared->wake.wait_until(lock, nextTick);
            }
            if (shared->stopping) {
                break;
            }
            input = shared->input;
            commands = std::exchange(shared->commands, {});
            previous = shared->latest;
            view = freeView(shared->pool, shared->latest);
        }
        nextTick = Clock::now() + framePeriod;
        const Clock::time_point workStart = Clock::now();
        for (SessionRunner::Command& command : commands) {
            command(*session);
        }
        session->pollBackgroundWork();
        std::optional<SessionEvent> event;
        if (const auto outcome = session->takeBackendSwitchOutcome()) {
            event = SessionEvent{outcome->succeeded ? SessionEventKind::BackendSwitched : SessionEventKind::BackendFailed,
                                 outcome->kind, outcome->forBallCollisions};
        }
        const double sinceLastUpdate = secondsBetween(lastUpdate, workStart);
        lastUpdate = workStart;
        if (!input.paused) {
            // Coming out of a pause, the paused time is not simulated.
            const double frameSeconds = wasPaused ? 0.0 : std::min(sinceLastUpdate, maximumPhysicsFrameSeconds);
            session->reportFrameWorkSeconds(std::max(rate.lastFrameSeconds(), input.uiWorkSeconds));
            session->update(static_cast<float>(frameSeconds));
        }
        wasPaused = input.paused;
        // Settings > Advanced > Physics frames: 0 is one frame per tick, -1 as fast as the thread can go.
        framePeriod = input.framesPerSecond > 0
                          ? std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / input.framesPerSecond))
                      : input.framesPerSecond < 0 ? Clock::duration::zero()
                                                  : tickPeriod;
        if (input.drawing != drawing) {
            drawing = input.drawing;
            session->simulation().backend().setDrawing(drawing);
        }
        fillView(*session, *view, previous.get(), boards, input.visibilitySlotCells, field);
        rate.frame(workStart, Clock::now());
        view->frameNumber = ++frameNumber;
        view->physicsFrameMs = rate.lastFrameMs();
        view->physicsFramesPerSecond = rate.framesPerSecond();
        view->physicsLongestFrameMs = rate.longestFrameMs();
        view->physicsSlowFrames = rate.slowFrameCount();
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

void SessionRunner::setBallCollisions(bool enabled) {
    // The physics thread keeps Shared alive while it runs commands, so the raw pointer cannot dangle.
    post([enabled, shared = shared.get()](GameSession& session) {
        const BallCollisionsChange change = session.setBallCollisions(enabled);
        if (change == BallCollisionsChange::None) {
            return;
        }
        SessionEvent event{SessionEventKind::BallCollisionsChanged, session.backend()};
        event.collisionsChange = change;
        std::lock_guard lock(shared->mutex);
        shared->events.push_back(event);
    });
}

std::vector<SessionEvent> SessionRunner::takeEvents() {
    std::lock_guard lock(shared->mutex);
    return std::exchange(shared->events, {});
}

void describeSession(const GameSession& session, SessionView& view, const SessionView* previous,
                     BoardSnapshots* boards) {
    describeSessionState(session, view);
    const BrickBoard& board = session.simulation().board();
    if (boards) {
        view.board = boards->publish(board, previous ? previous->board : nullptr);
        return;
    }
    // Like the physics thread's views, a board that has not changed is shared rather than copied.
    if (previous && previous->board && previous->board->revision() == board.revision() && !board.isAnimating()) {
        view.board = previous->board;
    } else {
        view.board = std::make_shared<const BrickBoard>(board);
    }
}

bool SessionRunner::waitForStoppedRunners(std::chrono::milliseconds timeout) {
    std::unique_lock lock(liveThreadsMutex());
    return liveThreadsChanged().wait_for(lock, timeout, [] { return liveThreadCount() == 0; });
}
