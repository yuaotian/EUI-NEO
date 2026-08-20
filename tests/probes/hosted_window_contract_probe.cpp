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

bool hasRoleStyle(HWND hwnd, eui::WindowRole role) {
    if (hwnd == nullptr) {
        return false;
    }
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (role == eui::WindowRole::Tool) {
        return (style & WS_EX_TOOLWINDOW) != 0 && (style & WS_EX_APPWINDOW) == 0;
    }
    return (style & WS_EX_APPWINDOW) != 0 && (style & WS_EX_TOOLWINDOW) == 0;
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

    eui::WindowConfig mainConfig = baseConfig("contract-main");
    mainConfig.role = eui::WindowRole::Main;
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

    const auto mainInitial = host.windowPlacement(mainId);
    expect(mainInitial.has_value() && sameNormalBounds(*mainInitial, *mainConfig.initialPlacement),
           "Main initial placement 可查询");

    eui::WindowConfig mainWithOwner = baseConfig("main-with-owner");
    mainWithOwner.owner = mainId;
    expect(host.createWindow(mainWithOwner) == eui::kInvalidWindowId, "拒绝 Main owner");

    eui::WindowConfig toolConfig = baseConfig("contract-tool");
    toolConfig.role = eui::WindowRole::Tool;
    toolConfig.owner = mainId;
    toolConfig.initialPlacement = eui::WindowPlacement{true, 320, 260, 480, 300, false};
    const eui::WindowId toolId = host.createWindow(toolConfig);
    expect(toolId != eui::kInvalidWindowId, "创建隐藏 Tool");
    HWND toolHwnd = nativeHwnd(host, toolId);
    expect(toolHwnd != nullptr && IsWindow(toolHwnd) != FALSE, "Tool native HWND 有效");
    expect(IsWindowVisible(toolHwnd) == FALSE, "Tool create 后保持隐藏");
    expect(GetForegroundWindow() == foregroundBeforeTool, "Tool create 不改变前台窗口");
    expect(hasRoleStyle(toolHwnd, eui::WindowRole::Tool), "Tool 使用 TOOLWINDOW 样式");
    expect(GetWindow(toolHwnd, GW_OWNER) == mainHwnd, "Tool GW_OWNER 指向 Main");

    const auto toolInitial = host.windowPlacement(toolId);
    expect(toolInitial.has_value() && sameNormalBounds(*toolInitial, *toolConfig.initialPlacement),
           "Tool initial placement 可查询");

    eui::WindowConfig toolOwnedByTool = baseConfig("tool-owned-by-tool");
    toolOwnedByTool.role = eui::WindowRole::Tool;
    toolOwnedByTool.owner = toolId;
    expect(host.createWindow(toolOwnedByTool) == eui::kInvalidWindowId, "拒绝 Tool owner 链");

    const LONG_PTR mainProc = GetWindowLongPtrW(mainHwnd, GWLP_WNDPROC);
    const LONG_PTR toolProc = GetWindowLongPtrW(toolHwnd, GWLP_WNDPROC);
    expect(mainProc != 0 && GetPropW(mainHwnd, L"EuiNeoImeFilter") != nullptr,
           "Main IME filter 已安装");
    expect(toolProc != 0 && GetPropW(toolHwnd, L"EuiNeoImeFilter") != nullptr,
           "Tool IME filter 已安装");

    expect(host.showWindow(mainId), "显式显示 Main");
    expect(host.showWindow(toolId), "显式显示 Tool");
    pumpMessages();
    expect(IsWindowVisible(mainHwnd) != FALSE && IsWindowVisible(toolHwnd) != FALSE,
           "显式 show 后窗口可见");
    expect(GetWindowLongPtrW(mainHwnd, GWLP_WNDPROC) == mainProc &&
               GetWindowLongPtrW(toolHwnd, GWLP_WNDPROC) == toolProc,
           "show/placement 不覆盖 IME WndProc");
    expect(!host.destroyWindow(mainId), "Main 存在 Tool 时拒绝销毁");
    expect(IsWindow(mainHwnd) != FALSE && IsWindow(toolHwnd) != FALSE,
           "拒绝销毁后 owner 与 Tool 仍有效");
    expect(host.destroyWindow(toolId), "先销毁 Tool");
    expect(host.destroyWindow(mainId), "再销毁 Main");

    eui::WindowConfig pendingMaxConfig = baseConfig("pending-max-main");
    pendingMaxConfig.initialPlacement = eui::WindowPlacement{false, 0, 0, 0, 0, true};
    const HWND foregroundBeforeMax = GetForegroundWindow();
    const eui::WindowId pendingMaxId = host.createWindow(pendingMaxConfig);
    HWND pendingMaxHwnd = nativeHwnd(host, pendingMaxId);
    expect(pendingMaxId != eui::kInvalidWindowId, "positioned=false 仍接受 pending maximize");
    expect(IsWindowVisible(pendingMaxHwnd) == FALSE && IsZoomed(pendingMaxHwnd) == FALSE,
           "pending maximize 创建阶段不显示也不提前最大化");
    expect(GetForegroundWindow() == foregroundBeforeMax, "pending maximize 创建阶段不抢前台");
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
    HWND shutdownMainHwnd = nativeHwnd(host, shutdownMainId);
    HWND shutdownToolHwnd = nativeHwnd(host, shutdownToolId);
    expect(shutdownMainId != eui::kInvalidWindowId && shutdownToolId != eui::kInvalidWindowId,
           "创建 shutdown owner 关系");
    host.shutdown();
    expect(IsWindow(shutdownToolHwnd) == FALSE && IsWindow(shutdownMainHwnd) == FALSE,
           "shutdown 按 Tool-first/Main-last 清理 HWND");

    std::fprintf(stderr, "hosted window contract: failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
#endif
}
