#pragma once

#include <optional>

namespace core::window {

using Handle = void*;
using ContextKey = void*;
using CursorHandle = void*;

enum class CursorType {
    Arrow,
    Hand
};

enum class RenderApi {
    OpenGL,
    Vulkan
};

enum class WindowRole {
    Main,
    Tool,
    Dialog,
    Popup,
    Overlay
};

struct WindowPlacement {
    // false 表示 normal 位置和尺寸继续由平台决定，但 maximized 仍作为首次显示状态生效。
    bool positioned = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool maximized = false;
};

struct WindowCreateRequest {
    int width = 0;
    int height = 0;
    // 0 表示对应方向不限制最小尺寸，负值属于非法请求。
    int minWidth = 0;
    int minHeight = 0;
    const char* title = "";
    bool resizable = true;
    bool highDpi = true;
    bool modal = false;
    bool visible = true;
    bool borderless = false;
    bool transparentFramebuffer = false;
    bool alwaysOnTop = false;
    bool noActivate = false;
    bool clickThrough = false;
    bool focusOnShow = true;
    WindowRole role = WindowRole::Main;
    // owner 是后端窗口句柄，独立于 OpenGL context share 使用的 parent。
    Handle owner = nullptr;
    std::optional<WindowPlacement> initialPlacement;
    Handle parent = nullptr;
    RenderApi renderApi = RenderApi::OpenGL;
};

struct NativeWindowInfo {
    Handle handle = nullptr;
    void* platformWindow = nullptr;
    void* platformDisplay = nullptr;
    void* platformView = nullptr;
};

} // namespace core::window
