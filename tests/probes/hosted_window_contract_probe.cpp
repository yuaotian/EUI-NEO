#include "eui/host.h"

#include <cstdio>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

namespace {

int failures = 0;

void expect(bool condition, const char* label) {
    std::fprintf(stderr, "%s hosted window contract: %s\n", condition ? "[PASS]" : "[FAIL]", label);
    if (!condition) {
        ++failures;
    }
}

eui::WindowConfig baseConfig(const char* title) {
    eui::WindowConfig config;
    config.title = title;
    config.pageId = title;
    config.width = 720;
    config.height = 480;
    config.visible = false;
    config.compose = [](eui::Ui&, const eui::Screen&) {};
    return config;
}

bool sameNormalBounds(const eui::WindowPlacement& left, const eui::WindowPlacement& right) {
    return left.positioned && right.positioned &&
           left.x == right.x && left.y == right.y &&
           left.width == right.width && left.height == right.height;
}

#if defined(_WIN32)
void pumpMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

HWND nativeHwnd(eui::EuiAppHost& host, eui::WindowId id) {
    return static_cast<HWND>(host.nativeWindowInfo(id).platformWindow);
}

GLFWwindow* nativeGlfwWindow(eui::EuiAppHost& host, eui::WindowId id) {
    return static_cast<GLFWwindow*>(host.nativeWindowInfo(id).handle);
}

bool hasRoleStyle(HWND hwnd, eui::WindowRole role) {
    if (hwnd == nullptr) {
        return false;
    }
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (role != eui::WindowRole::Main) {
        return (style & WS_EX_TOOLWINDOW) != 0 && (style & WS_EX_APPWINDOW) == 0;
    }
    return (style & WS_EX_APPWINDOW) != 0 && (style & WS_EX_TOOLWINDOW) == 0;
}

bool hasExtendedStyle(HWND hwnd, LONG_PTR style) {
    return hwnd != nullptr && (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & style) != 0;
}

bool isBorderless(HWND hwnd) {
    if (hwnd == nullptr) {
        return false;
    }
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    return (style & WS_POPUP) != 0 && (style & (WS_CAPTION | WS_THICKFRAME)) == 0;
}

bool hasWindowAttribute(eui::EuiAppHost& host, eui::WindowId id, int attribute) {
    GLFWwindow* window = nativeGlfwWindow(host, id);
    return window != nullptr && glfwGetWindowAttrib(window, attribute) == GLFW_TRUE;
}

bool hasMinimumTrackSize(HWND hwnd, int minWidth, int minHeight) {
    if (hwnd == nullptr) {
        return false;
    }

    MINMAXINFO limits{};
    SendMessageW(hwnd, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&limits));

    RECT expected{0, 0, minWidth, minHeight};
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    using AdjustWindowRectExForDpiFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(user32, "GetDpiForWindow"));
    const auto adjustForDpi = reinterpret_cast<AdjustWindowRectExForDpiFn>(
        GetProcAddress(user32, "AdjustWindowRectExForDpi"));

    BOOL adjusted = FALSE;
    if (getDpiForWindow != nullptr && adjustForDpi != nullptr) {
        adjusted = adjustForDpi(&expected, style, FALSE, exStyle, getDpiForWindow(hwnd));
    } else {
        adjusted = AdjustWindowRectEx(&expected, style, FALSE, exStyle);
    }
    return adjusted != FALSE &&
           limits.ptMinTrackSize.x == expected.right - expected.left &&
           limits.ptMinTrackSize.y == expected.bottom - expected.top;
}
#endif

} // namespace

