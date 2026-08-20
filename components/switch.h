#pragma once

#include "components/focus_ring.h"
#include "components/theme.h"
#include "core/dsl.h"
#include "core/render/text.h"
#include "eui/signal.h"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>

namespace components {

namespace detail {
struct SwitchToggleState {
    bool observed = false;
    bool value = false;
    bool initialized = false;
};
}

struct SwitchStyle {
    SwitchStyle() : SwitchStyle(theme::dark()) {}

    explicit SwitchStyle(const theme::ThemeColorTokens& tokens) {
        off = core::mixColor(tokens.surfaceHover, tokens.surfaceActive, 0.30f);
        on = tokens.primary;
        knob = core::mixColor(tokens.surface, theme::color(1.0f, 1.0f, 1.0f, 1.0f), 0.75f);
        text = tokens.text;
        rowHover = theme::withAlpha(tokens.text, tokens.dark ? 0.06f : 0.05f);
        rowPressed = theme::withAlpha(tokens.text, tokens.dark ? 0.10f : 0.08f);
    }

    core::Color off;
    core::Color on;
    core::Color knob;
    core::Color text;
    core::Color rowHover;
    core::Color rowPressed;
};

class SwitchBuilder {
public:
    SwitchBuilder(core::dsl::Ui& ui, std::string id)
        : ui_(ui), id_(std::move(id)) {}

    SwitchBuilder& size(float width, float height) { width_ = width; height_ = height; return *this; }
    SwitchBuilder& checked(bool value) { checked_ = value; return *this; }
    SwitchBuilder& bind(eui::Signal<bool>& signal) {
        checked(signal.get());
        onChange([&signal](bool value) { signal.set(value); });
        return *this;
    }
    SwitchBuilder& text(std::string value) { label_ = std::move(value); return *this; }
    SwitchBuilder& fontSize(float value) { fontSize_ = std::max(1.0f, value); return *this; }
    SwitchBuilder& disabled(bool value = true) { disabled_ = value; return *this; }
    SwitchBuilder& trackSize(float width, float height) {
        trackWidth_ = std::max(20.0f, width);
        trackHeight_ = std::max(12.0f, height);
        return *this;
    }
    SwitchBuilder& style(const SwitchStyle& value) { style_ = value; return *this; }
    SwitchBuilder& theme(const theme::ThemeColorTokens& tokens) {
        style_ = SwitchStyle(tokens);
        metrics_ = tokens.metrics;
        focusColor_ = tokens.primary;
        focusLineWidth_ = theme::fieldVisuals(tokens).focusLineHeight;
        return *this;
    }
    SwitchBuilder& transition(const core::Transition& value) { transition_ = value; return *this; }
    SwitchBuilder& transition(float duration, core::Ease ease = core::Ease::OutCubic) {
        transition_ = core::Transition::make(duration, ease);
        return *this;
    }
    SwitchBuilder& onChange(std::function<void(bool)> callback) { onChange_ = std::move(callback); return *this; }

