#include "components/components.h"
#include "core/input/input_state.h"
#include "eui/host.h"

#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* label) {
    std::fprintf(stderr, "%s hosted dialog focus: %s\n",
                 condition ? "[PASS]" : "[FAIL]", label);
    if (!condition) {
        ++failures;
    }
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

struct ProbeState {
    bool showOpener = true;
    bool dialogAOpen = false;
    bool dialogBOpen = false;
    bool emptyScopeOpen = false;
    int dialogAZIndex = 1000;
    int dialogBZIndex = 2000;
    int dialogAPrimary = 0;
    int dialogBPrimary = 0;
    int dialogACloses = 0;
    int dialogBCloses = 0;
    int emptyScopeCloses = 0;
    bool dialogAScopeContract = false;
    bool dialogBScopeContract = false;
    bool dialogAPrimaryContract = false;
    bool dialogBPrimaryContract = false;
    bool emptyScopeContract = false;
    std::string currentFocus;
    std::vector<std::string> focusTrace;
    std::vector<std::string> callbackTrace;
};

constexpr const char* kObservedFocusIds[] = {
    "opener.bg",
    "dialog-a.secondary.bg",
    "dialog-a.primary.bg",
    "dialog-b.secondary.bg",
    "dialog-b.primary.bg",
};

void observeContracts(eui::Ui& ui, ProbeState& state) {
    const core::dsl::Element* dialogA = ui.find("dialog-a");
    const core::dsl::Element* dialogB = ui.find("dialog-b");
    const core::dsl::Element* dialogAPrimary = ui.find("dialog-a.primary.bg");
    const core::dsl::Element* dialogBPrimary = ui.find("dialog-b.primary.bg");
    const core::dsl::Element* emptyScope = ui.find("empty-scope");
    const core::dsl::Element* emptyVisual = ui.find("empty-scope.visual");
    state.dialogAScopeContract = dialogA != nullptr && dialogA->modalFocusScope &&
                                 static_cast<bool>(dialogA->onEscape);
    state.dialogBScopeContract = dialogB != nullptr && dialogB->modalFocusScope &&
                                 static_cast<bool>(dialogB->onEscape);
    state.dialogAPrimaryContract = dialogAPrimary != nullptr &&
                                   dialogAPrimary->focusable &&
                                   dialogAPrimary->initialFocus;
    state.dialogBPrimaryContract = dialogBPrimary != nullptr &&
                                   dialogBPrimary->focusable &&
                                   dialogBPrimary->initialFocus;
    state.emptyScopeContract = emptyScope == nullptr ||
        (emptyScope->modalFocusScope && static_cast<bool>(emptyScope->onEscape) &&
         emptyVisual != nullptr && !emptyVisual->focusable);

    std::string focused;
    for (const char* id : kObservedFocusIds) {
        if (ui.isFocused(id)) {
            focused = id;
            break;
        }
    }
    if (focused != state.currentFocus) {
        state.currentFocus = focused;
        if (!focused.empty()) {
            state.focusTrace.push_back(focused);
        }
    }
}

} // namespace

