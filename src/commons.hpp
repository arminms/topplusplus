// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ftxui/ftxui.hpp>

namespace detail {

constexpr std::size_t window = 60;

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

// Wireframe surface in the style of canvas_animated.cpp's "2D gaussian plot".
// The mesh's x axis is time (newest on the right), its y axis is memory
// usage in [0, 1] and its height is load in [0, 1]: each time step raises
// a gaussian ridge cross-section at its memory usage with its load as the
// height, so the history reads as a moving mountain range.
template <typename SampleValue, typename DrawLegend>
ftxui::Element plot_2d_mesh
(   std::string title
,   std::size_t sample_count
,   SampleValue sample_value
,   std::string legend
,   int legend_rows
,   DrawLegend draw_legend
,   std::string axis_label
,   std::string time_label
)
{   using namespace ftxui;
    const std::size_t n = std::min(sample_count, window);
    auto plot = canvas
    (   [sample_count, n, sample_value, legend_rows, draw_legend
        ,   axis_label = std::move(axis_label)
        ,   time_label = std::move(time_label)
        ](Canvas& c)
        {   paint_background(c);
            const int width = c.width();
            const int height = c.height();
            const int axis_px
            =   axis_label.empty() ? 0 : 2 * (string_width(axis_label) + 6);
            const int top = legend_rows * 4;
            const int bottom = height - 1 - (time_label.empty() ? 0 : 4);
            if (width < 2 || height <= top + 1)
                return;
            draw_legend(c);
            if (width <= axis_px + 1 || bottom <= top + 1)
                return;

            constexpr int rows = 15;
            constexpr double peak_width = 0.2;
            constexpr int column_px = 3;

            // Oblique projection: x runs right, y runs back (up and sideways)
            const double peak_px = 0.3 * (bottom - top);
            const double depth_px = (bottom - top) - peak_px;
            const double shear_px = std::min
            (   0.6 * depth_px, 0.4 * (width - 1 - axis_px)
            );
            const double available_span = (width - 1 - axis_px) - shear_px;
            const double span_px = time_label.empty()
            ?   available_span
            :   std::min
                (   available_span
                ,   std::max
                    (   1.0
                    ,   double(width - axis_px - 2 * string_width(time_label) - 2)
                    )
                )
            ;
            const int cols = std::clamp
            (   static_cast<int>(span_px / column_px), 2, static_cast<int>(window)
            );

            const auto project = [=](int col, int row, double z)
            {   return std::pair
                {   axis_px + static_cast<int>
                    (   std::lround
                        (   span_px * col / (cols - 1)
                        +   shear_px * row / (rows - 1)
                        )
                    )
                ,   static_cast<int>
                    (   std::lround
                        (   bottom
                        -   depth_px * row / (rows - 1)
                        -   peak_px * z
                        )
                    )
                };
            };
            const auto draw_axis = [&]
            {   if (!axis_label.empty())
                    for (const int percent : {0, 50, 100})
                    {   const auto [x, y] = project(0, percent * (rows - 1) / 100, 0);
                        const auto marker = percent
                        ?   std::format("{} {}%", axis_label, percent)
                        :   std::string("0")
                        ;
                        c.DrawText
                        (   (x / 2) * 2 - 2 * (string_width(marker) + 1)
                        ,   !percent && !time_label.empty() ? height - 4 : (y / 4) * 4
                        ,   marker, Color::White
                        );
                    }
                if (!time_label.empty())
                {   const int x = project(cols - 1, 0, 0).first;
                    c.DrawText
                    (   2 * (x / 2 + 1)
                    ,   height - 4, time_label, Color::White
                    );
                }
            };
            if (!n)
            {   draw_axis();
                return;
            }

            // One (load, memory) pair per window slot, the newest in the last
            std::vector<std::optional<std::pair<double, double>>> series(window);
            for (std::size_t k = 0; k < n; ++k)
            {   const auto sample = sample_value(sample_count - n + k);
                if (sample)
                    series[window - n + k] = std::pair
                    {   std::clamp(sample->first, 0.0, 1.0)
                    ,   std::clamp(sample->second, 0.0, 1.0)
                    };
            }

            // Resample the series onto the mesh columns
            std::vector<double> surface(cols * rows, 0.0);
            for (int col = 0; col < cols; ++col)
            {   const double pos = double(col) * (window - 1) / (cols - 1);
                const auto lo = static_cast<std::size_t>(pos);
                const auto hi = std::min(lo + 1, window - 1);
                const double t = pos - lo;
                const auto& a = series[lo];
                const auto& b = series[hi];
                std::optional<std::pair<double, double>> point;
                if (a && b)
                    point = std::pair
                    {   a->first + (b->first - a->first) * t
                    ,   a->second + (b->second - a->second) * t
                    };
                else
                    point = t < 0.5 ? a : b;
                if (!point)
                    continue;
                const double centre = point->second * (rows - 1);
                for (int row = 0; row < rows; ++row)
                {   const double d = row - centre;
                    surface[row * cols + col]
                    =   point->first * std::exp(-peak_width * d * d);
                }
            }

            const auto edge = [&](int c0, int r0, int c1, int r1)
            {   const double z0 = surface[r0 * cols + c0];
                const double z1 = surface[r1 * cols + c1];
                const auto [ax, ay] = project(c0, r0, z0);
                const auto [bx, by] = project(c1, r1, z1);
                c.DrawPointLine(ax, ay, bx, by, load_color((z0 + z1) / 2));
            };
            for (int row = 0; row < rows; ++row)
                for (int col = 0; col < cols; ++col)
                {   if (col)
                        edge(col - 1, row, col, row);
                    if (row)
                        edge(col, row - 1, col, row);
                }
            draw_axis();
        }
    )
    |   size(HEIGHT, GREATER_THAN, 15)
    |   flex
    ;
    Elements content;
    if (!title.empty())
    {   content.push_back(text(std::move(title)) | bold | hcenter);
        content.push_back(separator());
    }
    content.push_back
    (   hbox
            (   {   text(" ")
                ,   std::move(plot)
                ,   text(" ")
                }
            )
        |   flex
    );
    if (!legend.empty())
        content.push_back(text(std::move(legend)) | dim | hcenter);
    return vbox(std::move(content))
    |   borderStyled(plot_border_color)
    |   flex
    ;
}

template <typename History, typename LoadValue, typename MemoryValue>
ftxui::Element plot_2d_mesh
(   std::string title
,   History const& history
,   LoadValue load_value
,   MemoryValue memory_value
)
{   return plot_2d_mesh
    (   std::move(title), history.size()
    ,   [&history, load_value, memory_value](std::size_t index)
        -> std::optional<std::pair<double, double>>
        {   const auto& sample = history[index];
            if (sample)
            {   const auto load = load_value(*sample);
                const auto memory = memory_value(*sample);
                if (load && memory)
                    return std::pair{*load, *memory};
            }
            return std::nullopt;
        }
    ,   "x: time  y: memory usage  height: GPU load"
    ,   0
    ,   [](ftxui::Canvas&) {}
    ,   ""
    ,   ""
    );
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
