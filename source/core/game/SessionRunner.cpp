#include "game/SessionRunner.h"

#include "game/PhysicsRate.h"
#include "level/LevelParser.h"

#if !defined(__EMSCRIPTEN__)
#include "sim/HybridBackend.h"
#endif

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

}

struct FieldSize {
    float width = 1.0f;
    float height = 1.0f;
};

struct LoopState {
    BoardSnapshots boards;
    FieldSize field;
    PhysicsRate rate;
    uint64_t frameNumber = 0;
    bool wasPaused = false;
    bool drawing = true;
    Clock::time_point lastUpdate = Clock::now();
    Clock::duration framePeriod = Clock::duration::zero();
};

struct SessionRunner::Shared {
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false;
    PhysicsFrameInput input;
    std::vector<Command> commands;
    std::vector<SessionEvent> events;
    std::shared_ptr<const SessionView> latest;

    std::array<std::shared_ptr<SessionView>, viewPoolSize> pool;
    std::optional<LevelLoadOutcome> loadOutcome;
#if defined(__EMSCRIPTEN__)

    std::unique_ptr<GameSession> webSession;
    std::unique_ptr<LoopState> webLoop;
#endif
};

namespace {

std::shared_ptr<SessionView> freeView(std::array<std::shared_ptr<SessionView>, viewPoolSize>& pool,
                                      const std::shared_ptr<const SessionView>& latest) {
    std::shared_ptr<SessionView> chosen;
    for (std::shared_ptr<SessionView>& view : pool) {
        if (!view) {
            view = std::make_shared<SessionView>();
        }

        if (view.use_count() == 1 && view != latest) {

            view->board.reset();
            if (!chosen) {
                chosen = view;
            }
        }
    }
    return chosen ? chosen : std::make_shared<SessionView>();
}

void describeSessionState(const GameSession& session, SessionView& view) {
    const Simulation& simulation = session.simulation();
    view.mode = session.mode();
    view.state = session.state();
    view.elapsedSeconds = session.elapsedSeconds();
    view.launchers = session.launchers();
    view.pendingSpawn = session.spawner().pendingBalls();
    view.endlessSpawn = session.streamsEndlessly();
    view.autoReleasing = session.isAutoReleasing();
    view.spawnRateIndex = session.spawnRateIndex();
    view.ballLimitIndex = session.ballLimitIndex();
    view.ballLimit = session.ballLimit();
    view.usingMachineLimit = session.isUsingMachineBallLimit();
    view.machineLimit = session.machineBallLimit();
    view.backend = session.backend();
    view.pendingBackend = session.pendingBackend();
    view.refilling = session.ballsLeftToRefill();
    view.backendFault = session.lastBackendFault();
    view.deviceStepMs = simulation.backend().deviceStepMs();
#if defined(__EMSCRIPTEN__)

#else
    if (const auto* hybrid = dynamic_cast<const HybridBackend*>(&simulation.backend())) {
        view.hybrid = HybridSplit{hybrid->cpuBallCount(), hybrid->ballCount(), hybrid->cpuStepMs(), hybrid->gpuStepMs()};
    } else {
        view.hybrid.reset();
    }
#endif
    view.collisionsRequested = simulation.ballCollisionsRequested();
    view.collisionsActive = simulation.ballCollisionsActive();
    view.stats = simulation.stats();
    view.liveBalls = simulation.liveBallCount();
    view.launchX = simulation.launchX();
    view.launchY = simulation.launchY();
    view.ballRadius = simulation.tuning().ballRadius;
    view.targetTicksPerSecond = 1.0 / static_cast<double>(simulation.tuning().tickSeconds);
}

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

void physicsThreadFinished() {
    {
        std::lock_guard lock(liveThreadsMutex());
        --liveThreadCount();
    }
    liveThreadsChanged().notify_all();
}

void runSessionFrame(const std::shared_ptr<SessionRunner::Shared>& shared, GameSession& session, LoadProgress* progress,
                     LoopState& state) {
    PhysicsFrameInput input;
    std::vector<SessionRunner::Command> commands;
    std::shared_ptr<SessionView> view;
    std::shared_ptr<const SessionView> previous;
    {
        std::lock_guard lock(shared->mutex);
        input = shared->input;
        commands = std::exchange(shared->commands, {});
        previous = shared->latest;
        view = freeView(shared->pool, shared->latest);
    }
    const Clock::time_point workStart = Clock::now();
    for (SessionRunner::Command& command : commands) {
        command(session);
    }
    session.pollBackgroundWork();
    std::optional<SessionEvent> event;
    if (const auto outcome = session.takeBackendSwitchOutcome()) {
        event = SessionEvent{outcome->succeeded ? SessionEventKind::BackendSwitched : SessionEventKind::BackendFailed,
                             outcome->kind, outcome->forBallCollisions};
    }
    const double sinceLastUpdate = secondsBetween(state.lastUpdate, workStart);
    state.lastUpdate = workStart;
    int ticks = 0;
    if (!input.paused) {

        const double frameSeconds = state.wasPaused ? 0.0 : std::min(sinceLastUpdate, maximumPhysicsFrameSeconds);
        session.reportFrameWorkSeconds(std::max(state.rate.lastFrameSeconds(), input.uiWorkSeconds));
        session.update(static_cast<float>(frameSeconds));
        ticks = session.simulation().stats().ticksLastFrame;
    }
    state.wasPaused = input.paused;

    state.framePeriod =
        input.framesPerSecond > 0
            ? std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / input.framesPerSecond))
        : input.framesPerSecond < 0 ? Clock::duration::zero()
                                    : std::chrono::duration_cast<Clock::duration>(
                                          std::chrono::duration<double>(session.simulation().tuning().tickSeconds));
    if (input.drawing != state.drawing) {
        state.drawing = input.drawing;
        session.simulation().backend().setDrawing(state.drawing);
    }
    fillView(session, *view, previous.get(), state.boards, input.visibilitySlotCells, state.field);
    state.rate.frame(workStart, Clock::now(), ticks);
    view->frameNumber = ++state.frameNumber;
    view->physicsFrameMs = state.rate.lastFrameMs();
    view->physicsFramesPerSecond = state.rate.framesPerSecond();
    view->physicsTicksPerSecond = state.rate.ticksPerSecond();
    view->physicsLongestFrameMs = state.rate.longestFrameMs();
    view->physicsSlowFrames = state.rate.slowFrameCount();
    {
        std::lock_guard lock(shared->mutex);
        shared->latest = view;
        if (event) {
            shared->events.push_back(*event);
        }
    }
    if (progress && state.frameNumber == 1) {
        progress->finish();
    }
}

