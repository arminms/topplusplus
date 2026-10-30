// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <utility>

#include <ftxui/ftxui.hpp>
#include <hwinfo/hwinfo.h>

#include "detail/cpu_elements.hpp"
#include "monitor.hpp"

namespace detail {

template <typename Value>
ftxui::Element gpu_plot
(   std::string title
,   circular_buffer_64<hwinfo::result<hwinfo::GpuStatus>> const& history
,   Value value
)
{   using namespace ftxui;
    const std::size_t n = std::min(history.size(), window);
    plot_geometry geo{n};
    Canvas c(geo.px_w, geo.px_h);
    const auto fill = [&](int x, double load)
    {   c.DrawPointLine(x, geo.y(load), x, geo.px_h - 1, load_color(load));
    };

    std::optional<double> previous;
    int previous_x = 0;
    for (std::size_t k = 0; k < n; ++k)
    {   const auto& sample = history[history.size() - n + k];
        const auto current = sample ? value(*sample) : std::nullopt;
        const int x = geo.x(k);
        if (current)
        {   if (previous)
                for (int px = previous_x + 1; px <= x; ++px)
                    fill(px, *previous + (*current - *previous) * (px - previous_x) / (x - previous_x));
            else
                fill(x, *current);
        }
        previous = current;
        previous_x = x;
    }

    return vbox
    (   {   text(std::move(title)) | bold | hcenter
        ,   separator()
        ,   canvas(std::move(c)) | size(HEIGHT, EQUAL, graph_rows)
        }
    )
    |   border
    |   flex
    ;
}

inline std::string gpu_memory_string(std::optional<hwinfo::Bytes> memory)
{   return memory
    ?   std::format("{:.2GiB}", *memory)
    :   "Unavailable"
    ;
}

inline ftxui::Element gpu_info_row(std::string label, std::string value)
{   using namespace ftxui;
    return hbox
    (   {   text(std::move(label)) | dim | size(WIDTH, EQUAL, 16)
        ,   separator()
        ,   text(std::format(" {}", value))
        }
    );
}

inline ftxui::Element gpu_content
(   decltype(ftxui::App::Fullscreen()) const& screen
,   hwinfo::Gpu const& gpu
,   std::size_t gpu_position
,   monitor const& mon
)
{   using namespace ftxui;
    const auto& snap = mon();
    const hwinfo::GpuStatus* status
    =   gpu_position < snap.gpus.size() && snap.gpus[gpu_position]
    ?   &*snap.gpus[gpu_position]
    :   nullptr
    ;
    const auto total_memory
    =   status && status->memory_total
    ?   status->memory_total
    :   gpu.dedicated_memory ? gpu.dedicated_memory : gpu.shared_memory
    ;
    const auto used_memory = status ? status->memory_used : std::nullopt;
    const auto power = status && status->power
    ?   std::format("{:.1W}", *status->power)
    :   std::string("Unavailable")
    ;

    static const circular_buffer_64<hwinfo::result<hwinfo::GpuStatus>> empty_history;
    const auto& history = gpu_position < mon.gpu_history().size()
    ?   mon.gpu_history()[gpu_position]
    :   empty_history
    ;
    const auto load = [](hwinfo::GpuStatus const& sample)
    {   return sample.utilization;
    };
    const auto memory_load = [memory = total_memory](hwinfo::GpuStatus const& sample)
    {   const auto total = sample.memory_total ? sample.memory_total : memory;
        return sample.memory_used && total && total->value
        ?   std::optional<double>
            (   static_cast<double>(sample.memory_used->value)
                / static_cast<double>(total->value)
            )
        :   std::nullopt
        ;
    };
    const auto load_label = status && status->utilization
    ?   std::format("GPU load  {:.1f}%", *status->utilization * 100.0)
    :   std::string("GPU load  unavailable")
    ;
    const auto memory_label = used_memory && total_memory && total_memory->value
    ?   std::format
        (   "Memory usage  {:.1f}%"
        ,   100.0 * static_cast<double>(used_memory->value)
            / static_cast<double>(total_memory->value)
        )
    :   std::string("Memory usage  unavailable")
    ;

    auto plots = screen.dimx() > 76
    ?   hbox
        (   {   gpu_plot(load_label, history, load)
            ,   gpu_plot(memory_label, history, memory_load)
            }
        )
    :   vbox
        (   {   gpu_plot(load_label, history, load)
            ,   gpu_plot(memory_label, history, memory_load)
            }
        )
    ;

    return vbox
    (   {   text(std::format("Device {} - {}", gpu.index, hwinfo::to_string(gpu)))
            |   bold
            |   hcenter
        ,   separator()
        ,   vbox
            (   {   hbox
                    (   {   gpu_info_row("Total memory", gpu_memory_string(total_memory))
                        ,   filler()
                        ,   gpu_info_row("Used memory", gpu_memory_string(used_memory))
                        }
                    )
                ,   gpu_info_row("Power", power)
                }
            )
        ,   separator()
        ,   std::move(plots)
        }
    )
    |   border
    |   flex
    ;
}

} // namespace detail
