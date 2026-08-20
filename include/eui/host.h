#pragma once

#include "eui/dsl.h"
#include "eui/types.h"
#include "eui/window.h"

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>

namespace eui {

using WindowId = std::uint64_t;
constexpr WindowId kInvalidWindowId = 0;

// 宿主窗口描述只保存 EUI 页面所需的数据，不接管宿主线程的消息泵。
struct WindowConfig {
    std::string title = "EUI Window";
    std::string pageId = "window";
    Color clearColor = {0.16f, 0.18f, 0.20f, 1.0f};
    int width = 640;
    int height = 420;
    bool resizable = true;
    bool highDpi = true;
    bool visible = true;
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

    WindowId createWindow(const WindowConfig& config);
    bool showWindow(WindowId id);
    bool hideWindow(WindowId id);
    bool destroyWindow(WindowId id);
    bool isVisible(WindowId id) const;
    bool shouldClose(WindowId id) const;
    window::NativeWindowInfo nativeWindowInfo(WindowId id) const;

    // nowSeconds 使用与 core::window::timeSeconds 相同的单调时钟单位。
    // tick 只更新状态，不读取线程消息队列，也不提交交换缓冲区。
    TickResult tick(double nowSeconds = -1.0, bool updateRequested = false);
    bool render();
    double nextDeadline() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace eui
