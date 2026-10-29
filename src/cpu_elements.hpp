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

ftxui::Elements socket_content
(   decltype(ftxui::App::Fullscreen()) const& screen
,   hwinfo::Cpu const& cpu
,   hwinfo::result<hwinfo::CpuLoad> const& cpu_load
,   hwinfo::result<std::vector<hwinfo::Hertz>> const& frequencies
)
{   using namespace ftxui;
    using namespace hwinfo;
    using namespace hwinfo::literals;

    Elements socket_elements;
    if (cpu_load)
    {   int term_width = screen.dimx() > 6 ? screen.dimx() - 6 : 40;
        Elements gauge_elements;
        size_t first = cpu.socket * cpu.logical_cores;
        for
        (   size_t i = first
        ;   i < first + cpu.logical_cores
        ;   ++i
        )
        {   const double tl = cpu_load->per_thread[i];
            const auto frequency
            =   frequencies && i < frequencies->size()
            ?   std::format("{:>11GHz}", (*frequencies)[i]) : std::string{};
            auto item = vbox
            (   {   hbox
                    (   {   text(std::format(" Core{:3}", i))
                        |   color(Color::RGB(197, 207, 245))
                        |   size(WIDTH, EQUAL, 8)
                        ,   text(std::format("{:^16}", frequency))
                        |   dim
                        |   size(WIDTH, EQUAL, 16)
                        ,   text(std::format("{:6.1f}% ", tl * 100.0))
                        |   color(Color::RGB(197, 207, 245))
                        |   size(WIDTH, EQUAL, 7)
                        }
                    )
                ,   separator()
                ,   gauge(tl)
                    |   color(Color::RGB(198, 160, 246))
                    |   bgcolor(Color::RGB(24, 26, 38))
                    |   size(WIDTH, EQUAL, 31)
                    |   center
                }
            ) 
            | border 
            | size(WIDTH, EQUAL, 35);

            gauge_elements.push_back(item);
        }
        // Fixed-width cells: always lay out every gauge, wrapping by columns
        constexpr int cell_width = 35;
        const size_t columns = std::max(1, (term_width - 2) / cell_width);
        std::vector<Elements> rows;
        for (size_t i = 0; i < gauge_elements.size(); i += columns)
        {   Elements row;
            for (size_t j = i; j < i + columns; ++j)
                row.push_back(j < gauge_elements.size() ? gauge_elements[j] : emptyElement());
            rows.push_back(std::move(row));
        }
        auto grid_content
        =   gridbox(std::move(rows))
        |   color(Color::RGB(126, 133, 163));

        const auto count = [&](CoreType type)
        {   return std::ranges::count(cpu.cores, type, &Core::type);
        };
        std::string core_count
        =   cpu.is_hybrid()
        ?   std::format
            (   "{} cores ({} Performance + {} Efficiency, {} threads)"
            ,   cpu.physical_cores
            ,   count(CoreType::performance)
            ,   count(CoreType::efficiency)
            ,   cpu.logical_cores
            )
        :   std::format
            (   "{} cores ({} threads)"
            ,   cpu.physical_cores
            ,   cpu.logical_cores
            )
        ;

        socket_elements.push_back
        (   vbox
            ({  hbox
                ({  text(std::format("Socket {} - ", cpu.socket))
                |   bold
                ,   text(core_count)
                |   dim
                })
                |   color(Color::RGB(197, 207, 245))
                |   hcenter
            ,   separator()
            ,   grid_content
            })
            // |   color(Color::RGB(197, 207, 245))
            |   border
            |   flex
        );
    }
    else
    {   socket_elements.push_back
        (   vbox
            (   {   text(std::format("Error: {}", cpu_load.error()))
                |   color(Color::RGB(197, 207, 245))
                |   hcenter
                }
            )
        |   border
        );
    }
    return socket_elements;
}

