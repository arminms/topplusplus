// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>

namespace detail {

inline auto count_cores(hwinfo::Cpu const& cpu, hwinfo::CoreType type)
{   return std::ranges::count(cpu.cores, type, &hwinfo::Core::type);
}

inline std::string core_count_string(hwinfo::Cpu const& cpu)
{   using hwinfo::CoreType;
    return cpu.is_hybrid()
    ?   std::format
        (   "{} cores ({} Performance + {} Efficiency, {} threads)"
        ,   cpu.physical_cores
        ,   count_cores(cpu, CoreType::performance)
        ,   count_cores(cpu, CoreType::efficiency)
        ,   cpu.logical_cores
        )
    :   std::format
        (   "{} cores ({} threads)"
        ,   cpu.physical_cores
        ,   cpu.logical_cores
        )
    ;
}

inline const auto text_color = ftxui::Color::RGB(197, 207, 245);
inline const auto graph_color = ftxui::Color::RGB(198, 160, 246);

constexpr int cell_width = 35;
constexpr int graph_cols = 31;
constexpr int graph_rows = 6;
constexpr std::size_t window = 60;

inline ftxui::Elements error_box(hwinfo::error const& message)
{   using namespace ftxui;
    return
    {   text(std::format("Error: {}", message))
    |   color(text_color)
    |   hcenter
    |   border
    };
}

inline std::string frequency_string(monitor::snapshot const& snap, size_t i)
{   return
        snap.frequencies && i < snap.frequencies->size()
    ?   std::format("{:>11GHz}", (*snap.frequencies)[i]) : std::string{};
}

// One bordered cell: core label, frequency and load on top, graphic below.
inline ftxui::Element core_cell
(   size_t i
,   std::string const& frequency
,   double load
,   ftxui::Element graphic
)
{   using namespace ftxui;
    return vbox
    (   {   hbox
            (   {   text(std::format(" Core{:3}", i))
                |   color(text_color)
                |   size(WIDTH, EQUAL, 8)
                ,   text(std::format("{:^16}", frequency))
                |   dim
                |   size(WIDTH, EQUAL, 16)
                ,   text(std::format("{:6.1f}% ", load * 100.0))
                |   color(text_color)
                |   size(WIDTH, EQUAL, 7)
                }
            )
        ,   separator()
        ,   std::move(graphic)
            |   bgcolor(Color::RGB(24, 26, 38))
            |   size(WIDTH, EQUAL, graph_cols)
            |   center
        }
    )
    |   border
    |   size(WIDTH, EQUAL, cell_width);
}

// Fixed-width cells: always lay out every cell, wrapping by columns.
inline ftxui::Element cell_grid
(   decltype(ftxui::App::Fullscreen()) const& screen
,   ftxui::Elements const& cells
)
{   using namespace ftxui;
    int term_width = screen.dimx() > 6 ? screen.dimx() - 6 : 40;
    const size_t columns = std::max(1, (term_width - 2) / cell_width);
    std::vector<Elements> rows;
    for (size_t i = 0; i < cells.size(); i += columns)
    {   Elements row;
        for (size_t j = i; j < i + columns; ++j)
            row.push_back(j < cells.size() ? cells[j] : emptyElement());
        rows.push_back(std::move(row));
    }
    return gridbox(std::move(rows)) | color(Color::RGB(126, 133, 163));
}

inline ftxui::Elements socket_box
(   ftxui::Element header
,   ftxui::Element grid
)
{   using namespace ftxui;
    return
    {   vbox
        (   {   std::move(header)
                |   color(text_color)
                |   hcenter
            ,   separator()
            ,   std::move(grid)
            }
        )
    |   border
    |   flex
    };
}

// Maps load/sample index to canvas pixels; newest sample is at the right edge.
struct plot_geometry
{   static constexpr int px_w = graph_cols * 2;
    static constexpr int px_h = graph_rows * 4;
    std::size_t n;

    int y(double load) const
    {   return static_cast<int>
        (   std::lround((1.0 - std::clamp(load, 0.0, 1.0)) * (px_h - 1))
        );
    }
    int x(std::size_t k) const
    {   const double step = double(px_w - 1) / (window - 1);
        return static_cast<int>
        (   std::lround((px_w - 1) - (n - 1 - k) * step)
        );
    }
};

// Builds the whole socket box with one canvas per thread; draw(canvas, geo,
// samples, offset) renders the visible samples samples[offset..offset+geo.n).
template <typename Draw>
ftxui::Elements plot_socket
(   decltype(ftxui::App::Fullscreen()) const& screen
,   hwinfo::Cpu const& cpu
,   monitor const& mon
,   Draw draw
)
{   using namespace ftxui;
    const auto& history = mon.cpu_history();
    const auto& snap = mon();
    if (!snap.cpu)
        return error_box(snap.cpu.error());

    Elements cells;
    size_t first = cpu.socket * cpu.logical_cores;
    for
    (   size_t i = first
    ;   i < first + cpu.logical_cores && i < history.size()
    ;   ++i
    )
    {   const auto& h = history[i];
        const std::size_t n = std::min(h.size(), window);
        const double now = n ? h[h.size() - 1] : 0.0;
        plot_geometry geo{n};
        Canvas c(geo.px_w, geo.px_h);
        draw(c, geo, h, h.size() - n);
        cells.push_back
        (   core_cell
            (   i, frequency_string(snap, i), now
            ,   canvas(std::move(c)) | size(HEIGHT, EQUAL, graph_rows)
            )
        );
    }

    return socket_box
    (   text
        (   std::format
            (   "Socket {} - {}, last {} seconds"
            ,   cpu.socket, core_count_string(cpu), window
            )
        )
    |   bold
    ,   cell_grid(screen, cells)
    );
}

}   // namespace detail
