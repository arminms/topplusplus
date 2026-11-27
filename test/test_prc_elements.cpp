// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#include <catch2/catch_test_macros.hpp>

#include "sysproc.hpp"
#include "prc_elements.hpp"
#include "scroll_view.hpp"

namespace {

std::string render(ftxui::Element element, int width, int height = 10)
{   auto screen = ftxui::Screen::Create
    (   ftxui::Dimension::Fixed(width), ftxui::Dimension::Fixed(height)
    );
    ftxui::Render(screen, std::move(element));
    return screen.ToString();
}

}

TEST_CASE("Process plot shows PID and usage on one line")
{   circular_buffer_64<std::optional<double>> cpu_history;
    circular_buffer_64<std::optional<double>> ram_history;
    const auto draw = [&](std::optional<uint32_t> pid, std::optional<double> cpu,
                          std::optional<double> ram)
    {   auto screen = ftxui::Screen::Create
        (   ftxui::Dimension::Fixed(90), ftxui::Dimension::Fixed(12)
        );
        ftxui::Render
        (   screen, detail::process_usage_plot(pid, cpu, ram, cpu_history, ram_history)
        );
        return screen;
    };
    const auto line = [](ftxui::Screen& screen, int y)
    {   std::string result;
        for (int x = 0; x < screen.dimx(); ++x)
        {   const auto& character = screen.PixelAt(x, y).character;
            result += character.empty() ? " " : character.size() > 1 ? "#" : character;
        }
        return result;
    };
    auto screen = draw(12345, 0.5, 0.25);
    auto legend = line(screen, 1);
    REQUIRE(legend.find("PID: 12345 | # CPU: 50.0% | # RAM: 25.0%") != std::string::npos);
    const auto first_separator = legend.find(" | ");
    const auto second_separator = legend.find(" | ", first_separator + 3);
    REQUIRE(first_separator != std::string::npos);
    REQUIRE(second_separator != std::string::npos);
    for (const auto start : {first_separator, second_separator})
        for (std::size_t x = start; x < start + 3; ++x)
        {   INFO("legend: " << legend << ", separator cell " << x
                 << ": " << screen.PixelAt(static_cast<int>(x), 1).character);
            REQUIRE(screen.PixelAt(static_cast<int>(x), 1).dim);
        }
    const auto cpu = legend.find("50.0%");
    const auto ram = legend.find("25.0%");
    REQUIRE(screen.PixelAt(static_cast<int>(cpu), 1).foreground_color == detail::load_color(0.5));
    REQUIRE(screen.PixelAt(static_cast<int>(ram), 1).foreground_color == detail::graph_color);

    screen = draw(std::nullopt, std::nullopt, std::nullopt);
    legend = line(screen, 1);
    REQUIRE(legend.find("PID: unavailable | # CPU: unavailable | # RAM: unavailable")
            != std::string::npos);
}

TEST_CASE("Process plot shows only 100, 50 and 0 axis markers")
{   circular_buffer_64<std::optional<double>> history;
    for (const int height : {10, 12, 15})
    {   INFO("height " << height);
        auto screen = ftxui::Screen::Create
        (   ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(height)
        );
        ftxui::Render
        (   screen, detail::process_usage_plot(1, 0.5, 0.25, history, history)
        );
        std::vector<std::string> markers;
        for (int y = 2; y < height - 1; ++y)
        {   std::string marker;
            for (int x = 2; x < 5; ++x)
            {   const auto& character = screen.PixelAt(x, y).character;
                if (character >= "0" && character <= "9")
                    marker += character;
            }
            if (!marker.empty())
                markers.push_back(marker);
        }
        REQUIRE(markers == std::vector<std::string>{"100", "50", "0"});
    }
}