ftxui::Elements socket_content_plot
(   decltype(ftxui::App::Fullscreen()) const& screen
,   hwinfo::Cpu const& cpu
,   monitor const& mon
)
{   using namespace ftxui;

    constexpr std::size_t window = 60;
    constexpr int cell_width = 35;
    constexpr int plot_cols = 31;
    constexpr int plot_rows = 6;
    constexpr int px_w = plot_cols * 2;
    constexpr int px_h = plot_rows * 4;
    const auto text_color = Color::RGB(197, 207, 245);
    const auto line_color = Color::RGB(198, 160, 246);

    Elements socket_elements;
    const auto& history = mon.cpu_history();
    const auto& snap = mon();
    if (!snap.cpu)
    {   socket_elements.push_back
        (   text(std::format("Error: {}", snap.cpu.error()))
        |   color(text_color)
        |   hcenter
        |   border
        );
        return socket_elements;
    }

    int term_width = screen.dimx() > 6 ? screen.dimx() - 6 : 40;
    Elements cells;
    size_t first = cpu.socket * cpu.logical_cores;
    for
    (   size_t i = first
    ;   i < first + cpu.logical_cores && i < history.size()
    ;   ++i
    )
    {   const auto& h = history[i];
        const std::size_t n = std::min(h.size(), window);
        const std::size_t offset = h.size() - n;
        const double now = n ? h[h.size() - 1] : 0.0;
        const auto frequency
        =   snap.frequencies && i < snap.frequencies->size()
        ?   std::format("{:>11GHz}", (*snap.frequencies)[i]) : std::string{};

        const auto to_y = [&](double load)
        {   return static_cast<int>
            (   std::lround((1.0 - std::clamp(load, 0.0, 1.0)) * (px_h - 1))
            );
        };
        // Newest sample sits at the right edge; older ones scroll left.
        const double step = double(px_w - 1) / (window - 1);
        const auto to_x = [&](std::size_t k)
        {   return static_cast<int>
            (   std::lround((px_w - 1) - (n - 1 - k) * step)
            );
        };
        Canvas c(px_w, px_h);
        for (std::size_t k = 0; k < n; ++k)
        {   const int x = to_x(k);
            const int y = to_y(h[offset + k]);
            if (k)
                c.DrawPointLine
                (   to_x(k - 1), to_y(h[offset + k - 1]), x, y, line_color
                );
            else
                c.DrawPoint(x, y, true, line_color);
        }

        cells.push_back
        (   vbox
            (   {   hbox
                    (   {   text(std::format(" Core{:3}", i))
                        |   color(text_color)
                        |   size(WIDTH, EQUAL, 8)
                        ,   text(std::format("{:^16}", frequency))
                        |   dim
                        |   size(WIDTH, EQUAL, 16)
                        ,   text(std::format("{:6.1f}% ", now * 100.0))
                        |   color(text_color)
                        |   size(WIDTH, EQUAL, 7)
                        }
                    )
                ,   separator()
                ,   canvas(std::move(c))
                    |   bgcolor(Color::RGB(24, 26, 38))
                    |   size(WIDTH, EQUAL, plot_cols)
                    |   size(HEIGHT, EQUAL, plot_rows)
                    |   center
                }
            )
        |   border
        |   size(WIDTH, EQUAL, cell_width)
        );
    }

    const size_t columns = std::max(1, (term_width - 2) / cell_width);
    std::vector<Elements> rows;
    for (size_t i = 0; i < cells.size(); i += columns)
    {   Elements row;
        for (size_t j = i; j < i + columns; ++j)
            row.push_back(j < cells.size() ? cells[j] : emptyElement());
        rows.push_back(std::move(row));
    }

    socket_elements.push_back
    (   vbox
        (   {   text
                (   std::format
                    (   "Socket {} - {} threads, last {} s"
                    ,   cpu.socket, cpu.logical_cores, window
                    )
                )
                |   bold
                |   color(text_color)
                |   hcenter
            ,   separator()
            ,   gridbox(std::move(rows))
                |   color(Color::RGB(126, 133, 163))
            }
        )
    |   border
    |   flex
    );
    return socket_elements;
}