int main() {
#if !defined(_WIN32)
    std::fprintf(stderr, "[SKIP] hosted dialog focus: 当前 probe 只验证 Win32 hosted Runtime 输入链\n");
    return 0;
#else
    eui::EuiAppHost host;
    expect(host.initialize(), "host 初始化");
    if (!host.initialized()) {
        return 1;
    }

    ProbeState state;
    eui::WindowConfig config;
    config.title = "hosted-dialog-focus";
    config.pageId = "hosted-dialog-focus";
    config.role = eui::WindowRole::Popup;
    config.width = 640;
    config.height = 420;
    config.visible = false;
    config.initialPlacement = eui::WindowPlacement{true, 360, 220, 640, 420, false};
    config.compose = [&](eui::Ui& ui, const eui::Screen& screen) {
        ui.stack("page")
            .size(screen.width, screen.height)
            .content([&] {
                if (state.showOpener) {
                    components::button(ui, "opener")
                        .position(24.0f, 24.0f)
                        .size(180.0f, 38.0f)
                        .text("Open dialog")
                        .onClick([] {})
                        .build();
                }
            })
            .build();

        // 源码顺序故意让 B 在 A 前，top scope 必须由 paint order/zIndex 决定。
        components::dialog(ui, "dialog-b")
            .screen(screen.width, screen.height)
            .size(400.0f, 210.0f)
            .title("Dialog B")
            .message("Second modal focus scope")
            .primaryText("Confirm B")
            .secondaryText("Cancel B")
            .open(state.dialogBOpen)
            .zIndex(state.dialogBZIndex)
            .onPrimary([&state] {
                ++state.dialogBPrimary;
                state.callbackTrace.emplace_back("dialog-b-primary");
            })
            .onOpenChange([&state](bool open) {
                state.dialogBOpen = open;
                if (!open) {
                    ++state.dialogBCloses;
                    state.callbackTrace.emplace_back("dialog-b-close");
                }
            })
            .build();

        components::dialog(ui, "dialog-a")
            .screen(screen.width, screen.height)
            .size(420.0f, 220.0f)
            .title("Dialog A")
            .message("First modal focus scope")
            .primaryText("Confirm A")
            .secondaryText("Cancel A")
            .open(state.dialogAOpen)
            .zIndex(state.dialogAZIndex)
            .onPrimary([&state] {
                ++state.dialogAPrimary;
                state.callbackTrace.emplace_back("dialog-a-primary");
            })
            .onOpenChange([&state](bool open) {
                state.dialogAOpen = open;
                if (!open) {
                    ++state.dialogACloses;
                    state.callbackTrace.emplace_back("dialog-a-close");
                }
            })
            .build();

        if (state.emptyScopeOpen) {
            ui.stack("empty-scope")
                .size(screen.width, screen.height)
                .zIndex(3000)
                .modalFocusScope()
                .onEscape([&state] {
                    state.emptyScopeOpen = false;
                    ++state.emptyScopeCloses;
                    state.callbackTrace.emplace_back("empty-scope-close");
                })
                .content([&] {
                    ui.rect("empty-scope.visual")
                        .size(screen.width, screen.height)
                        .color({0.0f, 0.0f, 0.0f, 0.1f})
                        .build();
                })
                .build();
        }

        observeContracts(ui, state);
    };

    const eui::WindowId id = host.createWindow(config);
    const eui::window::NativeWindowInfo native = host.nativeWindowInfo(id);
    expect(id != eui::kInvalidWindowId && native.handle != nullptr,
           "创建 hosted Dialog 焦点窗口");
    expect(host.showWindow(id), "显示 hosted Dialog 焦点窗口");
    host.tick(host.timeSeconds(), true);

    // scope 的初焦/恢复发生在 compose 收尾；宿主必须在同一 tick 消费有界重组请求。
    const auto settle = [&] {
        host.tick(host.timeSeconds(), true);
    };
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

    queueKey(core::InputKey::Tab);
    expect(state.currentFocus == "opener.bg" &&
               exactTail(state.focusTrace, 0, {"opener.bg"}),
           "无 scope 时 Tab 聚焦 opener 稳定 ID");

    const std::size_t firstDialogFocusStart = state.focusTrace.size();
    const std::size_t firstDialogCallbackStart = state.callbackTrace.size();
    state.dialogAOpen = true;
    settle();
    expect(state.currentFocus == "dialog-a.primary.bg" &&
               state.dialogAScopeContract && state.dialogAPrimaryContract,
           "Dialog A 打开后 scope 选择内置 primary.bg 初焦");
    queueKey(core::InputKey::Tab);
    queueKey(core::InputKey::Tab);
    queueKey(core::InputKey::Tab, core::KeyAction::Press, true);
    queueKey(core::InputKey::Tab, core::KeyAction::Press, true);
    expect(state.currentFocus == "dialog-a.primary.bg" &&
               exactTail(state.focusTrace,
                         firstDialogFocusStart,
                         {"dialog-a.primary.bg", "dialog-a.secondary.bg",
                          "dialog-a.primary.bg", "dialog-a.secondary.bg",
                          "dialog-a.primary.bg"}),
           "Dialog A Tab/Shift+Tab 均限制在 scope 内并正反环回");

    queueKey(core::InputKey::Enter);
    expect(state.dialogAPrimary == 1 &&
               exactTail(state.callbackTrace,
                         firstDialogCallbackStart,
                         {"dialog-a-primary"}),
           "Dialog A primary 在 scope 内只响应当前 Enter Press");
    queueKey(core::InputKey::Escape, core::KeyAction::Repeat);
    expect(state.dialogAOpen && state.dialogACloses == 0,
           "Dialog Escape Repeat 不关闭 scope");

    core::queueTextEditing(native.handle, "zuhe");
    core::queueKeyInput(native.handle,
                        {core::InputKey::Escape, core::KeyAction::Press, {false, false}});
    host.tick(host.timeSeconds(), false);
    expect(state.dialogAOpen && state.dialogACloses == 0,
           "Dialog 在 IME composing 时不消费 Escape Press");
    core::queueTextEditing(native.handle, {});
    host.tick(host.timeSeconds(), false);

    queueKeys({
        {core::InputKey::Escape, core::KeyAction::Press, {false, false}},
        {core::InputKey::Enter, core::KeyAction::Press, {false, false}},
    });
    expect(!state.dialogAOpen && state.dialogACloses == 1 &&
               state.dialogAPrimary == 1 && state.currentFocus == "opener.bg" &&
               exactTail(state.callbackTrace,
                         firstDialogCallbackStart,
                         {"dialog-a-primary", "dialog-a-close"}) &&
               exactTail(state.focusTrace,
                         firstDialogFocusStart,
                         {"dialog-a.primary.bg", "dialog-a.secondary.bg",
                          "dialog-a.primary.bg", "dialog-a.secondary.bg",
                          "dialog-a.primary.bg", "opener.bg"}),
           "同 tick Escape 先关闭并截断 Enter，随后恢复 opener");

    const std::size_t backdropFocusStart = state.focusTrace.size();
    const std::size_t backdropCallbackStart = state.callbackTrace.size();
    state.dialogAOpen = true;
    settle();
    clickAt(8.0, 8.0);
    expect(!state.dialogAOpen && state.dialogACloses == 2 &&
               state.currentFocus == "opener.bg" &&
               exactTail(state.callbackTrace,
                         backdropCallbackStart,
                         {"dialog-a-close"}) &&
               exactTail(state.focusTrace,
                         backdropFocusStart,
                         {"dialog-a.primary.bg", "opener.bg"}),
           "Dialog backdrop pointer 关闭后按 restore 链恢复 opener");

    const std::size_t invalidRestoreFocusStart = state.focusTrace.size();
    state.dialogAOpen = true;
    settle();
    state.showOpener = false;
    settle();
    expect(state.currentFocus == "dialog-a.primary.bg",
           "移除 opener 时 Dialog A 内当前焦点保持有效");
    state.dialogAOpen = false;
    settle();
    expect(state.currentFocus.empty(), "Dialog 关闭时失效 restore target 被清空");
    state.showOpener = true;
    settle();
    expect(state.currentFocus.empty(), "restore target 重新出现后不追溯恢复旧焦点");
    queueKey(core::InputKey::Tab);
    expect(state.currentFocus == "opener.bg" &&
               exactTail(state.focusTrace,
                         invalidRestoreFocusStart,
                         {"dialog-a.primary.bg", "opener.bg"}),
           "失效 restore 后由下一次 Tab 显式恢复普通焦点流");

    const std::size_t nestedTopFocusStart = state.focusTrace.size();
    const std::size_t nestedTopCallbackStart = state.callbackTrace.size();
    state.dialogAOpen = true;
    settle();
    state.dialogBOpen = true;
    settle();
    expect(state.currentFocus == "dialog-b.primary.bg" &&
               state.dialogBScopeContract && state.dialogBPrimaryContract,
           "第二层 Dialog B 成为 top scope 并获得 primary 初焦");
    queueKey(core::InputKey::Enter);
    expect(state.dialogBPrimary == 1 && state.dialogAPrimary == 1 &&
               exactTail(state.callbackTrace,
                         nestedTopCallbackStart,
                         {"dialog-b-primary"}),
           "源码顺序在前但 zIndex 更高的 Dialog B 独占 Enter 键");
    state.dialogBOpen = false;
    settle();
    expect(state.currentFocus == "dialog-a.primary.bg",
           "关闭顶层 Dialog B 后恢复 Dialog A 焦点");
    state.dialogAOpen = false;
    settle();
    expect(state.currentFocus == "opener.bg" &&
               exactTail(state.focusTrace,
                         nestedTopFocusStart,
                         {"dialog-a.primary.bg", "dialog-b.primary.bg",
                          "dialog-a.primary.bg", "opener.bg"}),
           "两层 Dialog 依次关闭后沿 scope 链恢复 opener");

    const std::size_t nonTopFocusStart = state.focusTrace.size();
    state.dialogAOpen = true;
    settle();
    state.dialogBOpen = true;
    settle();
    state.dialogAOpen = false;
    settle();
    expect(state.currentFocus == "dialog-b.primary.bg",
           "关闭非 top Dialog A 不改变 Dialog B 当前焦点");
    state.dialogBOpen = false;
    settle();
    expect(state.currentFocus == "opener.bg" &&
               exactTail(state.focusTrace,
                         nonTopFocusStart,
                         {"dialog-a.primary.bg", "dialog-b.primary.bg", "opener.bg"}),
           "关闭非 top scope 后 restore 链拼接到 opener");

    const std::size_t reorderFocusStart = state.focusTrace.size();
    const std::size_t reorderCallbackStart = state.callbackTrace.size();
    const int reorderAPrimaryStart = state.dialogAPrimary;
    const int reorderBPrimaryStart = state.dialogBPrimary;
    state.dialogAZIndex = 1000;
    state.dialogBZIndex = 2000;
    state.dialogAOpen = true;
    state.dialogBOpen = true;
    settle();
    expect(state.currentFocus == "dialog-b.primary.bg",
           "A/B 同时可见时较高 zIndex 的 Dialog B 首先成为 top");
    queueKey(core::InputKey::Enter);

    state.dialogAZIndex = 3000;
    settle();
    expect(state.currentFocus == "dialog-a.primary.bg",
           "动态提高 zIndex 后 Dialog A 接管 top 焦点");
    queueKey(core::InputKey::Enter);
    queueKey(core::InputKey::Tab);
    expect(state.currentFocus == "dialog-a.secondary.bg",
           "Dialog A 位于 top 时保存 secondary 作为后续精确恢复目标");

    state.dialogBZIndex = 4000;
    settle();
    expect(state.currentFocus == "dialog-b.primary.bg",
           "再次提高 zIndex 后 Dialog B 重新接管 top 焦点");
    queueKey(core::InputKey::Enter);
    expect(state.dialogAPrimary == reorderAPrimaryStart + 1 &&
               state.dialogBPrimary == reorderBPrimaryStart + 2 &&
               exactTail(state.callbackTrace,
                         reorderCallbackStart,
                         {"dialog-b-primary", "dialog-a-primary", "dialog-b-primary"}) &&
               exactTail(state.focusTrace,
                         reorderFocusStart,
                         {"dialog-b.primary.bg", "dialog-a.primary.bg",
                          "dialog-a.secondary.bg", "dialog-b.primary.bg"}),
           "B→A→B 每个动态 top 均独占 Enter 且连续 trace 无旁路");

    state.dialogBOpen = false;
    settle();
    expect(state.currentFocus == "dialog-a.secondary.bg",
           "动态重排后关闭 top Dialog B 精确恢复 Dialog A 先前 secondary 焦点");
    state.dialogAOpen = false;
    settle();
    expect(state.currentFocus == "opener.bg" &&
               exactTail(state.focusTrace,
                         reorderFocusStart,
                         {"dialog-b.primary.bg", "dialog-a.primary.bg",
                          "dialog-a.secondary.bg", "dialog-b.primary.bg",
                          "dialog-a.secondary.bg", "opener.bg"}),
           "动态 sibling 重排未形成 parent 环，按 B→A 关闭最终恢复 opener");

    const std::size_t emptyScopeFocusStart = state.focusTrace.size();
    const std::size_t emptyScopeCallbackStart = state.callbackTrace.size();
    state.emptyScopeOpen = true;
    settle();
    expect(state.currentFocus.empty() && state.emptyScopeContract,
           "空 modal scope 打开后保持无焦点且合同可观测");
    queueKey(core::InputKey::Tab);
    queueKey(core::InputKey::Tab, core::KeyAction::Press, true);
    expect(state.currentFocus.empty(), "空 modal scope 的 Tab/Shift+Tab 保持稳定");
    queueKey(core::InputKey::Escape, core::KeyAction::Repeat);
    expect(state.emptyScopeOpen && state.emptyScopeCloses == 0,
           "空 modal scope 同样忽略 Escape Repeat");
    queueKey(core::InputKey::Escape);
    expect(!state.emptyScopeOpen && state.emptyScopeCloses == 1 &&
               state.currentFocus == "opener.bg" &&
               exactTail(state.callbackTrace,
                         emptyScopeCallbackStart,
                         {"empty-scope-close"}) &&
               exactTail(state.focusTrace, emptyScopeFocusStart, {"opener.bg"}),
           "空 modal scope 由 Escape Press 关闭并恢复 opener");

    expect(state.dialogAPrimary == 2 && state.dialogBPrimary == 3 &&
               state.dialogBCloses == 0,
           "层级 fixture 只记录实际 top primary，外部关闭不伪造 Dialog B close 回调");
    expect(host.destroyWindow(id), "销毁 hosted Dialog 焦点窗口");
    host.shutdown();
    std::fprintf(stderr,
                 "hosted dialog focus (Runtime E3): failures=%d\n",
                 failures);
    return failures == 0 ? 0 : 1;
#endif
}
