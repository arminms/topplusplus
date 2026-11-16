// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#include <format>
#include <string>
#include <utility>
#include <vector>

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

inline ftxui::Element new_info_box
(   std::vector<std::pair<std::string, std::string>> rows
)
{   using namespace ftxui;
    Elements labels;
    Elements values;
    labels.reserve(rows.size());
    values.reserve(rows.size());
    for (auto& [label, value] : rows)
    {   labels.push_back
        (   text(std::move(label))
            |   dim
            |   size(WIDTH, EQUAL, 16)
        );
        values.push_back(text(std::format(" {}", value)));
    }
    return hbox
    (   {   vbox(std::move(labels))
        ,   separator()
        ,   vbox(std::move(values))
        }
    );
}

}   // namespace detail
