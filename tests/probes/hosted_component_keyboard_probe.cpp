#include "components/components.h"
#include "core/input/input_state.h"
#include "eui/host.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* label) {
    std::fprintf(stderr, "%s hosted component keyboard: %s\n",
                 condition ? "[PASS]" : "[FAIL]", label);
    if (!condition) {
        ++failures;
    }
}

bool near(float actual, float expected) {
    return std::abs(actual - expected) <= 0.0001f;
}

bool exactTail(const std::vector<std::string>& trace,
               std::size_t start,
               std::initializer_list<const char*> expected) {
    if (trace.size() != start + expected.size()) {
        return false;
    }
    std::size_t index = start;
    for (const char* value : expected) {
        if (trace[index++] != value) {
            return false;
        }
    }
    return true;
}

bool exactFloatTail(const std::vector<float>& trace,
                    std::size_t start,
                    std::initializer_list<float> expected) {
    if (trace.size() != start + expected.size()) {
        return false;
    }
    std::size_t index = start;
    for (float value : expected) {
        if (!near(trace[index++], value)) {
            return false;
        }
    }
    return true;
}

struct ProbeState {
    int buttonActions = 0;
    bool checkboxValue = false;
    int checkboxChanges = 0;
    bool switchValue = false;
    int switchChanges = 0;
    bool radioValue = false;
    int radioChanges = 0;
    int radioGroupValue = 0;
    int radioGroupChanges = 0;
    int tabsValue = 0;
    int tabsChanges = 0;
    float sliderValue = 0.5f;
    int inputEnter = 0;
    int inputEscape = 0;
    int disabledCallbacks = 0;
    bool emptyCompositesDisabled = false;
    std::string inputValue;
    std::string currentFocus;
    std::vector<std::string> focusTrace;
    std::vector<std::string> callbackTrace;
    std::vector<float> sliderTrace;
    std::unordered_map<std::string, bool> stableFocusable;
    std::unordered_map<std::string, bool> ringStructural;
    std::unordered_map<std::string, bool> ringVisible;
};

struct FocusSpec {
    const char* focusId;
    const char* ringId;
};

constexpr FocusSpec kFocusSpecs[] = {
    {"button.bg", "button.focus"},
    {"checkbox.hit", "checkbox.focus"},
    {"switch.hit", "switch.focus"},
    {"radio.hit", "radio.focus"},
    {"radio-group", "radio-group.focus"},
    {"tabs", "tabs.focus"},
    {"slider.hit", "slider.focus"},
    {"input.hit", "input.focus"},
};

void observeContracts(eui::Ui& ui, ProbeState& state) {
    std::string focused;
    for (const FocusSpec& spec : kFocusSpecs) {
        const core::dsl::Element* target = ui.find(spec.focusId);
        const core::dsl::Element* ring = ui.find(spec.ringId);
        state.stableFocusable[spec.focusId] = target != nullptr && target->focusable;
        state.ringStructural[spec.ringId] =
            ring != nullptr && ring->border.width > 0.0f && ring->border.color.a > 0.0f &&
            !ring->interactive && !ring->focusable;
        state.ringVisible[spec.ringId] = ring != nullptr && ring->opacity > 0.5f;
        if (ui.isFocused(spec.focusId)) {
            focused = spec.focusId;
        }
    }
    const core::dsl::Element* emptyRadioGroup = ui.find("radio-group-empty");
    const core::dsl::Element* emptyTabs = ui.find("tabs-empty");
    state.emptyCompositesDisabled = emptyRadioGroup != nullptr && emptyRadioGroup->disabled &&
                                    emptyTabs != nullptr && emptyTabs->disabled;

    if (focused != state.currentFocus) {
        state.currentFocus = focused;
        if (!focused.empty()) {
            state.focusTrace.push_back(focused);
        }
    }
}

bool focusedWithRing(const ProbeState& state,
                     const std::string& focusId,
                     const std::string& ringId) {
    const auto structural = state.ringStructural.find(ringId);
    const auto visible = state.ringVisible.find(ringId);
    return state.currentFocus == focusId &&
           structural != state.ringStructural.end() && structural->second &&
           visible != state.ringVisible.end() && visible->second;
}

} // namespace