int main() {
#if !defined(_WIN32)
    std::fprintf(stderr, "[SKIP] hosted window contract: 当前 probe 只验证 Win32 原生合同\n");
    return 0;
#else
    SetConsoleOutputCP(CP_UTF8);

    eui::EuiAppHost host;
    expect(host.initialize(), "host 初始化");
    if (!host.initialized()) {
        return 1;
    }

    eui::WindowConfig invalidTool = baseConfig("invalid-tool");
    invalidTool.role = eui::WindowRole::Tool;
    invalidTool.owner = 999999;
    expect(host.createWindow(invalidTool) == eui::kInvalidWindowId, "拒绝不存在的 Tool owner");

    eui::WindowConfig invalidPlacement = baseConfig("invalid-placement");
    invalidPlacement.initialPlacement = eui::WindowPlacement{true, 100, 100, 0, 300, false};
    expect(host.createWindow(invalidPlacement) == eui::kInvalidWindowId, "拒绝非法 positioned placement");

    eui::WindowConfig invalidMinWidth = baseConfig("invalid-min-width");
    invalidMinWidth.minWidth = -1;
    expect(host.createWindow(invalidMinWidth) == eui::kInvalidWindowId, "拒绝负数 minWidth");

    eui::WindowConfig invalidMinHeight = baseConfig("invalid-min-height");
    invalidMinHeight.minHeight = -1;
    expect(host.createWindow(invalidMinHeight) == eui::kInvalidWindowId, "拒绝负数 minHeight");

    eui::WindowConfig invalidDialog = baseConfig("invalid-dialog");
    invalidDialog.role = eui::WindowRole::Dialog;
    expect(host.createWindow(invalidDialog) == eui::kInvalidWindowId, "拒绝缺少 owner 的 Dialog");

    eui::WindowConfig invalidClickThrough = baseConfig("invalid-click-through");
    invalidClickThrough.clickThrough = true;
    expect(host.createWindow(invalidClickThrough) == eui::kInvalidWindowId,
           "拒绝带装饰窗口启用 click-through");

    eui::WindowConfig mainConfig = baseConfig("contract-main");
    mainConfig.role = eui::WindowRole::Main;
    mainConfig.minWidth = 520;
    mainConfig.minHeight = 320;
    mainConfig.initialPlacement = eui::WindowPlacement{true, 240, 180, 640, 420, false};
    const HWND foregroundBeforeMain = GetForegroundWindow();
    const eui::WindowId mainId = host.createWindow(mainConfig);
    expect(mainId != eui::kInvalidWindowId, "创建隐藏 Main");
    HWND mainHwnd = nativeHwnd(host, mainId);
    const HWND foregroundBeforeTool = GetForegroundWindow();
    expect(mainHwnd != nullptr && IsWindow(mainHwnd) != FALSE, "Main native HWND 有效");
    expect(IsWindowVisible(mainHwnd) == FALSE, "Main create 后保持隐藏");
    expect(foregroundBeforeTool == foregroundBeforeMain, "Main create 不改变前台窗口");
    expect(hasRoleStyle(mainHwnd, eui::WindowRole::Main), "Main 使用 APPWINDOW 样式");
    expect(hasWindowAttribute(host, mainId, GLFW_DECORATED), "Main 默认保留窗口边框");
    expect(hasWindowAttribute(host, mainId, GLFW_FOCUS_ON_SHOW), "Main 默认 show 时聚焦");
    expect(hasMinimumTrackSize(mainHwnd, mainConfig.minWidth, mainConfig.minHeight),
           "Main 应用最小跟踪尺寸");

    const auto mainInitial = host.windowPlacement(mainId);
    expect(mainInitial.has_value() && sameNormalBounds(*mainInitial, *mainConfig.initialPlacement),
           "Main initial placement 可查询");

    eui::WindowConfig mainWithOwner = baseConfig("main-with-owner");
    mainWithOwner.owner = mainId;
    expect(host.createWindow(mainWithOwner) == eui::kInvalidWindowId, "拒绝 Main owner");

    eui::WindowConfig overlayWithOwner = baseConfig("overlay-with-owner");
    overlayWithOwner.role = eui::WindowRole::Overlay;
    overlayWithOwner.owner = mainId;
    expect(host.createWindow(overlayWithOwner) == eui::kInvalidWindowId, "拒绝 Overlay owner");

    eui::WindowConfig invalidPopupOwner = baseConfig("popup-with-invalid-owner");
    invalidPopupOwner.role = eui::WindowRole::Popup;
    invalidPopupOwner.owner = 999999;
    expect(host.createWindow(invalidPopupOwner) == eui::kInvalidWindowId, "拒绝不存在的 Popup owner");

    eui::WindowConfig invalidModalTool = baseConfig("modal-tool");
    invalidModalTool.role = eui::WindowRole::Tool;
    invalidModalTool.owner = mainId;
    invalidModalTool.modal = true;
    expect(host.createWindow(invalidModalTool) == eui::kInvalidWindowId, "拒绝非 Dialog 使用 modal");

    eui::WindowConfig toolConfig = baseConfig("contract-tool");
    toolConfig.role = eui::WindowRole::Tool;
    toolConfig.owner = mainId;
    toolConfig.minWidth = 360;
    toolConfig.minHeight = 240;
    toolConfig.initialPlacement = eui::WindowPlacement{true, 320, 260, 480, 300, false};
    const eui::WindowId toolId = host.createWindow(toolConfig);
    expect(toolId != eui::kInvalidWindowId, "创建隐藏 Tool");
    HWND toolHwnd = nativeHwnd(host, toolId);
    expect(toolHwnd != nullptr && IsWindow(toolHwnd) != FALSE, "Tool native HWND 有效");
    expect(IsWindowVisible(toolHwnd) == FALSE, "Tool create 后保持隐藏");
    expect(GetForegroundWindow() == foregroundBeforeTool, "Tool create 不改变前台窗口");
    expect(hasRoleStyle(toolHwnd, eui::WindowRole::Tool), "Tool 使用 TOOLWINDOW 样式");
    expect(GetWindow(toolHwnd, GW_OWNER) == mainHwnd, "Tool GW_OWNER 指向 Main");
    expect(hasWindowAttribute(host, toolId, GLFW_FOCUS_ON_SHOW), "Tool 默认 show 时聚焦");
    expect(hasMinimumTrackSize(toolHwnd, toolConfig.minWidth, toolConfig.minHeight),
           "Tool 应用最小跟踪尺寸");

    const auto toolInitial = host.windowPlacement(toolId);
    expect(toolInitial.has_value() && sameNormalBounds(*toolInitial, *toolConfig.initialPlacement),
           "Tool initial placement 可查询");

    eui::WindowConfig toolOwnedByTool = baseConfig("tool-owned-by-tool");
    toolOwnedByTool.role = eui::WindowRole::Tool;
    toolOwnedByTool.owner = toolId;
    expect(host.createWindow(toolOwnedByTool) == eui::kInvalidWindowId, "拒绝 Tool owner 链");

    eui::WindowConfig dialogConfig = baseConfig("contract-dialog");
    dialogConfig.role = eui::WindowRole::Dialog;
    dialogConfig.owner = toolId;
    dialogConfig.modal = true;
    const eui::WindowId dialogId = host.createWindow(dialogConfig);
    HWND dialogHwnd = nativeHwnd(host, dialogId);
    expect(dialogId != eui::kInvalidWindowId && dialogHwnd != nullptr,
           "创建 Tool owned modal Dialog");
    expect(IsWindowVisible(dialogHwnd) == FALSE, "Dialog create 后保持隐藏");
    expect(hasRoleStyle(dialogHwnd, eui::WindowRole::Dialog) &&
               GetWindow(dialogHwnd, GW_OWNER) == toolHwnd,
           "Dialog 使用 TOOLWINDOW 且精确关联 owner");
    expect(!hasExtendedStyle(dialogHwnd, WS_EX_NOACTIVATE) &&
               hasWindowAttribute(host, dialogId, GLFW_FOCUS_ON_SHOW),
           "modal Dialog 可激活且 show 时聚焦");

    eui::WindowConfig popupConfig = baseConfig("contract-popup");
    popupConfig.role = eui::WindowRole::Popup;
    popupConfig.owner = mainId;
    popupConfig.alwaysOnTop = true;
    popupConfig.initialPlacement = eui::WindowPlacement{true, 410, 310, 360, 220, false};
    const eui::WindowId popupId = host.createWindow(popupConfig);
    HWND popupHwnd = nativeHwnd(host, popupId);
    expect(popupId != eui::kInvalidWindowId && popupHwnd != nullptr, "创建 owned Popup");
    expect(IsWindowVisible(popupHwnd) == FALSE && isBorderless(popupHwnd),
           "Popup hidden create 且角色归一化为 borderless");
    expect(hasRoleStyle(popupHwnd, eui::WindowRole::Popup) &&
               GetWindow(popupHwnd, GW_OWNER) == mainHwnd,
           "Popup 使用 TOOLWINDOW 且保留可选 owner");
    expect(hasExtendedStyle(popupHwnd, WS_EX_NOACTIVATE) &&
               !hasWindowAttribute(host, popupId, GLFW_FOCUS_ON_SHOW),
           "Popup 使用 NOACTIVATE 且 show 不聚焦");
    expect(hasExtendedStyle(popupHwnd, WS_EX_TOPMOST) &&
               hasWindowAttribute(host, popupId, GLFW_FLOATING),
           "Popup 应用 always-on-top");

    eui::WindowConfig overlayConfig = baseConfig("contract-overlay");
    overlayConfig.role = eui::WindowRole::Overlay;
    overlayConfig.clickThrough = true;
    overlayConfig.initialPlacement = eui::WindowPlacement{true, 120, 90, 500, 320, false};
    const eui::WindowId overlayId = host.createWindow(overlayConfig);
    HWND overlayHwnd = nativeHwnd(host, overlayId);
    expect(overlayId != eui::kInvalidWindowId && overlayHwnd != nullptr, "创建 Overlay");
    expect(IsWindowVisible(overlayHwnd) == FALSE && isBorderless(overlayHwnd),
           "Overlay hidden create 且角色归一化为 borderless");
    expect(hasRoleStyle(overlayHwnd, eui::WindowRole::Overlay) &&
               GetWindow(overlayHwnd, GW_OWNER) == nullptr,
           "Overlay 使用无 owner 的 TOOLWINDOW");
    expect(hasExtendedStyle(overlayHwnd, WS_EX_NOACTIVATE) &&
               !hasWindowAttribute(host, overlayId, GLFW_FOCUS_ON_SHOW),
           "Overlay 使用 NOACTIVATE 且 show 不聚焦");
    expect(hasWindowAttribute(host, overlayId, GLFW_TRANSPARENT_FRAMEBUFFER),
           "Overlay 请求 transparent framebuffer");
    expect(hasExtendedStyle(overlayHwnd, WS_EX_TOPMOST) &&
               hasWindowAttribute(host, overlayId, GLFW_FLOATING),
           "Overlay 角色归一化为 always-on-top");
    expect(hasExtendedStyle(overlayHwnd, WS_EX_TRANSPARENT) &&
               hasWindowAttribute(host, overlayId, GLFW_MOUSE_PASSTHROUGH),
           "Overlay 应用 click-through");

    eui::WindowConfig dialogWithNoActivateOwner = baseConfig("dialog-with-no-activate-owner");
    dialogWithNoActivateOwner.role = eui::WindowRole::Dialog;
    dialogWithNoActivateOwner.owner = overlayId;
    expect(host.createWindow(dialogWithNoActivateOwner) == eui::kInvalidWindowId,
           "拒绝 Dialog 关联 no-activate owner");

    eui::WindowConfig popupWithNoActivateOwner = baseConfig("popup-with-no-activate-owner");
    popupWithNoActivateOwner.role = eui::WindowRole::Popup;
    popupWithNoActivateOwner.owner = overlayId;
    expect(host.createWindow(popupWithNoActivateOwner) == eui::kInvalidWindowId,
           "拒绝 Popup 关联 no-activate owner");

    const LONG_PTR mainProc = GetWindowLongPtrW(mainHwnd, GWLP_WNDPROC);
    const LONG_PTR toolProc = GetWindowLongPtrW(toolHwnd, GWLP_WNDPROC);
    const LONG_PTR dialogProc = GetWindowLongPtrW(dialogHwnd, GWLP_WNDPROC);
    const LONG_PTR popupProc = GetWindowLongPtrW(popupHwnd, GWLP_WNDPROC);
    const LONG_PTR overlayProc = GetWindowLongPtrW(overlayHwnd, GWLP_WNDPROC);
    expect(mainProc != 0 && GetPropW(mainHwnd, L"EuiNeoImeFilter") != nullptr,
           "Main IME filter 已安装");
    expect(toolProc != 0 && GetPropW(toolHwnd, L"EuiNeoImeFilter") != nullptr,
           "Tool IME filter 已安装");
    expect(dialogProc != 0 && GetPropW(dialogHwnd, L"EuiNeoImeFilter") != nullptr,
           "Dialog IME filter 已安装");
    expect(popupProc != 0 && overlayProc != 0 &&
               GetPropW(popupHwnd, L"EuiNeoImeFilter") != nullptr &&
               GetPropW(overlayHwnd, L"EuiNeoImeFilter") != nullptr,
           "Popup/Overlay IME filter 已安装");

    expect(host.showWindow(mainId), "显式显示 Main");
    expect(host.showWindow(toolId), "显式显示 Tool");
    pumpMessages();
    expect(IsWindowVisible(mainHwnd) != FALSE && IsWindowVisible(toolHwnd) != FALSE,
           "显式 show 后窗口可见");

    expect(host.showWindow(dialogId), "显式显示 modal Dialog");
    pumpMessages();
    expect(IsWindowVisible(dialogHwnd) != FALSE && IsWindowEnabled(toolHwnd) == FALSE,
           "modal Dialog 显示时禁用 owner");
    SendMessageW(toolHwnd, WM_CLOSE, 0, 0);
    pumpMessages();
    expect(!host.shouldClose(toolId) && GetFocus() == dialogHwnd,
           "modal Dialog 阻止 owner 关闭并保留焦点");
    expect(host.hideWindow(dialogId), "隐藏 modal Dialog");
    pumpMessages();
    expect(IsWindowEnabled(toolHwnd) != FALSE && GetFocus() == toolHwnd,
           "最后一个 modal Dialog 隐藏后恢复 owner 与焦点");

    const HWND foregroundBeforePopupShow = GetForegroundWindow();
    expect(host.showWindow(popupId), "显式显示 Popup");
    pumpMessages();
    expect(IsWindowVisible(popupHwnd) != FALSE &&
               GetForegroundWindow() == foregroundBeforePopupShow,
           "Popup show 不抢前台");
    const eui::WindowPlacement movedPopup{true, 460, 330, 390, 250, false};
    expect(host.setWindowPlacement(popupId, movedPopup), "动态设置 Popup bounds");
    const auto movedPopupPlacement = host.windowPlacement(popupId);
    expect(movedPopupPlacement.has_value() && sameNormalBounds(*movedPopupPlacement, movedPopup),
           "动态 Popup bounds 可 query round-trip");
    expect(host.setWindowAlwaysOnTop(popupId, false) &&
               !hasExtendedStyle(popupHwnd, WS_EX_TOPMOST),
           "动态关闭 Popup always-on-top");
    expect(host.setWindowAlwaysOnTop(popupId, true) &&
               hasExtendedStyle(popupHwnd, WS_EX_TOPMOST),
           "动态恢复 Popup always-on-top");
    expect(!host.setWindowClickThrough(mainId, true),
           "拒绝 decorated Main 动态启用 click-through");

    const HWND foregroundBeforeOverlayShow = GetForegroundWindow();
    expect(host.showWindow(overlayId), "显式显示 Overlay");
    pumpMessages();
    expect(IsWindowVisible(overlayHwnd) != FALSE &&
               GetForegroundWindow() == foregroundBeforeOverlayShow,
           "Overlay show 不抢前台");
    expect(host.setWindowClickThrough(overlayId, false) &&
               !hasExtendedStyle(overlayHwnd, WS_EX_TRANSPARENT),
           "动态关闭 Overlay click-through");
    expect(host.setWindowClickThrough(overlayId, true) &&
               hasExtendedStyle(overlayHwnd, WS_EX_TRANSPARENT),
           "动态恢复 Overlay click-through");
    expect(host.setWindowAlwaysOnTop(overlayId, false) &&
               !hasExtendedStyle(overlayHwnd, WS_EX_TOPMOST),
           "动态关闭 Overlay always-on-top");
    expect(host.setWindowAlwaysOnTop(overlayId, true) &&
               hasExtendedStyle(overlayHwnd, WS_EX_TOPMOST),
           "动态恢复 Overlay always-on-top");
    expect(!host.setWindowPlacement(overlayId, eui::WindowPlacement{}),
           "拒绝未定位的动态 placement");

    expect(GetWindowLongPtrW(mainHwnd, GWLP_WNDPROC) == mainProc &&
               GetWindowLongPtrW(toolHwnd, GWLP_WNDPROC) == toolProc &&
               GetWindowLongPtrW(dialogHwnd, GWLP_WNDPROC) == dialogProc &&
               GetWindowLongPtrW(popupHwnd, GWLP_WNDPROC) == popupProc &&
               GetWindowLongPtrW(overlayHwnd, GWLP_WNDPROC) == overlayProc,
           "show/placement/style 动态变更不覆盖 IME WndProc");
    expect(host.hideWindow(popupId) && host.hideWindow(overlayId),
           "隐藏 Popup/Overlay");
    expect(!host.destroyWindow(mainId), "Main 存在 Tool 时拒绝销毁");
    expect(!host.destroyWindow(toolId), "Tool 存在 Dialog 时拒绝销毁");
    expect(IsWindow(mainHwnd) != FALSE && IsWindow(toolHwnd) != FALSE &&
               IsWindow(dialogHwnd) != FALSE,
           "拒绝销毁后 owner 链仍有效");
    expect(host.destroyWindow(dialogId), "先销毁 Dialog");
    expect(host.destroyWindow(toolId), "再销毁 Tool");
    expect(!host.destroyWindow(mainId), "Main 存在 Popup 时拒绝销毁");
    expect(host.destroyWindow(popupId), "先销毁 Popup");
    expect(host.destroyWindow(mainId), "再销毁 Main");
    expect(host.destroyWindow(overlayId), "销毁独立 Overlay");

    eui::WindowConfig pendingMaxConfig = baseConfig("pending-max-main");
    pendingMaxConfig.initialPlacement = eui::WindowPlacement{false, 0, 0, 0, 0, true};
    const eui::WindowId pendingMaxId = host.createWindow(pendingMaxConfig);
    HWND pendingMaxHwnd = nativeHwnd(host, pendingMaxId);
    expect(pendingMaxId != eui::kInvalidWindowId, "positioned=false 仍接受 pending maximize");
    expect(IsWindowVisible(pendingMaxHwnd) == FALSE && IsZoomed(pendingMaxHwnd) == FALSE,
           "pending maximize 创建阶段不显示也不提前最大化");
    expect(GetForegroundWindow() != pendingMaxHwnd, "pending maximize 创建阶段不抢前台");
    expect(host.showWindow(pendingMaxId), "首次显式 show 应用 pending maximize");
    pumpMessages();
    const auto maximizedPlacement = host.windowPlacement(pendingMaxId);
    expect(IsWindowVisible(pendingMaxHwnd) != FALSE && IsZoomed(pendingMaxHwnd) != FALSE,
           "首次显式 show 后窗口最大化");
    expect(maximizedPlacement.has_value() && maximizedPlacement->positioned &&
               maximizedPlacement->maximized,
           "最大化 query 返回 normal bounds 与 maximized");
    expect(host.destroyWindow(pendingMaxId), "销毁首次最大化窗口");

    eui::WindowConfig roundTripConfig = baseConfig("round-trip-main");
    roundTripConfig.initialPlacement = maximizedPlacement;
    const eui::WindowId roundTripId = host.createWindow(roundTripConfig);
    HWND roundTripHwnd = nativeHwnd(host, roundTripId);
    expect(roundTripId != eui::kInvalidWindowId, "使用 query placement 重建窗口");
    expect(IsWindowVisible(roundTripHwnd) == FALSE && IsZoomed(roundTripHwnd) == FALSE,
           "round-trip create 仍保持 hidden pending maximize");
    const auto roundTripHidden = host.windowPlacement(roundTripId);
    expect(roundTripHidden.has_value() && maximizedPlacement.has_value() &&
               sameNormalBounds(*roundTripHidden, *maximizedPlacement),
           "round-trip hidden normal bounds 一致");
    expect(host.showWindow(roundTripId), "round-trip 显式 show");
    pumpMessages();
    const auto roundTripShown = host.windowPlacement(roundTripId);
    expect(roundTripShown.has_value() && maximizedPlacement.has_value() &&
               roundTripShown->maximized && sameNormalBounds(*roundTripShown, *maximizedPlacement),
           "round-trip 显示后最大化且 normal bounds 一致");
    expect(host.destroyWindow(roundTripId), "销毁 round-trip 窗口");

    eui::WindowConfig shutdownMainConfig = baseConfig("shutdown-main");
    const eui::WindowId shutdownMainId = host.createWindow(shutdownMainConfig);
    eui::WindowConfig shutdownToolConfig = baseConfig("shutdown-tool");
    shutdownToolConfig.role = eui::WindowRole::Tool;
    shutdownToolConfig.owner = shutdownMainId;
    const eui::WindowId shutdownToolId = host.createWindow(shutdownToolConfig);
    eui::WindowConfig shutdownDialogConfig = baseConfig("shutdown-dialog");
    shutdownDialogConfig.role = eui::WindowRole::Dialog;
    shutdownDialogConfig.owner = shutdownToolId;
    const eui::WindowId shutdownDialogId = host.createWindow(shutdownDialogConfig);
    eui::WindowConfig shutdownPopupConfig = baseConfig("shutdown-popup");
    shutdownPopupConfig.role = eui::WindowRole::Popup;
    shutdownPopupConfig.owner = shutdownDialogId;
    const eui::WindowId shutdownPopupId = host.createWindow(shutdownPopupConfig);
    eui::WindowConfig shutdownOverlayConfig = baseConfig("shutdown-overlay");
    shutdownOverlayConfig.role = eui::WindowRole::Overlay;
    const eui::WindowId shutdownOverlayId = host.createWindow(shutdownOverlayConfig);
    HWND shutdownMainHwnd = nativeHwnd(host, shutdownMainId);
    HWND shutdownToolHwnd = nativeHwnd(host, shutdownToolId);
    HWND shutdownDialogHwnd = nativeHwnd(host, shutdownDialogId);
    HWND shutdownPopupHwnd = nativeHwnd(host, shutdownPopupId);
    HWND shutdownOverlayHwnd = nativeHwnd(host, shutdownOverlayId);
    expect(shutdownMainId != eui::kInvalidWindowId && shutdownToolId != eui::kInvalidWindowId &&
               shutdownDialogId != eui::kInvalidWindowId &&
               shutdownPopupId != eui::kInvalidWindowId &&
               shutdownOverlayId != eui::kInvalidWindowId,
           "创建 shutdown owner 链与独立 Overlay");
    host.shutdown();
    expect(IsWindow(shutdownPopupHwnd) == FALSE && IsWindow(shutdownDialogHwnd) == FALSE &&
               IsWindow(shutdownToolHwnd) == FALSE && IsWindow(shutdownMainHwnd) == FALSE &&
               IsWindow(shutdownOverlayHwnd) == FALSE,
           "shutdown 逆 WindowId 按 owned child-first 清理全部 HWND");

    std::fprintf(stderr, "hosted window contract: failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
#endif
}
