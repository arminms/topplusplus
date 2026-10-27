// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#include <format>

#include <ftxui/ftxui.hpp>
#include <hwinfo/hwinfo.h>

ftxui::Component cpu_component
(   decltype(ftxui::App::Fullscreen()) const& screen
,   hwinfo::Cpu const& cpu
,   hwinfo::result<hwinfo::CpuLoad> const& cpu_load
,   hwinfo::result<std::vector<hwinfo::Hertz>> const& frequencies
)
{   using namespace ftxui;
    using namespace hwinfo;
    using namespace hwinfo::literals;
    return Renderer
    (   [&cpu, &cpu_load, &frequencies, &screen]
        {   if (cpu_load)
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
                // Configure flexbox wrapping with gaps
                FlexboxConfig config;
                config.wrap = FlexboxConfig::Wrap::Wrap;
                config.direction = FlexboxConfig::Direction::Row;
                // config.SetGap(1, 1);
                // Build the flexbox grid
                auto grid_content
                =   flexbox(gauge_elements, config)
                |   color(Color::RGB(126, 133, 163))
                |   size(WIDTH, LESS_THAN, term_width);

                // Make it scrollable by piping into vscroll_indicator, frame, and a height limit
                // auto scrollable_grid
                // =   grid_content
                // |   vscroll_indicator 
                // |   frame 
                // | 

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

                return vbox
                ({  hbox
                    ({  text(std::format("Socket {} - ", cpu.socket))
                    |   bold
                    ,   text(core_count)
                    |   dim
                    })
                    |   color(Color::RGB(197, 207, 245))
                    |   hcenter
                ,   separator()
                ,   grid_content // comment out this line and uncomment the next line to use scrollable grid
                // ,   scrollable_grid
                })
                // |   color(Color::RGB(197, 207, 245))
                |   border
                |   flex
                ;
            }
            else
            {   return text(std::format("Error: {}", cpu_load.error()))
            |   color(Color::RGB(197, 207, 245));
            }
        }
    );
}
