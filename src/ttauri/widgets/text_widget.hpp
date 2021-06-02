// Copyright Take Vos 2021.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at https://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "widget.hpp"
#include "label_delegate.hpp"
#include "../GUI/draw_context.hpp"
#include "../observable.hpp"
#include "../alignment.hpp"
#include <memory>
#include <string>
#include <array>
#include <optional>
#include <future>

namespace tt {

class text_widget final : public widget {
public:
    using super = widget;

    text_widget(
        gui_window &window,
        std::shared_ptr<widget> parent,
        std::shared_ptr<label_delegate> delegate,
        alignment alignment = alignment::top_left,
        text_style style = theme::global->labelStyle) noexcept :
        super(window, std::move(parent), std::move(delegate)), _alignment(alignment), _style(style)
    {
    }

    ~text_widget();

    [[nodiscard]] std::string text() const noexcept
    {
        return delegate<label_delegate>().text(*this);
    }

    [[nodiscard]] bool update_constraints(hires_utc_clock::time_point display_time_point, bool need_reconstrain) noexcept override;

    [[nodiscard]] void update_layout(hires_utc_clock::time_point displayTimePoint, bool need_layout) noexcept override;

    void draw(draw_context context, hires_utc_clock::time_point display_time_point) noexcept override;

private:
    alignment _alignment;
    text_style _style;
    shaped_text _shaped_text;
    matrix2 _shaped_text_transform;
};

} // namespace tt