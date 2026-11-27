// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#include <algorithm>
#include <chrono>
#include <limits>
#include <format>
#include <memory>
#include <optional>
#include <vector>

#include "monitor.hpp"
#include "cpu_elements.hpp"
#include "gpu_elements.hpp"
#include "ram_elements.hpp"
#include "prc_elements.hpp"
#include "sys_elements.hpp"
#include "scroll_view.hpp"
#include "commons.hpp"

#include <ftxui/dom/node.hpp>

using namespace std::chrono_literals;
using namespace ftxui;
using namespace hwinfo::literals;

int main()
{   const auto os = hwinfo::os();
    const auto computer = hwinfo::computer();
    const auto virtualization = hwinfo::virtualization();
    const auto mainboard = hwinfo::mainboard();
    const auto cpus = hwinfo::cpus();
    const auto gpus = hwinfo::gpus();
    const auto disks = hwinfo::disks();
    const auto networks = hwinfo::network_interfaces();
    double fps = 0.0, target_fps = 60.0;
    monitor monitors(static_cast<std::size_t>(target_fps));
    auto screen = App::Fullscreen();

    scroll_state cpu_scroll;
    scroll_state gpu_scroll;
    scroll_state ram_scroll;
    scroll_state sys_scroll;
    scroll_state prc_scroll;

// -----------------------------------------------------------------------------
// CPU tab
// -----------------------------------------------------------------------------

    auto cpu_tab = Renderer
    (   [&screen, &cpus, &monitors, &cpu_scroll]
        {   if (!cpus)
                return text(std::format("Error: {}", cpus.error())) | flex;
            if (cpus->empty())
                return text("No CPUs were detected") | flex;
            return detail::cpu_content
            (   screen, *cpus, monitors, scroll_view(cpu_scroll)
            );
        }
    );

// -----------------------------------------------------------------------------
// GPU tab
// -----------------------------------------------------------------------------

    auto gpu_tab = Renderer
    (   [&screen, &gpus, &monitors, &gpu_scroll]
        {   if (!gpus)
                return text(std::format("Error: {}", gpus.error())) | flex;
            if (gpus->empty())
                return text("No GPUs were detected") | flex;

            Elements devices;
            for (std::size_t i = 0; i < gpus->size(); ++i)
                devices.push_back(detail::gpu_content(screen, (*gpus)[i], i, monitors));
            const int term_width = screen.dimx() > 6 ? screen.dimx() - 6 : 40;
            auto content = detail::gpu_grid
            (   term_width, gpu_scroll.view_height, std::move(devices)
            )
            |   size(WIDTH, LESS_THAN, term_width)
            |   color(Color::RGB(126, 133, 163));
            return content | scroll_view(gpu_scroll) | flex;
        }
    );

// -----------------------------------------------------------------------------
// RAM tab
// -----------------------------------------------------------------------------

    auto modules = detail::ram_modules(monitors.memory_info());
    auto ram_tab = Renderer
    (   modules
    ,   [&monitors, &ram_scroll, &modules]
        {   return detail::ram_content(monitors, modules)
            |   scroll_view(ram_scroll)
            |   flex;
        }
    );

// -----------------------------------------------------------------------------
// SYS tab
// -----------------------------------------------------------------------------

    detail::sys_state system_state;
    auto system = detail::sys_content
    (   computer, virtualization, mainboard, os, disks, networks, system_state
    );
    auto sys_tab = Renderer
    (   system
    ,   [&system, &sys_scroll]
        {   return system->Render() | scroll_view(sys_scroll) | flex;
        }
    );

// -----------------------------------------------------------------------------
// PRC tab
// -----------------------------------------------------------------------------

    Element prc_table;
    detail::prc_sort_state prc_sort;
    std::size_t prc_generation = 0;
    int prc_width = -1;
    auto prc_tab = Renderer
    (   [&]
        {   if (!prc_table || prc_generation != monitors.snapshot_generation()
                || prc_width != screen.dimx())
        {   const auto total_memory_bytes = monitors().memory
                ? monitors().memory->total.value
                : 0;
            prc_table = detail::prc_content
            (   monitors().processes, screen.dimx(), total_memory_bytes, &prc_sort
            );
                prc_generation = monitors.snapshot_generation();
                prc_width = screen.dimx();
            }
            constexpr int header_lines = 3; // Top border, header, and separator.
            return prc_table | scroll_view(prc_scroll, true, header_lines) | flex;
        }
    );

// -----------------------------------------------------------------------------
// Main container
// -----------------------------------------------------------------------------

    constexpr int sys_tab_index = 0;
    constexpr int cpu_tab_index = 1;
    constexpr int gpu_tab_index = 2;
    const bool has_gpu_tab = !(gpus && gpus->empty());
    const int ram_tab_index = has_gpu_tab ? 3 : 2;
    const int prc_tab_index = has_gpu_tab ? 4 : 3;
    int tab_index = cpu_tab_index;
    std::vector<std::string> tab_entries =
    {   "  SYS  "
    ,   "  CPU  "
    ,   "  GPU  "
    ,   "  RAM  "
    ,   "  PRC  "
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
    (   {   sys_tab,
            cpu_tab,
            gpu_tab,
            ram_tab,
            prc_tab,
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
    if (!has_gpu_tab)
    {   tab_entries.erase(tab_entries.begin() + gpu_tab_index);
        tab_container->ChildAt(gpu_tab_index)->Detach();
    }
    auto main_renderer = Renderer
    (   main_container
    ,   [&]
        {   const auto uptime = hwinfo::uptime();
            const auto host_info
            =   virtualization && virtualization->vm
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
                        ,   text(" | ") | dim
                        ,   uptime_info | color(Color::RGB(197, 207, 245))
                        ,   text(" | ") | dim
                        ,   text(std::format("FPS: {:.1f}", fps)) | color(Color::RGB(197, 207, 245))
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

    auto scrollable = CatchEvent
    (   main_renderer
    ,   [&](Event e)
        {   if (tab_index < 0
                || tab_index >= static_cast<int>(tab_entries.size()))
                return false;
            if (tab_index == ram_tab_index
                && detail::ram_module_navigation(e, modules, tab_selection))
                return true;
            if (tab_index == sys_tab_index
                && detail::sys_navigation
                   (   e, system, tab_selection, system_state, sys_scroll
                   ))
                return true;
            auto& scroll = tab_index == sys_tab_index
            ?   sys_scroll
            :   tab_index == cpu_tab_index
                ?   cpu_scroll
                :   tab_index == gpu_tab_index && has_gpu_tab
                ?   gpu_scroll
                :   tab_index == prc_tab_index
                ?   prc_scroll
                :   ram_scroll
            ;
            const int view_h = scroll.view_height;
            if (e.is_mouse())
            {   if (tab_index == prc_tab_index
                    && detail::prc_sort_click(e, prc_scroll, prc_sort))
                {   prc_table = nullptr;
                    return true;
                }
                if (!scroll.view.Contain(e.mouse().x, e.mouse().y))
                    return false;
                if (e.mouse().button == Mouse::WheelUp)
                    scroll.offset -= 3;
                else if (e.mouse().button == Mouse::WheelDown)
                    scroll.offset += 3;
                else
                    return false;
                return true;
            }
            if (e == Event::ArrowUp) scroll.offset -= 1;
            else if (e == Event::ArrowDown) scroll.offset += 1;
            else if (e == Event::PageUp) scroll.offset -= view_h;
            else if (e == Event::PageDown) scroll.offset += view_h;
            else if (e == Event::Home) scroll.offset = 0;
            else if (e == Event::End)
                scroll.offset = std::numeric_limits<int>::max() / 2;
            else if (e == Event::Escape || e == Event::Character('q'))
            {   screen.Exit();
                return true;
            }
            else
                return false;
            return true;
        }
    );

// -----------------------------------------------------------------------------
// Main loop
// -----------------------------------------------------------------------------

    Loop loop(&screen, scrollable);
    auto fps_window_start = std::chrono::steady_clock::now();
    int frames_since_update = 0;
    int wait_time_ms = 1000 / target_fps; // Targeting 60 FPS
    while (!loop.HasQuitted())
    {   // Update the state of the application.
        monitors.tick();

        // Request a new frame to be drawn.
        screen.RequestAnimationFrame();

        // Execute events, and draw the next frame.
        loop.RunOnce();

        ++frames_since_update;
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = now - fps_window_start;
        if (elapsed >= 1s)
        {   fps = frames_since_update / std::chrono::duration<double>(elapsed).count();
            frames_since_update = 0;
            fps_window_start = now;
            // Adjust wait_time_ms based on the actual FPS to maintain the target FPS.
            if (fps < target_fps)
                wait_time_ms--;
            else if (fps > target_fps)
                wait_time_ms++;
            wait_time_ms = std::clamp<int>(wait_time_ms, 1, 50);
        }

        // Sleep for a short duration to control the frame rate.
        std::this_thread::sleep_for(std::chrono::milliseconds(wait_time_ms));
    }

    return 0;
}