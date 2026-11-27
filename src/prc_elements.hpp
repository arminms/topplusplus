// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>

#include <ftxui/ftxui.hpp>

#include "commons.hpp"
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

inline ftxui::Element prc_content
(   const std::vector<sysproc::process_info>& processes, int width
,   uint64_t total_memory_bytes = 0
)
{   using namespace ftxui;
    if (processes.empty())
        return text("No processes available (enumeration may have failed)");

    const bool show_details = width >= 60;
    const bool show_state = width >= 100;
    const bool show_io = width >= 140;
    std::vector<std::vector<Element>> rows;
    std::vector<std::string> commands;
    std::vector<int> column_widths;
    rows.reserve(processes.size() + 1);
    commands.reserve(processes.size() + 1);
    auto make_row = [&](const sysproc::process_info* process)
    {   std::vector<Element> row;
        const auto cell = [&](std::string value, int max_width, bool numeric = false)
        {   const auto column = row.size();
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
        cell(process ? std::to_string(process->pid) : "PID", 10, true);
        if (show_details)
            cell(process ? process_text(process->username) : "USER", 8);
        if (show_state)
        {   cell(process ? sysproc::to_string(process->state) : "STATE", 9);
            cell(process ? std::to_string(process->thread_count) : "THR", 10, true);
        }
        cell(process ? process_number(process->cpu_percent, 8) : "CPU%", 8, true);
        if (process)
            row.back() = std::move(row.back()) | color(load_color(process->cpu_percent / 100.0));
        if (show_details)
        {   cell
            (   process
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
            cell(process ? process_time(process->cpu_time_sec) : "TIME+", 8, true);
        }
        if (show_io)
        {   cell(process ? process_number(process->io_read_bytes_per_sec / 1024.0, 10) : "READ KiB/s", 10, true);
            cell(process ? process_number(process->io_write_bytes_per_sec / 1024.0, 11) : "WRITE KiB/s", 11, true);
        }
        commands.push_back(process ? process_command(*process) : "COMMAND");
        return row;
    };
    rows.push_back(make_row(nullptr));
    for (const auto& process : processes)
        rows.push_back(make_row(&process));

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
    return table.Render() | xflex;
}

} // namespace detail
