// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <vector>
#include <cmath>

#include <ftxui/ftxui.hpp>
#include <hwinfo/hwinfo.h>

#include "monitor.hpp"
#include "detail/cpu_elements.hpp"

namespace detail {

inline std::optional<double> system_memory_load
(   hwinfo::result<hwinfo::MemoryUsage> const& memory
)
{   if (!memory || !memory->total.value
        || memory->available.value > memory->total.value)
        return std::nullopt;
    return static_cast<double>
    (   memory->total.value - memory->available.value
    )   / static_cast<double>(memory->total.value);
}

ftxui::Element cpu_load_n_memory_plot
(   std::optional<double> current_load
,   std::optional<double> current_memory_load
,   circular_buffer_64<std::optional<double>> const& load_history
,   circular_buffer_64<hwinfo::result<hwinfo::MemoryUsage>> const& memory_history
)
{   using namespace ftxui;
    auto plot = canvas
    (   [&load_history, &memory_history, current_load, current_memory_load]
        (Canvas& c)
        {   paint_background(c);
            constexpr int legend_rows = 2;
            constexpr int cell_height = 4;
            constexpr int top = legend_rows * cell_height;
            constexpr int axis_px = 2 * 4;
            const int width = c.width();
            const int height = c.height();
            if (width <= axis_px + 1 || height <= top + 1)
                return;

            c.DrawText
            (   0, 0, "■"
            ,   current_load ? load_color(*current_load) : Color::Default
            );
            c.DrawText(4, 0, "CPU");
            const int load_percent_x = 16;
            if (current_load)
                c.DrawText
                (   load_percent_x, 0
                ,   std::format("{:.1f}%", *current_load * 100.0)
                ,   load_color(*current_load)
                );
            else
                c.DrawText(load_percent_x, 0, "unavailable");

            c.DrawText(0, cell_height, "■", graph_color);
            if (current_memory_load)
                c.DrawText
                (   4, cell_height
                ,   std::format
                    (   "System memory {:.1f}%"
                    ,   *current_memory_load * 100.0
                    )
                ,   graph_color
                );
            else
                c.DrawText(4, cell_height, "System memory unavailable", graph_color);

            const auto y = [height](double value)
            {   return top + static_cast<int>
                (   std::lround
                    (   (1.0 - std::clamp(value, 0.0, 1.0))
                    *   (height - 1 - top)
                    )
                );
            };
            int previous_row = -1;
            for (const int percent : {100, 75, 50, 25, 0})
            {   const int row = y(percent / 100.0) / cell_height;
                if (row == previous_row)
                    continue;
                previous_row = row;
                const auto marker = std::to_string(percent);
                c.DrawText
                (   2 * (3 - static_cast<int>(marker.size()))
                ,   row * cell_height
                ,   marker
                ,   Color::White
                );
            }

            const int plot_width = width - axis_px;
            const auto trace = [&](auto const& history, auto const& value, auto const& shade)
            {   const std::size_t n = std::min(history.size(), window);
                std::optional<std::pair<int, int>> previous;
                double previous_value = 0.0;
                for (std::size_t k = 0; k < n; ++k)
                {   const auto current = value(history[history.size() - n + k]);
                    if (!current)
                    {   previous.reset();
                        continue;
                    }
                    const int x = width - 1 - static_cast<int>
                    (   std::lround
                        (   double(n - 1 - k) * (plot_width - 1)
                        /   (window - 1)
                        )
                    );
                    const std::pair<int, int> point{x, y(*current)};
                    if (previous)
                    {   const int dx = point.first - previous->first;
                        const int dy = point.second - previous->second;
                        const int steps = std::max({std::abs(dx), std::abs(dy), 1});
                        const auto at = [&](int i)
                        {   return std::pair
                            {   previous->first
                                + static_cast<int>(std::lround(double(dx) * i / steps))
                            ,   previous->second
                                + static_cast<int>(std::lround(double(dy) * i / steps))
                            };
                        };
                        for (int i = 0; i < steps; ++i)
                        {   const auto from = at(i);
                            const auto to = at(i + 1);
                            c.DrawPointLine
                            (   from.first, from.second, to.first, to.second
                            ,   shade
                                (   previous_value
                                    + (*current - previous_value) * (i + 0.5) / steps
                                )
                            );
                        }
                    }
                    else
                        c.DrawPoint(point.first, point.second, true, shade(*current));
                    previous = point;
                    previous_value = *current;
                }
            };
            trace
            (   load_history
            ,   [](std::optional<double> load) { return load; }
            ,   [](double load) { return load_color(load); }
            );
            trace
            (   memory_history
            ,   [](hwinfo::result<hwinfo::MemoryUsage> const& sample)
                {   return system_memory_load(sample);
                }
            ,   [](double) { return graph_color; }
            );
        }
    )
    |   flex
    ;
    return hbox
    (   {   text(" ")
        ,   std::move(plot)
        ,   text(" ")
        }
    )
    |   borderStyled(plot_border_color)
    |   size(HEIGHT, GREATER_THAN, 10)
    |   size(HEIGHT, LESS_THAN, 15)
    |   flex
    ;
}

}   // namespace detail