TEST_CASE("Process table shows sampled information and adapts to width")
{   sysproc::process_info process;
    process.pid = 123;
    process.username = "alice";
    process.state = sysproc::process_state::running;
    process.thread_count = 4;
    process.cpu_percent = 125.5;
    process.memory_bytes = 32 * 1024 * 1024;
    process.cpu_time_sec = 12.5;
    process.start_time = std::chrono::time_point_cast<std::chrono::microseconds>
        (std::chrono::system_clock::now())
        - std::chrono::seconds(12);
    process.io_read_bytes_per_sec = 2048;
    process.io_write_bytes_per_sec = 3072;
    process.args = {"worker", "--test"};
    const std::vector processes{process};

    const auto compact = render
    (   detail::prc_content(processes, 80, 1024ull * 1024 * 1024), 80
    );
    REQUIRE(compact.find("123") != std::string::npos);
    REQUIRE(compact.find("alice") != std::string::npos);
    REQUIRE(compact.find("125.5") != std::string::npos);
    REQUIRE(compact.find("3.1") != std::string::npos);
    REQUIRE(compact.find("ELAPSED") != std::string::npos);
    REQUIRE(compact.find("00:00:12") != std::string::npos);
    REQUIRE(compact.find("worker --test") != std::string::npos);
    REQUIRE(compact.find("IO/R") == std::string::npos);

    const auto wide = render
    (   detail::prc_content(processes, 160, 1024ull * 1024 * 1024), 160
    );
    REQUIRE(wide.find("Running") != std::string::npos);
    REQUIRE(wide.find("IO/R") != std::string::npos);
    REQUIRE(wide.find("IO/W") != std::string::npos);
    REQUIRE(wide.find("2.00 K/s") != std::string::npos);
    REQUIRE(wide.find("3.00 K/s") != std::string::npos);

    const auto narrow = render(detail::prc_content(processes, 35), 35);
    REQUIRE(narrow.find("123") != std::string::npos);
    REQUIRE(narrow.find("125.5") != std::string::npos);
    REQUIRE(narrow.find("worker") != std::string::npos);
    REQUIRE(narrow.find("MEM%") == std::string::npos);
}

TEST_CASE("Process table handles empty snapshots and scrolling")
{   REQUIRE(render(detail::prc_content({}, 80), 80).find("No processes available")
            != std::string::npos);
    std::vector<sysproc::process_info> processes(20);
    for (std::size_t i = 0; i < processes.size(); ++i)
    {   processes[i].pid = static_cast<uint32_t>(i + 1);
        processes[i].name = "worker" + std::to_string(i + 1);
    }
    scroll_state scroll;
    auto draw = [&]
    {   return render(detail::prc_content(processes, 80) | scroll_view(scroll, true, 3), 80, 5);
    };
    REQUIRE(draw().find("worker1") != std::string::npos);
    REQUIRE(scroll.max_offset > 0);
    REQUIRE(scroll.view_height == 2);
    scroll.offset = scroll.max_offset;
    const auto bottom = draw();
    REQUIRE(bottom.find("PID") != std::string::npos);
    REQUIRE(bottom.find("COMMAND") != std::string::npos);
    REQUIRE(bottom.find("worker20") != std::string::npos);
    REQUIRE(bottom.find("worker1 ") == std::string::npos);
}

TEST_CASE("Process table keeps its header aligned while scrolling")
{   std::vector<sysproc::process_info> processes(20);
    for (std::size_t i = 0; i < processes.size(); ++i)
    {   processes[i].pid = i == processes.size() - 1 ? 123456 : 1;
        processes[i].cpu_percent = i == processes.size() - 1 ? 1234.5 : 1.0;
        processes[i].name = "worker" + std::to_string(i + 1);
    }
    for (const int width : {35, 80, 110, 160})
    {   INFO("width: " << width);
        scroll_state scroll;
        const auto draw = [&]
        {   auto screen = ftxui::Screen::Create
            (   ftxui::Dimension::Fixed(width), ftxui::Dimension::Fixed(7)
            );
            ftxui::Render(screen, detail::prc_content(processes, width) | scroll_view(scroll, true, 3));
            return screen;
        };
        const auto line = [&](ftxui::Screen& screen, int y)
        {   std::string result;
            for (int x = 0; x < width; ++x)
            {   const auto& character = screen.PixelAt(x, y).character;
                result += character.empty() ? " " : character;
            }
            return result;
        };
        auto top = draw();
        REQUIRE(scroll.max_offset > 0);
        const auto header = line(top, 1);
        scroll.offset = scroll.max_offset;
        auto bottom = draw();
        REQUIRE(line(bottom, 1) == header);
        REQUIRE(line(bottom, 0) == line(top, 0));
        REQUIRE(line(bottom, 2) == line(top, 2));
        const auto row = line(bottom, 5);
        INFO(header << "\n" << row);
        for (const auto& [label, value] : std::vector<std::pair<std::string, std::string>>
             {{"PID", "123456"}, {"CPU%", "1234.5"}})
        {   const auto position = header.find(label);
            REQUIRE(position != std::string::npos);
            const auto end = position + label.size();
            REQUIRE(end >= value.size());
            REQUIRE(row.substr(end - value.size(), value.size()) == value);
        }
        REQUIRE(row.find("worker20") != std::string::npos);
    }
}

