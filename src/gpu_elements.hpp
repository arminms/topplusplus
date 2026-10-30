// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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
    auto plot = canvas
    (   [&history, n, value](Canvas& c)
        {   paint_background(c);
            const int width = c.width();
            const int height = c.height();
            if (!width || !height || !n)
                return;
            const auto x = [width, n](std::size_t k)
            {   return width - 1 - static_cast<int>
                (   std::lround
                    (   double(n - 1 - k) * (width - 1)
                    /   (window - 1)
                    )
                );
            };
            const auto y = [height](double load)
            {   return static_cast<int>
                (   std::lround
                    (   (1.0 - std::clamp(load, 0.0, 1.0)) * (height - 1)
                    )
                );
            };
            const auto fill = [&](int px, double load)
            {   c.DrawPointLine(px, y(load), px, height - 1, load_color(load));
            };

            std::optional<double> previous;
            int previous_x = 0;
            for (std::size_t k = 0; k < n; ++k)
            {   const auto& sample = history[history.size() - n + k];
                const auto current = sample ? value(*sample) : std::nullopt;
                const int current_x = x(k);
                if (current)
                {   if (previous)
                    {   const int span = current_x - previous_x;
                        if (!span)
                            fill(current_x, *current);
                        else
                            for (int px = previous_x + 1; px <= current_x; ++px)
                                fill
                                (   px
                                ,   *previous + (*current - *previous)
                                    * (px - previous_x) / span
                                );
                    }
                    else
                        fill(current_x, *current);
                }
                previous = current;
                previous_x = current_x;
            }
        }
    )
    |   size(HEIGHT, GREATER_THAN, 15)
    |   flex
    ;
    return vbox
    (   {   text(std::move(title)) | bold | hcenter
        ,   separator()
        ,   hbox
            (   {   text(" ")
                ,   std::move(plot)
                ,   text(" ")
                }
            )
        |   flex
        }
    )
    |   border
    |   flex
    ;
}

// Wireframe surface in the style of canvas_animated.cpp's "2D gaussian plot".
// The mesh's x axis is time (newest on the right), its y axis is GPU memory
// usage in [0, 1] and its height is GPU load in [0, 1]: each time step raises
// a gaussian ridge cross-section at its memory usage with its load as the
// height, so the history reads as a moving mountain range.
template <typename LoadValue, typename MemoryValue>
ftxui::Element gpu_plot_2d
(   std::string title
,   circular_buffer_64<hwinfo::result<hwinfo::GpuStatus>> const& history
,   LoadValue load_value
,   MemoryValue memory_value
)
{   using namespace ftxui;
    const std::size_t n = std::min(history.size(), window);
    auto plot = canvas
    (   [&history, n, load_value, memory_value](Canvas& c)
        {   paint_background(c);
            const int width = c.width();
            const int height = c.height();
            if (width < 2 || height < 2 || !n)
                return;

            constexpr int rows = 15;
            constexpr double peak_width = 0.2;
            constexpr int column_px = 3;

            // Oblique projection: x runs right, y runs back (up and sideways)
            const double peak_px = 0.3 * (height - 1);
            const double depth_px = (height - 1) - peak_px;
            const double shear_px = std::min(0.6 * depth_px, 0.4 * (width - 1));
            const double span_px = (width - 1) - shear_px;
            const int cols = std::clamp
            (   static_cast<int>(span_px / column_px), 2, static_cast<int>(window)
            );

            // One (load, memory) pair per window slot, the newest in the last
            std::vector<std::optional<std::pair<double, double>>> series(window);
            for (std::size_t k = 0; k < n; ++k)
            {   const auto& sample = history[history.size() - n + k];
                const auto load = sample ? load_value(*sample) : std::nullopt;
                const auto usage = sample ? memory_value(*sample) : std::nullopt;
                if (load && usage)
                    series[window - n + k] = std::pair
                    {   std::clamp(*load, 0.0, 1.0)
                    ,   std::clamp(*usage, 0.0, 1.0)
                    };
            }

            // Resample the series onto the mesh columns
            std::vector<double> surface(cols * rows, 0.0);
            for (int col = 0; col < cols; ++col)
            {   const double pos = double(col) * (window - 1) / (cols - 1);
                const auto lo = static_cast<std::size_t>(pos);
                const auto hi = std::min(lo + 1, window - 1);
                const double t = pos - lo;
                const auto& a = series[lo];
                const auto& b = series[hi];
                std::optional<std::pair<double, double>> point;
                if (a && b)
                    point = std::pair
                    {   a->first + (b->first - a->first) * t
                    ,   a->second + (b->second - a->second) * t
                    };
                else
                    point = t < 0.5 ? a : b;
                if (!point)
                    continue;
                const double centre = point->second * (rows - 1);
                for (int row = 0; row < rows; ++row)
                {   const double d = row - centre;
                    surface[row * cols + col]
                    =   point->first * std::exp(-peak_width * d * d);
                }
            }

            const auto project = [=](int col, int row, double z)
            {   return std::pair
                {   static_cast<int>
                    (   std::lround
                        (   span_px * col / (cols - 1)
                        +   shear_px * row / (rows - 1)
                        )
                    )
                ,   static_cast<int>
                    (   std::lround
                        (   (height - 1)
                        -   depth_px * row / (rows - 1)
                        -   peak_px * z
                        )
                    )
                };
            };
            const auto edge = [&](int c0, int r0, int c1, int r1)
            {   const double z0 = surface[r0 * cols + c0];
                const double z1 = surface[r1 * cols + c1];
                const auto [ax, ay] = project(c0, r0, z0);
                const auto [bx, by] = project(c1, r1, z1);
                c.DrawPointLine(ax, ay, bx, by, load_color((z0 + z1) / 2));
            };
            for (int row = 0; row < rows; ++row)
                for (int col = 0; col < cols; ++col)
                {   if (col)
                        edge(col - 1, row, col, row);
                    if (row)
                        edge(col, row - 1, col, row);
                }
        }
    )
    |   size(HEIGHT, GREATER_THAN, 15)
    |   flex
    ;
    return vbox
    (   {   text(std::move(title)) | bold | hcenter
        ,   separator()
        ,   hbox
            (   {   text(" ")
                ,   std::move(plot)
                ,   text(" ")
                }
            )
        |   flex
        ,   text("x: time  y: memory usage  height: GPU load") | dim | hcenter
        }
    )
    |   border
    |   flex
    ;
}

