// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ftxui/ftxui.hpp>

#include "commons.hpp"
#include "circular_buffer.hpp"
#include "scroll_view.hpp"
#include "sysproc.hpp"

namespace detail {

inline std::string process_text(std::string value)
{   for (auto& ch : value)
        if (static_cast<unsigned char>(ch) < 32 || ch == 127)
            ch = ' ';
    return value;
}

inline std::string process_command(const sysproc::process_info& process)
{   std::string command;
    for (const auto& arg : process.args)
    {   if (!command.empty())
            command += ' ';
        command += arg;
    }
    return process_text(command.empty() ? process.name : command);
}

inline std::string process_number(double value, std::size_t width)
{   auto formatted = std::format("{:.1f}", value);
    return formatted.size() <= width ? formatted : std::format("{:.1e}", value);
}

inline std::string process_io_rate(double bytes_per_sec, std::size_t width)
{   if (!std::isfinite(bytes_per_sec))
        return bytes_per_sec < 0.0 ? "-inf" : "nan";

    constexpr double kibibyte = 1024.0;
    double value = bytes_per_sec;
    std::string_view unit = "B/s";
    if (std::abs(value) >= kibibyte)
    {   value /= kibibyte;
        unit = "K/s";
    }
    if (std::abs(value) >= kibibyte)
    {   value /= kibibyte;
        unit = "M/s";
    }
    if (std::abs(value) >= kibibyte)
    {   value /= kibibyte;
        unit = "G/s";
    }
    if (std::abs(value) >= kibibyte)
    {   value /= kibibyte;
        unit = "T/s";
    }

    auto formatted = std::format("{:.2f} {}", value, unit);
    if (formatted.size() <= width)
        return formatted;
    return std::format("{:.1e} {}", value, unit);
}

inline ftxui::Element process_usage_plot
(   std::optional<double> cpu_load
,   std::optional<double> ram_load
,   const circular_buffer_64<std::optional<double>>& cpu_history
,   const circular_buffer_64<std::optional<double>>& ram_history
)
{
    using namespace ftxui;
    auto plot = canvas
    ([&cpu_history, &ram_history, cpu_load, ram_load](Canvas& c)
    {   paint_background(c);
        constexpr int legend_rows = 2;
        constexpr int cell_height = 4;
        constexpr int top = legend_rows * cell_height;
        constexpr int axis_px = 8;
        const int width = c.width();
        const int height = c.height();
        if (width <= axis_px + 1 || height <= top + 1)
            return;

        c.DrawText(0, 0, "■",
                   cpu_load ? load_color(*cpu_load) : Color::Default);
        c.DrawText(4, 0, "CPU");
        if (cpu_load)
            c.DrawText(16, 0,
                       std::format("{:.1f}%", (*cpu_load) * 100.0),
                       load_color(*cpu_load));
        else
            c.DrawText(16, 0, "unavailable");

        c.DrawText(0, cell_height, "■", graph_color);
        c.DrawText(4, cell_height, "RAM");
        if (ram_load)
            c.DrawText(16, cell_height,
                       std::format("{:.1f}%", (*ram_load) * 100.0),
                       graph_color);
        else
            c.DrawText(16, cell_height, "unavailable");

        const auto y = [height](double value)
        {   return top + static_cast<int>
            (std::lround((1.0 - std::clamp(value, 0.0, 1.0))
                         * (height - 1 - top)));
        };
        int previous_row = -1;
        for (const int percent : {100, 75, 50, 25, 0})
        {   const int row = y(percent / 100.0) / cell_height;
            if (row == previous_row)
                continue;
            previous_row = row;
            const auto marker = std::to_string(percent);
            c.DrawText(2 * (3 - static_cast<int>(marker.size())),
                       row * cell_height, marker, Color::White);
        }

        const int plot_width = width - axis_px;
        const auto trace = [&](const auto& history, const auto& shade)
        {   const std::size_t n = std::min(history.size(), detail::window);
            std::optional<std::pair<int, int>> previous;
            double previous_value = 0.0;
            for (std::size_t k = 0; k < n; ++k)
            {   const auto current = history[history.size() - n + k];
                if (!current)
                {   previous.reset();
                    continue;
                }
                const int x = width - 1 - static_cast<int>
                    (std::lround(double(n - 1 - k) * (plot_width - 1)
                                  / (detail::window - 1)));
                const std::pair<int, int> point{x, y(*current)};
                if (previous)
                {   const int dx = point.first - previous->first;
                    const int dy = point.second - previous->second;
                    const int steps = std::max({std::abs(dx), std::abs(dy), 1});
                    const auto at = [&](int i)
                    {   return std::pair
                        {   previous->first + static_cast<int>
                            (std::lround(double(dx) * i / steps))
                        ,   previous->second + static_cast<int>
                            (std::lround(double(dy) * i / steps))
                        };
                    };
                    for (int i = 0; i < steps; ++i)
                    {   const auto from = at(i);
                        const auto to = at(i + 1);
                        c.DrawPointLine(from.first, from.second,
                                        to.first, to.second,
                                        shade(previous_value
                                            + (*current - previous_value)
                                            * (i + 0.5) / steps));
                    }
                }
                else
                    c.DrawPoint(point.first, point.second, true, shade(*current));
                previous = point;
                previous_value = *current;
            }
        };
        trace(cpu_history,
              [](double load) { return load_color(load); });
        trace(ram_history,
              [](double) { return graph_color; });
    }) | flex;

    return hbox({text(" "), std::move(plot), text(" ")})
        | border
        | size(HEIGHT, GREATER_THAN, 10)
        | size(HEIGHT, LESS_THAN, 15);
}

inline std::string process_time
(   sysproc::time_point start_time
,   sysproc::time_point now = std::chrono::time_point_cast<std::chrono::microseconds>
        (std::chrono::system_clock::now())
)
{   if (start_time.time_since_epoch().count() == 0)
        return "--:--:--";
    const auto elapsed = now - start_time;
    if (elapsed.count() < 0)
        return "--:--:--";
    const auto total_seconds
        = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count());
    const auto hours = total_seconds / 3600;
    const auto minutes = (total_seconds % 3600) / 60;
    const auto remaining_seconds = total_seconds % 60;
    return std::format("{:02d}:{:02d}:{:02d}", hours, minutes, remaining_seconds);
}

