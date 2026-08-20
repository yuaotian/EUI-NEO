#include "eui/host.h"

#include "core/app/dsl_window_runtime.h"
#include "core/input/input_state.h"
#include "core/render/render_backend.h"
#include "core/render/render_surface.h"
#include "core/window/window_backend.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <utility>

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

namespace eui {

namespace {

struct EuiAppHostState;

struct HostedWindow {
    EuiAppHostState* hostState = nullptr;
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
    bool modal = false;
    bool modalActive = false;
    bool borderless = false;
    bool noActivate = false;
    bool alwaysOnTop = false;
    bool clickThrough = false;
    bool maximizeOnFirstShow = false;
    double lastTick = 0.0;
    double nextDeadline = std::numeric_limits<double>::infinity();
};

struct ModalOwnerState {
    std::size_t visibleModalCount = 0;
    bool restoreEnabled = false;
};

struct EuiAppHostState {
    bool initialized = false;
    bool shuttingDown = false;
    WindowId nextWindowId = 1;
    std::map<WindowId, std::unique_ptr<HostedWindow>> windows;
    std::map<WindowId, ModalOwnerState> modalOwners;
};

WindowConfig normalizedWindowConfig(const WindowConfig& config) {
    WindowConfig result = config;
    if (result.role == WindowRole::Popup) {
        result.borderless = true;
        result.noActivate = true;
    } else if (result.role == WindowRole::Overlay) {
        result.borderless = true;
        result.transparentFramebuffer = true;
        result.alwaysOnTop = true;
        result.noActivate = true;
    }
    if (result.noActivate) {
        result.focusOnShow = false;
    }
    if (result.modal && result.role == WindowRole::Dialog) {
        result.focusOnShow = true;
    }
    return result;
}

HostedWindow* newestVisibleModalChild(EuiAppHostState& state,
                                      WindowId owner,
                                      WindowId excluded = kInvalidWindowId) {
    for (auto iterator = state.windows.rbegin(); iterator != state.windows.rend(); ++iterator) {
        HostedWindow* candidate = iterator->second.get();
        if (candidate != nullptr && candidate->id != excluded && candidate->modal &&
            candidate->visible && candidate->owner == owner) {
            return candidate;
        }
    }
    return nullptr;
}

#if defined(_WIN32)
HWND nativeHwnd(const HostedWindow& hosted) {
    return static_cast<HWND>(core::window::nativeWindowInfo(hosted.window).platformWindow);
}
#endif

void focusHostedWindow(HostedWindow* hosted) {
    if (hosted != nullptr && hosted->visible && !hosted->noActivate) {
        glfwFocusWindow(hosted->window);
    }
}

bool foregroundBelongsToHostedSubtree(const EuiAppHostState& state, WindowId root) {
#if defined(_WIN32)
    HWND activeHwnd = GetForegroundWindow();
    if (activeHwnd == nullptr) {
        activeHwnd = GetActiveWindow();
    }

    WindowId activeId = kInvalidWindowId;
    for (const auto& entry : state.windows) {
        if (entry.second && nativeHwnd(*entry.second) == activeHwnd) {
            activeId = entry.first;
            break;
        }
    }
    while (activeId != kInvalidWindowId) {
        if (activeId == root) {
            return true;
        }
        const auto iterator = state.windows.find(activeId);
        if (iterator == state.windows.end() || !iterator->second) {
            return false;
        }
        activeId = iterator->second->owner;
    }
    return false;
#else
    (void)state;
    (void)root;
    return true;
#endif
}

bool shouldRestoreModalFocus(const EuiAppHostState& state, const HostedWindow& modal) {
    return modal.modalActive && foregroundBelongsToHostedSubtree(state, modal.id);
}

void activateModalOwner(EuiAppHostState& state, HostedWindow& modal) {
    if (!modal.modal || modal.modalActive || modal.owner == kInvalidWindowId) {
        return;
    }
    const auto ownerIterator = state.windows.find(modal.owner);
    if (ownerIterator == state.windows.end() || !ownerIterator->second) {
        return;
    }

    ModalOwnerState& ownerState = state.modalOwners[modal.owner];
#if defined(_WIN32)
    HWND ownerHwnd = nativeHwnd(*ownerIterator->second);
    if (ownerState.visibleModalCount == 0 && ownerHwnd != nullptr && IsWindow(ownerHwnd) != FALSE) {
        ownerState.restoreEnabled = IsWindowEnabled(ownerHwnd) != FALSE;
        if (ownerState.restoreEnabled) {
            EnableWindow(ownerHwnd, FALSE);
        }
    }
#endif
    ++ownerState.visibleModalCount;
    modal.modalActive = true;
}

void deactivateModalOwner(EuiAppHostState& state, HostedWindow& modal, bool restoreFocus) {
    if (!modal.modalActive || modal.owner == kInvalidWindowId) {
        return;
    }
    modal.modalActive = false;

    const auto ownerStateIterator = state.modalOwners.find(modal.owner);
    if (ownerStateIterator == state.modalOwners.end()) {
        return;
    }
    ModalOwnerState& ownerState = ownerStateIterator->second;
    if (ownerState.visibleModalCount > 0) {
        --ownerState.visibleModalCount;
    }
    if (ownerState.visibleModalCount > 0) {
        if (restoreFocus) {
            focusHostedWindow(newestVisibleModalChild(state, modal.owner, modal.id));
        }
        return;
    }

    const bool restoreEnabled = ownerState.restoreEnabled;
    state.modalOwners.erase(ownerStateIterator);
    const auto ownerIterator = state.windows.find(modal.owner);
    if (ownerIterator == state.windows.end() || !ownerIterator->second) {
        return;
    }
#if defined(_WIN32)
    HWND ownerHwnd = nativeHwnd(*ownerIterator->second);
    if (restoreEnabled && ownerHwnd != nullptr && IsWindow(ownerHwnd) != FALSE) {
        EnableWindow(ownerHwnd, TRUE);
    }
#else
    (void)restoreEnabled;
#endif
    if (restoreEnabled && restoreFocus && !state.shuttingDown) {
        focusHostedWindow(ownerIterator->second.get());
    }
}

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
    glfwSetWindowFocusCallback(hosted.window, [](GLFWwindow* current, int focused) {
        auto* hosted = static_cast<HostedWindow*>(glfwGetWindowUserPointer(current));
        if (hosted != nullptr) {
            hosted->paintRequested = true;
            if (focused == GLFW_TRUE && hosted->hostState != nullptr) {
                focusHostedWindow(newestVisibleModalChild(*hosted->hostState, hosted->id));
            }
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
            if (hosted->hostState != nullptr) {
                HostedWindow* modal = newestVisibleModalChild(*hosted->hostState, hosted->id);
                if (modal != nullptr) {
                    glfwSetWindowShouldClose(current, GLFW_FALSE);
                    if (foregroundBelongsToHostedSubtree(*hosted->hostState, hosted->id)) {
                        focusHostedWindow(modal);
                    }
                    return;
                }
            }
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

    impl_->state.shuttingDown = true;
    // owner 只能引用更早创建的窗口；逆 WindowId 销毁即可统一保证所有 owned child 先释放。
    for (auto iterator = impl_->state.windows.rbegin();
         iterator != impl_->state.windows.rend(); ++iterator) {
        if (iterator->second) {
            iterator->second->visible = false;
            deactivateModalOwner(impl_->state, *iterator->second, false);
            destroyHostedWindow(iterator->second);
        }
    }
    impl_->state.windows.clear();
    impl_->state.modalOwners.clear();
    glfwTerminate();
    impl_->state.initialized = false;
    impl_->state.shuttingDown = false;
    impl_->state.nextWindowId = 1;
}

bool EuiAppHost::initialized() const {
    return impl_->state.initialized;
}

double EuiAppHost::timeSeconds() const {
    return impl_->state.initialized ? core::window::timeSeconds() : 0.0;
}

WindowId EuiAppHost::createWindow(const WindowConfig& config) {
    WindowConfig effective = normalizedWindowConfig(config);
    if (!impl_->state.initialized || !effective.compose ||
        effective.width <= 0 || effective.height <= 0 ||
        effective.minWidth < 0 || effective.minHeight < 0 ||
        (effective.initialPlacement && !validPlacementBounds(*effective.initialPlacement)) ||
        (effective.clickThrough && !effective.borderless) ||
        (effective.noActivate && effective.initialPlacement &&
         effective.initialPlacement->maximized) ||
        (effective.modal && (effective.role != WindowRole::Dialog || effective.noActivate))) {
        return kInvalidWindowId;
    }

    HostedWindow* owner = nullptr;
    const bool ownerRequired = effective.role == WindowRole::Tool ||
        effective.role == WindowRole::Dialog;
    const bool ownerAllowed = ownerRequired || effective.role == WindowRole::Popup;
    if (effective.owner != kInvalidWindowId) {
        if (!ownerAllowed) {
            return kInvalidWindowId;
        }
        const auto ownerIterator = impl_->state.windows.find(effective.owner);
        if (ownerIterator == impl_->state.windows.end() || !ownerIterator->second) {
            return kInvalidWindowId;
        }
        owner = ownerIterator->second.get();
    } else if (ownerRequired) {
        return kInvalidWindowId;
    }
    if (effective.role == WindowRole::Tool && owner->role != WindowRole::Main) {
        return kInvalidWindowId;
    }
    if (owner != nullptr && owner->noActivate &&
        (effective.role == WindowRole::Dialog || effective.role == WindowRole::Popup)) {
        return kInvalidWindowId;
    }

    app::DslWindowRequest request;
    request.title = effective.title;
    request.pageId = effective.pageId;
    request.clearColor = effective.clearColor;
    request.width = effective.width;
    request.height = effective.height;
    request.modal = effective.modal;
    request.compose = effective.compose;

    core::window::WindowCreateRequest nativeRequest;
    nativeRequest.width = effective.width;
    nativeRequest.height = effective.height;
    nativeRequest.minWidth = effective.minWidth;
    nativeRequest.minHeight = effective.minHeight;
    nativeRequest.title = request.title.c_str();
    nativeRequest.resizable = effective.resizable;
    nativeRequest.highDpi = effective.highDpi;
    nativeRequest.modal = effective.modal;
    // hosted 先完成角色、placement、renderer、Runtime 与输入链，再按 config.visible 显式显示。
    nativeRequest.visible = false;
    nativeRequest.borderless = effective.borderless;
    nativeRequest.transparentFramebuffer = effective.transparentFramebuffer;
    nativeRequest.alwaysOnTop = effective.alwaysOnTop;
    nativeRequest.noActivate = effective.noActivate;
    nativeRequest.clickThrough = effective.clickThrough;
    nativeRequest.focusOnShow = effective.focusOnShow;
    nativeRequest.role = effective.role;
    nativeRequest.owner = owner != nullptr ? owner->window : nullptr;
    nativeRequest.initialPlacement = effective.initialPlacement;
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
    hosted->hostState = &impl_->state;
    hosted->id = impl_->state.nextWindowId++;
    hosted->window = window;
    hosted->renderer = std::move(renderer);
    hosted->visible = effective.visible;
    hosted->role = effective.role;
    hosted->owner = effective.owner;
    hosted->modal = effective.modal;
    hosted->borderless = effective.borderless;
    hosted->noActivate = effective.noActivate;
    hosted->alwaysOnTop = effective.alwaysOnTop;
    hosted->clickThrough = effective.clickThrough;
    hosted->maximizeOnFirstShow =
        effective.initialPlacement && effective.initialPlacement->maximized;
    hosted->lastTick = core::window::timeSeconds();
    hosted->nextDeadline = hosted->lastTick;
    // hosted 模式不依赖 standalone app 配置；窗口缩放由 DPI 和此显式值决定。
    if (!hosted->runtime.initialize(window, std::move(request), 1.0f)) {
        destroyHostedWindow(hosted);
        return kInvalidWindowId;
    }
    installWindowCallbacks(*hosted);

    const WindowId id = hosted->id;
    impl_->state.windows.emplace(id, std::move(hosted));
    HostedWindow& stored = *impl_->state.windows.at(id);
    if (effective.visible) {
        activateModalOwner(impl_->state, stored);
        showHostedWindow(stored);
    } else {
        glfwHideWindow(window);
        stored.paintRequested = false;
    }
    return id;
}

bool EuiAppHost::showWindow(WindowId id) {
    const auto iterator = impl_->state.windows.find(id);
    if (iterator == impl_->state.windows.end() || !iterator->second) {
        return false;
    }

    HostedWindow& hosted = *iterator->second;
    const bool wasVisible = hosted.visible;
    const bool iconified = glfwGetWindowAttrib(hosted.window, GLFW_ICONIFIED) == GLFW_TRUE;
#if defined(_WIN32)
    HWND noActivateHwnd = nullptr;
    if (iconified && hosted.noActivate) {
        noActivateHwnd = nativeHwnd(hosted);
        if (noActivateHwnd == nullptr) {
            return false;
        }
    }
#endif
    hosted.visible = true;
    hosted.closeRequested = false;
    glfwSetWindowShouldClose(hosted.window, GLFW_FALSE);
    if (iconified) {
#if defined(_WIN32)
        if (hosted.noActivate) {
            // GLFW 的普通 restore 使用 SW_RESTORE；noActivate 窗口必须恢复而不激活。
            ShowWindow(noActivateHwnd, SW_SHOWNOACTIVATE);
        } else {
            glfwRestoreWindow(hosted.window);
        }
#else
        glfwRestoreWindow(hosted.window);
#endif
    }
    if (!wasVisible) {
        activateModalOwner(impl_->state, hosted);
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
    if (HostedWindow* modal = newestVisibleModalChild(impl_->state, id)) {
        if (foregroundBelongsToHostedSubtree(impl_->state, id)) {
            focusHostedWindow(modal);
        }
        return false;
    }

    HostedWindow& hosted = *iterator->second;
    const bool wasVisible = hosted.visible;
    const bool restoreModalFocus = shouldRestoreModalFocus(impl_->state, hosted);
    hosted.visible = false;
    glfwHideWindow(hosted.window);
    if (wasVisible) {
        deactivateModalOwner(impl_->state, hosted, restoreModalFocus);
    }
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

bool EuiAppHost::setWindowAlwaysOnTop(WindowId id, bool enabled) {
    const auto iterator = impl_->state.windows.find(id);
    if (iterator == impl_->state.windows.end() || !iterator->second) {
        return false;
    }
    HostedWindow& hosted = *iterator->second;
    glfwSetWindowAttrib(hosted.window, GLFW_FLOATING, enabled ? GLFW_TRUE : GLFW_FALSE);
    hosted.alwaysOnTop = enabled;
    return glfwGetWindowAttrib(hosted.window, GLFW_FLOATING) ==
        (enabled ? GLFW_TRUE : GLFW_FALSE);
}

bool EuiAppHost::setWindowClickThrough(WindowId id, bool enabled) {
    const auto iterator = impl_->state.windows.find(id);
    if (iterator == impl_->state.windows.end() || !iterator->second ||
        (enabled && !iterator->second->borderless)) {
        return false;
    }
    HostedWindow& hosted = *iterator->second;
    glfwSetWindowAttrib(hosted.window, GLFW_MOUSE_PASSTHROUGH,
                        enabled ? GLFW_TRUE : GLFW_FALSE);
    hosted.clickThrough = enabled;
    return glfwGetWindowAttrib(hosted.window, GLFW_MOUSE_PASSTHROUGH) ==
        (enabled ? GLFW_TRUE : GLFW_FALSE);
}

bool EuiAppHost::setWindowPlacement(WindowId id, const WindowPlacement& placement) {
    const auto iterator = impl_->state.windows.find(id);
    if (iterator == impl_->state.windows.end() || !iterator->second ||
        !placement.positioned || !validPlacementBounds(placement)) {
        return false;
    }
    HostedWindow& hosted = *iterator->second;
    if (!core::window::setWindowPlacement(hosted.window, placement)) {
        return false;
    }
    hosted.maximizeOnFirstShow = !hosted.visible && placement.maximized;
    requestFullPaint(hosted);
    hosted.nextDeadline = hosted.visible
        ? core::window::timeSeconds()
        : std::numeric_limits<double>::infinity();
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

    const bool restoreModalFocus = shouldRestoreModalFocus(impl_->state, *iterator->second);
    iterator->second->visible = false;
    deactivateModalOwner(impl_->state, *iterator->second, restoreModalFocus);
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
        const auto modalOwnerIterator = impl_->state.modalOwners.find(hosted.id);
        const bool inputEnabled = modalOwnerIterator == impl_->state.modalOwners.end() ||
            modalOwnerIterator->second.visibleModalCount == 0;

        if (hosted.runtime.update(hosted.window,
                                  deltaSeconds,
                                  logicalWidth,
                                  logicalHeight,
                                  pointerScale,
                                  dpiScale,
                                  updateRequested,
                                  inputEnabled)) {
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