TEST_CASE("Process command falls back to name and removes control characters")
{   sysproc::process_info process;
    process.name = "task\nname";
    REQUIRE(detail::process_command(process) == "task name");
    process.args = {"task", "arg\tvalue"};
    REQUIRE(detail::process_command(process) == "task arg value");
}

TEST_CASE("Process table keeps large numeric values readable")
{   sysproc::process_info process;
    process.pid = 4294967295u;
    process.thread_count = 4294967295u;
    process.cpu_percent = 123456789.0;
    process.memory_bytes = UINT64_MAX;
    process.name = "worker";
    const auto output = render
    (   detail::prc_content({process}, 100, 1024ull * 1024 * 1024), 100
    );
    REQUIRE(output.find("4294967295") != std::string::npos);
    REQUIRE(output.find("1.2e+08") != std::string::npos);
    REQUIRE(output.find("1.7e+12") != std::string::npos);
    REQUIRE(output.find("worker") != std::string::npos);
}

TEST_CASE("Process table has an outer border and a header separator")
{   sysproc::process_info process;
    process.pid = 123;
    process.name = "worker";
    const auto output = render(detail::prc_content({process}, 80), 80);
    INFO(output);
    const auto top = output.find("┌");
    const auto header = output.find("PID");
    const auto separator = output.find("├", header);
    const auto row = output.find("worker", separator);
    const auto bottom = output.find("└", row);
    REQUIRE(top != std::string::npos);
    REQUIRE(header != std::string::npos);
    REQUIRE(separator != std::string::npos);
    REQUIRE(row != std::string::npos);
    REQUIRE(bottom != std::string::npos);
    REQUIRE(top < header);
    REQUIRE(output.find("┐", top) < header);
    REQUIRE(output.find("┤", separator) < row);
    REQUIRE(output.find("┘", bottom) != std::string::npos);
}

TEST_CASE("Process table has spaces instead of vertical separators")
{   sysproc::process_info process;
    process.pid = 123;
    process.username = "alice";
    process.name = "worker";
    auto screen = ftxui::Screen::Create
    (   ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(5)
    );
    ftxui::Render(screen, detail::prc_content({process}, 80));
    for (int x = 1; x < 79; ++x)
    {   REQUIRE(screen.PixelAt(x, 0).character == "─");
        REQUIRE(screen.PixelAt(x, 2).character == "─");
        REQUIRE(screen.PixelAt(x, 4).character == "─");
        REQUIRE(screen.PixelAt(x, 1).character != "│");
        REQUIRE(screen.PixelAt(x, 3).character != "│");
    }
    REQUIRE(screen.PixelAt(0, 3).character == "│");
    REQUIRE(screen.PixelAt(79, 3).character == "│");
    std::string row;
    for (int x = 0; x < screen.dimx(); ++x)
    {   const auto& character = screen.PixelAt(x, 3).character;
        row += character.empty() ? " " : character;
    }
    REQUIRE(row.find("123  alice") != std::string::npos);
}