enum class prc_column
{   pid, user, state, threads, cpu, memory, time, read, write, command, count
};

struct prc_sort_state
{   std::optional<prc_column> column;
    bool descending = true;
    std::optional<uint32_t> selected_pid;
    std::array<ftxui::Box, static_cast<std::size_t>(prc_column::count)> headers;
    std::vector<prc_column> visible;
};

inline bool prc_row_click
(   ftxui::Event event
,   const scroll_state& scroll
,   const std::vector<sysproc::process_info>& processes
,   prc_sort_state& sort
,   uint64_t total_memory_bytes = 0
)
{
    (void)total_memory_bytes;
    using namespace ftxui;
    if (!event.is_mouse())
        return false;
    const auto& mouse = event.mouse();
    if (mouse.button != Mouse::Left || mouse.motion != Mouse::Pressed
        || !scroll.view.Contain(mouse.x, mouse.y))
        return false;
    constexpr int header_lines = 3;
    const int first_row_y = scroll.view.y_min + header_lines;
    if (mouse.y < first_row_y)
        return false;
    const std::size_t row_index
        = static_cast<std::size_t>(mouse.y - first_row_y + scroll.offset);
    if (row_index >= processes.size())
        return false;

    std::vector<const sysproc::process_info*> ordered;
    ordered.reserve(processes.size());
    for (const auto& process : processes)
        ordered.push_back(&process);
    if (sort.column)
    {   const auto less = [column = *sort.column](const auto* a, const auto* b)
        {   switch (column)
            {   case prc_column::pid:     return a->pid < b->pid;
                case prc_column::user:    return a->username < b->username;
                case prc_column::state:   return std::string_view(sysproc::to_string(a->state)) < sysproc::to_string(b->state);
                case prc_column::threads: return a->thread_count < b->thread_count;
                case prc_column::cpu:     return a->cpu_percent < b->cpu_percent;
                case prc_column::memory:  return a->memory_bytes < b->memory_bytes;
                case prc_column::time:    return a->start_time < b->start_time;
                case prc_column::read:    return a->io_read_bytes_per_sec < b->io_read_bytes_per_sec;
                case prc_column::write:   return a->io_write_bytes_per_sec < b->io_write_bytes_per_sec;
                case prc_column::command: return process_command(*a) < process_command(*b);
                case prc_column::count:   return false;
            }
            return false;
        };
        std::stable_sort(ordered.begin(), ordered.end(), [&](const auto* a, const auto* b)
        {   return sort.descending ? less(b, a) : less(a, b); });
    }
    sort.selected_pid = ordered[row_index]->pid;
    return true;
}

