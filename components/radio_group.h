#pragma once

#include "components/focus_ring.h"
#include "components/radio.h"
#include "components/theme.h"
#include "eui/signal.h"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace components {

namespace detail {
struct RadioGroupKeyboardState {
    int observed = -1;
    int value = 0;
};
}

class RadioGroupBuilder {
public:
    RadioGroupBuilder(core::dsl::Ui& ui, std::string id)
        : ui_(ui), id_(std::move(id)) {}

    RadioGroupBuilder& size(float width, float itemHeight) {
        width_ = width;
        itemHeight_ = itemHeight;
        return *this;
    }
    RadioGroupBuilder& items(std::vector<std::string> value) {
        items_ = std::move(value);
        return *this;
    }
    RadioGroupBuilder& selected(int value) { selected_ = value; return *this; }
    RadioGroupBuilder& bind(eui::Signal<int>& signal) {
        selected(signal.get());
        onChange([&signal](int value) { signal.set(value); });
        return *this;
    }
    RadioGroupBuilder& gap(float value) { gap_ = std::max(0.0f, value); return *this; }
    RadioGroupBuilder& disabled(bool value = true) { disabled_ = value; return *this; }
    RadioGroupBuilder& initialFocus(bool value = true) { initialFocus_ = value; return *this; }
    RadioGroupBuilder& theme(const theme::ThemeColorTokens& tokens) {
        tokens_ = tokens;
        style_ = RadioStyle(tokens);
        metrics_ = tokens.metrics;
        focusColor_ = tokens.primary;
        focusLineWidth_ = theme::fieldVisuals(tokens).focusLineHeight;
        return *this;
    }
    RadioGroupBuilder& style(const RadioStyle& value) { style_ = value; return *this; }
    RadioGroupBuilder& transition(const core::Transition& value) { transition_ = value; return *this; }
    RadioGroupBuilder& onChange(std::function<void(int)> callback) {
        onChange_ = std::move(callback);
        return *this;
    }

    void build() {
        const int count = static_cast<int>(items_.size());
        const int selected = count > 0 ? std::clamp(selected_, 0, count - 1) : 0;
        const float totalHeight = count > 0
            ? itemHeight_ * static_cast<float>(count) + gap_ * static_cast<float>(count - 1)
            : 0.0f;
        const bool effectiveDisabled = disabled_ || count <= 0;
        const std::function<void(int)> onChange = onChange_;
        // 组根节点是唯一 Tab 停靠点，局部状态负责连续方向键的环回选择。
        auto& keyboard = ui_.state<detail::RadioGroupKeyboardState>(id_ + ".keyboard");
        if (keyboard.observed != selected) {
            keyboard.observed = selected;
            keyboard.value = selected;
        }
        const std::function<void(int)> select = [onChange, &keyboard](int value) {
            keyboard.value = value;
            keyboard.observed = value;
            if (onChange) {
                onChange(value);
            }
        };
        const std::function<void(const core::KeyEvent&)> onKey =
            [select, &keyboard, count](const core::KeyEvent& event) {
                if ((event.action != core::KeyAction::Press && event.action != core::KeyAction::Repeat) ||
                    count <= 0) {
                    return;
                }
                int next = keyboard.value;
                switch (event.key) {
                case core::InputKey::Left:
                case core::InputKey::Up:
                    next = next <= 0 ? count - 1 : next - 1;
                    break;
                case core::InputKey::Right:
                case core::InputKey::Down:
                    next = (next + 1) % count;
                    break;
                case core::InputKey::Home:
                    next = 0;
                    break;
                case core::InputKey::End:
                    next = count - 1;
                    break;
                default:
                    return;
                }
                select(next);
            };
        const bool focused = ui_.isFocused(id_);
        const float focusY = itemHeight_ * static_cast<float>(keyboard.value) +
                             gap_ * static_cast<float>(keyboard.value);

        ui_.stack(id_)
            .size(width_, totalHeight)
            .disabled(effectiveDisabled)
            .focusable()
            .initialFocus(initialFocus_)
            .onKey(onKey)
            .content([&] {
                ui_.column(id_ + ".items")
                    .size(width_, totalHeight)
                    .gap(gap_)
                    .content([&] {
                        for (int index = 0; index < count; ++index) {
                            radio(ui_, id_ + ".radio." + std::to_string(index))
                                .theme(tokens_)
                                .style(style_)
                                .size(width_, itemHeight_)
                                .selected(index == selected)
                                .text(items_[index])
                                .transition(transition_)
                                .disabled(disabled_)
                                .keyboardFocus(false)
                                .focusTarget(id_)
                                .onChange([select, index](bool value) {
                                    if (value) {
                                        select(index);
                                    }
                                })
                                .build();
                        }
                    })
                    .build();

                detail::focusRing(ui_,
                                  id_ + ".focus",
                                  {0.0f, focusY, width_, itemHeight_},
                                  std::max(metrics_.radius.small, itemHeight_ * 0.20f),
                                  focusColor_,
                                  focusLineWidth_,
                                  focused && !effectiveDisabled,
                                  transition_);
            })
            .build();
    }

private:
    core::dsl::Ui& ui_;
    std::string id_;
    std::vector<std::string> items_;
    RadioStyle style_;
    theme::ThemeColorTokens tokens_ = theme::dark();
    theme::ThemeMetricTokens metrics_;
    core::Transition transition_ = core::Transition::make(0.16f, core::Ease::OutCubic);
    std::function<void(int)> onChange_;
    int selected_ = 0;
    float width_ = 180.0f;
    float itemHeight_ = 30.0f;
    float gap_ = 8.0f;
    bool disabled_ = false;
    bool initialFocus_ = false;
    core::Color focusColor_ = theme::dark().primary;
    float focusLineWidth_ = theme::fieldVisuals(theme::dark()).focusLineHeight;
};

inline RadioGroupBuilder radioGroup(core::dsl::Ui& ui, const std::string& id) {
    return RadioGroupBuilder(ui, id);
}

} // namespace components