void runSession(std::shared_ptr<SessionRunner::Shared> shared, std::unique_ptr<GameSession> session,
                LoadProgress* progress) {
    beginLoadStep(progress, "Drawing the first frame");
    LoopState state;
    state.framePeriod = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(session->simulation().tuning().tickSeconds));
    Clock::time_point nextTick = Clock::now();
    while (true) {
        {
            std::unique_lock lock(shared->mutex);
            while (!shared->stopping && Clock::now() < nextTick && shared->commands.empty()) {
                shared->wake.wait_until(lock, nextTick);
            }
            if (shared->stopping) {
                break;
            }
        }
        runSessionFrame(shared, *session, progress, state);
        nextTick = Clock::now() + state.framePeriod;
    }
    session.reset();
    physicsThreadFinished();
}

void runPhysics(std::shared_ptr<SessionRunner::Shared> shared, GameMode mode, LevelData level, BackendKind backend,
                int ticksPerSecond) {
    runSession(shared, std::make_unique<GameSession>(mode, std::move(level), backend, ticksPerSecond), nullptr);
}

void loadAndRunPhysics(std::shared_ptr<SessionRunner::Shared> shared, GameMode mode, LevelFiles files,
                       BackendKind backend, int ticksPerSecond, std::shared_ptr<LoadProgress> progress) {
    auto level = loadLevel(files, progress.get());
    if (!level) {
        progress->finish();
        {
            std::lock_guard lock(shared->mutex);
            shared->loadOutcome = LevelLoadOutcome{false, level.error()};
        }
        physicsThreadFinished();
        return;
    }
    std::string repairNotice = levelRepairNotice(*level);
    auto session = std::make_unique<GameSession>(mode, std::move(*level), backend, ticksPerSecond, progress.get());
    {
        std::lock_guard lock(shared->mutex);
        shared->loadOutcome = LevelLoadOutcome{true, std::move(repairNotice)};
    }
    runSession(shared, std::move(session), progress.get());
}

}

#if defined(__EMSCRIPTEN__)

