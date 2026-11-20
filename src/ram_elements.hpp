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

inline ftxui::Element ram_usage_plot
(   hwinfo::result<hwinfo::MemoryUsage> const& memory
,   circular_buffer_64<hwinfo::result<hwinfo::MemoryUsage>> const& history
)
{   using namespace ftxui;
    auto plot = canvas
    (   [&history, memory](Canvas& c)
        {   paint_background(c);
            constexpr int legend_rows = 1;
            constexpr int cell_height = 4;
            constexpr int top = legend_rows * cell_height;
            constexpr int axis_px = 2 * 4;
            constexpr std::size_t window = 60;
            const int width = c.width();
            const int height = c.height();
            if (width <= axis_px + 1 || height <= top + 1)
                return;

            c.DrawText(0, 0, "■", graph_color);
            c.DrawText(4, 0, "RAM");
            if (const auto current_load = system_memory_load(memory))
            {   const auto used = hwinfo::Bytes
                {   memory->total.value - memory->available.value
                };
                c.DrawText
                (   12, 0
                ,   std::format
                    (   "{:5.1f}% ({:.1GiB}/{:.1GiB})"
                    ,   100.0 * *current_load, used, memory->total
                    )
                ,   graph_color
                );
            }
            else
                c.DrawText(12, 0, "unavailable");

            const auto y = [height](double value)
            {   return top + static_cast<int>
                (   std::lround
                    (   (1.0 - std::clamp(value, 0.0, 1.0))
                    *   (height - 1 - top)
                    )
                );
            };
            int previous_row = -1;
            for (const int percent : {100, 75, 50, 25, 0})
            {   const int row = y(percent / 100.0) / cell_height;
                if (row == previous_row)
                    continue;
                previous_row = row;
                const auto marker = std::to_string(percent);
                c.DrawText
                (   2 * (3 - static_cast<int>(marker.size()))
                ,   row * cell_height, marker, Color::White
                );
            }

            const int plot_width = width - axis_px;
            std::optional<std::pair<int, int>> previous;
            for (std::size_t k = 0, n = std::min(history.size(), window); k < n; ++k)
            {   const auto load = system_memory_load(history[history.size() - n + k]);
                if (!load)
                {   previous.reset();
                    continue;
                }
                const int x = width - 1 - static_cast<int>
                (   std::lround
                    (   double(n - 1 - k) * (plot_width - 1)
                    /   (window - 1)
                    )
                );
                const std::pair<int, int> point{x, y(*load)};
                if (previous)
                    c.DrawPointLine
                    (   previous->first, previous->second
                    ,   point.first, point.second, graph_color
                    );
                else
                    c.DrawPoint(point.first, point.second, true, graph_color);
                previous = point;
            }
        }
    )
    |   flex
    ;
    return hbox
    (   {   text(" ")
        ,   std::move(plot)
        ,   text(" ")
        }
    )
    |   border
    |   size(HEIGHT, GREATER_THAN, 10)
    |   size(HEIGHT, LESS_THAN, 15)
    |   flex
    ;
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
    if (memory_info)
        for (const auto& module : memory_info->modules)
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
        if (installed.modules.empty())
            rows.emplace_back(" Modules", "Unavailable ");
        else
            rows.emplace_back(" Modules", std::to_string(installed.modules.size()));
    }
    else
        rows.emplace_back(" Modules", std::format("Error: {}", memory_info.error()));

    auto information = ram_information(std::move(rows), modules);
    return hbox
    (   {   std::move(information)
        ,   ram_usage_plot(memory, mon.memory_history())
        }
    )
    |   flex
    ;
}

}   // namespace detail
