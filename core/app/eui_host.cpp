#include "eui/host.h"

#include "core/app/dsl_window_runtime.h"
#include "core/input/input_state.h"
#include "core/render/render_backend.h"
#include "core/render/render_surface.h"
#include "core/window/window_backend.h"

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <utility>

namespace eui {

namespace {

struct HostedWindow {
    WindowId id = kInvalidWindowId;
    GLFWwindow* window = nullptr;
    app::DslWindowRuntime runtime;
    std::unique_ptr<core::render::RenderBackend> renderer;
    bool visible = false;
    bool paintRequested = true;
    bool iconified = false;
    bool closeRequested = false;
    WindowRole role = WindowRole::Main;
    WindowId owner = kInvalidWindowId;
    bool maximizeOnFirstShow = false;
    double lastTick = 0.0;
    double nextDeadline = std::numeric_limits<double>::infinity();
};

struct EuiAppHostState {
    bool initialized = false;
    WindowId nextWindowId = 1;
    std::map<WindowId, std::unique_ptr<HostedWindow>> windows;
};

float windowDpiScale(GLFWwindow* window) {
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    glfwGetWindowContentScale(window, &scaleX, &scaleY);
    return std::max(0.01f, (scaleX + scaleY) * 0.5f);
}

float windowPointerScale(GLFWwindow* window) {
    int windowWidth = 0;
    int windowHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    if (windowWidth <= 0 || windowHeight <= 0) {
        return 1.0f;
    }

    const float scaleX = static_cast<float>(framebufferWidth) /
                         static_cast<float>(windowWidth);
    const float scaleY = static_cast<float>(framebufferHeight) /
                         static_cast<float>(windowHeight);
    return std::max(0.01f, (scaleX + scaleY) * 0.5f);
}

void requestFullPaint(HostedWindow& hosted) {
    hosted.paintRequested = true;
    hosted.runtime.requestFullPaint();
}

bool validPlacementBounds(const WindowPlacement& placement) {
    if (!placement.positioned) {
        return true;
    }
    if (placement.width <= 0 || placement.height <= 0) {
        return false;
    }
    const std::int64_t right = static_cast<std::int64_t>(placement.x) + placement.width;
    const std::int64_t bottom = static_cast<std::int64_t>(placement.y) + placement.height;
    return right >= std::numeric_limits<int>::min() &&
           right <= std::numeric_limits<int>::max() &&
           bottom >= std::numeric_limits<int>::min() &&
           bottom <= std::numeric_limits<int>::max();
}

void showHostedWindow(HostedWindow& hosted) {
    if (hosted.maximizeOnFirstShow) {
        // 最大化推迟到显式 show，确保 renderer、Runtime 和输入链准备完成前 HWND 始终隐藏。
        glfwMaximizeWindow(hosted.window);
        hosted.maximizeOnFirstShow = false;
    }
    glfwShowWindow(hosted.window);
}

void installWindowCallbacks(HostedWindow& hosted) {
    glfwSetWindowUserPointer(hosted.window, &hosted);
    glfwSetFramebufferSizeCallback(hosted.window, [](GLFWwindow* current, int width, int height) {
        auto* hosted = static_cast<HostedWindow*>(glfwGetWindowUserPointer(current));
        if (hosted == nullptr) {
            return;
        }
        hosted->paintRequested = true;
        if (width > 0 && height > 0) {
            hosted->runtime.requestFullPaint();
        }
    });
    glfwSetWindowRefreshCallback(hosted.window, [](GLFWwindow* current) {
        auto* hosted = static_cast<HostedWindow*>(glfwGetWindowUserPointer(current));
        if (hosted != nullptr) {
            requestFullPaint(*hosted);
        }
    });
    glfwSetWindowContentScaleCallback(hosted.window, [](GLFWwindow* current, float, float) {
        auto* hosted = static_cast<HostedWindow*>(glfwGetWindowUserPointer(current));
        if (hosted != nullptr) {
            requestFullPaint(*hosted);
        }
    });
    glfwSetWindowFocusCallback(hosted.window, [](GLFWwindow* current, int) {
        auto* hosted = static_cast<HostedWindow*>(glfwGetWindowUserPointer(current));
        if (hosted != nullptr) {
            hosted->paintRequested = true;
        }
    });
    glfwSetWindowIconifyCallback(hosted.window, [](GLFWwindow* current, int iconified) {
        auto* hosted = static_cast<HostedWindow*>(glfwGetWindowUserPointer(current));
        if (hosted == nullptr) {
            return;
        }
        hosted->iconified = iconified == GLFW_TRUE;
        if (!hosted->iconified) {
            requestFullPaint(*hosted);
        }
    });
    glfwSetWindowCloseCallback(hosted.window, [](GLFWwindow* current) {
        auto* hosted = static_cast<HostedWindow*>(glfwGetWindowUserPointer(current));
        if (hosted != nullptr) {
            // 关闭请求只记录状态；销毁由宿主显式决定，避免误退出后台 App。
            hosted->closeRequested = true;
        }
    });
}

void destroyHostedWindow(std::unique_ptr<HostedWindow>& hosted) {
    if (!hosted || hosted->window == nullptr) {
        hosted.reset();
        return;
    }

    GLFWwindow* window = hosted->window;
    if (hosted->renderer) {
        hosted->renderer->makeCurrent();
        hosted->renderer->releaseRenderCache();
        core::render::ScopedRenderBackend scoped(*hosted->renderer);
        hosted->runtime.shutdown(false);
    } else {
        hosted->runtime.shutdown(false);
    }
    core::releaseInputQueue(window);
    hosted->renderer.reset();
    core::window::destroyWindow(window);
    hosted.reset();
}

} // namespace

struct EuiAppHost::Impl {
    EuiAppHostState state;
};

EuiAppHost::EuiAppHost() : impl_(std::make_unique<Impl>()) {}

EuiAppHost::~EuiAppHost() {
    shutdown();
}

bool EuiAppHost::initialize() {
    if (impl_->state.initialized) {
        return true;
    }

    core::render::initializeRenderBackendLoader();
    if (!glfwInit()) {
        return false;
    }
    impl_->state.initialized = true;
    return true;
}

void EuiAppHost::shutdown() {
    if (!impl_->state.initialized) {
        return;
    }

    // Win32 会随 owner 自动销毁 owned HWND；必须先释放 Tool，避免留下失效的 GLFW/IME 状态。
    for (auto& entry : impl_->state.windows) {
        if (entry.second && entry.second->role == WindowRole::Tool) {
            destroyHostedWindow(entry.second);
        }
    }
    for (auto& entry : impl_->state.windows) {
        if (entry.second) {
            destroyHostedWindow(entry.second);
        }
    }
    impl_->state.windows.clear();
    glfwTerminate();
    impl_->state.initialized = false;
    impl_->state.nextWindowId = 1;
}

bool EuiAppHost::initialized() const {
    return impl_->state.initialized;
}

double EuiAppHost::timeSeconds() const {
    return impl_->state.initialized ? core::window::timeSeconds() : 0.0;
}

WindowId EuiAppHost::createWindow(const WindowConfig& config) {
    if (!impl_->state.initialized || !config.compose ||
        config.width <= 0 || config.height <= 0 ||
        (config.initialPlacement && !validPlacementBounds(*config.initialPlacement))) {
        return kInvalidWindowId;
    }

    HostedWindow* owner = nullptr;
    if (config.role == WindowRole::Tool) {
        const auto ownerIterator = impl_->state.windows.find(config.owner);
        if (config.owner == kInvalidWindowId || ownerIterator == impl_->state.windows.end() ||
            !ownerIterator->second || ownerIterator->second->role != WindowRole::Main) {
            return kInvalidWindowId;
        }
        owner = ownerIterator->second.get();
    } else if (config.owner != kInvalidWindowId) {
        return kInvalidWindowId;
    }

    app::DslWindowRequest request;
    request.title = config.title;
    request.pageId = config.pageId;
    request.clearColor = config.clearColor;
    request.width = config.width;
    request.height = config.height;
    request.modal = config.modal;
    request.compose = config.compose;

    core::window::WindowCreateRequest nativeRequest;
    nativeRequest.width = config.width;
    nativeRequest.height = config.height;
    nativeRequest.title = request.title.c_str();
    nativeRequest.resizable = config.resizable;
    nativeRequest.highDpi = config.highDpi;
    nativeRequest.modal = config.modal;
    // hosted 先完成角色、placement、renderer、Runtime 与输入链，再按 config.visible 显式显示。
    nativeRequest.visible = false;
    nativeRequest.role = config.role;
    nativeRequest.owner = owner != nullptr ? owner->window : nullptr;
    nativeRequest.initialPlacement = config.initialPlacement;
    nativeRequest.renderApi = core::render::windowRenderApi();
    // WindowCreateRequest::parent 当前表示 OpenGL context share，不是 HWND 父子关系。
    nativeRequest.parent = nullptr;

    auto* window = static_cast<GLFWwindow*>(core::window::createWindow(nativeRequest));
    if (window == nullptr) {
        return kInvalidWindowId;
    }

    auto renderer = core::render::createRenderBackend(window);
    if (!renderer || !renderer->initialize()) {
        renderer.reset();
        core::window::destroyWindow(window);
        return kInvalidWindowId;
    }

    auto hosted = std::make_unique<HostedWindow>();
    hosted->id = impl_->state.nextWindowId++;
    hosted->window = window;
    hosted->renderer = std::move(renderer);
    hosted->visible = config.visible;
    hosted->role = config.role;
    hosted->owner = config.owner;
    hosted->maximizeOnFirstShow =
        config.initialPlacement && config.initialPlacement->maximized;
    hosted->lastTick = core::window::timeSeconds();
    hosted->nextDeadline = hosted->lastTick;
    // hosted 模式不依赖 standalone app 配置；窗口缩放由 DPI 和此显式值决定。
    if (!hosted->runtime.initialize(window, std::move(request), 1.0f)) {
        destroyHostedWindow(hosted);
        return kInvalidWindowId;
    }
    installWindowCallbacks(*hosted);

    const WindowId id = hosted->id;
    if (config.visible) {
        showHostedWindow(*hosted);
    } else {
        glfwHideWindow(window);
        hosted->paintRequested = false;
    }
    impl_->state.windows.emplace(id, std::move(hosted));
    return id;
}

bool EuiAppHost::showWindow(WindowId id) {
    const auto iterator = impl_->state.windows.find(id);
    if (iterator == impl_->state.windows.end() || !iterator->second) {
        return false;
    }

    HostedWindow& hosted = *iterator->second;
    hosted.visible = true;
    hosted.closeRequested = false;
    glfwSetWindowShouldClose(hosted.window, GLFW_FALSE);
    if (glfwGetWindowAttrib(hosted.window, GLFW_ICONIFIED) == GLFW_TRUE) {
        glfwRestoreWindow(hosted.window);
    }
    showHostedWindow(hosted);
    requestFullPaint(hosted);
    hosted.lastTick = core::window::timeSeconds();
    hosted.nextDeadline = hosted.lastTick;
    return true;
}

bool EuiAppHost::hideWindow(WindowId id) {
    const auto iterator = impl_->state.windows.find(id);
    if (iterator == impl_->state.windows.end() || !iterator->second) {
        return false;
    }

    HostedWindow& hosted = *iterator->second;
    hosted.visible = false;
    glfwHideWindow(hosted.window);
    if (hosted.renderer) {
        hosted.renderer->makeCurrent();
        hosted.renderer->releaseRenderCache();
    }
    // 隐藏窗口释放渲染缓存；Runtime 状态保留，显示时通过完整重绘恢复。
    hosted.runtime.requestFullPaint();
    hosted.paintRequested = false;
    hosted.nextDeadline = std::numeric_limits<double>::infinity();
    return true;
}

bool EuiAppHost::destroyWindow(WindowId id) {
    const auto iterator = impl_->state.windows.find(id);
    if (iterator == impl_->state.windows.end()) {
        return false;
    }

    for (const auto& entry : impl_->state.windows) {
        if (entry.second && entry.second->owner == id) {
            // 不隐式级联，避免调用方仍持有的 Tool WindowId 静默失效。
            return false;
        }
    }

    destroyHostedWindow(iterator->second);
    impl_->state.windows.erase(iterator);
    return true;
}

bool EuiAppHost::isVisible(WindowId id) const {
    const auto iterator = impl_->state.windows.find(id);
    return iterator != impl_->state.windows.end() && iterator->second && iterator->second->visible;
}

bool EuiAppHost::shouldClose(WindowId id) const {
    const auto iterator = impl_->state.windows.find(id);
    if (iterator == impl_->state.windows.end() || !iterator->second) {
        return false;
    }
    return iterator->second->closeRequested || glfwWindowShouldClose(iterator->second->window);
}

std::optional<WindowPlacement> EuiAppHost::windowPlacement(WindowId id) const {
    const auto iterator = impl_->state.windows.find(id);
    if (iterator == impl_->state.windows.end() || !iterator->second) {
        return std::nullopt;
    }
    WindowPlacement placement;
    if (!core::window::queryWindowPlacement(iterator->second->window, placement)) {
        return std::nullopt;
    }
    return placement;
}

window::NativeWindowInfo EuiAppHost::nativeWindowInfo(WindowId id) const {
    const auto iterator = impl_->state.windows.find(id);
    if (iterator == impl_->state.windows.end() || !iterator->second) {
        return {};
    }
    return core::window::nativeWindowInfo(iterator->second->window);
}

TickResult EuiAppHost::tick(double nowSeconds, bool updateRequested) {
    TickResult result;
    if (!impl_->state.initialized) {
        return result;
    }

    const double now = nowSeconds >= 0.0 ? nowSeconds : core::window::timeSeconds();

    for (auto& entry : impl_->state.windows) {
        HostedWindow& hosted = *entry.second;
        if (hosted.closeRequested || glfwWindowShouldClose(hosted.window)) {
            result.hasClosedWindows = true;
        }
        if (!hosted.visible || hosted.closeRequested || glfwWindowShouldClose(hosted.window)) {
            continue;
        }

        result.hasVisibleWindows = true;
        hosted.iconified = glfwGetWindowAttrib(hosted.window, GLFW_ICONIFIED) == GLFW_TRUE;
        if (hosted.iconified) {
            if (hosted.renderer) {
                hosted.renderer->makeCurrent();
                hosted.renderer->releaseRenderCache();
            }
            requestFullPaint(hosted);
            hosted.nextDeadline = std::numeric_limits<double>::infinity();
            continue;
        }

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(hosted.window, &framebufferWidth, &framebufferHeight);
        if (framebufferWidth <= 0 || framebufferHeight <= 0) {
            requestFullPaint(hosted);
            hosted.nextDeadline = now;
            result.needsRender = true;
            continue;
        }

        const float dpiScale = windowDpiScale(hosted.window);
        const float pointerScale = windowPointerScale(hosted.window);
        const float logicalWidth = static_cast<float>(framebufferWidth) / dpiScale;
        const float logicalHeight = static_cast<float>(framebufferHeight) / dpiScale;
        const float deltaSeconds = static_cast<float>(std::clamp(
            now - hosted.lastTick, 0.0, 0.25));
        hosted.lastTick = now;

        if (hosted.runtime.update(hosted.window,
                                  deltaSeconds,
                                  logicalWidth,
                                  logicalHeight,
                                  pointerScale,
                                  dpiScale,
                                  updateRequested)) {
            hosted.paintRequested = true;
        }

        const bool animating = hosted.runtime.isAnimating();
        const bool needsRender = hosted.paintRequested || hosted.runtime.paintRequested();
        result.animating = result.animating || animating;
        result.needsRender = result.needsRender || needsRender;
        if (needsRender) {
            hosted.nextDeadline = now;
        } else if (animating) {
            hosted.nextDeadline = now + (1.0 / 60.0);
        } else {
            hosted.nextDeadline = std::numeric_limits<double>::infinity();
        }
        result.nextDeadline = std::min(result.nextDeadline, hosted.nextDeadline);
    }

    return result;
}

bool EuiAppHost::render() {
    if (!impl_->state.initialized) {
        return false;
    }

    bool rendered = false;
    for (auto& entry : impl_->state.windows) {
        HostedWindow& hosted = *entry.second;
        if (!hosted.visible || hosted.closeRequested || glfwWindowShouldClose(hosted.window) ||
            hosted.iconified || !hosted.renderer ||
            (!hosted.paintRequested && !hosted.runtime.paintRequested())) {
            continue;
        }

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(hosted.window, &framebufferWidth, &framebufferHeight);
        if (framebufferWidth <= 0 || framebufferHeight <= 0) {
            continue;
        }

        const float dpiScale = windowDpiScale(hosted.window);
        hosted.renderer->makeCurrent();
        hosted.renderer->beginFrame({
            hosted.window,
            core::window::nativeWindowInfo(hosted.window),
            framebufferWidth,
            framebufferHeight,
            dpiScale
        });
        hosted.runtime.render(*hosted.renderer, framebufferWidth, framebufferHeight, dpiScale);
        hosted.renderer->present();
        hosted.paintRequested = false;
        // deadline 沿用最近 tick 的宿主时钟域，不在 render 阶段切回其他 epoch。
        hosted.nextDeadline = hosted.runtime.isAnimating()
            ? hosted.lastTick + (1.0 / 60.0)
            : std::numeric_limits<double>::infinity();
        rendered = true;
    }
    return rendered;
}

double EuiAppHost::nextDeadline() const {
    double deadline = std::numeric_limits<double>::infinity();
    for (const auto& entry : impl_->state.windows) {
        if (entry.second && entry.second->visible) {
            deadline = std::min(deadline, entry.second->nextDeadline);
        }
    }
    return deadline;
}

} // namespace eui
