// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#include <algorithm>
#include <chrono>
#include <limits>
#include <format>
#include <memory>

#include "monitor.hpp"
#include "cpu_elements.hpp"

#include <ftxui/dom/node.hpp>

namespace
{   struct scroll_state
    {   int offset = 0;
        int max_offset = 0;
        int view_height = 1;
        ftxui::Box view;
    };

    // Vertical viewport whose offset is controlled by the application.
    ftxui::Decorator scroll_view(scroll_state& st)
    {   class Impl : public ftxui::Node
        {   public:
                Impl(ftxui::Element child, scroll_state& st)
                :   Node(ftxui::Elements{std::move(child)}), st_(st) {}
                void ComputeRequirement() override
                {   Node::ComputeRequirement();
                    requirement_ = children_[0]->requirement();
                    requirement_.min_y = 1;
                    requirement_.flex_grow_y = 1;
                    requirement_.flex_shrink_y = 1;
                }
                void SetBox(ftxui::Box box) override
                {   Node::SetBox(box);
                    const int height = box.y_max - box.y_min + 1;
                    const int content
                    =   std::max(children_[0]->requirement().min_y, height);
                    st_.view = box;
                    st_.view_height = std::max(1, height);
                    st_.max_offset = content - height;
                    st_.offset = std::clamp(st_.offset, 0, st_.max_offset);
                    ftxui::Box cb = box;
                    cb.y_min = box.y_min - st_.offset;
                    cb.y_max = cb.y_min + content - 1;
                    children_[0]->SetBox(cb);
                }
                void Render(ftxui::Screen& screen) override
                {   const ftxui::Box saved = screen.stencil;
                    screen.stencil = ftxui::Box::Intersection(box_, saved);
                    struct restore { ftxui::Box& r; ftxui::Box v; ~restore() { r = v; } } rs{screen.stencil, saved};
                    children_[0]->Render(screen);
                    if (st_.max_offset <= 0)
                        return;
                    const int height = box_.y_max - box_.y_min + 1;
                    const int content = height + st_.max_offset;
                    const int size = std::max(1, height * height / content);
                    const int start
                    =   st_.offset * (height - size) / st_.max_offset;
                    for (int y = 0; y < height; ++y)
                    {   auto& px = screen.PixelAt(box_.x_max, box_.y_min + y);
                        px.character = y >= start && y < start + size ? "┃" : " ";
                    }
                }
            private:
                scroll_state& st_;
        };
        return [&st](ftxui::Element child)
        {   return std::make_shared<Impl>(std::move(child), st); };
    }
}

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

    scroll_state scroll;
    auto cpu_tab = Renderer
    (   [&screen, &cpus, &monitors, &scroll]
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
            auto scrollable_grid = grid_content | scroll_view(scroll);

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

    auto scrollable = CatchEvent
    (   main_renderer
    ,   [&](Event e)
        {   if (tab_index != 0)
                return false;
            const int view_h = scroll.view_height;
            if (e.is_mouse())
            {   if (!scroll.view.Contain(e.mouse().x, e.mouse().y))
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
            else if (e == Event::End) scroll.offset = std::numeric_limits<int>::max() / 2;
            else return false;
            return true;
        }
    );

    Loop loop(&screen, scrollable);
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