#pragma once

namespace core::dsl {

inline bool Runtime::initialize() {
    return true;
}

inline bool Runtime::initialize(core::window::Handle window) {
    core::window::installInputCallbacks(window);
    return true;
}

template <typename ComposeFn>
inline void Runtime::compose(const std::string& pageId, float logicalWidth, float logicalHeight, ComposeFn&& composeFn) {
    const std::vector<runtime::ElementSnapshot> previousStructure = elementStructure_;
    const Screen screen{logicalWidth, logicalHeight};
    ui_.begin(pageId);
    ui_.setFocusedId(focusedId_);
    composeFn(ui_, screen);
    ui_.end();
    ui_.layout(screen);
    elementStructure_ = collectElementStructure();
    syncScrollStateBindings();
    for (const std::string& scope : ui_.consumeReleasedStateScopes()) {
        const std::string childPrefix = scope + ".";
        if (focusedId_ == scope || focusedId_.rfind(childPrefix, 0) == 0) {
            focusedId_.clear();
            ui_.setFocusedId(focusedId_);
        }
    }
    if (!focusedId_.empty()) {
        const std::vector<std::string> focusIds = focusableIds();
        if (std::find(focusIds.begin(), focusIds.end(), focusedId_) == focusIds.end()) {
            // compose 后元素可能被删除、禁用或透明隐藏，不能保留悬空焦点。
            setFocusedId({});
        }
    }

    if (elementStructure_ != previousStructure) {
        paintRequested_ = true;
        fullPaintRequested_ = true;
        pruneInstancesRequested_ = true;
    }

    if (logicalWidth_ != logicalWidth || logicalHeight_ != logicalHeight) {
        paintRequested_ = true;
        fullPaintRequested_ = true;
        pruneInstancesRequested_ = true;
    }
    fullTreeUpdateRequested_ = true;
    logicalWidth_ = logicalWidth;
    logicalHeight_ = logicalHeight;
}

inline bool Runtime::update(core::window::Handle window, float deltaSeconds, float pointerScale, float dpiScale, bool inputEnabled) {
    ++updateFrameToken_;
    if (updateFrameToken_ == 0) {
        ++updateFrameToken_;
    }
    std::vector<PointerEvent> pointerEvents = readPointerEvents(window, pointerScale);
    const auto inputEvents = consumeInputEvents(window);
    KeyboardEvent keyboardEvent = inputEvents.first;
    ScrollEvent scrollEvent = inputEvents.second;
    if (!inputEnabled) {
        PointerEvent disabledEvent;
        disabledEvent.x = -1000000.0;
        disabledEvent.y = -1000000.0;
        disabledEvent.releasedThisFrame = true;
        pointerEvents.assign(1, disabledEvent);
        keyboardEvent = {};
        scrollEvent = {};
    }
    animating_ = false;
    composeRequested_ = false;
    wantsHandCursor_ = false;
    if (pruneInstancesRequested_) {
        instances_.markInstancesUnseen();
    }
    instances_.markTimersUnseen();
    if (ImagePrimitive::consumeRemoteImageReady()) {
        fullPaintRequested_ = true;
        paintRequested_ = true;
    }

    syncScrollStateBindings();
    const PointerEvent& finalPointerEvent = pointerEvents.back();
    if (scrollEvent.active()) {
        updateScroll(scrollEvent, hitTestScrollable(finalPointerEvent, dpiScale));
        hoverTargetCacheValid_ = false;
    }
    updateScrollMotion(deltaSeconds);

    for (std::size_t index = 0; index < pointerEvents.size(); ++index) {
        const PointerEvent& event = pointerEvents[index];
        wantsHandCursor_ = false;
        if (event.pressedThisFrame) {
            setFocusedId(hitTestFocusable(event, dpiScale));
        }

        const std::string hoverTargetId = resolveHoverTarget(event, dpiScale, inputEnabled);
        if (index + 1 == pointerEvents.size()) {
            updateElementTree(event, deltaSeconds, dpiScale, hoverTargetId);
        } else {
            // 同一 tick 内按序处理按钮边沿，但动画、计时和 frame callback 只推进一次。
            updateInputTree(event, dpiScale, hoverTargetId);
        }
    }
    updateDependentVisualDirtyRegions(dpiScale);

    if (keyboardEvent.hasInput()) {
        // 文本/IME 先以聚合事件交付一次，保持原有编辑合同；按键随后逐个交付，保证同一 tick 的顺序。
        KeyboardEvent textEvent = keyboardEvent;
        textEvent.keys.clear();
        if (textEvent.hasInput()) {
            updateTextInput(textEvent);
        }
        for (const KeyEvent& key : keyboardEvent.keys) {
            KeyboardEvent keyEvent;
            keyEvent.keys.push_back(key);
            updateTextInput(keyEvent);
            dispatchKey(key);
        }
    }
    instances_.releaseUnseenTimers();
    updateImeCursorRect(window, dpiScale);
    applyCursor(window);

    promoteBackdropBlurDirtyRegions(dpiScale);
    if (pruneInstancesRequested_) {
        instances_.releaseUnseenInstances();
        pruneInstancesRequested_ = false;
    }
    fullTreeUpdateRequested_ = false;
    previousFrameAnimating_ = animating_;

    const bool result = paintRequested_;
    paintRequested_ = false;
    return result;
}

inline bool Runtime::isAnimating() const {
    return animating_;
}

inline bool Runtime::composeRequested() const {
    return composeRequested_;
}

inline bool Runtime::paintRequested() const {
    return paintRequested_;
}

inline void Runtime::requestFullPaint() {
    fullPaintRequested_ = true;
    paintRequested_ = true;
}

inline void Runtime::render(int windowWidth, int windowHeight, float dpiScale, const Color& clearColor) {
    core::render::RenderBackend* renderBackend = core::render::activeRenderBackend();
    if (renderBackend == nullptr) {
        return;
    }

    core::render::beginRenderFrameStats(windowWidth, windowHeight);
    ImagePrimitive::beginRenderFrame();
    core::render::RenderFrameStats& stats = core::render::currentRenderFrameStats();

    const bool hasRenderableContent = !ui_.roots().empty();
    if (!hasRenderableContent) {
        ++stats.clearCalls;
        renderBackend->clear(clearColor);
        dirtyRects_.clear();
        fullPaintRequested_ = false;
        core::render::publishRenderFrameStats();
        return;
    }

    if (!renderBackend->ensureRenderCache(windowWidth, windowHeight)) {
        ++stats.clearCalls;
        renderBackend->clear(clearColor);
        ++stats.renderDirectPasses;
        RuntimeRenderer(ui_, instances_).renderDirect(
            *renderBackend, windowWidth, windowHeight, dpiScale);
        dirtyRects_.clear();
        fullPaintRequested_ = false;
        core::render::publishRenderFrameStats();
        return;
    }
    stats.usedRenderCache = true;
    if (renderBackend->renderCacheWasRecreated()) {
        fullPaintRequested_ = true;
        stats.renderCacheRecreated = true;
    }

    if (!fullPaintRequested_ && dirtyRects_.empty()) {
        renderBackend->blitRenderCache(windowWidth, windowHeight, core::render::RenderCacheBlitMode::Existing);
        core::render::publishRenderFrameStats();
        return;
    }

    stats.fullPaint = fullPaintRequested_;
    const std::vector<Rect> dirtyRects = fullPaintRequested_
        ? std::vector<Rect>{}
        : core::dsl::resolveDirtyRects(dirtyRects_, windowWidth, windowHeight, dpiScale);
    if (!fullPaintRequested_ && dirtyRects.empty()) {
        dirtyRects_.clear();
        renderBackend->blitRenderCache(windowWidth, windowHeight, core::render::RenderCacheBlitMode::Existing);
        core::render::publishRenderFrameStats();
        return;
    }
    stats.dirtyRectCount = static_cast<int>(dirtyRects.size());
    for (const Rect& dirty : dirtyRects) {
        const float width = std::max(0.0f, dirty.width);
        const float height = std::max(0.0f, dirty.height);
        stats.dirtyPixels += static_cast<std::uint64_t>(width * height);
    }

    renderBackend->beginRenderCacheFrame(windowWidth, windowHeight, dirtyRects);

    if (fullPaintRequested_) {
        renderBackend->setScissor(false, {}, windowHeight);
        ++stats.clearCalls;
        renderBackend->clear(clearColor);
        ++stats.renderDirectPasses;
        RuntimeRenderer(ui_, instances_).renderDirect(
            *renderBackend, windowWidth, windowHeight, dpiScale);
    } else {
        for (const Rect& dirty : dirtyRects) {
            renderBackend->setScissor(true, dirty, windowHeight);
            ++stats.clearCalls;
            renderBackend->clear(clearColor);
            ++stats.renderDirectPasses;
            RuntimeRenderer(ui_, instances_).renderDirect(
                *renderBackend, windowWidth, windowHeight, dpiScale, &dirty);
        }
        renderBackend->setScissor(false, {}, windowHeight);
    }

    renderBackend->endRenderCacheFrame();
    renderBackend->blitRenderCache(windowWidth,
                                   windowHeight,
                                   fullPaintRequested_ ? core::render::RenderCacheBlitMode::Full
                                                       : core::render::RenderCacheBlitMode::Dirty,
                                   dirtyRects);
    const bool retainedLayerWarmupNeeded =
        stats.retainedLayerMisses > 0 && stats.retainedLayerRebuilds == 0;
    dirtyRects_.clear();
    fullPaintRequested_ = retainedLayerWarmupNeeded;
    paintRequested_ = retainedLayerWarmupNeeded;
    core::render::publishRenderFrameStats();
}

inline void Runtime::render(int windowWidth, int windowHeight, float dpiScale) {
    core::render::RenderBackend* renderBackend = core::render::activeRenderBackend();
    if (renderBackend == nullptr) {
        return;
    }

    ImagePrimitive::beginRenderFrame();

    RuntimeRenderer(ui_, instances_).renderDirect(
        *renderBackend, windowWidth, windowHeight, dpiScale);
}

inline void Runtime::shutdown(bool releaseCachedImageTextures) {
    releaseGraphicsResources(releaseCachedImageTextures);
    instances_.clear();
    elementStructure_.clear();
    hoverTargetCacheValid_ = false;
    ui_.clearState();
}

inline void Runtime::releaseGraphicsResources(bool releaseCachedImageTextures) {
    instances_.releaseGraphicsResources(releaseCachedImageTextures);
    destroyCursors();
    fullPaintRequested_ = true;
    paintRequested_ = true;
}

inline void Runtime::applyCursor(core::window::Handle window) {
    if (!arrowCursor_) {
        arrowCursor_ = core::window::createStandardCursor(core::window::CursorType::Arrow);
    }
    if (!handCursor_) {
        handCursor_ = core::window::createStandardCursor(core::window::CursorType::Hand);
    }

    core::window::CursorHandle target = wantsHandCursor_ && handCursor_ ? handCursor_ : arrowCursor_;
    if (target != currentCursor_) {
        core::window::setCursor(window, target);
        currentCursor_ = target;
    }
}

inline void Runtime::destroyCursors() {
    if (arrowCursor_) {
        core::window::destroyCursor(arrowCursor_);
        arrowCursor_ = nullptr;
    }
    if (handCursor_) {
        core::window::destroyCursor(handCursor_);
        handCursor_ = nullptr;
    }
    currentCursor_ = nullptr;
}

} // namespace core::dsl
