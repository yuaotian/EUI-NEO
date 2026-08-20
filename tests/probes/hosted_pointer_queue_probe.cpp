#include "eui/host.h"

#include <cstdio>

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
    std::fprintf(stderr, "%s hosted pointer queue: %s\n", condition ? "[PASS]" : "[FAIL]", label);
    if (!condition) {
        ++failures;
    }
}

#if defined(_WIN32)
void pumpMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

bool setClientCursorPosition(HWND hwnd, POINT clientPoint) {
    POINT screenPoint = clientPoint;
    return hwnd != nullptr &&
           ClientToScreen(hwnd, &screenPoint) != FALSE &&
           SetCursorPos(screenPoint.x, screenPoint.y) != FALSE;
}

bool queuePointerSequence(HWND hwnd,
                          POINT pressPoint,
                          POINT releasePoint,
                          POINT finalPoint) {
    return setClientCursorPosition(hwnd, pressPoint) &&
           PostMessageW(hwnd,
                        WM_LBUTTONDOWN,
                        MK_LBUTTON,
                        MAKELPARAM(pressPoint.x, pressPoint.y)) != FALSE &&
           setClientCursorPosition(hwnd, releasePoint) &&
           PostMessageW(hwnd,
                        WM_LBUTTONUP,
                        0,
                        MAKELPARAM(releasePoint.x, releasePoint.y)) != FALSE &&
           setClientCursorPosition(hwnd, finalPoint);
}
#endif

} // namespace

int main() {
#if !defined(_WIN32)
    std::fprintf(stderr, "[SKIP] hosted pointer queue: 当前 probe 只验证 Win32 原生消息坐标\n");
    return 0;
#else
    SetConsoleOutputCP(CP_UTF8);

    eui::EuiAppHost host;
    expect(host.initialize(), "host 初始化");
    if (!host.initialized()) {
        return 1;
    }

    int clickCount = 0;
    int dragCount = 0;
    double lastDragX = 0.0;
    double lastDragY = 0.0;
    bool hovered = false;
    eui::WindowConfig config;
    config.title = "hosted-pointer-queue";
    config.pageId = "hosted-pointer-queue";
    config.role = eui::WindowRole::Popup;
    config.width = 320;
    config.height = 240;
    config.visible = false;
    config.initialPlacement = eui::WindowPlacement{true, 320, 220, 320, 240, false};
    config.compose =
        [&clickCount, &dragCount, &lastDragX, &lastDragY, &hovered](
            eui::Ui& ui,
            const eui::Screen& screen) {
        ui.stack("pointer-target")
            .size(screen.width, screen.height)
            .onClick([&clickCount] { ++clickCount; })
            .onDrag([&dragCount, &lastDragX, &lastDragY](const auto& event) {
                ++dragCount;
                lastDragX = event.x;
                lastDragY = event.y;
            })
            .onHover([&hovered](bool value) { hovered = value; })
            .build();
    };

    const eui::WindowId id = host.createWindow(config);
    const HWND hwnd = static_cast<HWND>(host.nativeWindowInfo(id).platformWindow);
    expect(id != eui::kInvalidWindowId && hwnd != nullptr, "创建 Popup 输入窗口");
    expect(host.showWindow(id), "显示 Popup 输入窗口");
    pumpMessages();
    auto frame = host.tick(host.timeSeconds(), true);
    if (frame.needsRender) {
        host.render();
    }

    RECT client{};
    expect(GetClientRect(hwnd, &client) != FALSE && client.right > 0 && client.bottom > 0,
           "读取 Popup 客户区");
    const POINT inside{client.right / 2, client.bottom / 2};
    const POINT outside{-30, -30};
    POINT originalCursor{};
    const bool capturedOriginalCursor = GetCursorPos(&originalCursor) != FALSE;

    auto dispatchPointerSequence = [&](POINT press, POINT release, POINT final) {
        const bool queued = queuePointerSequence(hwnd, press, release, final);
        pumpMessages();
        frame = host.tick(host.timeSeconds(), false);
        if (frame.needsRender) {
            host.render();
        }
        return queued;
    };

    expect(dispatchPointerSequence(inside, inside, outside) && clickCount == 1 && !hovered,
           "目标内按下释放后移出仍点击一次，最终 hover 在外");
    expect(dispatchPointerSequence(outside, outside, inside) && clickCount == 1 && hovered,
           "目标外按下释放后移入不伪点击，最终 hover 在内");
    const int dragCountBeforeReleaseMove = dragCount;
    expect(dispatchPointerSequence(inside, outside, inside) &&
               clickCount == 1 && hovered && dragCount > dragCountBeforeReleaseMove &&
               lastDragX < 0.0 && lastDragY < 0.0,
           "目标内按下、目标外释放不点击，并兑现释放前拖动终点");
    expect(dispatchPointerSequence(inside, inside, inside) && clickCount == 2 && hovered,
           "同坐标同 tick 按下释放仍点击一次");

    if (capturedOriginalCursor) {
        SetCursorPos(originalCursor.x, originalCursor.y);
    }
    expect(host.destroyWindow(id), "销毁 Popup 输入窗口");
    host.shutdown();

    std::fprintf(stderr, "hosted pointer queue: failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
#endif
}
