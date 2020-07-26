// Copyright 2019 Pokitec
// All rights reserved.

#pragma once

#include "GridWidget.hpp"
#include "../GUI/Theme.hpp"
#include <memory>

namespace tt {

class ColumnWidget : public GridWidget {
public:
    ColumnWidget(Window &window, Widget *parent) noexcept :
        GridWidget(window, parent) {}

};

}