TEST_CASE("Process selection highlights the entire row")
{   std::vector<sysproc::process_info> processes(2);
    processes[0].pid = 1;
    processes[0].name = "first";
    processes[1].pid = 2;
    processes[1].name = "second";
    processes[1].cpu_percent = 50;
    detail::prc_sort_state sort;
    scroll_state scroll;
    const auto draw = [&]
    {   auto screen = ftxui::Screen::Create
        (   ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(6)
        );
        ftxui::Render
        (   screen, detail::prc_content(processes, 80, 0, &sort)
                    | scroll_view(scroll, true, 3)
        );
        return screen;
    };
    auto screen = draw();
    ftxui::Mouse mouse;
    mouse.button = ftxui::Mouse::Left;
    mouse.motion = ftxui::Mouse::Pressed;
    mouse.x = 10;
    mouse.y = scroll.view.y_min + 4;
    REQUIRE(detail::prc_row_click(ftxui::Event::Mouse("", mouse), scroll, processes, sort));
    REQUIRE(sort.selected_pid == 2);
    screen = draw();
    const auto highlight = ftxui::Color::RGB(73, 77, 103);
    for (int x = 1; x < 79; ++x)
    {   INFO("column " << x);
        REQUIRE(screen.PixelAt(x, 4).background_color == highlight);
        REQUIRE(screen.PixelAt(x, 3).background_color != highlight);
    }
    bool cpu_color_seen = false;
    for (int x = 1; x < 79; ++x)
        if (screen.PixelAt(x, 4).foreground_color == detail::load_color(0.5))
            cpu_color_seen = true;
    REQUIRE(cpu_color_seen);
    REQUIRE(screen.PixelAt(0, 4).background_color != highlight);
    REQUIRE(screen.PixelAt(79, 4).background_color != highlight);
}

TEST_CASE("Only selected process values and CPU values are undimmed")
{   std::vector<sysproc::process_info> processes(2);
    for (std::size_t i = 0; i < processes.size(); ++i)
    {   auto& process = processes[i];
        process.pid = static_cast<uint32_t>(i + 1);
        process.username = "user";
        process.state = sysproc::process_state::running;
        process.thread_count = 4;
        process.cpu_percent = 50;
        process.memory_bytes = 100;
        process.io_read_bytes_per_sec = 1024;
        process.io_write_bytes_per_sec = 2048;
        process.name = i == 0 ? "first" : "second";
    }
    detail::prc_sort_state sort;
    scroll_state scroll;
    const auto draw = [&]
    {   auto screen = ftxui::Screen::Create
        (   ftxui::Dimension::Fixed(160), ftxui::Dimension::Fixed(6)
        );
        ftxui::Render
        (   screen, detail::prc_content(processes, 160, 1000, &sort)
                    | scroll_view(scroll, true, 3) | ftxui::color(detail::text_color)
        );
        return screen;
    };
    const auto check_row = [](ftxui::Screen& screen, int y, bool selected)
    {   int cpu_chars = 0;
        int other_chars = 0;
        for (int x = 1; x < 159; ++x)
        {   const auto& cell = screen.PixelAt(x, y);
            if (cell.character.empty() || cell.character == " ")
                continue;
            INFO("row " << y << ", column " << x << ": " << cell.character);
            if (cell.foreground_color == detail::load_color(0.5))
            {   REQUIRE_FALSE(cell.dim);
                ++cpu_chars;
            }
            else
            {   REQUIRE(cell.dim == !selected);
                ++other_chars;
            }
        }
        REQUIRE(cpu_chars > 0);
        REQUIRE(other_chars > 0);
    };
    auto screen = draw();
    check_row(screen, 3, true);
    check_row(screen, 4, false);

    ftxui::Mouse mouse;
    mouse.button = ftxui::Mouse::Left;
    mouse.motion = ftxui::Mouse::Pressed;
    mouse.x = 10;
    mouse.y = scroll.view.y_min + 4;
    REQUIRE(detail::prc_row_click(ftxui::Event::Mouse("", mouse), scroll, processes, sort));
    screen = draw();
    check_row(screen, 3, false);
    check_row(screen, 4, true);
}

