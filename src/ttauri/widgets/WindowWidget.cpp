// Copyright 2019 Pokitec
// All rights reserved.

#include "WindowWidget.hpp"
#include "WindowTrafficLightsWidget.hpp"
#include "ToolbarWidget.hpp"
#include "ColumnWidget.hpp"
#if TT_OPERATING_SYSTEM == TT_OS_WINDOWS
#include "SystemMenuWidget.hpp"
#endif
#include "../GUI/utils.hpp"

namespace tt {

using namespace std;

WindowWidget::WindowWidget(Window &window, Label title) noexcept :
    ContainerWidget(window, nullptr), title(std::move(title))
{
    toolbar = &makeWidget<ToolbarWidget,""_ca>();
    toolbar->placeLeft(0.0f);
    toolbar->placeRight(0.0f);
    toolbar->placeAtTop(0.0f);

    if constexpr (Theme::operatingSystem == OperatingSystem::Windows) {
#if TT_OPERATING_SYSTEM == TT_OS_WINDOWS
        toolbar->makeWidget<SystemMenuWidget>(title.icon());
#endif
        toolbar->makeWidget<WindowTrafficLightsWidget,"R0T0"_ca>();
    } else if constexpr (Theme::operatingSystem == OperatingSystem::MacOS) {
        toolbar->makeWidget<WindowTrafficLightsWidget>();
    } else {
        tt_no_default;
    }


    content = &makeWidget<ColumnWidget,""_ca>();
    content->elevation = elevation;
    content->placeLeft();
    content->placeRight();
    content->placeAtBottom();
    content->placeBelow(*toolbar);

    // Add constraints for the window widget itself.
    window.addConstraint(left == 0);
    window.addConstraint(bottom == 0);
    // A upper bound constraint is needed to allow the suggest(width, limit::max()) and suggest(height, limit::max()) to
    // fallback on a upper bound, otherwise it will select the lower bounds instead.
    window.addConstraint(width <= std::numeric_limits<uint16_t>::max());
    window.addConstraint(height <= std::numeric_limits<uint16_t>::max());
}

HitBox WindowWidget::hitBoxTest(vec position) const noexcept
{
    constexpr float BORDER_WIDTH = 5.0;

    auto r = HitBox{this, elevation};

    if (position.x() <= BORDER_WIDTH) {
        if (position.y() <= BORDER_WIDTH) {
            r.type = HitBox::Type::BottomLeftResizeCorner;
        } else if (position.y() >= (rectangle().height() - BORDER_WIDTH)) {
            r.type = HitBox::Type::TopLeftResizeCorner;
        } else {
            r.type = HitBox::Type::LeftResizeBorder;
        }

    } else if (position.x() >= (rectangle().width() - BORDER_WIDTH)) {
        if (position.y() <= BORDER_WIDTH) {
            r.type = HitBox::Type::BottomRightResizeCorner;
        } else if (position.y() >= (rectangle().height() - BORDER_WIDTH)) {
            r.type = HitBox::Type::TopRightResizeCorner;
        } else {
            r.type = HitBox::Type::RightResizeBorder;
        }

    } else if (position.y() <= BORDER_WIDTH) {
        r.type = HitBox::Type::BottomResizeBorder;

    } else if (position.y() >= (rectangle().height() - BORDER_WIDTH)) {
        r.type = HitBox::Type::TopResizeBorder;
    }

    if (r.type != HitBox::Type::Outside) {
        // Resize corners need to override anything else, so that it is
        // always possible to resize a window.
        return r;
    }

    for (ttlet &child : children) {
        r = std::max(r, child->hitBoxTest(position - child->offsetFromParent()));
    }

    return r;
}

}