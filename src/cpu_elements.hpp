// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#include <algorithm>
#include <format>
#include <vector>

#include <ftxui/ftxui.hpp>
#include <hwinfo/hwinfo.h>

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