TEST_CASE("Process table adds left padding to every column")
{   sysproc::process_info process;
    process.pid = 123;
    process.username = "user";
    process.state = sysproc::process_state::running;
    process.thread_count = 123;
    process.cpu_percent = 12.3;
    process.memory_bytes = 12 * 1024 * 1024;
    process.cpu_time_sec = 12.3;
    process.io_read_bytes_per_sec = 12.3 * 1024;
    process.io_write_bytes_per_sec = 12.3 * 1024;
    process.name = "worker";
    constexpr int width = 160;
    constexpr int height = 12;
    auto screen = ftxui::Screen::Create
    (   ftxui::Dimension::Fixed(width), ftxui::Dimension::Fixed(height)
    );
    ftxui::Render(screen, detail::prc_content({process}, width));
    const auto line = [&](int y)
    {   std::string result;
        for (int x = 0; x < width; ++x)
        {   const auto& character = screen.PixelAt(x, y).character;
            result += character.empty() ? " " : character;
        }
        return result;
    };
    const auto header = line(1);
    std::string output;
    for (int y = 0; y < height; ++y)
        output += line(y) + '\n';
    INFO(header << "\n" << output);
    REQUIRE(header.find("IO/R") != std::string::npos);
    REQUIRE(header.find("IO/W") != std::string::npos);
    REQUIRE(output.find("COMMAND") != std::string::npos);
    REQUIRE(output.find("123") != std::string::npos);
    REQUIRE(output.find("worker") != std::string::npos);
}

TEST_CASE("Process table lines and unselected process contents are dim")
{   sysproc::process_info process;
    process.pid = 123;
    process.name = "worker";
    auto screen = ftxui::Screen::Create
    (   ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(10)
    );
    ftxui::Render(screen, detail::prc_content({process}, 80));
    const std::vector<std::string> lines
    {   "┌", "┐", "└", "┘", "│", "─", "┬", "┴", "├", "┤", "┼"
    };
    int line_count = 0;
    bool header_seen = false;
    bool process_seen = false;
    for (int y = 0; y < screen.dimy(); ++y)
        for (int x = 0; x < screen.dimx(); ++x)
        {   const auto& cell = screen.PixelAt(x, y);
            if (std::find(lines.begin(), lines.end(), cell.character) != lines.end())
            {   INFO("line at " << x << "," << y << ": " << cell.character);
                REQUIRE(cell.dim);
                ++line_count;
            }
            if (cell.character == "P")
            {   REQUIRE_FALSE(cell.dim);
                REQUIRE(cell.bold);
                header_seen = true;
            }
            if (cell.character == "w")
            {   REQUIRE(cell.dim);
                process_seen = true;
            }
        }
    REQUIRE(line_count > 0);
    REQUIRE(header_seen);
    REQUIRE(process_seen);
}

TEST_CASE("Process CPU values use load colors")
{   std::vector<sysproc::process_info> processes(4);
    const std::vector<double> loads{0.0, 50.0, 100.0, 250.0};
    for (std::size_t i = 0; i < processes.size(); ++i)
    {   processes[i].pid = static_cast<uint32_t>(i + 1);
        processes[i].cpu_percent = loads[i];
        processes[i].name = "worker";
    }
    auto screen = ftxui::Screen::Create
    (   ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(8)
    );
    ftxui::Render(screen, detail::prc_content(processes, 80) | ftxui::color(detail::text_color));
    int cpu_end = -1;
    for (int x = 0; x < screen.dimx(); ++x)
        if (screen.PixelAt(x, 1).character == "%")
            cpu_end = x;
    REQUIRE(cpu_end >= 0);
    REQUIRE(screen.PixelAt(cpu_end, 1).foreground_color == detail::text_color);
    for (std::size_t i = 0; i < loads.size(); ++i)
    {   const auto value = std::format("{:.1f}", loads[i]);
        const int row = static_cast<int>(i) + 3;
        int start = -1;
        for (int x = 0; x + static_cast<int>(value.size()) <= screen.dimx(); ++x)
        {   bool matches = true;
            for (std::size_t j = 0; j < value.size(); ++j)
            {   const auto& cell = screen.PixelAt(x + static_cast<int>(j), row);
                if (cell.character != std::string(1, value[j]))
                {   matches = false;
                    break;
                }
            }
            if (matches)
            {   start = x;
                break;
            }
        }
        REQUIRE(start >= 0);
        for (std::size_t j = 0; j < value.size(); ++j)
        {   const auto& cell = screen.PixelAt(start + static_cast<int>(j), row);
            REQUIRE(cell.character == std::string(1, value[j]));
            REQUIRE(cell.foreground_color == detail::load_color(loads[i] / 100.0));
        }
        REQUIRE(screen.PixelAt(1, row).foreground_color == detail::text_color);
    }
}

