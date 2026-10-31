#include "TestFramework.h"

#include "sim/DeviceInventory.h"
#include "ui/MenuModel.h"

namespace {

std::vector<std::string> labels(const std::vector<UiOption>& options) {
    std::vector<std::string> result;
    for (const UiOption& option : options) {
        result.push_back(option.label);
    }
    return result;
}

int selectedIndex(const std::vector<UiOption>& options) {
    for (size_t index = 0; index < options.size(); ++index) {
        if (options[index].selected) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

const DebugRow* findRow(const std::vector<DebugRow>& rows, std::string_view label) {
    for (const DebugRow& row : rows) {
        if (row.label == label) {
            return &row;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("the control gadget lists the game's spawn rates, ball counts and ball limits with Auto first") {
    EXPECT((labels(spawnRateChoices(2)) == std::vector<std::string>{"10", "50", "200", "1000", "2000", "10k", "50k", "200k", "1M", "Max"}));
    EXPECT(selectedIndex(spawnRateChoices(2)) == 2);

    SessionView view;
    view.mode = GameMode::Performance;
    EXPECT((labels(ballLimitChoices(view)) == std::vector<std::string>{"Level", "Auto", "Max 2^64-1"}));
    EXPECT(selectedIndex(ballLimitChoices(view)) == 0);
    view.ballLimitIndex = static_cast<int>(BallLimitChoice::Auto);
    view.usingMachineLimit = true;
    EXPECT(isAutoBallLimit(view));
    EXPECT(selectedIndex(ballLimitChoices(view)) == 1);
}

TEST_CASE("the release button starts once and then counts the launchers left") {
    SessionView view;
    EXPECT(releaseButton(view).label == "Start (1 click)");
    EXPECT(releaseButton(view).enabled);
    view.state = SessionState::Running;
    EXPECT(releaseButton(view).label == "Auto releasing - 0 launchers left");
    EXPECT(!releaseButton(view).enabled);
}

TEST_CASE("Settings rows follow the design: orientation, off and on, every selectable backend") {
    EXPECT((labels(orientationOptions(LayoutOrientation::Portrait)) ==
            std::vector<std::string>{"Auto", "Landscape", "Portrait"}));
    EXPECT(selectedIndex(orientationOptions(LayoutOrientation::Portrait)) == 2);
    EXPECT(selectedIndex(offOnOptions(true)) == 1);

    // offeredBackend: the scoring build lists only the CUDA+AVX2 pair, whatever the CPU backend is.
    const std::vector<BackendRow> rows = defaultBackendRows(offeredBackend(fastestAvailableCpuBackend()), {});
    EXPECT(!rows.empty());
    int selected = 0;
    for (const BackendRow& row : rows) {
        selected += row.option.selected ? 1 : 0;
        EXPECT(row.option.enabled || !row.option.detail.empty());
    }
    EXPECT(selected == 1);
}

TEST_CASE("a device row offers Off first, and the last device on cannot be turned off") {
    const std::vector<PhysicalDevice>& devices = physicalDevices();
    EXPECT(!devices.empty());
    const std::vector<DevicePickRow> empty = devicePickRows({});
    EXPECT(empty.size() == devices.size());
    EXPECT(empty.front().options.front().label == "Off");
    EXPECT(empty.front().options.front().selected);

    const std::vector<BackendKind> kinds = pickableKinds(devices.front());
    EXPECT(!kinds.empty());
    const std::vector<BackendPick> one{{kinds.front(), devices.front().id}};
    const std::vector<DevicePickRow> rows = devicePickRows(one);
    EXPECT(!rows.front().options.front().enabled);
    EXPECT(rows.front().options[1].selected);
    EXPECT(!pickDeviceOption(one, rows, 0, 0));
    EXPECT(!pickDeviceOption(one, rows, 0, 1));
}

TEST_CASE("ball collisions offer Off and On with the game's hint") {
    const BallCollisionsChoice choice = ballCollisionsChoice(false);
    EXPECT((labels(choice.options) == std::vector<std::string>{"Off", "On"}));
    EXPECT(choice.options[0].selected);
    EXPECT(choice.hint.rfind("Testing only - the game rules have balls pass through each other.", 0) == 0);
    EXPECT(choice.options[1].enabled == isBackendSelectable(BackendKind::Scalar));
    EXPECT(ballCollisionsOffered() == isBackendSelectable(BackendKind::Scalar));
}

TEST_CASE("backendChoiceOffered matches the number of selectable backends") {
    EXPECT(backendChoiceOffered() == (selectableBackends().size() > 1));
}

TEST_CASE("the debug gadget shows the game's rows and hides backend rows while Advanced Settings is closed") {
    SessionView view;
    view.mode = GameMode::Performance;
    view.ballLimitIndex = static_cast<int>(BallLimitChoice::Auto);
    view.usingMachineLimit = true;
    view.stats.ballCount = 1'234'567;
    view.physicsFramesPerSecond = 60.0;
    view.physicsTicksPerSecond = 119.0;
    view.targetTicksPerSecond = 120.0;
    view.state = SessionState::Running;
    FrameTimings frames;
    const std::vector<DebugRow> closed = debugGadgetRows(view, frames, {});
    EXPECT(closed.front().kind == DebugRow::Kind::Heading);
    EXPECT(closed.front().label == "Performance");
    EXPECT(findRow(closed, "Backend") == nullptr);
    EXPECT(findRow(closed, "Ball collisions") == nullptr);
    EXPECT(findRow(closed, "Balls")->value == "1,234,567");
    EXPECT(findRow(closed, "Physics FPS") == nullptr);
    EXPECT(findRow(closed, "Tick rate") == nullptr);
    EXPECT(findRow(closed, "Physics ms")->tone == UiTone::Normal);
    SessionView behind = view;
    behind.physicsTicksPerSecond = 80.0;
    behind.state = SessionState::Running;
    EXPECT(findRow(debugGadgetRows(behind, frames, {}), "Physics ms")->tone == UiTone::Danger);
    behind.state = SessionState::Ready;
    EXPECT(findRow(debugGadgetRows(behind, frames, {}), "Physics ms")->tone == UiTone::Normal);
    EXPECT(findRow(closed, "Ball limit")->value == "Auto");
    int colorRows = 0;
    for (const DebugRow& row : closed) {
        colorRows += row.kind == DebugRow::Kind::Color ? 1 : 0;
    }
    EXPECT(colorRows == ballColorCount);

    DebugGadgetOptions open;
    open.showBackend = true;
    open.renderer = "WebGPU / Vulkan / GPU";
    view.collisionsRequested = true;
    view.backendFault = "device lost";
    const std::vector<DebugRow> shown = debugGadgetRows(view, frames, open);
    EXPECT(findRow(shown, "Backend") != nullptr);
    EXPECT(findRow(shown, "Renderer")->value == "WebGPU / Vulkan / GPU");
    if (ballCollisionsOffered()) {
        EXPECT(findRow(shown, "Ball collisions")->value == "On (unsupported)");
        EXPECT(findRow(shown, "Ball collisions")->tone == UiTone::Danger);
    } else {
        EXPECT(findRow(shown, "Ball collisions") == nullptr);
    }
    EXPECT(findRow(shown, "Fell back to CPU")->tone == UiTone::Danger);
}

TEST_CASE("gameplay toasts read the same everywhere") {
    EXPECT(backendFailedMessage(BackendKind::Cuda) == "Could not start the CUDA backend");
    EXPECT(computeGpuMessage("RTX 3070") == "Vulkan physics on RTX 3070");
    EXPECT(groupedCount(1000) == "1,000");
    EXPECT(groupedCount(999) == "999");
}

TEST_CASE("brick labels shorten counts past 9,999 to three significant digits") {
    EXPECT(brickHitLabel(9999) == "9999");
    EXPECT(brickHitLabel(12'400) == "12.4K");
    EXPECT(brickHitLabel(107'142'857) == "107M");
    EXPECT(brickHitLabel(7'500'009'375) == "7.5B");
    EXPECT(brickHitLabel(2'000'000) == "2M");
}
