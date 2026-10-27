// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#include <chrono>
#include <format>
#include <memory>

#include <ftxui/ftxui.hpp>
#include <hwinfo/hwinfo.h>

using namespace std::chrono_literals;
using namespace ftxui;
using namespace hwinfo::literals;

struct monitor
{   struct snapshot
    {   hwinfo::result<hwinfo::CpuLoad> cpu;
        hwinfo::result<std::vector<hwinfo::Hertz>> frequencies;
        // std::vector<hwinfo::result<hwinfo::GpuStatus>> gpus;
        // hwinfo::result<hwinfo::MemoryUsage> memory;
        // std::vector<std::pair<std::filesystem::path, hwinfo::result<hwinfo::DiskSpace>>> disks;
    };

    monitor(std::size_t tick_max = 60)
    :   tick_max_(tick_max)
    ,   tick_count_(0)
    ,   cpu_sampler_(std::make_unique<hwinfo::CpuSampler>())
    ,   last_snapshot_(take_snapshot())
    {}

    void tick()
    {   tick_count_++;
        if (tick_count_ > tick_max_)
        {   tick_count_ = 0;
            last_snapshot_ = take_snapshot();
        }
    }

    const snapshot& operator()() const
    {   return last_snapshot_;
    }

    snapshot take_snapshot()
    {   return snapshot
        {   .cpu = cpu_sampler_->sample()
        ,   .frequencies = hwinfo::cpu_frequencies()
            // ,   .gpus = std::ranges::to<std::vector>
            //     (   gpu_samplers
            //     |   std::views::transform([](auto& gpu){ return gpu.sample(); })
            //     )
            // ,   .memory = hwinfo::memory_usage()
            // ,   .disks = std::ranges::to<std::vector>
            //     (   mount_points
            //     |   std::views::transform
            //         (   [](const auto& path)
            //             {   return std::pair{path, hwinfo::disk_space(path)};
            //             }
            //         )
            //     )
        };
    }

private:
    std::size_t tick_max_;
    std::size_t tick_count_;
    std::unique_ptr<hwinfo::CpuSampler> cpu_sampler_;
    snapshot last_snapshot_;
};

ftxui::Component cpu_component
(   decltype(App::Fullscreen()) const& screen
,   hwinfo::Cpu const& cpu
,   hwinfo::result<hwinfo::CpuLoad> const& cpu_load
,   hwinfo::result<std::vector<hwinfo::Hertz>> const& frequencies
)
{   return ftxui::Renderer
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

                const auto count = [&](hwinfo::CoreType type)
                {   return std::ranges::count(cpu.cores, type, &hwinfo::Core::type);
                };
                std::string core_count
                =   cpu.is_hybrid()
                ?   std::format
                    (   "{} cores ({} Performance + {} Efficiency, {} threads)"
                    ,   cpu.physical_cores
                    ,   count(hwinfo::CoreType::performance)
                    ,   count(hwinfo::CoreType::efficiency)
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
            {   return ftxui::text(std::format("Error: {}", cpu_load.error()))
            |   ftxui::color(ftxui::Color::RGB(197, 207, 245));
            }
        }
    );
}

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
            cpu_tab->Add(Renderer([cpu_info] { return cpu_info; }));
            for (const auto& cpu : *cpus)
                cpu_tab->Add(cpu_component(screen, cpu, monitors().cpu, monitors().frequencies));
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