TEST_CASE("Process table fills the width by expanding the command column")
{   sysproc::process_info process;
    process.pid = 123;
    process.name = std::string(200, 'x');
    const auto draw = [&](int width)
    {   auto screen = ftxui::Screen::Create
        (   ftxui::Dimension::Fixed(width), ftxui::Dimension::Fixed(5)
        );
        ftxui::Render(screen, detail::prc_content({process}, width));
        return screen;
    };
    auto narrow = draw(80);
    auto wider = draw(90);
    REQUIRE(narrow.PixelAt(79, 0).character == "┐");
    REQUIRE(wider.PixelAt(89, 0).character == "┐");
    for (int x = 0; x < 78; ++x)
        REQUIRE(narrow.PixelAt(x, 1).character == wider.PixelAt(x, 1).character);

    auto wide = draw(160);
    REQUIRE(wide.PixelAt(159, 0).character == "┐");
    int command_characters = 0;
    for (int x = 0; x < 160; ++x)
        if (wide.PixelAt(x, 3).character == "x")
            ++command_characters;
    REQUIRE(command_characters > 60);
}

TEST_CASE("Process table right-aligns numeric columns at each layout width")
{   sysproc::process_info first;
    first.pid = 1;
    first.username = "alice";
    first.state = sysproc::process_state::running;
    first.thread_count = 2;
    first.cpu_percent = 1.0;
    first.memory_bytes = 1024 * 1024;
    first.cpu_time_sec = 3.0;
    first.start_time = std::chrono::time_point_cast<std::chrono::microseconds>
        (std::chrono::system_clock::now()) - std::chrono::seconds(3);
    first.io_read_bytes_per_sec = 1024;
    first.io_write_bytes_per_sec = 2048;
    first.name = "worker";
    auto second = first;
    second.pid = 123456;
    second.thread_count = 12345;
    second.cpu_percent = 1234.5;
    second.memory_bytes = 123456ull * 1024 * 1024;
    second.cpu_time_sec = 3603.45;
    second.start_time = std::chrono::time_point_cast<std::chrono::microseconds>
        (std::chrono::system_clock::now()) - std::chrono::seconds(3603);
    second.io_read_bytes_per_sec = 123456 * 1024;
    second.io_write_bytes_per_sec = 123456 * 1024;

    for (const int width : {35, 80, 110, 160})
    {   INFO("width: " << width);
        auto screen = ftxui::Screen::Create
        (   ftxui::Dimension::Fixed(width), ftxui::Dimension::Fixed(6)
        );
        ftxui::Render
        (   screen
        ,   detail::prc_content({first, second}, width, 1000ull * 1024 * 1024)
        );
        const auto line = [&](int y)
        {   std::string result;
            for (int x = 0; x < width; ++x)
            {   const auto& character = screen.PixelAt(x, y).character;
                result += character.empty() ? " " : character;
            }
            return result;
        };
        const auto header = line(1);
        const auto short_row = line(3);
        const auto long_row = line(4);
        INFO(header << "\n" << short_row << "\n" << long_row);
        const auto check = [&](std::string_view label, std::string_view short_value,
                               std::string_view long_value)
        {   const auto h = header.find(label);
            REQUIRE(h != std::string::npos);
            const auto end = h + label.size();
            REQUIRE(end >= short_value.size());
            REQUIRE(end >= long_value.size());
            REQUIRE(short_row.substr(end - short_value.size(), short_value.size()) == short_value);
            REQUIRE(long_row.substr(end - long_value.size(), long_value.size()) == long_value);
        };
        check("PID", "1", "123456");
        check("CPU%", "1.0", "1234.5");
        if (width >= 60)
        {   check("MEM%", "0.1", "12345.6");
            check("ELAPSED", "00:00:03", "01:00:03");
        }
        if (width >= 100)
            check("THR", "2", "12345");
        if (width >= 140)
        {   check("IO/R", "1.00 K/s", "120.56 M/s");
            check("IO/W", "2.00 K/s", "120.56 M/s");
        }
    }
}

