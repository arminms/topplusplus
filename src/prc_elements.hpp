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

inline std::string process_time(double seconds)
{   const auto centiseconds = static_cast<uint64_t>(std::round(seconds * 100.0));
    const auto minutes = centiseconds / 6000;
    const auto seconds_in_minute = (centiseconds % 6000) / 100.0;
    return std::format("{}:{:05.2f}", minutes, seconds_in_minute);
}

enum class prc_column
{   pid, user, state, threads, cpu, memory, time, read, write, command, count
};

struct prc_sort_state
{   std::optional<prc_column> column;
    bool descending = true;
    std::array<ftxui::Box, static_cast<std::size_t>(prc_column::count)> headers;
    std::vector<prc_column> visible;
};

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
                case prc_column::time:    return number_less(x.cpu_time_sec, y.cpu_time_sec);
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
            cell(prc_column::time, process ? process_time(process->cpu_time_sec) : "TIME+", 8, true);
        }
        if (show_io)
        {   cell(prc_column::read, process ? process_number(process->io_read_bytes_per_sec / 1024.0, 10) : "READ KiB/s", 10, true);
            cell(prc_column::write, process ? process_number(process->io_write_bytes_per_sec / 1024.0, 11) : "WRITE KiB/s", 11, true);
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
        rows[i].push_back
        (   text(std::move(commands[i])) | size(WIDTH, LESS_THAN, command_width)
        );

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
    if (sort)
        for (std::size_t i = 0; i < sort->visible.size(); ++i)
            table.SelectCell(static_cast<int>(i), 0).DecorateCells
            (   reflect(sort->headers[static_cast<std::size_t>(sort->visible[i])])
            );
    return table.Render() | xflex;
}

} // namespace detail