int main() {
#if !defined(_WIN32)
    std::fprintf(stderr, "[SKIP] hosted component keyboard: 当前 probe 只验证 Win32 hosted Runtime 输入链\n");
    return 0;
#else
    eui::EuiAppHost host;
    expect(host.initialize(), "host 初始化");
    if (!host.initialized()) {
        return 1;
    }

    ProbeState state;
    eui::WindowConfig config;
    config.title = "hosted-component-keyboard";
    config.pageId = "hosted-component-keyboard";
    config.role = eui::WindowRole::Popup;
    config.width = 600;
    config.height = 750;
    config.visible = false;
    config.initialPlacement = eui::WindowPlacement{true, 300, 160, 600, 750, false};
    config.compose = [&](eui::Ui& ui, const eui::Screen& screen) {
        const auto slot = [&](const std::string& id,
                              float y,
                              float height,
                              const std::function<void()>& content) {
            ui.stack(id)
                .position(20.0f, y)
                .size(520.0f, height)
                .content(content)
                .build();
        };

        ui.stack("component-root")
            .size(screen.width, screen.height)
            .content([&] {
                slot("slot.button", 16.0f, 34.0f, [&] {
                    components::button(ui, "button")
                        .size(160.0f, 34.0f)
                        .text("Button")
                        .onClick([&state] {
                            ++state.buttonActions;
                            state.callbackTrace.emplace_back("button");
                        })
                        .build();
                });
                slot("slot.button-disabled", 54.0f, 34.0f, [&] {
                    components::button(ui, "button-disabled")
                        .size(160.0f, 34.0f)
                        .text("Button disabled")
                        .disabled()
                        .onClick([&state] { ++state.disabledCallbacks; })
                        .build();
                });
                slot("slot.checkbox", 92.0f, 30.0f, [&] {
                    components::checkbox(ui, "checkbox")
                        .size(220.0f, 30.0f)
                        .text("Checkbox")
                        .checked(state.checkboxValue)
                        .onChange([&state](bool value) {
                            state.checkboxValue = value;
                            ++state.checkboxChanges;
                            state.callbackTrace.emplace_back(value ? "checkbox:1" : "checkbox:0");
                        })
                        .build();
                });
                slot("slot.checkbox-disabled", 126.0f, 30.0f, [&] {
                    components::checkbox(ui, "checkbox-disabled")
                        .size(220.0f, 30.0f)
                        .text("Checkbox disabled")
                        .disabled()
                        .onChange([&state](bool) { ++state.disabledCallbacks; })
                        .build();
                });
                slot("slot.switch", 160.0f, 30.0f, [&] {
                    components::toggleSwitch(ui, "switch")
                        .size(220.0f, 30.0f)
                        .text("Switch")
                        .checked(state.switchValue)
                        .onChange([&state](bool value) {
                            state.switchValue = value;
                            ++state.switchChanges;
                            state.callbackTrace.emplace_back(value ? "switch:1" : "switch:0");
                        })
                        .build();
                });
                slot("slot.switch-disabled", 194.0f, 30.0f, [&] {
                    components::toggleSwitch(ui, "switch-disabled")
                        .size(220.0f, 30.0f)
                        .text("Switch disabled")
                        .disabled()
                        .onChange([&state](bool) { ++state.disabledCallbacks; })
                        .build();
                });
                slot("slot.radio", 228.0f, 30.0f, [&] {
                    components::radio(ui, "radio")
                        .size(220.0f, 30.0f)
                        .text("Radio")
                        .selected(state.radioValue)
                        .onChange([&state](bool value) {
                            state.radioValue = value;
                            ++state.radioChanges;
                            state.callbackTrace.emplace_back(value ? "radio:1" : "radio:0");
                        })
                        .build();
                });
                slot("slot.radio-disabled", 262.0f, 30.0f, [&] {
                    components::radio(ui, "radio-disabled")
                        .size(220.0f, 30.0f)
                        .text("Radio disabled")
                        .disabled()
                        .onChange([&state](bool) { ++state.disabledCallbacks; })
                        .build();
                });
                slot("slot.radio-group", 296.0f, 76.0f, [&] {
                    components::radioGroup(ui, "radio-group")
                        .size(220.0f, 24.0f)
                        .gap(2.0f)
                        .items({"A", "B", "C"})
                        .selected(state.radioGroupValue)
                        .onChange([&state](int value) {
                            state.radioGroupValue = value;
                            ++state.radioGroupChanges;
                            state.callbackTrace.emplace_back("radio-group:" + std::to_string(value));
                        })
                        .build();
                });
                slot("slot.radio-group-disabled", 376.0f, 76.0f, [&] {
                    components::radioGroup(ui, "radio-group-disabled")
                        .size(220.0f, 24.0f)
                        .gap(2.0f)
                        .items({"A", "B", "C"})
                        .disabled()
                        .onChange([&state](int) { ++state.disabledCallbacks; })
                        .build();
                });
                slot("slot.tabs", 456.0f, 36.0f, [&] {
                    components::tabs(ui, "tabs")
                        .size(300.0f, 36.0f)
                        .items({"A", "B", "C"})
                        .selected(state.tabsValue)
                        .onChange([&state](int value) {
                            state.tabsValue = value;
                            ++state.tabsChanges;
                            state.callbackTrace.emplace_back("tabs:" + std::to_string(value));
                        })
                        .build();
                });
                slot("slot.tabs-disabled", 496.0f, 36.0f, [&] {
                    components::tabs(ui, "tabs-disabled")
                        .size(300.0f, 36.0f)
                        .items({"A", "B", "C"})
                        .disabled()
                        .onChange([&state](int) { ++state.disabledCallbacks; })
                        .build();
                });
                slot("slot.slider", 536.0f, 32.0f, [&] {
                    components::slider(ui, "slider")
                        .size(300.0f, 32.0f)
                        .value(state.sliderValue)
                        .step(0.25f)
                        .onChange([&state](float value) {
                            state.sliderValue = value;
                            state.sliderTrace.push_back(value);
                            state.callbackTrace.emplace_back("slider");
                        })
                        .build();
                });
                slot("slot.slider-disabled", 572.0f, 32.0f, [&] {
                    components::slider(ui, "slider-disabled")
                        .size(300.0f, 32.0f)
                        .value(0.5f)
                        .disabled()
                        .onChange([&state](float) { ++state.disabledCallbacks; })
                        .build();
                });
                slot("slot.input", 608.0f, 38.0f, [&] {
                    components::input(ui, "input")
                        .size(260.0f, 38.0f)
                        .value(state.inputValue)
                        .onChange([&state](const std::string& value) { state.inputValue = value; })
                        .onEnter([&state] {
                            ++state.inputEnter;
                            state.callbackTrace.emplace_back("input-enter");
                        })
                        .onEscape([&state] {
                            ++state.inputEscape;
                            state.callbackTrace.emplace_back("input-escape");
                        })
                        .build();
                });
                slot("slot.input-disabled", 650.0f, 38.0f, [&] {
                    components::input(ui, "input-disabled")
                        .size(260.0f, 38.0f)
                        .disabled()
                        .onChange([&state](const std::string&) { ++state.disabledCallbacks; })
                        .onEnter([&state] { ++state.disabledCallbacks; })
                        .onEscape([&state] { ++state.disabledCallbacks; })
                        .build();
                });
                slot("slot.radio-group-empty", 692.0f, 1.0f, [&] {
                    components::radioGroup(ui, "radio-group-empty")
                        .size(220.0f, 24.0f)
                        .items(std::vector<std::string>{})
                        .build();
                });
                slot("slot.tabs-empty", 697.0f, 30.0f, [&] {
                    components::tabs(ui, "tabs-empty")
                        .size(300.0f, 30.0f)
                        .items(std::vector<std::string>{})
                        .build();
                });
            })
            .build();

        observeContracts(ui, state);
    };

    const eui::WindowId id = host.createWindow(config);
    const eui::window::NativeWindowInfo native = host.nativeWindowInfo(id);
    expect(id != eui::kInvalidWindowId && native.handle != nullptr,
           "创建 hosted 组件键盘窗口");
    expect(host.showWindow(id), "显示 hosted 组件键盘窗口");
    host.tick(host.timeSeconds(), true);

    const auto queueKey = [&](core::InputKey key,
                              core::KeyAction action = core::KeyAction::Press,
                              bool shift = false) {
        core::queueKeyInput(native.handle, {key, action, {false, shift}});
        host.tick(host.timeSeconds(), false);
    };
    const auto queueKeys = [&](std::initializer_list<core::KeyEvent> keys) {
        for (const core::KeyEvent& key : keys) {
            core::queueKeyInput(native.handle, key);
        }
        host.tick(host.timeSeconds(), false);
    };
    const auto clickAt = [&](double x, double y) {
        core::queuePointerButton(native.handle, x, y, 0, true);
        core::queuePointerButton(native.handle, x, y, 0, false);
        host.tick(host.timeSeconds(), false);
    };

    const std::size_t buttonTraceStart = state.callbackTrace.size();
    queueKeys({
        {core::InputKey::Tab, core::KeyAction::Press, {false, false}},
        {core::InputKey::Enter, core::KeyAction::Press, {false, false}},
    });
    expect(focusedWithRing(state, "button.bg", "button.focus") && state.buttonActions == 1,
           "同一 tick 的 Tab 后 Enter 聚焦并激活 Button");
    queueKey(core::InputKey::Space);
    queueKeys({
        {core::InputKey::Enter, core::KeyAction::Repeat, {false, false}},
        {core::InputKey::Space, core::KeyAction::Repeat, {false, false}},
    });
    expect(state.buttonActions == 2 &&
               exactTail(state.callbackTrace, buttonTraceStart, {"button", "button"}),
           "Button Enter/Space Press 各激活一次且 Repeat 不激活");

    queueKey(core::InputKey::Tab);
    expect(focusedWithRing(state, "checkbox.hit", "checkbox.focus"),
           "Tab 跳过 disabled Button 并聚焦 Checkbox 稳定 hit ID");
    const std::size_t checkboxTraceStart = state.callbackTrace.size();
    queueKey(core::InputKey::Enter);
    queueKey(core::InputKey::Space);
    queueKey(core::InputKey::Enter, core::KeyAction::Repeat);
    queueKey(core::InputKey::Space, core::KeyAction::Repeat);
    expect(!state.checkboxValue && state.checkboxChanges == 2 &&
               exactTail(state.callbackTrace, checkboxTraceStart, {"checkbox:1", "checkbox:0"}),
           "Checkbox Enter/Space 连续切换且 Repeat 不重复回调");
    const std::size_t checkboxSameTickTraceStart = state.callbackTrace.size();
    const int checkboxSameTickChangesStart = state.checkboxChanges;
    queueKeys({
        {core::InputKey::Enter, core::KeyAction::Press, {false, false}},
        {core::InputKey::Space, core::KeyAction::Press, {false, false}},
    });
    expect(!state.checkboxValue &&
               state.checkboxChanges == checkboxSameTickChangesStart + 2 &&
               exactTail(state.callbackTrace,
                         checkboxSameTickTraceStart,
                         {"checkbox:1", "checkbox:0"}),
           "Checkbox 同一 tick 两次 Press 按累积状态精确切换两次");

    queueKey(core::InputKey::Tab);
    expect(focusedWithRing(state, "switch.hit", "switch.focus"),
           "Tab 跳过 disabled Checkbox 并聚焦 Switch 稳定 hit ID");
    const std::size_t switchTraceStart = state.callbackTrace.size();
    queueKey(core::InputKey::Enter);
    queueKey(core::InputKey::Space);
    queueKey(core::InputKey::Enter, core::KeyAction::Repeat);
    queueKey(core::InputKey::Space, core::KeyAction::Repeat);
    expect(!state.switchValue && state.switchChanges == 2 &&
               exactTail(state.callbackTrace, switchTraceStart, {"switch:1", "switch:0"}),
           "Switch Enter/Space 连续切换且 Repeat 不重复回调");
    const std::size_t switchSameTickTraceStart = state.callbackTrace.size();
    const int switchSameTickChangesStart = state.switchChanges;
    queueKeys({
        {core::InputKey::Enter, core::KeyAction::Press, {false, false}},
        {core::InputKey::Space, core::KeyAction::Press, {false, false}},
    });
    expect(!state.switchValue && state.switchChanges == switchSameTickChangesStart + 2 &&
               exactTail(state.callbackTrace,
                         switchSameTickTraceStart,
                         {"switch:1", "switch:0"}),
           "Switch 同一 tick 两次 Press 按累积状态精确切换两次");

    queueKey(core::InputKey::Tab);
    expect(focusedWithRing(state, "radio.hit", "radio.focus"),
           "Tab 跳过 disabled Switch 并聚焦 Radio 稳定 hit ID");
    const std::size_t radioTraceStart = state.callbackTrace.size();
    queueKey(core::InputKey::Enter);
    queueKey(core::InputKey::Space);
    queueKey(core::InputKey::Enter, core::KeyAction::Repeat);
    queueKey(core::InputKey::Space, core::KeyAction::Repeat);
    expect(state.radioValue && state.radioChanges == 2 &&
               exactTail(state.callbackTrace, radioTraceStart, {"radio:1", "radio:1"}),
           "Radio Enter/Space 选择且 Repeat 不重复回调");

    queueKey(core::InputKey::Tab);
    expect(focusedWithRing(state, "radio-group", "radio-group.focus"),
           "Tab 跳过 disabled Radio 并聚焦 RadioGroup 复合根 ID");
    const std::size_t groupTraceStart = state.callbackTrace.size();
    queueKey(core::InputKey::Right);
    queueKey(core::InputKey::Right, core::KeyAction::Repeat);
    queueKey(core::InputKey::Right, core::KeyAction::Repeat);
    queueKey(core::InputKey::Left);
    queueKey(core::InputKey::Down);
    queueKey(core::InputKey::Up, core::KeyAction::Repeat);
    queueKey(core::InputKey::Home);
    queueKey(core::InputKey::End, core::KeyAction::Repeat);
    expect(state.radioGroupValue == 2 && state.radioGroupChanges == 8 &&
               exactTail(state.callbackTrace,
                         groupTraceStart,
                         {"radio-group:1", "radio-group:2", "radio-group:0",
                          "radio-group:2", "radio-group:0", "radio-group:2",
                          "radio-group:0", "radio-group:2"}),
           "RadioGroup 四方向环回、Home/End 与 Repeat 自动选择顺序稳定");
    state.radioGroupValue = 0;
    host.tick(host.timeSeconds(), true);
    const std::size_t groupSameTickTraceStart = state.callbackTrace.size();
    const int groupSameTickChangesStart = state.radioGroupChanges;
    queueKeys({
        {core::InputKey::Right, core::KeyAction::Press, {false, false}},
        {core::InputKey::Right, core::KeyAction::Repeat, {false, false}},
        {core::InputKey::Down, core::KeyAction::Press, {false, false}},
        {core::InputKey::Left, core::KeyAction::Press, {false, false}},
        {core::InputKey::Home, core::KeyAction::Press, {false, false}},
        {core::InputKey::End, core::KeyAction::Repeat, {false, false}},
    });
    expect(state.radioGroupValue == 2 &&
               state.radioGroupChanges == groupSameTickChangesStart + 6 &&
               exactTail(state.callbackTrace,
                         groupSameTickTraceStart,
                         {"radio-group:1", "radio-group:2", "radio-group:0",
                          "radio-group:2", "radio-group:0", "radio-group:2"}),
           "RadioGroup 同一 tick 多方向/Home/End 按队列顺序累积选择");
    const std::size_t groupActivationTraceStart = state.callbackTrace.size();
    const int groupActivationChangesStart = state.radioGroupChanges;
    queueKeys({
        {core::InputKey::Enter, core::KeyAction::Press, {false, false}},
        {core::InputKey::Space, core::KeyAction::Press, {false, false}},
        {core::InputKey::Enter, core::KeyAction::Repeat, {false, false}},
        {core::InputKey::Space, core::KeyAction::Repeat, {false, false}},
    });
    expect(state.radioGroupValue == 2 &&
               state.radioGroupChanges == groupActivationChangesStart &&
               state.callbackTrace.size() == groupActivationTraceStart,
           "automatic-selection RadioGroup 的 Enter/Space Press/Repeat 均为 no-op");

    queueKey(core::InputKey::Tab);
    expect(focusedWithRing(state, "tabs", "tabs.focus"),
           "Tab 跳过 disabled RadioGroup 并聚焦 Tabs 复合根 ID");
    const std::size_t tabsTraceStart = state.callbackTrace.size();
    queueKey(core::InputKey::Right);
    queueKey(core::InputKey::Right, core::KeyAction::Repeat);
    queueKey(core::InputKey::Right, core::KeyAction::Repeat);
    queueKey(core::InputKey::Left);
    queueKey(core::InputKey::Home);
    queueKey(core::InputKey::End, core::KeyAction::Repeat);
    expect(state.tabsValue == 2 && state.tabsChanges == 6 &&
               exactTail(state.callbackTrace,
                         tabsTraceStart,
                         {"tabs:1", "tabs:2", "tabs:0", "tabs:2", "tabs:0", "tabs:2"}),
           "Tabs 左右环回、Home/End 与 Repeat 自动选择顺序稳定");
    state.tabsValue = 0;
    host.tick(host.timeSeconds(), true);
    const std::size_t tabsSameTickTraceStart = state.callbackTrace.size();
    const int tabsSameTickChangesStart = state.tabsChanges;
    queueKeys({
        {core::InputKey::Right, core::KeyAction::Press, {false, false}},
        {core::InputKey::Right, core::KeyAction::Repeat, {false, false}},
        {core::InputKey::Right, core::KeyAction::Repeat, {false, false}},
        {core::InputKey::Left, core::KeyAction::Press, {false, false}},
        {core::InputKey::Home, core::KeyAction::Press, {false, false}},
        {core::InputKey::End, core::KeyAction::Repeat, {false, false}},
    });
    expect(state.tabsValue == 2 && state.tabsChanges == tabsSameTickChangesStart + 6 &&
               exactTail(state.callbackTrace,
                         tabsSameTickTraceStart,
                         {"tabs:1", "tabs:2", "tabs:0", "tabs:2", "tabs:0", "tabs:2"}),
           "Tabs 同一 tick 多方向/Home/End 按队列顺序累积选择");
    const std::size_t tabsActivationTraceStart = state.callbackTrace.size();
    const int tabsActivationChangesStart = state.tabsChanges;
    queueKeys({
        {core::InputKey::Enter, core::KeyAction::Press, {false, false}},
        {core::InputKey::Space, core::KeyAction::Press, {false, false}},
        {core::InputKey::Enter, core::KeyAction::Repeat, {false, false}},
        {core::InputKey::Space, core::KeyAction::Repeat, {false, false}},
    });
    expect(state.tabsValue == 2 && state.tabsChanges == tabsActivationChangesStart &&
               state.callbackTrace.size() == tabsActivationTraceStart,
           "automatic-selection Tabs 的 Enter/Space Press/Repeat 均为 no-op");

    queueKey(core::InputKey::Tab);
    expect(focusedWithRing(state, "slider.hit", "slider.focus"),
           "Tab 跳过 disabled Tabs 并聚焦 Slider 稳定 hit ID");
    const std::size_t sliderTraceStart = state.sliderTrace.size();
    const std::size_t sliderCallbackStart = state.callbackTrace.size();
    queueKey(core::InputKey::Right);
    queueKey(core::InputKey::Right, core::KeyAction::Repeat);
    queueKey(core::InputKey::Left);
    queueKey(core::InputKey::Down, core::KeyAction::Repeat);
    queueKey(core::InputKey::Home);
    queueKey(core::InputKey::Up);
    queueKey(core::InputKey::End, core::KeyAction::Repeat);
    const std::size_t sliderBoundaryValueStart = state.sliderTrace.size();
    const std::size_t sliderBoundaryCallbackStart = state.callbackTrace.size();
    queueKeys({
        {core::InputKey::Right, core::KeyAction::Press, {false, false}},
        {core::InputKey::Up, core::KeyAction::Repeat, {false, false}},
        {core::InputKey::End, core::KeyAction::Repeat, {false, false}},
    });
    expect(near(state.sliderValue, 1.0f) &&
               state.sliderTrace.size() == sliderBoundaryValueStart &&
               state.callbackTrace.size() == sliderBoundaryCallbackStart,
           "Slider 已在上边界时方向键与 End Repeat 不产生重复回调");
    queueKey(core::InputKey::Enter);
    queueKey(core::InputKey::Space);
    queueKey(core::InputKey::Enter, core::KeyAction::Repeat);
    queueKey(core::InputKey::Space, core::KeyAction::Repeat);
    expect(near(state.sliderValue, 1.0f) &&
               exactFloatTail(state.sliderTrace,
                              sliderTraceStart,
                              {0.75f, 1.0f, 0.75f, 0.5f, 0.0f, 0.25f, 1.0f}) &&
               state.callbackTrace.size() == sliderCallbackStart + 7,
           "Slider 四方向、Home/End 和 Repeat 更新，Enter/Space 不产生值回调");

    state.sliderValue = 0.5f;
    host.tick(host.timeSeconds(), true);
    const std::size_t mixedSliderTraceStart = state.sliderTrace.size();
    core::queuePointerButton(native.handle, 95.0, 552.0, 0, true);
    core::queuePointerButton(native.handle, 95.0, 552.0, 0, false);
    core::queueKeyInput(native.handle,
                        {core::InputKey::End, core::KeyAction::Press, {false, false}});
    host.tick(host.timeSeconds(), false);
    expect(near(state.sliderValue, 1.0f) &&
               exactFloatTail(state.sliderTrace, mixedSliderTraceStart, {0.25f, 1.0f}),
           "Slider 同一 tick 先按 pointer 坐标更新，再按 End 键更新");

    queueKey(core::InputKey::Tab);
    expect(focusedWithRing(state, "input.hit", "input.focus"),
           "Tab 跳过 disabled Slider 并聚焦 Input 稳定 hit ID");
    const std::size_t inputTraceStart = state.callbackTrace.size();
    const std::string inputBeforeComposingEnter = state.inputValue;
    core::queueTextEditing(native.handle, "zuhe");
    core::queueKeyInput(native.handle,
                        {core::InputKey::Enter, core::KeyAction::Press, {false, false}});
    core::queueKeyInput(native.handle,
                        {core::InputKey::Enter, core::KeyAction::Repeat, {false, false}});
    host.tick(host.timeSeconds(), false);
    expect(state.inputEnter == 0 && state.inputValue == inputBeforeComposingEnter &&
               state.callbackTrace.size() == inputTraceStart,
           "单行 Input 在 IME composing 时忽略 Enter Press/Repeat 且不改文本");
    core::queueTextEditing(native.handle, {});
    host.tick(host.timeSeconds(), false);
    queueKey(core::InputKey::Enter);
    queueKey(core::InputKey::Enter, core::KeyAction::Repeat);
    queueKey(core::InputKey::Space);
    queueKey(core::InputKey::Space, core::KeyAction::Repeat);
    expect(state.inputEnter == 1 && state.inputEscape == 0 &&
               exactTail(state.callbackTrace, inputTraceStart, {"input-enter"}),
           "单行 Input Enter 仅 Press 提交，Space/Repeat 不触发提交");

    core::queueTextEditing(native.handle, "zuhe");
    core::queueKeyInput(native.handle,
                        {core::InputKey::Escape, core::KeyAction::Press, {false, false}});
    host.tick(host.timeSeconds(), false);
    expect(state.inputEscape == 0 &&
               exactTail(state.callbackTrace, inputTraceStart, {"input-enter"}),
           "Input 在 IME composing 时不触发 Escape 回调");
    core::queueTextEditing(native.handle, {});
    host.tick(host.timeSeconds(), false);
    queueKey(core::InputKey::Escape);
    queueKey(core::InputKey::Escape, core::KeyAction::Repeat);
    expect(state.inputEscape == 1 &&
               exactTail(state.callbackTrace,
                         inputTraceStart,
                         {"input-enter", "input-escape"}),
           "Input Escape 仅非 composing 的 Press 触发一次");

    queueKey(core::InputKey::Tab);
    const std::vector<std::string> expectedFocusTrace{
        "button.bg", "checkbox.hit", "switch.hit", "radio.hit",
        "radio-group", "tabs", "slider.hit", "input.hit", "button.bg",
    };
    expect(state.currentFocus == "button.bg" && state.focusTrace == expectedFocusTrace,
           "完整 Tab trace 按组件文档顺序环回并跳过全部 disabled 控件");

    const bool allStableIds = std::all_of(
        std::begin(kFocusSpecs), std::end(kFocusSpecs), [&](const FocusSpec& spec) {
            const auto found = state.stableFocusable.find(spec.focusId);
            return found != state.stableFocusable.end() && found->second;
        });
    const bool allRingsStructural = std::all_of(
        std::begin(kFocusSpecs), std::end(kFocusSpecs), [&](const FocusSpec& spec) {
            const auto found = state.ringStructural.find(spec.ringId);
            return found != state.ringStructural.end() && found->second;
        });
    const int visibleRings = static_cast<int>(std::count_if(
        state.ringVisible.begin(), state.ringVisible.end(), [](const auto& entry) {
            return entry.second;
        }));
    expect(allStableIds, "八类组件稳定焦点 ID 均存在且 focusable");
    expect(state.emptyCompositesDisabled,
           "空 Tabs/RadioGroup root 使用 effective disabled 并被完整 Tab trace 跳过");
    expect(allRingsStructural,
           "八类焦点环均有正宽度/非透明 border 且 Element 非 interactive/focusable");
    expect(visibleRings == 1 && state.ringVisible["button.focus"],
           "环回后仅当前 Button 焦点环声明为可见");

    const std::size_t proxyFocusTraceStart = state.focusTrace.size();
    const std::size_t proxyCallbackTraceStart = state.callbackTrace.size();
    clickAt(40.0, 334.0);
    expect(focusedWithRing(state, "radio-group", "radio-group.focus") &&
               state.radioGroupValue == 1,
           "点击 RadioGroup 子 radio.1.hit 后焦点代理到复合 root");
    clickAt(170.0, 474.0);
    expect(focusedWithRing(state, "tabs", "tabs.focus") && state.tabsValue == 1 &&
               exactTail(state.focusTrace,
                         proxyFocusTraceStart,
                         {"radio-group", "tabs"}) &&
               exactTail(state.callbackTrace,
                         proxyCallbackTraceStart,
                         {"radio-group:1", "tabs:1"}),
           "点击 Tabs 子 hit.1 后焦点代理到 root，复合控件 pointer trace 连续");

    const std::vector<core::Vec2> disabledPoints{
        {40.0f, 71.0f},
        {30.0f, 141.0f},
        {30.0f, 209.0f},
        {30.0f, 277.0f},
        {30.0f, 388.0f},
        {50.0f, 514.0f},
        {100.0f, 588.0f},
        {40.0f, 669.0f},
    };
    const std::size_t focusTraceBeforeDisabled = state.focusTrace.size();
    for (const core::Vec2& point : disabledPoints) {
        clickAt(point.x, point.y);
    }
    expect(state.disabledCallbacks == 0 && state.focusTrace.size() == focusTraceBeforeDisabled &&
               state.currentFocus.empty(),
           "八类 disabled 组件均拒绝 pointer 回调与焦点获取");

    expect(host.destroyWindow(id), "销毁 hosted 组件键盘窗口");
    host.shutdown();
    std::fprintf(stderr,
                 "hosted component keyboard (Runtime E3): failures=%d\n",
                 failures);
    return failures == 0 ? 0 : 1;
#endif
}
