// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("Process table shows sampled information and adapts to width")
{   sysproc::process_info process;
    process.pid = 123;
    process.username = "alice";
    process.state = sysproc::process_state::running;
    process.thread_count = 4;
    process.cpu_percent = 125.5;
    process.memory_bytes = 32 * 1024 * 1024;
    process.cpu_time_sec = 12.5;
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
    REQUIRE(compact.find("0:12.50") != std::string::npos);
    REQUIRE(compact.find("worker --test") != std::string::npos);
    REQUIRE(compact.find("READ KiB/s") == std::string::npos);

    const auto wide = render
    (   detail::prc_content(processes, 160, 1024ull * 1024 * 1024), 160
    );
    REQUIRE(wide.find("Running") != std::string::npos);
    REQUIRE(wide.find("READ KiB/s") != std::string::npos);
    REQUIRE(wide.find("WRITE KiB/s") != std::string::npos);
    REQUIRE(wide.find("2.0") != std::string::npos);
    REQUIRE(wide.find("3.0") != std::string::npos);

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
    const auto output = screen.ToString();
    REQUIRE(output.find("123  alice") != std::string::npos);
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
    REQUIRE(header.find
    (   "│ PID  USER  STATE    THR  CPU%  MEM%    TIME+  READ KiB/s  WRITE KiB/s"
    ) != std::string::npos);
    REQUIRE(output.find("COMMAND") != std::string::npos);
    REQUIRE(output.find("123") != std::string::npos);
    REQUIRE(output.find("worker") != std::string::npos);
}

TEST_CASE("Process table lines are dim without dimming the contents")
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
            {   REQUIRE_FALSE(cell.dim);
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
        const int start = cpu_end - static_cast<int>(value.size()) + 1;
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
    first.io_read_bytes_per_sec = 1024;
    first.io_write_bytes_per_sec = 2048;
    first.name = "worker";
    auto second = first;
    second.pid = 123456;
    second.thread_count = 12345;
    second.cpu_percent = 1234.5;
    second.memory_bytes = 123456ull * 1024 * 1024;
    second.cpu_time_sec = 3603.45;
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
            check("TIME+", "0:03.00", "60:03.45");
        }
        if (width >= 100)
            check("THR", "2", "12345");
        if (width >= 140)
        {   check("READ KiB/s", "1.0", "123456.0");
            check("WRITE KiB/s", "2.0", "123456.0");
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
