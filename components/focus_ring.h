#pragma once

#include "core/dsl.h"

#include <algorithm>
#include <string>

namespace components::detail {

inline void focusRing(core::dsl::Ui& ui,
                      const std::string& id,
                      const core::Rect& frame,
                      float radius,
                      const core::Color& color,
                      float lineWidth,
                      bool visible,
                      const core::Transition& transition) {
    // 焦点环是独立的非交互视觉层，不替换组件原有边框，也不参与布局和命中。
    ui.rect(id)
        .position(frame.x, frame.y)
        .size(std::max(0.0f, frame.width), std::max(0.0f, frame.height))
        .color({0.0f, 0.0f, 0.0f, 0.0f})
        .border(std::max(0.0f, lineWidth), color)
        .radius(std::max(0.0f, radius))
        .opacity(visible ? 1.0f : 0.0f)
        .zIndex(100)
        .transition(transition)
        .animate(core::AnimProperty::Opacity | core::AnimProperty::Border)
        .build();
}

} // namespace components::detail
