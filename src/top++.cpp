// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#include <chrono>
#include <format>
#include <memory>

#include "monitor.hpp"
#include "cpu_elements.hpp"

using namespace std::chrono_literals;
using namespace ftxui;
using namespace hwinfo::literals;

int main()
{   const auto os = hwinfo::os();
    const auto computer = hwinfo::computer();
    const auto virtualization = hwinfo::virtualization();
    const auto cpus = hwinfo::cpus();
    const auto gpus = hwinfo::gpus();
    monitor monitors;
    auto screen = App::Fullscreen();

    auto cpu_tab = Renderer
    (   [&screen, &cpus, &monitors]
        {   const auto& cpu = cpus->front();
            const auto& cpu_load = monitors().cpu;
            int term_width = screen.dimx() > 6 ? screen.dimx() - 6 : 40;
            auto cpu_tab = Container::Vertical({});
            auto cpu_info = vbox
            (   {   hbox
                    (   {   text(" Vendor")
                        |   dim
                        |   size(WIDTH, EQUAL, 10)
                        ,   separator()
                        ,   text(std::format(" {}", cpu.vendor))
                        }
                    )
                ,   separator()
                ,   hbox
                    (   {   text(" Model")
                        |   dim
                        |   size(WIDTH, EQUAL, 10)
                        ,   separator()
                        ,   text(std::format(" {}", cpu.model))
                        }
                    )
                ,   separator()
                ,   hbox
                    ({  text(" Load")
                    |   dim
                    |   size(WIDTH, EQUAL, 10)
                    ,   separator()
                    ,   text(" ")
                    ,   gauge(cpu_load->total)
                    |   color(Color::RGB(198, 160, 246))
                    |   bgcolor(Color::RGB(24, 26, 38))
                    |   size(WIDTH, EQUAL, term_width - 18)
                    |   center
                    ,   filler()
                    ,   separator()
                    ,   text(std::format("{:6.1f}% ", cpu_load->total * 100.0))
                    |   color(Color::RGB(198, 160, 246))
                    |   bold
                    })
                }
            )
            |   border
            // |   bgcolor(Color::RGB(30, 35, 57))
            ;

            Elements sockets;
            for (const auto& cpu : *cpus)
                sockets.append_range(socket_content(screen, cpu, monitors().cpu, monitors().frequencies));

            // Configure flexbox wrapping with gaps
            FlexboxConfig config;
            config.wrap = FlexboxConfig::Wrap::Wrap;
            config.direction = FlexboxConfig::Direction::Column;
            // config.SetGap(1, 1);
            // Build the flexbox grid
            auto grid_content
            =   flexbox(sockets, config)
            |   color(Color::RGB(126, 133, 163))
            |   size(WIDTH, LESS_THAN, term_width);

            // Make it scrollable by piping into vscroll_indicator, frame, and a height limit
            auto scrollable_grid
            =   grid_content
            |   vscroll_indicator 
            |   frame 
            ;

            cpu_tab->Add(Renderer([cpu_info] { return cpu_info; }));
            cpu_tab->Add(Renderer([scrollable_grid] { return scrollable_grid; }));
            return cpu_tab->Render()
            |   flex
            ;
        }
    );

    auto gpu_tab = Renderer
    (   [&gpus]
        {   auto gpu_list = Container::Vertical({});
            if (!gpus)
                return text(std::format("Error: {}", gpus.error())) | color(Color::Blue);
            if (gpus->empty())
                return text("No GPUs were detected") | color(Color::Blue);
            for (const auto& gpu : *gpus)
                gpu_list->Add(Renderer([gpu] { return text(" - " + hwinfo::to_string(gpu)) | color(Color::Blue); }));
            return gpu_list->Render() | flex | color(Color::Blue);
        }
    );

// -----------------------------------------------------------------------------
// Tabs
// -----------------------------------------------------------------------------

    int tab_index = 0;
    std::vector<std::string> tab_entries =
    {   " CPU "
    ,   " GPU "
    // ,   " Memory "
    };
    auto option = MenuOption::HorizontalAnimated();
    option.underline.color_inactive = Color::RGB(126, 133, 163);
    option.underline.color_active = Color::RGB(138, 92, 245);
    auto tab_selection = Menu
    (   &tab_entries
    ,   &tab_index
    ,   option
    )
    |   color(Color::RGB(197, 207, 245))
    ;
    auto tab_container = Container::Tab
    (   {   cpu_tab,
            gpu_tab,
            // memory,
        }
    ,   &tab_index
    );
    auto tab_content = tab_container
    |  color(Color::RGB(197, 207, 245))
    ;
    auto exit_button = Button
    (   "x"
    ,   [&] { screen.Exit(); }
    ,   ButtonOption::Ascii()
    );
    auto main_container = Container::Vertical
    (   {   Container::Horizontal
            (   {   tab_selection
                ,   exit_button
                }
            )
        ,   tab_content,
        }
    );
    // remove GPU tab if no GPUs are detected
    if (gpus && gpus->empty())
    {   tab_entries.erase(tab_entries.begin() + 1);
        tab_container->ChildAt(1)->Detach();
    }
    auto main_renderer = Renderer
    (   main_container
    ,   [&]
        {   const auto uptime = hwinfo::uptime();
            const auto host_info
            =   virtualization->vm.has_value()
                ?   text(hwinfo::to_string(*virtualization))
                :   computer.has_value()
                    ?   text(hwinfo::to_string(*computer))
                    :   text("Unknown host");
            Element uptime_info;
            if (uptime)
            {   const auto uptime_duration = *uptime;
                const auto days = std::chrono::duration_cast<std::chrono::days>(uptime_duration);
                const std::chrono::hh_mm_ss time_of_day(uptime_duration - days);
                uptime_info = text(std::format("Uptime: {} days, {:02}:{:02}:{:02}", days.count(), time_of_day.hours().count(), time_of_day.minutes().count(), time_of_day.seconds().count()));
            }
            else
            {   uptime_info = text(std::format("Uptime unavailable: {}", uptime.error()));
            }
            return vbox
            (   {   hbox
                    (   {   text(" 🔝++") | bold | color(Color::RGB(138, 92, 245))
                        ,   filler()
                        ,   host_info | color(Color::RGB(197, 207, 245))
                        ,   text(" • ") | bold | color(Color::RGB(198, 160, 246))
                        // ,   text(hwinfo::to_string(*os)) | color(Color::GrayLight)
                        ,   uptime_info | color(Color::RGB(197, 207, 245))
                        ,   filler()
                        ,   exit_button->Render()
                        }
                    )
                    |   borderRounded
                    |   bgcolor(Color::RGB(36, 39, 58))
                    |   color(Color::RGB(126, 133, 163))
                ,   hbox
                    (   {   tab_selection->Render() | flex
                        }
                    )
                    // | borderRounded
                    // | bgcolor(Color::RGB(36, 39, 58))
                    // | color(Color::LightPink1)
                ,   tab_content->Render() | flex
                }
            )
            |   bgcolor(Color::RGB(36, 39, 58))
            |   color(Color::RGB(126, 133, 163));
        }
    );

    Loop loop(&screen, main_renderer);
    while (!loop.HasQuitted())
    {   // Update the state of the application.
        monitors.tick();

        // Request a new frame to be drawn.
        screen.RequestAnimationFrame();

        // Execute events, and draw the next frame.
        loop.RunOnce();

        // Sleep for a short duration to control the frame rate (60 FPS).
        std::this_thread::sleep_for(std::chrono::milliseconds(1000 / 60));
    }

    return 0;
}