    void build() {
        const float fontSize = fontSize_ > 0.0f ? fontSize_ : metrics_.typography.control;
        const float gap = gap_ > 0.0f ? gap_ : metrics_.spacing.control;
        const float trackWidth = trackWidth_ > 0.0f ? trackWidth_ : metrics_.control.switchWidth;
        const float trackHeight = trackHeight_ > 0.0f ? trackHeight_ : metrics_.control.switchHeight;
        const float trackY = (height_ - trackHeight) * 0.5f;
        const float margin = std::max(metrics_.spacing.micro, trackHeight * 0.125f);
        const float knobSize = std::max(0.0f, trackHeight - margin * 2.0f);
        const float knobTravel = std::max(0.0f, trackWidth - margin * 2.0f - knobSize);
        const float knobX = margin + (checked_ ? knobTravel : 0.0f);
        const float labelX = trackWidth + gap;
        const float horizontalInset = metrics_.spacing.control;
        const float contentX = horizontalInset;
        const float labelWidth = std::max(0.0f, width_ - labelX - horizontalInset);
        const float labelLineHeight = fontSize;
        const float labelY = std::max(0.0f, (height_ - labelLineHeight) * 0.5f);
        const float hitWidth = label_.empty()
            ? trackWidth + horizontalInset * 2.0f
            : std::min(width_, labelX + textWidth(label_, fontSize) + horizontalInset * 2.0f);
        const std::function<void(bool)> onChange = onChange_;
        // 鼠标与同一帧内的连续键盘激活共用累积状态，保证逐次切换。
        auto& toggle = ui_.state<detail::SwitchToggleState>(id_ + ".toggle");
        if (!toggle.initialized || toggle.observed != checked_) {
            toggle.observed = checked_;
            toggle.value = checked_;
            toggle.initialized = true;
        }
        const std::function<void()> action = [onChange, &toggle] {
            toggle.value = !toggle.value;
            toggle.observed = toggle.value;
            if (onChange) {
                onChange(toggle.value);
            }
        };
        const std::string focusId = id_ + ".hit";
        const bool focused = ui_.isFocused(focusId);

        ui_.stack(id_)
            .size(width_, height_)
            .disabled(disabled_)
            .content([&] {
                ui_.rect(id_ + ".hit")
                    .size(hitWidth, height_)
                    .states(theme::color(0.0f, 0.0f, 0.0f, 0.0f), style_.rowHover, style_.rowPressed)
                    .radius(std::max(metrics_.radius.small, height_ * 0.20f))
                    .transition(transition_)
                    .onClick(action)
                    .onActivate(action)
                    .build();

                detail::focusRing(ui_,
                                  id_ + ".focus",
                                  {0.0f, 0.0f, hitWidth, height_},
                                  std::max(metrics_.radius.small, height_ * 0.20f),
                                  focusColor_,
                                  focusLineWidth_,
                                  focused && !disabled_,
                                  transition_);

                ui_.rect(id_ + ".track")
                    .x(contentX)
                    .y(trackY)
                    .size(trackWidth, trackHeight)
                    .color(checked_ ? style_.on : style_.off)
                    .radius(trackHeight * 0.5f)
                    .transition(transition_)
                    .animate(core::AnimProperty::Color)
                    .build();

                ui_.rect(id_ + ".knob")
                    .x(contentX + knobX)
                    .y(trackY + margin)
                    .size(knobSize, knobSize)
                    .color(style_.knob)
                    .radius(knobSize * 0.5f)
                    .transition(transition_)
                    .animate(core::AnimProperty::Frame | core::AnimProperty::Color)
                    .build();

                if (!label_.empty()) {
                    ui_.text(id_ + ".label")
                        .x(contentX + labelX)
                        .y(labelY)
                        .size(labelWidth, labelLineHeight)
                        .text(label_)
                        .fontSize(fontSize)
                        .lineHeight(labelLineHeight)
                        .color(style_.text)
                        .verticalAlign(core::VerticalAlign::Top)
                        .build();
                }
            })
            .build();
    }

private:
    static float textWidth(const std::string& value, float fontSize) {
        return core::TextPrimitive::measureTextWidth(value, {}, fontSize, 400);
    }

    core::dsl::Ui& ui_;
    std::string id_;
    SwitchStyle style_;
    theme::ThemeMetricTokens metrics_;
    core::Transition transition_ = core::Transition::make(0.18f, core::Ease::OutCubic);
    std::function<void(bool)> onChange_;
    std::string label_;
    bool checked_ = false;
    bool disabled_ = false;
    float width_ = 180.0f;
    float height_ = 32.0f;
    float trackWidth_ = 0.0f;
    float trackHeight_ = 0.0f;
    float gap_ = 0.0f;
    float fontSize_ = 0.0f;
    core::Color focusColor_ = theme::dark().primary;
    float focusLineWidth_ = theme::fieldVisuals(theme::dark()).focusLineHeight;
};

inline SwitchBuilder toggleSwitch(core::dsl::Ui& ui, const std::string& id) {
    return SwitchBuilder(ui, id);
}

} // namespace components
