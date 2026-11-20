// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/ftxui.hpp>
#include <hwinfo/hwinfo.h>

#include "commons.hpp"
#include "monitor.hpp"

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

inline void draw_cpu_memory_legend
(   ftxui::Canvas& c
,   std::optional<double> current_load
,   hwinfo::result<hwinfo::MemoryUsage> const& memory
)
{   using namespace ftxui;
    constexpr int cell_height = 4;
    constexpr int percent_cells = 6;
    constexpr int load_percent_x = 16;
    const auto draw_percent = [&](int row, double value, Color shade)
    {   const auto percent = std::format("{:.1f}%", value * 100.0);
        c.DrawText
        (   load_percent_x
            + 2 * (percent_cells - static_cast<int>(percent.size()))
        ,   row, percent, shade
        );
    };

    c.DrawText
    (   0, 0, "■"
    ,   current_load ? load_color(*current_load) : Color::Default
    );
    c.DrawText(4, 0, "CPU");
    if (current_load)
        draw_percent(0, *current_load, load_color(*current_load));
    else
        c.DrawText(load_percent_x, 0, "unavailable");

    c.DrawText(0, cell_height, "■", graph_color);
    c.DrawText(4, cell_height, "RAM");
    if (const auto memory_load = system_memory_load(memory))
    {   draw_percent(cell_height, *memory_load, graph_color);
        const hwinfo::Bytes used
        {   memory->total.value - memory->available.value
        };
        c.DrawText
        (   load_percent_x + 2 * (percent_cells + 1), cell_height
        ,   std::format("({:.1GiB}/{:.1GiB})", used, memory->total)
        ,   Color::White
        );
    }
    else
        c.DrawText(load_percent_x, cell_height, "unavailable");
}

inline ftxui::Element ram_surface_plot
(   std::optional<double> current_load
,   hwinfo::result<hwinfo::MemoryUsage> const& memory
,   circular_buffer_64<std::optional<double>> const& cpu_history
,   circular_buffer_64<hwinfo::result<hwinfo::MemoryUsage>> const& memory_history
)
{   const auto count = std::min(cpu_history.size(), memory_history.size());
    return plot_2d_mesh
    (   "", count
    ,   [&cpu_history, &memory_history, count](std::size_t index)
        -> std::optional<std::pair<double, double>>
        {   const auto& cpu = cpu_history[cpu_history.size() - count + index];
            const auto memory = system_memory_load
            (   memory_history[memory_history.size() - count + index]
            );
            if (cpu && memory)
                return std::pair{*cpu, *memory};
            return std::nullopt;
        }
    ,   ""
    ,   2
    ,   [current_load, memory](ftxui::Canvas& c)
        {   draw_cpu_memory_legend(c, current_load, memory);
        }
    ,   "RAM"
    ,   "60 seconds"
    );
}

inline ftxui::Component ram_module(hwinfo::MemoryModule module)
{   using namespace ftxui;
    auto label = std::format
    (   "Module {} - {} ", module.index, module.name.value_or("Details")
    );
    auto details = Renderer
    (   [module = std::move(module)]
        {   std::vector<std::pair<std::string, std::string>> rows
            {   {"   Vendor", module.vendor.value_or("Unavailable")}
            ,   {"   Model", module.model.value_or("Unavailable")}
            ,   {"   Type", module.type.value_or("Unavailable")}
            ,   {"   Form factor", std::format("{}", module.form_factor)}
            ,   {   "   Size"
                ,   module.size
                    ?   std::format("{:.2GiB}", *module.size)
                    :   "Unavailable"
                }
            ,   {   "   Frequency"
                ,   module.frequency
                    ?   std::format("{:.0MHz}", *module.frequency)
                    :   "Unavailable"
                }
            ,   {   "   Max frequency"
                ,   module.max_frequency
                    ?   std::format("{:.0MHz}", *module.max_frequency)
                    :   "Unavailable"
                }
            ,   {   "   Rank"
                ,   module.rank ? std::to_string(*module.rank) : "Unavailable"
                }
            ,   {   "   Data width"
                ,   module.data_width
                    ?   std::format("{} bits", *module.data_width)
                    :   "Unavailable"
                }
            ,   {   "   Total width"
                ,   module.total_width
                    ?   std::format("{} bits", *module.total_width)
                    :   "Unavailable"
                }
            ,   {   "   Voltage"
                ,   module.voltage_mv
                    ?   std::format("{} mV", *module.voltage_mv)
                    :   "Unavailable"
                }
            };
            Elements elements;
            for (auto& [name, value] : rows)
                elements.push_back
                (   hbox
                    (   {   text(std::move(name)) | dim | size(WIDTH, EQUAL, 16)
                        ,   text(std::format(" {}", value))
                        }
                    )
                );
            return vbox(std::move(elements));
        }
    );
    return Collapsible(std::move(label), std::move(details), false);
}

