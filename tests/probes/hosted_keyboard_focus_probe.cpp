#include "core/input/input_state.h"
#include "eui/host.h"

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* label) {
    std::fprintf(stderr, "%s hosted keyboard focus: %s\n",
                 condition ? "[PASS]" : "[FAIL]", label);
    if (!condition) {
        ++failures;
    }
}

} // namespace

int main() {
#if !defined(_WIN32)
    std::fprintf(stderr, "[SKIP] hosted keyboard focus: 当前 probe 只验证 Win32 hosted 输入链\n");
    return 0;
#else
    eui::EuiAppHost host;
    expect(host.initialize(), "host 初始化");
    if (!host.initialized()) {
        return 1;
    }

    std::vector<std::string> focusTrace;
    std::vector<std::string> keyTrace;
    std::vector<std::string> textKeyTrace;
    std::vector<std::string> activationTrace;
    std::vector<std::string> deliveryTrace;
    std::vector<std::string> textTrace;
    std::vector<std::string> compositionTrace;
    int keyDispatchCount = 0;
    int activationCount = 0;
    bool hideSecond = false;
    bool removeFirst = false;

    eui::WindowConfig config;
    config.title = "hosted-keyboard-focus";
    config.pageId = "hosted-keyboard-focus";
    config.role = eui::WindowRole::Popup;
    config.width = 360;
    config.height = 260;
    config.visible = false;
    config.initialPlacement = eui::WindowPlacement{true, 360, 240, 360, 260, false};
    config.compose = [&](eui::Ui& ui, const eui::Screen&) {
        ui.column("focus.root")
            .size(320.0f, 220.0f)
            .content([&] {
                if (!removeFirst) {
                    ui.rect("first")
                        .size(120.0f, 40.0f)
                        .zIndex(20)
                        .focusable()
                        .onFocusChanged([&focusTrace](bool focused) {
                            if (focused) {
                                focusTrace.emplace_back("first");
                            }
                        })
                        .onTextInput([&textKeyTrace, &textTrace, &compositionTrace, &deliveryTrace](const core::KeyboardEvent& event) {
                            for (const core::KeyEvent& key : event.keys) {
                                if (key.key == core::InputKey::Enter ||
                                    key.key == core::InputKey::Space) {
                                    textKeyTrace.emplace_back("first");
                                    deliveryTrace.emplace_back("first:text");
                                }
                            }
                            if (!event.text.empty()) {
                                textTrace.emplace_back("first:" + event.text);
                            }
                            if (event.compositionChanged) {
                                compositionTrace.emplace_back("first:" + event.compositionText);
                            }
                        })
                        .onKey([&keyDispatchCount, &keyTrace, &deliveryTrace](const core::KeyEvent& event) {
                            if (event.key == core::InputKey::Enter ||
                                event.key == core::InputKey::Space) {
                                ++keyDispatchCount;
                                keyTrace.emplace_back("first");
                                deliveryTrace.emplace_back("first:key");
                            }
                        })
                        .onActivate([&activationCount, &activationTrace, &deliveryTrace] {
                            ++activationCount;
                            activationTrace.emplace_back("first");
                            deliveryTrace.emplace_back("first:activate");
                        })
                        .build();
                }

                ui.rect("disabled")
                    .size(120.0f, 40.0f)
                    .zIndex(-10)
                    .focusable()
                    .disabled()
                    .build();

                ui.rect("hidden")
                    .size(120.0f, 40.0f)
                    .zIndex(-20)
                    .focusable()
                    .opacity(0.0f)
                    .build();

                ui.rect("second")
                    .size(120.0f, 40.0f)
                    .zIndex(-30)
                    .focusable()
                    .opacity(hideSecond ? 0.0f : 1.0f)
                    .onFocusChanged([&focusTrace](bool focused) {
                        if (focused) {
                            focusTrace.emplace_back("second");
                        }
                    })
                    .onTextInput([&textKeyTrace, &textTrace, &compositionTrace, &deliveryTrace](const core::KeyboardEvent& event) {
                        for (const core::KeyEvent& key : event.keys) {
                            if (key.key == core::InputKey::Enter ||
                                key.key == core::InputKey::Space) {
                                textKeyTrace.emplace_back("second");
                                deliveryTrace.emplace_back("second:text");
                            }
                        }
                        if (!event.text.empty()) {
                            textTrace.emplace_back("second:" + event.text);
                        }
                        if (event.compositionChanged) {
                            compositionTrace.emplace_back("second:" + event.compositionText);
                        }
                    })
                    .onKey([&keyDispatchCount, &keyTrace, &deliveryTrace](const core::KeyEvent& event) {
                        if (event.key == core::InputKey::Enter ||
                            event.key == core::InputKey::Space) {
                            ++keyDispatchCount;
                            keyTrace.emplace_back("second");
                            deliveryTrace.emplace_back("second:key");
                        }
                    })
                    .onActivate([&activationCount, &activationTrace, &deliveryTrace] {
                        ++activationCount;
                        activationTrace.emplace_back("second");
                        deliveryTrace.emplace_back("second:activate");
                    })
                    .build();
            })
            .build();
    };

    const eui::WindowId id = host.createWindow(config);
    const eui::window::NativeWindowInfo native = host.nativeWindowInfo(id);
    expect(id != eui::kInvalidWindowId && native.handle != nullptr,
           "创建 hosted 键盘焦点窗口");
    expect(host.showWindow(id), "显示 hosted 键盘焦点窗口");
    host.tick(host.timeSeconds(), true);

    const auto queueKey = [&](core::InputKey key,
                              bool shift = false,
                              core::KeyAction action = core::KeyAction::Press) {
        core::queueKeyInput(native.handle, {key, action, {false, shift}});
        host.tick(host.timeSeconds(), false);
    };
    const auto queueKeys = [&](std::initializer_list<core::KeyEvent> keys) {
        for (const core::KeyEvent& key : keys) {
            core::queueKeyInput(native.handle, key);
        }
        host.tick(host.timeSeconds(), false);
    };

    queueKey(core::InputKey::Tab);
    expect(!focusTrace.empty() && focusTrace.back() == "first",
           "Tab 从无焦点进入第一个可见控件");
    queueKey(core::InputKey::Tab);
    expect(focusTrace.back() == "second",
           "Tab 跳过 disabled/opacity=0 控件");
    queueKey(core::InputKey::Tab);
    expect(focusTrace.back() == "first",
           "Tab 在末尾环回第一个控件");
    queueKey(core::InputKey::Tab, true);
    expect(focusTrace.back() == "second",
           "Shift+Tab 反向环回");

    const std::size_t focusCountBeforeRepeat = focusTrace.size();
    const int keyCountBeforeRepeat = keyDispatchCount;
    const int activationCountBeforeRepeat = activationCount;
    queueKey(core::InputKey::Tab, false, core::KeyAction::Repeat);
    queueKey(core::InputKey::Enter, false, core::KeyAction::Repeat);
    queueKey(core::InputKey::Space, false, core::KeyAction::Repeat);
    expect(focusTrace.size() == focusCountBeforeRepeat &&
               keyDispatchCount == keyCountBeforeRepeat + 2 &&
               activationCount == activationCountBeforeRepeat,
           "Tab repeat 不移动，Enter/Space repeat 只分发不激活");

    const int keyCountBefore = keyDispatchCount;
    const int activationBefore = activationCount;
    queueKey(core::InputKey::Enter);
    expect(keyDispatchCount == keyCountBefore + 1 &&
               activationCount == activationBefore + 1 &&
               keyTrace.back() == "second" && activationTrace.back() == "second",
           "Enter 只分发一次并激活一次");
    queueKey(core::InputKey::Space);
    expect(keyDispatchCount == keyCountBefore + 2 &&
               activationCount == activationBefore + 2 &&
               keyTrace.back() == "second" && activationTrace.back() == "second",
           "Space 只分发一次并激活一次");

    const int keyCountBeforeOrdered = keyDispatchCount;
    const int activationCountBeforeOrdered = activationCount;
    const std::size_t textKeyCountBeforeOrdered = textKeyTrace.size();
    const std::size_t deliveryCountBeforeOrdered = deliveryTrace.size();
    queueKeys({
        {core::InputKey::Tab, core::KeyAction::Press, {false, false}},
        {core::InputKey::Enter, core::KeyAction::Press, {false, false}}
    });
    expect(focusTrace.back() == "first" &&
               keyDispatchCount == keyCountBeforeOrdered + 1 &&
               activationCount == activationCountBeforeOrdered + 1 &&
               textKeyTrace.size() == textKeyCountBeforeOrdered + 1 &&
               deliveryTrace.size() == deliveryCountBeforeOrdered + 3 &&
               deliveryTrace[deliveryCountBeforeOrdered] == "first:text" &&
               deliveryTrace[deliveryCountBeforeOrdered + 1] == "first:key" &&
               deliveryTrace[deliveryCountBeforeOrdered + 2] == "first:activate" &&
               keyTrace.back() == "first" && activationTrace.back() == "first" &&
               !textKeyTrace.empty() && textKeyTrace.back() == "first",
           "同一 tick 的 Tab 后 Enter 按顺序落到新焦点");

    const std::size_t textKeyCountBeforeSpace = textKeyTrace.size();
    const std::size_t deliveryCountBeforeSpace = deliveryTrace.size();
    queueKeys({
        {core::InputKey::Tab, core::KeyAction::Press, {false, false}},
        {core::InputKey::Space, core::KeyAction::Press, {false, false}}
    });
    expect(focusTrace.back() == "second" &&
               textKeyTrace.size() == textKeyCountBeforeSpace + 1 &&
               deliveryTrace.size() == deliveryCountBeforeSpace + 3 &&
               deliveryTrace[deliveryCountBeforeSpace] == "second:text" &&
               deliveryTrace[deliveryCountBeforeSpace + 1] == "second:key" &&
               deliveryTrace[deliveryCountBeforeSpace + 2] == "second:activate" &&
               keyTrace.back() == "second" && activationTrace.back() == "second" &&
               textKeyTrace.back() == "second",
           "同一 tick 的 Tab 后 Space 按顺序落到新焦点");

    const int keyCountBeforeHidden = keyDispatchCount;
    const int activationCountBeforeHidden = activationCount;
    hideSecond = true;
    host.tick(host.timeSeconds(), true);
    queueKey(core::InputKey::Enter);
    expect(keyDispatchCount == keyCountBeforeHidden &&
               activationCount == activationCountBeforeHidden,
           "焦点元素 opacity=0 后清理 stale focus 且不再收键");
    queueKey(core::InputKey::Tab);
    expect(focusTrace.back() == "first",
           "stale focus 清理后 Tab 从有效集合恢复");

    core::queueTextInput(native.handle, "A");
    host.tick(host.timeSeconds(), false);
    expect(std::count(textTrace.begin(), textTrace.end(), "first:A") == 1,
           "文本输入仍交付给当前焦点一次");
    core::queueTextEditing(native.handle, "zhong");
    host.tick(host.timeSeconds(), false);
    expect(std::count(compositionTrace.begin(), compositionTrace.end(), "first:zhong") == 1,
           "IME 组合串仍交付给当前焦点一次");
    core::queueTextEditing(native.handle, {});
    host.tick(host.timeSeconds(), false);

    const int keyCountBeforeRemoved = keyDispatchCount;
    const int activationCountBeforeRemoved = activationCount;
    removeFirst = true;
    host.tick(host.timeSeconds(), true);
    queueKey(core::InputKey::Enter);
    expect(keyDispatchCount == keyCountBeforeRemoved &&
               activationCount == activationCountBeforeRemoved,
           "焦点元素移出 compose 树后清理 stale focus 且不再收键");

    eui::WindowConfig emptyConfig;
    emptyConfig.title = "hosted-keyboard-focus-empty";
    emptyConfig.pageId = "hosted-keyboard-focus-empty";
    emptyConfig.role = eui::WindowRole::Popup;
    emptyConfig.width = 220;
    emptyConfig.height = 140;
    emptyConfig.visible = false;
    emptyConfig.initialPlacement = eui::WindowPlacement{true, 760, 240, 220, 140, false};
    emptyConfig.compose = [](eui::Ui&, const eui::Screen&) {};
    const eui::WindowId emptyId = host.createWindow(emptyConfig);
    const auto emptyNative = host.nativeWindowInfo(emptyId);
    expect(emptyId != eui::kInvalidWindowId && emptyNative.handle != nullptr,
           "创建无焦点元素窗口");
    expect(host.showWindow(emptyId), "显示无焦点元素窗口");
    core::queueKeyInput(emptyNative.handle,
                        {core::InputKey::Tab, core::KeyAction::Press, {false, false}});
    host.tick(host.timeSeconds(), false);
    expect(host.isVisible(emptyId), "空焦点列表收到 Tab 后保持稳定");

    expect(host.destroyWindow(emptyId), "销毁无焦点元素窗口");
    expect(host.destroyWindow(id), "销毁 hosted 键盘焦点窗口");
    host.shutdown();
    std::fprintf(stderr, "hosted keyboard focus: failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
#endif
}