TEST_CASE("Process table border remains visible beside the scrollbar")
{   sysproc::process_info process;
    process.pid = 4294967295u;
    process.username = "longusername";
    process.cpu_percent = 123456789.0;
    process.memory_bytes = UINT64_MAX;
    process.cpu_time_sec = 123456789.0;
    process.name = std::string(100, 'x');
    std::vector processes(10, process);
    scroll_state scroll;
    auto draw = [&]
    {   auto screen = ftxui::Screen::Create
        (   ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(5)
        );
        ftxui::Render(screen, detail::prc_content(processes, 80) | scroll_view(scroll, true, 3));
        return screen;
    };
    auto top = draw();
    REQUIRE(top.PixelAt(78, 0).character == "┐");
    REQUIRE(scroll.max_offset > 0);
    scroll.offset = scroll.max_offset;
    auto bottom = draw();
    REQUIRE(bottom.PixelAt(78, 4).character == "┘");

    processes.resize(1);
    auto unscrolled = draw();
    REQUIRE(scroll.max_offset == 0);
    REQUIRE(unscrolled.PixelAt(79, 0).character == "┐");
}

TEST_CASE("Process table sorts each visible column when clicked")
{   sysproc::process_info first;
    first.pid = 2;
    first.username = "zebra";
    first.state = sysproc::process_state::zombie;
    first.thread_count = 20;
    first.cpu_percent = 20;
    first.memory_bytes = 200;
    first.cpu_time_sec = 20;
    first.io_read_bytes_per_sec = 200;
    first.io_write_bytes_per_sec = 200;
    first.start_time = std::chrono::time_point_cast<std::chrono::microseconds>
        (std::chrono::system_clock::now()) - std::chrono::seconds(20);
    first.args = {"z-command"};
    auto second = first;
    second.pid = 10;
    second.username = "alice";
    second.state = sysproc::process_state::running;
    second.thread_count = 3;
    second.cpu_percent = 3;
    second.memory_bytes = 30;
    second.cpu_time_sec = 3;
    second.io_read_bytes_per_sec = 30;
    second.io_write_bytes_per_sec = 30;
    second.start_time = std::chrono::time_point_cast<std::chrono::microseconds>
        (std::chrono::system_clock::now()) - std::chrono::seconds(3);
    second.args = {"a-command"};

    for (const auto& [label, high_command] :
         std::vector<std::pair<std::string, std::string>>
         {{"PID", "a-command"}, {"USER", "z-command"}, {"STATE", "z-command"},
          {"THR", "z-command"}, {"CPU%", "z-command"}, {"MEM%", "z-command"},
          {"ELAPSED", "a-command"}, {"IO/R", "z-command"},
          {"IO/W", "z-command"}, {"COMMAND", "z-command"}})
    {   INFO(label);
        detail::prc_sort_state sort;
        scroll_state scroll;
        std::vector processes{first, second};
        const auto draw = [&]
        {   auto screen = ftxui::Screen::Create
            (   ftxui::Dimension::Fixed(160), ftxui::Dimension::Fixed(6)
            );
            ftxui::Render(screen, detail::prc_content(processes, 160, 1000, &sort)
                          | scroll_view(scroll, true, 3));
            return screen;
        };
        auto screen = draw();
        int x = -1;
        for (int i = 0; i + static_cast<int>(label.size()) <= 160; ++i)
        {   std::string text;
            for (std::size_t j = 0; j < label.size(); ++j)
                text += screen.PixelAt(i + static_cast<int>(j), 1).character;
            if (text == label)
            {   x = i;
                break;
            }
        }
        REQUIRE(x >= 0);
        ftxui::Mouse mouse;
        mouse.button = ftxui::Mouse::Left;
        mouse.motion = ftxui::Mouse::Pressed;
        mouse.x = x;
        mouse.y = scroll.view.y_min + 1;
        const auto click = [&]
        {   return detail::prc_sort_click(ftxui::Event::Mouse("", mouse), scroll, sort); };
        REQUIRE(click());
        auto output = draw().ToString();
        INFO("x=" << x << " selected=" << static_cast<int>(*sort.column) << "\n" << output);
        REQUIRE(output.find("▼ " + label) != std::string::npos);
        const auto low_command = high_command == "z-command" ? "a-command" : "z-command";
        REQUIRE(output.find(high_command) != std::string::npos);
        REQUIRE(output.find(low_command) != std::string::npos);
        REQUIRE(output.find(high_command) < output.find(low_command));
        REQUIRE(click());
        output = draw().ToString();
        REQUIRE(output.find("▲ " + label) != std::string::npos);
        REQUIRE(output.find(low_command) < output.find(high_command));
    }
}