// Plots the last 60 seconds of load history of every thread of a socket,
// one canvas per thread, instead of instantaneous gauges.
ftxui::Elements socket_content_plot_filled
(   decltype(ftxui::App::Fullscreen()) const& screen
,   hwinfo::Cpu const& cpu
,   monitor const& mon
)
{   using namespace ftxui;

    constexpr std::size_t window = 60;
    constexpr int cell_width = 35;
    constexpr int plot_cols = 31;
    constexpr int plot_rows = 6;
    constexpr int px_w = plot_cols * 2;
    constexpr int px_h = plot_rows * 4;
    const auto text_color = Color::RGB(197, 207, 245);
    const auto line_color = Color::RGB(198, 160, 246);

    Elements socket_elements;
    const auto& history = mon.cpu_history();
    const auto& snap = mon();
    if (!snap.cpu)
    {   socket_elements.push_back
        (   text(std::format("Error: {}", snap.cpu.error()))
        |   color(text_color)
        |   hcenter
        |   border
        );
        return socket_elements;
    }

    int term_width = screen.dimx() > 6 ? screen.dimx() - 6 : 40;
    Elements cells;
    size_t first = cpu.socket * cpu.logical_cores;
    for
    (   size_t i = first
    ;   i < first + cpu.logical_cores && i < history.size()
    ;   ++i
    )
    {   const auto& h = history[i];
        const std::size_t n = std::min(h.size(), window);
        const std::size_t offset = h.size() - n;
        const double now = n ? h[h.size() - 1] : 0.0;
        const auto frequency
        =   snap.frequencies && i < snap.frequencies->size()
        ?   std::format("{:>11GHz}", (*snap.frequencies)[i]) : std::string{};

        const auto to_y = [&](double load)
        {   return static_cast<int>
            (   std::lround((1.0 - std::clamp(load, 0.0, 1.0)) * (px_h - 1))
            );
        };
        // Newest sample sits at the right edge; older ones scroll left.
        const double step = double(px_w - 1) / (window - 1);
        const auto to_x = [&](std::size_t k)
        {   return static_cast<int>
            (   std::lround((px_w - 1) - (n - 1 - k) * step)
            );
        };
        Canvas c(px_w, px_h);
        // Filled area: a vertical line from the baseline up to the
        // (linearly interpolated) load at every pixel column.
        const auto fill = [&](int x, int y)
        {   c.DrawPointLine(x, y, x, px_h - 1, line_color);
        };
        for (std::size_t k = 0; k < n; ++k)
        {   const int x1 = to_x(k);
            const int y1 = to_y(h[offset + k]);
            if (k == 0)
            {   fill(x1, y1);
                continue;
            }
            const int x0 = to_x(k - 1);
            const int y0 = to_y(h[offset + k - 1]);
            for (int x = x0 + 1; x <= x1; ++x)
                fill
                (   x
                ,   x1 == x0
                    ?   y1
                    :   y0 + static_cast<int>
                        (   std::lround(double(y1 - y0) * (x - x0) / (x1 - x0))
                        )
                );
        }

        cells.push_back
        (   vbox
            (   {   hbox
                    (   {   text(std::format(" Core{:3}", i))
                        |   color(text_color)
                        |   size(WIDTH, EQUAL, 8)
                        ,   text(std::format("{:^16}", frequency))
                        |   dim
                        |   size(WIDTH, EQUAL, 16)
                        ,   text(std::format("{:6.1f}% ", now * 100.0))
                        |   color(text_color)
                        |   size(WIDTH, EQUAL, 7)
                        }
                    )
                ,   separator()
                ,   canvas(std::move(c))
                    |   bgcolor(Color::RGB(24, 26, 38))
                    |   size(WIDTH, EQUAL, plot_cols)
                    |   size(HEIGHT, EQUAL, plot_rows)
                    |   center
                }
            )
        |   border
        |   size(WIDTH, EQUAL, cell_width)
        );
    }

    const size_t columns = std::max(1, (term_width - 2) / cell_width);
    std::vector<Elements> rows;
    for (size_t i = 0; i < cells.size(); i += columns)
    {   Elements row;
        for (size_t j = i; j < i + columns; ++j)
            row.push_back(j < cells.size() ? cells[j] : emptyElement());
        rows.push_back(std::move(row));
    }

    socket_elements.push_back
    (   vbox
        (   {   text
                (   std::format
                    (   "Socket {} - {} threads, last {} s"
                    ,   cpu.socket, cpu.logical_cores, window
                    )
                )
                |   bold
                |   color(text_color)
                |   hcenter
            ,   separator()
            ,   gridbox(std::move(rows))
                |   color(Color::RGB(126, 133, 163))
            }
        )
    |   border
    |   flex
    );
    return socket_elements;
}