inline bool prc_sort_click
(   ftxui::Event event, const scroll_state& scroll, prc_sort_state& sort
)
{   using namespace ftxui;
    if (!event.is_mouse())
        return false;
    const auto& mouse = event.mouse();
    if (mouse.button != Mouse::Left || mouse.motion != Mouse::Pressed
        || !scroll.view.Contain(mouse.x, mouse.y)
        || mouse.y != scroll.view.y_min + 1)
        return false;
    for (const auto column : sort.visible)
    {   const auto& box = sort.headers[static_cast<std::size_t>(column)];
        if (mouse.x < box.x_min || mouse.x > box.x_max)
            continue;
        sort.descending = sort.column == column ? !sort.descending : true;
        sort.column = column;
        return true;
    }
    return false;
}

inline ftxui::Element prc_content
(   const std::vector<sysproc::process_info>& processes, int width
,   uint64_t total_memory_bytes = 0
,   prc_sort_state* sort = nullptr
)
{   using namespace ftxui;
    if (sort)
        sort->visible.clear();
    if (processes.empty())
        return text("No processes available (enumeration may have failed)");

    struct process_row
    {   const sysproc::process_info* process;
        std::string command;
        std::string username;
    };
    std::vector<process_row> ordered;
    ordered.reserve(processes.size());
    for (const auto& process : processes)
        ordered.push_back({&process, process_command(process), process_text(process.username)});
    if (sort && sort->column)
    {   const auto less = [column = *sort->column](const process_row& a, const process_row& b)
        {   const auto& x = *a.process;
            const auto& y = *b.process;
            const auto number_less = [](double lhs, double rhs)
            {   if (std::isnan(lhs)) return !std::isnan(rhs);
                if (std::isnan(rhs)) return false;
                return lhs < rhs;
            };
            switch (column)
            {   case prc_column::pid:     return x.pid < y.pid;
                case prc_column::user:    return a.username < b.username;
                case prc_column::state:   return std::string_view(sysproc::to_string(x.state)) < sysproc::to_string(y.state);
                case prc_column::threads: return x.thread_count < y.thread_count;
                case prc_column::cpu:     return number_less(x.cpu_percent, y.cpu_percent);
                case prc_column::memory:  return x.memory_bytes < y.memory_bytes;
                case prc_column::time:    return x.start_time < y.start_time;
                case prc_column::read:    return number_less(x.io_read_bytes_per_sec, y.io_read_bytes_per_sec);
                case prc_column::write:   return number_less(x.io_write_bytes_per_sec, y.io_write_bytes_per_sec);
                case prc_column::command: return a.command < b.command;
                case prc_column::count:   return false;
            }
            return false;
        };
        std::stable_sort(ordered.begin(), ordered.end(), [&](const auto& a, const auto& b)
        {   return sort->descending ? less(b, a) : less(a, b); });
    }
    if (sort && !sort->selected_pid && !ordered.empty())
        sort->selected_pid = ordered.front().process->pid;

    const bool show_details = width >= 60;
    const bool show_state = width >= 100;
    const bool show_io = width >= 140;
    std::vector<std::vector<Element>> rows;
    std::vector<std::string> commands;
    std::vector<int> column_widths;
    rows.reserve(processes.size() + 1);
    commands.reserve(processes.size() + 1);
    auto make_row = [&](const process_row* entry)
    {   const auto* process = entry ? entry->process : nullptr;
        std::vector<Element> row;
        const auto cell = [&](prc_column id, std::string value, int max_width, bool numeric = false)
        {   const auto column = row.size();
            if (!process && sort)
            {   sort->visible.push_back(id);
                if (sort->column == id)
                    value = (sort->descending ? "▼ " : "▲ ") + value;
                max_width = std::max(max_width, string_width(value));
            }
            if (column_widths.size() <= column)
                column_widths.resize(column + 1);
            column_widths[column] = std::max
            (   column_widths[column], std::min(string_width(value), max_width)
            );
            auto element = text(std::move(value)) | size(WIDTH, LESS_THAN, max_width);
            if (numeric)
                element = std::move(element) | align_right | xflex_grow_factor(0);
            if (process && id != prc_column::cpu
                && (!sort || sort->selected_pid != process->pid))
                element = std::move(element) | dim;
            row.push_back(std::move(element));
        };
        cell(prc_column::pid, process ? std::to_string(process->pid) : "PID", 10, true);
        if (show_details)
            cell(prc_column::user, process ? entry->username : "USER", 8);
        if (show_state)
        {   cell(prc_column::state, process ? sysproc::to_string(process->state) : "STATE", 9);
            cell(prc_column::threads, process ? std::to_string(process->thread_count) : "THR", 10, true);
        }
        cell(prc_column::cpu, process ? process_number(process->cpu_percent, 8) : "CPU%", 8, true);
        if (process)
            row.back() = std::move(row.back()) | color(load_color(process->cpu_percent / 100.0));
        if (show_details)
        {   cell
            (   prc_column::memory, process
                    ?   total_memory_bytes
                        ?   process_number
                            (   static_cast<double>(process->memory_bytes)
                                / static_cast<double>(total_memory_bytes) * 100.0
                            ,   9
                            )
                        :   "--"
                    :   "MEM%"
            ,   9, true
            );
            cell(prc_column::time, process ? process_time(process->start_time) : "ELAPSED", 10, true);
        }
        if (show_io)
        {   cell(prc_column::read, process ? process_io_rate(process->io_read_bytes_per_sec, 12) : "IO/R", 12, true);
            cell(prc_column::write, process ? process_io_rate(process->io_write_bytes_per_sec, 12) : "IO/W", 12, true);
        }
        auto command = process ? entry->command : "COMMAND";
        if (!process && sort)
        {   sort->visible.push_back(prc_column::command);
            if (sort->column == prc_column::command)
                command = (sort->descending ? "▼ " : "▲ ") + command;
        }
        commands.push_back(std::move(command));
        return row;
    };
    rows.push_back(make_row(nullptr));
    for (const auto& entry : ordered)
        rows.push_back(make_row(&entry));

    int fixed_width = 2 * static_cast<int>(column_widths.size()) + 3;
    for (const auto column_width : column_widths)
        fixed_width += column_width;
    const int command_width = std::max(1, width - fixed_width);
    for (std::size_t i = 0; i < rows.size(); ++i)
    {   auto command = text(std::move(commands[i])) | size(WIDTH, LESS_THAN, command_width);
        if (i > 0 && (!sort || sort->selected_pid != ordered[i - 1].process->pid))
            command = std::move(command) | dim;
        rows[i].push_back(std::move(command));
    }

    auto table = Table(std::move(rows));
    table.SelectAll().SeparatorVertical(EMPTY);
    table.SelectRow(0).BorderBottom(LIGHT, dim);
    table.SelectAll().Border(LIGHT, dim);
    table.SelectRow(0).Decorate(bold);
    table.SelectAll().DecorateCells
    (   [](Element cell)
        {   return hbox({text(" "), std::move(cell) | xflex_grow})
            | xflex_grow_factor(0);
        }
    );
    table.SelectColumn(-1).DecorateCells(xflex);
    if (sort && sort->selected_pid)
        for (std::size_t i = 0; i < ordered.size(); ++i)
            if (ordered[i].process->pid == *sort->selected_pid)
            {   const auto highlight = bgcolor(Color::RGB(73, 77, 103));
                auto selected = table.SelectRow(static_cast<int>(i + 1));
                selected.DecorateCells(highlight);
                selected.DecorateSeparatorVertical(highlight);
            }
    if (sort)
        for (std::size_t i = 0; i < sort->visible.size(); ++i)
            table.SelectCell(static_cast<int>(i), 0).DecorateCells
            (   reflect(sort->headers[static_cast<std::size_t>(sort->visible[i])])
            );
    return table.Render() | xflex;
}

} // namespace detail