inline ftxui::Component ram_modules
(   hwinfo::result<hwinfo::Memory> const& memory_info
)
{   using namespace ftxui;
    Components modules;
    if (memory_info && memory_info->modules)
        for (const auto& module : *memory_info->modules)
            modules.push_back(ram_module(module));
    return Container::Vertical(std::move(modules));
}

inline bool ram_module_navigation
(   ftxui::Event event
,   ftxui::Component const& modules
,   ftxui::Component const& tab_selection
)
{   using namespace ftxui;
    if (!modules->ChildCount()
        || (event != Event::Tab && event != Event::TabReverse))
        return false;
    const bool forward = event == Event::Tab;
    const auto last = modules->ChildCount() - 1;
    if (tab_selection->Focused())
    {   modules->ChildAt(forward ? 0 : last)->TakeFocus();
        return true;
    }
    if (modules->Focused()
        && modules->ChildAt(forward ? last : 0)->Focused())
    {   tab_selection->TakeFocus();
        return true;
    }
    return false;
}

inline ftxui::Element ram_information
(   std::vector<std::pair<std::string, std::string>> rows
,   ftxui::Component const& modules
)
{   using namespace ftxui;
    Elements information_rows{new_info_box(std::move(rows)), separator()};
    if (modules->ChildCount())
        information_rows.push_back(modules->Render());
    return vbox(std::move(information_rows)) | border;
}

inline ftxui::Element ram_content
(   monitor const& mon
,   ftxui::Component const& modules
)
{   using namespace ftxui;
    const auto& memory = mon().memory;
    const auto& memory_info = mon.memory_info();
    std::string total = "Unavailable";
    std::string used = "Unavailable";
    std::string available = "Unavailable";
    std::string usage = "Unavailable";
    std::string usage_status = memory
    ?   "Invalid memory usage data"
    :   std::format("Error: {}", memory.error())
    ;
    if (memory_info)
        total = std::format("{:.2GiB}", memory_info->total);
    if (const auto load = system_memory_load(memory))
    {   total = std::format("{:.2GiB}", memory->total);
        available = std::format("{:.2GiB}", memory->available);
        used = std::format
        (   "{:.2GiB}"
        ,   hwinfo::Bytes
            {   memory->total.value - memory->available.value
            }
        );
        usage = std::format("{:.1f}%", *load * 100.0);
        usage_status = "Available";
    }

    std::vector<std::pair<std::string, std::string>> rows
    {   {" Total", total}
    ,   {" Used", used}
    ,   {" Available", available}
    ,   {" Usage", usage}
    ,   {" Usage status", usage_status}
    };
    if (memory_info)
    {   const auto& installed = *memory_info;
        rows.emplace_back
        (   " Slots"
        ,   installed.slots
            ?   std::to_string(*installed.slots)
            :   "Unavailable"
        );
        rows.emplace_back
        (   " Max capacity"
        ,   installed.max_capacity
            ?   std::format("{:.2GiB}", *installed.max_capacity)
            :   "Unavailable"
        );
        if (!installed.modules || installed.modules->empty())
            rows.emplace_back(" Modules", "Unavailable ");
        else
            rows.emplace_back(" Modules", std::to_string(installed.modules->size()));
    }
    else
        rows.emplace_back(" Modules", std::format("Error: {}", memory_info.error()));

    auto information = ram_information(std::move(rows), modules);
    return hbox
    (   {   std::move(information)
        ,   ram_surface_plot
            (   mon().cpu ? std::optional<double>(mon().cpu->total) : std::nullopt
            ,   memory, mon.cpu_total_history(), mon.memory_history()
            )
        }
    )
    |   flex
    ;
}

}   // namespace detail
