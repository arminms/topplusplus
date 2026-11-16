// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#include <format>
#include <string>
#include <utility>

#include <ftxui/ftxui.hpp>

namespace detail {

inline ftxui::Element new_info_row(std::string label, std::string value)
{   using namespace ftxui;
    return hbox
    (   {   text(std::move(label))
        |   dim
        |   size(WIDTH, EQUAL, 16)
        ,   separator()
        ,   text(std::format(" {}", value))
        }
    );
}

}   // namespace detail
