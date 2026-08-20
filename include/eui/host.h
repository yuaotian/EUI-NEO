#pragma once

#include "eui/dsl.h"
#include "eui/types.h"
#include "eui/window.h"

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>

namespace eui {

using WindowId = std::uint64_t;
constexpr WindowId kInvalidWindowId = 0;
using WindowPlacement = window::WindowPlacement;
using WindowRole = window::WindowRole;

// 宿主窗口描述只保存 EUI 页面所需的数据，不接管宿主线程的消息泵。
struct WindowConfig {
    std::string title = "EUI Window";
    std::string pageId = "window";
    Color clearColor = {0.16f, 0.18f, 0.20f, 1.0f};
    int width = 640;
    int height = 420;
    // 0 表示对应方向不限制最小尺寸，负值属于非法请求。
    int minWidth = 0;
    int minHeight = 0;
    bool resizable = true;
    bool highDpi = true;
    bool visible = true;
    bool borderless = false;
    bool transparentFramebuffer = false;
    bool alwaysOnTop = false;
    bool noActivate = false;
    bool clickThrough = false;
    bool focusOnShow = true;
    WindowRole role = WindowRole::Main;
    // Popup 强制 borderless/noActivate；Overlay 另强制 transparentFramebuffer/alwaysOnTop。
    // Tool 仅允许 Main owner；Dialog 必填、Popup 可选，且二者 owner 必须可激活并属于同一 host。
    WindowId owner = kInvalidWindowId;
    // positioned=false 时由平台决定 normal bounds，但仍保留 maximized 首次显示状态。
    // noActivate 窗口不接受 maximized initial/dynamic placement。
    std::optional<WindowPlacement> initialPlacement;
    // 首版保留 EUI 请求语义，不把它解释为 Win32 父子窗口关系。
    bool modal = false;
    std::function<void(Ui&, const Screen&)> compose;
};

struct TickResult {
    bool needsRender = false;
    bool animating = false;
    bool hasVisibleWindows = false;
    bool hasClosedWindows = false;
    double nextDeadline = std::numeric_limits<double>::infinity();
};

// EUI hosted 生命周期适配器。
//
// 线程约束：所有方法必须在创建窗口的 UI 线程调用。消息泵由调用方统一
// PeekMessage/DispatchMessage；本类只处理 GLFW 输入回调、Runtime 更新和绘制。
class EuiAppHost {
public:
    EuiAppHost();
    ~EuiAppHost();

    EuiAppHost(const EuiAppHost&) = delete;
    EuiAppHost& operator=(const EuiAppHost&) = delete;

    bool initialize();
    void shutdown();
    bool initialized() const;
    // initialize 成功后，宿主使用此时钟计算 nextDeadline - timeSeconds()。
    double timeSeconds() const;

    WindowId createWindow(const WindowConfig& config);
    bool showWindow(WindowId id);
    // 存在 visible modal child 时拒绝隐藏 owner，并把焦点保留给 modal child。
    bool hideWindow(WindowId id);
    bool setWindowAlwaysOnTop(WindowId id, bool enabled);
    bool setWindowClickThrough(WindowId id, bool enabled);
    bool setWindowPlacement(WindowId id, const WindowPlacement& placement);
    // 窗口仍有 owned child 时返回 false，调用方必须先显式销毁 child。
    bool destroyWindow(WindowId id);
    bool isVisible(WindowId id) const;
    bool shouldClose(WindowId id) const;
    // 返回当前角色的 normal bounds；坐标只支持相同角色的持久化 round-trip。
    std::optional<WindowPlacement> windowPlacement(WindowId id) const;
    window::NativeWindowInfo nativeWindowInfo(WindowId id) const;

    // 显式 nowSeconds 必须来自 timeSeconds()；-1 表示内部读取同一时钟。
    // tick 只更新状态，不读取线程消息队列，也不提交交换缓冲区。
    TickResult tick(double nowSeconds = -1.0, bool updateRequested = false);
    bool render();
    double nextDeadline() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace eui