namespace {

void startWebSession(const std::shared_ptr<SessionRunner::Shared>& shared, std::unique_ptr<GameSession> session) {
    shared->webSession = std::move(session);
    shared->webLoop = std::make_unique<LoopState>();
    shared->webLoop->framePeriod = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(shared->webSession->simulation().tuning().tickSeconds));
}

}

SessionRunner::SessionRunner(GameMode mode, LevelData level, BackendKind backend, int ticksPerSecond)
    : shared(std::make_shared<Shared>()) {
    startWebSession(shared, std::make_unique<GameSession>(mode, std::move(level), backend, ticksPerSecond));
}

SessionRunner::SessionRunner(GameMode mode, LevelFiles files, BackendKind backend, int ticksPerSecond,
                             std::shared_ptr<LoadProgress> progress)
    : shared(std::make_shared<Shared>()) {
    if (!progress) {
        progress = std::make_shared<LoadProgress>();
    }

    auto level = loadLevel(files, progress.get());
    if (!level) {
        progress->finish();
        std::lock_guard lock(shared->mutex);
        shared->loadOutcome = LevelLoadOutcome{false, level.error()};
        return;
    }
    std::string repairNotice = levelRepairNotice(*level);
    startWebSession(shared, std::make_unique<GameSession>(mode, std::move(*level), backend, ticksPerSecond, progress.get()));
    {
        std::lock_guard lock(shared->mutex);
        shared->loadOutcome = LevelLoadOutcome{true, std::move(repairNotice)};
    }
}

#else

SessionRunner::SessionRunner(GameMode mode, LevelData level, BackendKind backend, int ticksPerSecond)
    : shared(std::make_shared<Shared>()) {
    {
        std::lock_guard lock(liveThreadsMutex());
        ++liveThreadCount();
    }
    std::thread(runPhysics, shared, mode, std::move(level), backend, ticksPerSecond).detach();
}

SessionRunner::SessionRunner(GameMode mode, LevelFiles files, BackendKind backend, int ticksPerSecond,
                             std::shared_ptr<LoadProgress> progress)
    : shared(std::make_shared<Shared>()) {
    if (!progress) {
        progress = std::make_shared<LoadProgress>();
    }
    {
        std::lock_guard lock(liveThreadsMutex());
        ++liveThreadCount();
    }
    std::thread(loadAndRunPhysics, shared, mode, std::move(files), backend, ticksPerSecond, std::move(progress)).detach();
}

#endif

SessionRunner::~SessionRunner() {
#if defined(__EMSCRIPTEN__)

    std::lock_guard lock(shared->mutex);
    shared->webSession.reset();
#else
    {
        std::lock_guard lock(shared->mutex);
        shared->stopping = true;
    }
    shared->wake.notify_one();
#endif
}

void SessionRunner::post(Command command) {
    {
        std::lock_guard lock(shared->mutex);
        shared->commands.push_back(std::move(command));
    }
    shared->wake.notify_one();
}

void SessionRunner::tick(const PhysicsFrameInput& input) {
    {
        std::lock_guard lock(shared->mutex);
        shared->input = input;
    }
#if defined(__EMSCRIPTEN__)

    if (shared->webSession) {
        runSessionFrame(shared, *shared->webSession, nullptr, *shared->webLoop);
    }
#endif
}

std::shared_ptr<const SessionView> SessionRunner::view() const {
    std::lock_guard lock(shared->mutex);
    return shared->latest;
}

void SessionRunner::setBallCollisions(bool enabled) {

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

std::optional<LevelLoadOutcome> SessionRunner::takeLoadOutcome() {
    std::lock_guard lock(shared->mutex);
    return std::exchange(shared->loadOutcome, std::nullopt);
}

void describeSession(const GameSession& session, SessionView& view, const SessionView* previous,
                     BoardSnapshots* boards) {
    describeSessionState(session, view);
    const BrickBoard& board = session.simulation().board();
    if (boards) {
        view.board = boards->publish(board, previous ? previous->board : nullptr);
        return;
    }

    if (previous && previous->board && previous->board->revision() == board.revision() && !board.isAnimating()) {
        view.board = previous->board;
    } else {
        view.board = std::make_shared<const BrickBoard>(board);
    }
}

bool SessionRunner::waitForStoppedRunners(std::chrono::milliseconds timeout) {
#if defined(__EMSCRIPTEN__)
    (void)timeout;
    return true;
#else
    std::unique_lock lock(liveThreadsMutex());
    return liveThreadsChanged().wait_for(lock, timeout, [] { return liveThreadCount() == 0; });
#endif
}
