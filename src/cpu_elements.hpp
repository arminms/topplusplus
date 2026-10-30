// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#include <algorithm>
#include <format>
#include <vector>
#include <cmath>

#include <ftxui/ftxui.hpp>
#include <hwinfo/hwinfo.h>

#include "monitor.hpp"
#include "detail/cpu_elements.hpp"

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