// Both series as lines in a single canvas: GPU load shaded by load_color and
// GPU memory usage in the application's purple graph_color. The legends sit in
// the upper left corner of the canvas, in headroom reserved above the plot
// area; the load marker and percentage are shaded like the load line. White
// 0-100 markers on the left give the percentage scale.
template <typename LoadValue, typename MemoryValue>
ftxui::Element gpu_plot_load_n_memory
(   std::string load_name
,   std::optional<double> current_load
,   std::string memory_legend
,   circular_buffer_64<hwinfo::result<hwinfo::GpuStatus>> const& history
,   LoadValue load_value
,   MemoryValue memory_value
)
{   using namespace ftxui;
    const std::size_t n = std::min(history.size(), window);
    auto plot = canvas
    (   [   &history, n, load_value, memory_value, current_load
        ,   load_name = std::move(load_name)
        ,   memory_legend = std::move(memory_legend)
        ]
        (Canvas& c)
        {   paint_background(c);
            constexpr int legend_rows = 2;
            constexpr int cell_height = 4;
            constexpr int top = legend_rows * cell_height;
            // Room for the widest percentage marker ("100") and a gap
            constexpr int axis_px = 2 * 4;
            const int width = c.width();
            const int height = c.height();
            if (width <= axis_px + 1 || height <= top + 1)
                return;

            c.DrawText
            (   0, 0, "■"
            ,   current_load ? load_color(*current_load) : Color::Default
            );
            c.DrawText(4, 0, load_name);
            const int percent_x = 4 + 2 * (string_width(load_name) + 1);
            if (current_load)
                c.DrawText
                (   percent_x, 0
                ,   std::format("{:.1f}%", *current_load * 100.0)
                ,   load_color(*current_load)
                );
            else
                c.DrawText(percent_x, 0, "unavailable");
            c.DrawText(0, cell_height, "■", graph_color);
            c.DrawText(4, cell_height, memory_legend);

            const auto y = [height](double value)
            {   return top + static_cast<int>
                (   std::lround
                    (   (1.0 - std::clamp(value, 0.0, 1.0))
                    *   (height - 1 - top)
                    )
                );
            };
            // Percentage markers, right-aligned in the column left of the plot
            int previous_row = -1;
            for (const int percent : {100, 75, 50, 25, 0})
            {   const int row = y(percent / 100.0) / cell_height;
                if (row == previous_row)
                    continue;
                previous_row = row;
                const auto marker = std::to_string(percent);
                c.DrawText
                (   2 * (3 - static_cast<int>(marker.size()))
                ,   row * cell_height
                ,   marker
                ,   Color::White
                );
            }

            if (!n)
                return;
            const int plot_width = width - axis_px;
            const auto x = [width, plot_width, n](std::size_t k)
            {   return width - 1 - static_cast<int>
                (   std::lround
                    (   double(n - 1 - k) * (plot_width - 1)
                    /   (window - 1)
                    )
                );
            };
            // Segments are split into one-pixel pieces so that the shade
            // follows the value along steep slopes
            const auto trace = [&](auto const& value, auto const& shade)
            {   std::optional<std::pair<int, int>> previous;
                double previous_value = 0.0;
                for (std::size_t k = 0; k < n; ++k)
                {   const auto& sample = history[history.size() - n + k];
                    const auto current = sample ? value(*sample) : std::nullopt;
                    if (!current)
                    {   previous.reset();
                        continue;
                    }
                    const std::pair<int, int> point{x(k), y(*current)};
                    if (previous)
                    {   const int dx = point.first - previous->first;
                        const int dy = point.second - previous->second;
                        const int steps
                        =   std::max({std::abs(dx), std::abs(dy), 1});
                        const auto at = [&](int i)
                        {   return std::pair
                            {   previous->first
                                + static_cast<int>(std::lround(double(dx) * i / steps))
                            ,   previous->second
                                + static_cast<int>(std::lround(double(dy) * i / steps))
                            };
                        };
                        for (int i = 0; i < steps; ++i)
                        {   const auto from = at(i);
                            const auto to = at(i + 1);
                            c.DrawPointLine
                            (   from.first, from.second, to.first, to.second
                            ,   shade
                                (   previous_value
                                    + (*current - previous_value)
                                    * (i + 0.5) / steps
                                )
                            );
                        }
                    }
                    else
                        c.DrawPoint(point.first, point.second, true, shade(*current));
                    previous = point;
                    previous_value = *current;
                }
            };
            trace(memory_value, [](double) { return graph_color; });
            trace(load_value, [](double load) { return load_color(load); });
        }
    )
    |   size(HEIGHT, GREATER_THAN, 15)
    |   flex
    ;
    return hbox
    (   {   text(" ")
        ,   std::move(plot)
        ,   text(" ")
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
    const auto has_load = std::any_of
    (   history.begin(), history.end()
    ,   [&](const auto& sample)
        {   return sample && load(*sample).has_value();
        }
    );
    const auto has_memory_load = std::any_of
    (   history.begin(), history.end()
    ,   [&](const auto& sample)
        {   return sample && memory_load(*sample).has_value();
        }
    );
    const auto load_name = std::format("GPU{} %", gpu.index);
    const auto memory_name = std::format("GPU{} mem%", gpu.index);
    const auto load_label = status && status->utilization
    ?   std::format("{} {:.1f}%", load_name, *status->utilization * 100.0)
    :   std::format("{} unavailable", load_name)
    ;
    const auto memory_label = used_memory && total_memory && total_memory->value
    ?   std::format
        (   "{} {:.1f}%"
        ,   memory_name
        ,   100.0 * static_cast<double>(used_memory->value)
            / static_cast<double>(total_memory->value)
        )
    :   std::format("{} unavailable", memory_name)
    ;

    Elements plots;
    if (has_load && has_memory_load)
        plots.push_back
        (   gpu_plot_load_n_memory
            (   std::format("{:<{}}", load_name, memory_name.size())
            ,   status ? status->utilization : std::nullopt
            ,   memory_label, history, load, memory_load
            )
        );
    else if (has_load)
        plots.push_back(gpu_plot(load_label, history, load));
    else if (has_memory_load)
        plots.push_back(gpu_plot(memory_label, history, memory_load));
    const bool has_plots = !plots.empty();
    Element plot_content = emptyElement();
    if (plots.size() == 2)
        plot_content = screen.dimx() > 76
        ?   hbox(std::move(plots)) | flex
        :   vbox(std::move(plots)) | flex
        ;
    else if (!plots.empty())
        plot_content = std::move(plots.front()) | flex;

    Elements content
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
        }
    );
    if (has_plots)
    {   content.push_back(separator());
        content.push_back(std::move(plot_content));
    }

    return vbox(std::move(content))
    |   border
    |   flex
    ;
}

} // namespace detail