ftxui::Elements socket_content
(   decltype(ftxui::App::Fullscreen()) const& screen
,   hwinfo::Cpu const& cpu
,   monitor const& mon
)
{   using namespace ftxui;

    const auto& snap = mon();
    if (!snap.cpu)
        return detail::error_box(snap.cpu.error());

    Elements cells;
    size_t first = cpu.socket * cpu.logical_cores;
    for
    (   size_t i = first
    ;   i < first + cpu.logical_cores && i < snap.cpu->per_thread.size()
    ;   ++i
    )
    {   const double tl = snap.cpu->per_thread[i];
        cells.push_back
        (   detail::core_cell
            (   i, detail::frequency_string(snap, i), tl
            ,   gauge(tl) | color(detail::graph_color)
            )
        );
    }

    return detail::socket_box
    (   hbox
        (   {   text(std::format("Socket {} - ", cpu.socket))
            |   bold
            ,   text(detail::core_count_string(cpu))
            |   dim
            }
        )
    ,   detail::cell_grid(screen, cells)
    );
}

// Plots the last 60 seconds of load history of every thread of a socket
ftxui::Elements socket_content_plot
(   decltype(ftxui::App::Fullscreen()) const& screen
,   hwinfo::Cpu const& cpu
,   monitor const& mon
)
{   using namespace ftxui;
    return detail::plot_socket
    (   screen, cpu, mon
    ,   [](Canvas& c, auto const& geo, auto const& h, std::size_t offset)
        {   for (std::size_t k = 0; k < geo.n; ++k)
            {   const int x = geo.x(k);
                const int y = geo.y(h[offset + k]);
                if (k)
                    c.DrawPointLine
                    (   geo.x(k - 1), geo.y(h[offset + k - 1]), x, y
                    ,   detail::load_color(h[offset + k])
                    );
                else
                    c.DrawPoint(x, y, true, detail::load_color(h[offset + k]));
            }
        }
    );
}

// Plots the last 60 seconds of load history of every thread of a socket
// filled up to the current load, instead of just a line.
ftxui::Elements socket_content_filled_plot
(   decltype(ftxui::App::Fullscreen()) const& screen
,   hwinfo::Cpu const& cpu
,   monitor const& mon
)
{   using namespace ftxui;
    return detail::plot_socket
    (   screen, cpu, mon
    ,   [](Canvas& c, auto const& geo, auto const& h, std::size_t offset)
        {   // A vertical line from the baseline up to the (linearly
            // interpolated) load at every pixel column, colored by that load.
            const auto fill = [&](int x, double load)
            {   c.DrawPointLine
                (   x, geo.y(load), x, geo.px_h - 1, detail::load_color(load)
                );
            };
            for (std::size_t k = 0; k < geo.n; ++k)
            {   const int x1 = geo.x(k);
                const double l1 = h[offset + k];
                if (k == 0)
                {   fill(x1, l1);
                    continue;
                }
                const int x0 = geo.x(k - 1);
                const double l0 = h[offset + k - 1];
                for (int x = x0 + 1; x <= x1; ++x)
                    fill
                    (   x
                    ,   x1 == x0 ? l1 : l0 + (l1 - l0) * (x - x0) / (x1 - x0)
                    );
            }
        }
    );
}
