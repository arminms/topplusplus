// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#include <format>
#include <string>
#include <utility>
#include <vector>
#include <cctype>

#include <ftxui/ftxui.hpp>

namespace detail {

// Colors used in the UI. They must be consolidated in a theme class or struct,
// but for now they are just free constants defined here.
inline const auto text_color = ftxui::Color::RGB(197, 207, 245);
inline const auto graph_color = ftxui::Color::RGB(198, 160, 246);
inline const auto plot_border_color = ftxui::Color::RGB(126, 133, 163);
inline const auto plot_background = ftxui::Color::RGB(24, 26, 38);

inline std::string join(auto&& range, std::string_view separator = ",")
{   std::string out;
    for (const auto& value : range)
        out += std::format("{}{}", out.empty() ? "" : separator, value);
    return out;
}

// Canvas nodes overwrite whole cells, so bgcolor() cannot be used on them;
// the background has to be set on every cell before anything is drawn.
inline void paint_background(ftxui::Canvas& c)
{   for (int y = 0; y < c.height(); y += 4)
        for (int x = 0; x < c.width(); x += 2)
            c.Style
            (   x, y
            ,   [](ftxui::Cell& cell) { cell.background_color = plot_background; }
            );
}

// Continuous shade from green (0) through yellow and orange to red (1)
inline ftxui::Color load_color(double load)
{   struct stop { double at; int r, g, b; };
    static constexpr stop stops[] =
    {   {0.00,  80, 200,  80}
    ,   {0.50, 240, 220,  60}
    ,   {0.75, 245, 150,  50}
    ,   {1.00, 230,  60,  60}
    };
    load = std::clamp(load, 0.0, 1.0);
    for (std::size_t i = 1; i < std::size(stops); ++i)
    {   if (load > stops[i].at)
            continue;
        const auto& a = stops[i - 1];
        const auto& b = stops[i];
        const double t = (load - a.at) / (b.at - a.at);
        const auto mix = [t](int u, int v)
        {   return static_cast<std::uint8_t>(std::lround(u + (v - u) * t));
        };
        return ftxui::Color::RGB(mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b));
    }
    return ftxui::Color::RGB(stops[3].r, stops[3].g, stops[3].b);
}

inline ftxui::Element new_info_row(std::string label, std::string value)
{   using namespace ftxui;
    value[0] = std::toupper(value[0]);
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
