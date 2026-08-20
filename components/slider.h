#pragma once

#include "components/focus_ring.h"
#include "components/theme.h"
#include "core/dsl.h"
#include "eui/signal.h"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>

namespace components {

namespace detail {
struct SliderKeyboardState {
    float observed = -1.0f;
    float value = 0.0f;
};
}

struct SliderStyle {
    SliderStyle() : SliderStyle(theme::dark()) {}

    explicit SliderStyle(const theme::ThemeColorTokens& tokens) {
        track = core::mixColor(tokens.surfaceHover, tokens.surfaceActive, tokens.dark ? 0.24f : 0.18f);
        fill = tokens.primary;
        knob = tokens.text;
    }

    core::Color track;
    core::Color fill;
    core::Color knob;
};

class SliderBuilder {
public:
    SliderBuilder(core::dsl::Ui& ui, std::string id)
        : ui_(ui), id_(std::move(id)) {}

    SliderBuilder& size(float width, float height) { width_ = width; height_ = height; return *this; }
    SliderBuilder& value(float value) { value_ = std::clamp(value, 0.0f, 1.0f); return *this; }
    SliderBuilder& step(float value) { step_ = std::clamp(value, 0.0001f, 1.0f); return *this; }
    SliderBuilder& disabled(bool value = true) { disabled_ = value; return *this; }
    SliderBuilder& initialFocus(bool value = true) { initialFocus_ = value; return *this; }
    SliderBuilder& bind(eui::Signal<float>& signal) {
        value(signal.get());
        onChange([&signal](float value) { signal.set(value); });
        return *this;
    }
    SliderBuilder& style(const SliderStyle& value) { style_ = value; return *this; }
    SliderBuilder& theme(const theme::ThemeColorTokens& tokens) {
        style_ = SliderStyle(tokens);
        metrics_ = tokens.metrics;
        focusColor_ = tokens.primary;
        focusLineWidth_ = theme::fieldVisuals(tokens).focusLineHeight;
        return *this;
    }
    SliderBuilder& transition(const core::Transition& value) { transition_ = value; return *this; }
    SliderBuilder& transition(float duration, core::Ease ease = core::Ease::OutCubic) {
        transition_ = core::Transition::make(duration, ease);
        return *this;
    }
    SliderBuilder& onChange(std::function<void(float)> callback) { onChange_ = std::move(callback); return *this; }

    void build() {
        const float trackHeight = std::max(metrics_.spacing.tiny - metrics_.spacing.hairline,
                                           height_ * 0.18f);
        const float trackY = (height_ - trackHeight) * 0.5f;
        const float knobSize = std::max(metrics_.typography.label, height_ * 0.72f);
        const std::function<void(float)> onChange = onChange_;
        // 指针与连续按键共用累积值，避免受控属性回写前丢失同一帧的步进。
        auto& keyboard = ui_.state<detail::SliderKeyboardState>(id_ + ".keyboard");
        if (keyboard.observed != value_) {
            keyboard.observed = value_;
            keyboard.value = value_;
        }
        const std::function<void(float)> setValue = [onChange, &keyboard](float value) {
            const float next = std::clamp(value, 0.0f, 1.0f);
            if (next == keyboard.value) {
                return;
            }
            keyboard.value = next;
            keyboard.observed = next;
            if (onChange) {
                onChange(keyboard.value);
            }
        };
        const std::function<void(const core::KeyEvent&)> onKey = [setValue, &keyboard, step = step_](const core::KeyEvent& event) {
            if (event.action != core::KeyAction::Press && event.action != core::KeyAction::Repeat) {
                return;
            }
            float next = keyboard.value;
            switch (event.key) {
            case core::InputKey::Left:
            case core::InputKey::Down:
                next -= step;
                break;
            case core::InputKey::Right:
            case core::InputKey::Up:
                next += step;
                break;
            case core::InputKey::Home:
                next = 0.0f;
                break;
            case core::InputKey::End:
                next = 1.0f;
                break;
            default:
                return;
            }
            setValue(next);
        };
        const bool focused = ui_.isFocused(id_ + ".hit");

        ui_.stack(id_)
            .size(width_, height_)
            .disabled(disabled_)
            .sliderState(id_, value_, width_, knobSize, setValue)
            .content([&] {
                ui_.rect(id_ + ".track")
                    .y(trackY)
                    .size(width_, trackHeight)
                    .color(style_.track)
                    .radius(trackHeight * 0.5f)
                    .build();

                ui_.rect(id_ + ".fill")
                    .y(trackY)
                    .size(width_ * value_, trackHeight)
                    .color(style_.fill)
                    .radius(trackHeight * 0.5f)
                    .transition(transition_)
                    .animate(core::AnimProperty::Color)
                    .sliderFillFrom(id_)
                    .build();

                ui_.rect(id_ + ".knob")
                    .y((height_ - knobSize) * 0.5f)
                    .size(knobSize, knobSize)
                    .color(style_.knob)
                    .radius(knobSize * 0.5f)
                    .shadow(12.0f, 0.0f, 4.0f, theme::withAlpha(style_.fill, 0.20f))
                    .transition(transition_)
                    .animate(core::AnimProperty::Color | core::AnimProperty::Shadow)
                    .sliderKnobFrom(id_)
                    .build();

                ui_.rect(id_ + ".hit")
                    .size(width_, height_)
                    .states(theme::color(0.0f, 0.0f, 0.0f, 0.0f),
                            theme::color(0.0f, 0.0f, 0.0f, 0.0f),
                            theme::color(0.0f, 0.0f, 0.0f, 0.0f))
                    .zIndex(10)
                    .interactive()
                    .focusable()
                    .initialFocus(initialFocus_)
                    .onKey(onKey)
                    .sliderInputFrom(id_)
                    .build();

                detail::focusRing(ui_,
                                  id_ + ".focus",
                                  {0.0f, 0.0f, width_, height_},
                                  metrics_.radius.control,
                                  focusColor_,
                                  focusLineWidth_,
                                  focused && !disabled_,
                                  transition_);
            })
            .build();
    }

private:
    core::dsl::Ui& ui_;
    std::string id_;
    SliderStyle style_;
    theme::ThemeMetricTokens metrics_;
    core::Transition transition_ = core::Transition::make(0.16f, core::Ease::OutCubic);
    std::function<void(float)> onChange_;
    float width_ = 300.0f;
    float height_ = 32.0f;
    float value_ = 0.0f;
    float step_ = 0.05f;
    bool disabled_ = false;
    bool initialFocus_ = false;
    core::Color focusColor_ = theme::dark().primary;
    float focusLineWidth_ = theme::fieldVisuals(theme::dark()).focusLineHeight;
};

inline SliderBuilder slider(core::dsl::Ui& ui, const std::string& id) {
    return SliderBuilder(ui, id);
}

} // namespace components
