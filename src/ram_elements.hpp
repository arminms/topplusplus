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

inline ftxui::Element ram_content(monitor const& mon)
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
            rows.emplace_back(" Modules", "Unavailable");
        else
            rows.emplace_back(" Modules", std::to_string(installed.modules.size()));
        for (const auto& module : installed.modules)
        {   rows.emplace_back
            (   std::format(" Module {}", module.index)
            ,   module.name.value_or("Details")
            );
            rows.emplace_back
            (   "   Vendor"
            ,   module.vendor.value_or("Unavailable")
            );
            rows.emplace_back
            (   "   Model"
            ,   module.model.value_or("Unavailable")
            );
            rows.emplace_back
            (   "   Type"
            ,   module.type.value_or("Unavailable")
            );
            rows.emplace_back
            (   "   Form factor"
            ,   std::format("{}", module.form_factor)
            );
            rows.emplace_back
            (   "   Size"
            ,   module.size
                ?   std::format("{:.2GiB}", *module.size)
                :   "Unavailable"
            );
            rows.emplace_back
            (   "   Frequency"
            ,   module.frequency
                ?   std::format("{:.0MHz}", *module.frequency)
                :   "Unavailable"
            );
            rows.emplace_back
            (   "   Max frequency"
            ,   module.max_frequency
                ?   std::format("{:.0MHz}", *module.max_frequency)
                :   "Unavailable"
            );
            rows.emplace_back
            (   "   Rank"
            ,   module.rank
                ?   std::to_string(*module.rank)
                :   "Unavailable"
            );
            rows.emplace_back
            (   "   Data width"
            ,   module.data_width
                ?   std::format("{} bits", *module.data_width)
                :   "Unavailable"
            );
            rows.emplace_back
            (   "   Total width"
            ,   module.total_width
                ?   std::format("{} bits", *module.total_width)
                :   "Unavailable"
            );
            rows.emplace_back
            (   "   Voltage"
            ,   module.voltage_mv
                ?   std::format("{} mV", *module.voltage_mv)
                :   "Unavailable"
            );
        }
    }
    else
        rows.emplace_back(" Modules", std::format("Error: {}", memory_info.error()));

    auto information = new_info_box(std::move(rows))
    |   border
    ;
    return hbox
    (   {   std::move(information)
        ,   ram_usage_plot(memory, mon.memory_history())
        }
    )
    |   flex
    ;
}

}   // namespace detail