TEST_CASE("Process sorting tracks headers through scrolling and snapshot updates")
{   std::vector<sysproc::process_info> processes(20);
    for (std::size_t i = 0; i < processes.size(); ++i)
    {   processes[i].pid = static_cast<uint32_t>(i + 1);
        processes[i].name = "worker" + std::to_string(i + 1);
    }
    detail::prc_sort_state sort;
    scroll_state scroll;
    const auto draw = [&](int width)
    {   return render(detail::prc_content(processes, width, 1000, &sort)
                     | scroll_view(scroll, true, 3), width, 6);
    };
    draw(80);
    scroll.offset = scroll.max_offset;
    draw(80);
    const auto click = [&](detail::prc_column column, ftxui::Mouse::Button button,
                           ftxui::Mouse::Motion motion, int y)
    {   const auto& box = sort.headers[static_cast<std::size_t>(column)];
        ftxui::Mouse mouse;
        mouse.button = button;
        mouse.motion = motion;
        mouse.x = box.x_min;
        mouse.y = y;
        return detail::prc_sort_click(ftxui::Event::Mouse("", mouse), scroll, sort);
    };
    const int header_y = scroll.view.y_min + 1;
    REQUIRE_FALSE(click(detail::prc_column::pid, ftxui::Mouse::Left, ftxui::Mouse::Released, header_y));
    REQUIRE_FALSE(click(detail::prc_column::pid, ftxui::Mouse::Right, ftxui::Mouse::Pressed, header_y));
    REQUIRE_FALSE(click(detail::prc_column::pid, ftxui::Mouse::Left, ftxui::Mouse::Pressed, header_y + 2));
    REQUIRE(click(detail::prc_column::pid, ftxui::Mouse::Left, ftxui::Mouse::Pressed, header_y));
    auto output = draw(80);
    REQUIRE(output.find("▼ PID") != std::string::npos);
    REQUIRE(click(detail::prc_column::cpu, ftxui::Mouse::Left, ftxui::Mouse::Pressed, header_y));
    output = draw(80);
    REQUIRE(output.find("▼ CPU%") != std::string::npos);
    REQUIRE(output.find("▼ PID") == std::string::npos);
    processes.back().cpu_percent = 100;
    scroll.offset = 0;
    output = draw(80);
    REQUIRE(output.find("worker20") != std::string::npos);
    output = draw(35);
    REQUIRE(output.find("▼ CPU%") != std::string::npos);
    REQUIRE(std::find(sort.visible.begin(), sort.visible.end(), detail::prc_column::user) == sort.visible.end());
    processes.clear();
    draw(80);
    REQUIRE(sort.visible.empty());
}
