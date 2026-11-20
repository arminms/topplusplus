// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#include <catch2/catch_test_macros.hpp>

#include "ram_elements.hpp"
#include "scroll_view.hpp"

namespace {

std::string render(ftxui::Element element)
{   auto screen = ftxui::Screen::Create
    (   ftxui::Dimension::Fixed(50), ftxui::Dimension::Fixed(35)
    );
    ftxui::Render(screen, std::move(element));
    return screen.ToString();
}

}

TEST_CASE("RAM modules collapse independently and keep their state")
{   hwinfo::Memory memory;
    memory.modules =
    {   hwinfo::MemoryModule{.index = 0, .vendor = "First vendor", .name = "A1"}
    ,   hwinfo::MemoryModule{.index = 1, .vendor = "Second vendor", .name = "B1"}
    };
    auto modules = detail::ram_modules(memory);
    auto draw = [&] { return render(detail::ram_information({{" Total", "16 GiB"}}, modules)); };

    auto initial = draw();
    REQUIRE(initial.find("Module 0 - A1") != std::string::npos);
    REQUIRE(initial.find("Module 1 - B1") != std::string::npos);
    REQUIRE(initial.find("First vendor") == std::string::npos);
    REQUIRE(initial.find("Second vendor") == std::string::npos);

    REQUIRE(modules->OnEvent(ftxui::Event::Return));
    auto expanded = draw();
    REQUIRE(expanded.find("First vendor") != std::string::npos);
    REQUIRE(expanded.find("Second vendor") == std::string::npos);
    REQUIRE(draw().find("First vendor") != std::string::npos);

    REQUIRE(modules->OnEvent(ftxui::Event::Tab));
    REQUIRE(modules->OnEvent(ftxui::Event::Return));
    REQUIRE(draw().find("Second vendor") != std::string::npos);
    REQUIRE(modules->OnEvent(ftxui::Event::TabReverse));
    REQUIRE(modules->OnEvent(ftxui::Event::Return));
    REQUIRE(draw().find("First vendor") == std::string::npos);
    REQUIRE(draw().find("Second vendor") != std::string::npos);
}

TEST_CASE("RAM information separates the module section")
{   hwinfo::Memory memory;
    auto empty = detail::ram_modules(memory);
    auto without_modules = render
    (   detail::ram_information({{" Modules", "Unavailable"}}, empty)
    );
    REQUIRE(without_modules.find("Empty container") == std::string::npos);
    REQUIRE(without_modules.find("Unavailable") < without_modules.find("├"));
    REQUIRE(without_modules.find("├") != std::string::npos);

    memory.modules.push_back(hwinfo::MemoryModule{.index = 0, .vendor = "Vendor"});
    auto modules = detail::ram_modules(memory);
    REQUIRE(modules->OnEvent(ftxui::Event::Return));
    auto with_modules = render(detail::ram_information({{" Total", "16 GiB"}}, modules));
    const auto header = with_modules.find("Module 0 - Details");
    const auto vendor = with_modules.find("Vendor");
    REQUIRE(header != std::string::npos);
    REQUIRE(vendor != std::string::npos);
    REQUIRE(with_modules.find("├") < header);
    const auto line_start = with_modules.rfind('\n', vendor);
    REQUIRE(line_start != std::string::npos);
    const auto left_border = with_modules.find("│", line_start);
    REQUIRE(left_border < vendor);
    REQUIRE(with_modules.find("│", left_border + std::string("│").size()) > vendor);
}

TEST_CASE("RAM module keyboard navigation reaches and leaves the tab menu")
{   using namespace ftxui;
    hwinfo::Memory memory;
    memory.modules =
    {   hwinfo::MemoryModule{.index = 0}
    ,   hwinfo::MemoryModule{.index = 1}
    };
    auto modules = detail::ram_modules(memory);
    std::vector<std::string> entries{"CPU", "RAM"};
    int selected = 1;
    auto menu = Menu(&entries, &selected, MenuOption::HorizontalAnimated());
    auto tabs = Container::Tab({Renderer([] { return text("CPU"); }), modules}, &selected);
    auto root = Container::Vertical({menu, tabs});
    auto send = [&](Event event)
    {   return detail::ram_module_navigation(event, modules, menu)
            || root->OnEvent(event);
    };

    REQUIRE(menu->Focused());
    REQUIRE(send(Event::Tab));
    REQUIRE(modules->ChildAt(0)->Focused());
    REQUIRE(send(Event::Tab));
    REQUIRE(modules->ChildAt(1)->Focused());
    REQUIRE(send(Event::Tab));
    REQUIRE(menu->Focused());
    REQUIRE(send(Event::TabReverse));
    REQUIRE(modules->ChildAt(1)->Focused());
    REQUIRE(send(Event::TabReverse));
    REQUIRE(modules->ChildAt(0)->Focused());
    REQUIRE(send(Event::TabReverse));
    REQUIRE(menu->Focused());
}

TEST_CASE("RAM surface uses paired CPU and memory history")
{   circular_buffer_64<std::optional<double>> cpu;
    circular_buffer_64<hwinfo::result<hwinfo::MemoryUsage>> memory;
    const auto sample = [](std::uint64_t used)
    {   return hwinfo::MemoryUsage
        {   .total = hwinfo::Bytes{100}
        ,   .available = hwinfo::Bytes{100 - used}
        };
    };
    const auto draw = [&](auto const& cpu_history, auto const& memory_history)
    {   return render
        (   detail::ram_surface_plot(0.125, sample(25), cpu_history, memory_history)
        );
    };
    for (int i = 0; i < 60; ++i)
    {   cpu.push(0.0);
        memory.push(sample(25));
    }
    const auto flat = draw(cpu, memory);
    REQUIRE(flat.find("height: CPU load") == std::string::npos);
    REQUIRE(flat.find("RAM %") == std::string::npos);
    REQUIRE(flat.find("RAM and CPU history") == std::string::npos);
    REQUIRE(flat.find("CPU") != std::string::npos);
    REQUIRE(flat.find("RAM") != std::string::npos);
    REQUIRE(flat.find("12.5%") != std::string::npos);
    REQUIRE(flat.find("25.0%") != std::string::npos);

    for (int i = 0; i < 60; ++i)
        cpu.push(1.0);
    const auto peak = draw(cpu, memory);
    REQUIRE(peak != flat);

    for (int i = 0; i < 60; ++i)
        memory.push(sample(75));
    const auto shifted = draw(cpu, memory);
    REQUIRE(shifted != peak);

    circular_buffer_64<std::optional<double>> shorter_cpu;
    circular_buffer_64<hwinfo::result<hwinfo::MemoryUsage>> longer_memory;
    circular_buffer_64<hwinfo::result<hwinfo::MemoryUsage>> shorter_memory;
    longer_memory.push(sample(25));
    for (int i = 0; i < 59; ++i)
    {   shorter_cpu.push(1.0);
        longer_memory.push(sample(75));
        shorter_memory.push(sample(75));
    }
    REQUIRE(draw(shorter_cpu, longer_memory) == draw(shorter_cpu, shorter_memory));

    for (int i = 0; i < 60; ++i)
        cpu.push(std::nullopt);
    REQUIRE(draw(cpu, memory) == flat);
    for (int i = 0; i < 60; ++i)
    {   cpu.push(1.0);
        memory.push(hwinfo::MemoryUsage{});
    }
    REQUIRE(draw(cpu, memory) == flat);
}

TEST_CASE("RAM surface legend reports unavailable current samples")
{   circular_buffer_64<std::optional<double>> cpu;
    circular_buffer_64<hwinfo::result<hwinfo::MemoryUsage>> memory;
    const auto output = render
    (   detail::ram_surface_plot(std::nullopt, hwinfo::MemoryUsage{}, cpu, memory)
    );
    const auto first = output.find("unavailable");
    REQUIRE(first != std::string::npos);
    REQUIRE(output.find("unavailable", first + 1) != std::string::npos);
}

TEST_CASE("Shared mesh supports GPU status history")
{   circular_buffer_64<hwinfo::result<hwinfo::GpuStatus>> history;
    const auto load = [](hwinfo::GpuStatus const& sample)
    {   return sample.utilization;
    };
    const auto memory = [](hwinfo::GpuStatus const&)
    {   return std::optional<double>{0.5};
    };
    for (int i = 0; i < 60; ++i)
        history.push(hwinfo::GpuStatus{.utilization = 0.0});
    const auto flat = render(detail::plot_2d_mesh("GPU", history, load, memory));
    REQUIRE(flat.find("height: GPU load") != std::string::npos);
    for (int i = 0; i < 60; ++i)
        history.push(hwinfo::GpuStatus{.utilization = 1.0});
    REQUIRE(render(detail::plot_2d_mesh("GPU", history, load, memory)) != flat);
}

TEST_CASE("RAM surface labels the memory axis")
{   using namespace ftxui;
    circular_buffer_64<std::optional<double>> cpu;
    circular_buffer_64<hwinfo::result<hwinfo::MemoryUsage>> memory;
    for (int i = 0; i < 60; ++i)
    {   cpu.push(1.0);
        memory.push(hwinfo::MemoryUsage
        {   .total = hwinfo::Bytes{100}, .available = hwinfo::Bytes{50}
        });
    }

    for (const auto [width, height] : {std::pair{50, 35}, std::pair{44, 20}})
    {   auto screen = Screen::Create(Dimension::Fixed(width), Dimension::Fixed(height));
        Render(screen, detail::ram_surface_plot(1.0, memory[0], cpu, memory));
        const auto find = [&](std::string const& text)
            -> std::optional<std::pair<int, int>>
        {   for (int y = 3; y < height - 1; ++y)
                for (int x = 1; x + static_cast<int>(text.size()) < width - 1; ++x)
                {   bool match = true;
                    for (std::size_t i = 0; i < text.size(); ++i)
                        if (screen.PixelAt(x + static_cast<int>(i), y).character
                            != std::string(1, text[i]))
                            match = false;
                    const auto digit = [](std::string const& c)
                    {   return c.size() == 1 && c[0] >= '0' && c[0] <= '9';
                    };
                    if (match
                        && !digit(screen.PixelAt(x - 1, y).character)
                        && !digit(screen.PixelAt(x + static_cast<int>(text.size()), y).character))
                        return std::pair{x, y};
                }
            return std::nullopt;
        };
        const auto hundred = find("RAM 100%");
        const auto fifty = find("RAM 50%");
        const auto zero = find("0");
        const auto time = find("60 seconds");
        REQUIRE(hundred);
        REQUIRE(fifty);
        REQUIRE(zero);
        REQUIRE(time);
        REQUIRE(screen.ToString().find("RAM %") == std::string::npos);
        REQUIRE(hundred->second < fifty->second);
        REQUIRE(fifty->second < zero->second);
        REQUIRE(hundred->first + 8 > fifty->first + 7);
        REQUIRE(fifty->first + 7 > zero->first + 1);
        REQUIRE(time->second == height - 2);
        REQUIRE(zero->second == time->second);
        REQUIRE(time->first > zero->first);
        REQUIRE(screen.PixelAt(time->first - 1, time->second - 1).character != " ");
    }
}

TEST_CASE("RAM surface fills the available tab space")
{   using namespace ftxui;
    monitor mon;
    auto modules = detail::ram_modules(hwinfo::Memory{});
    scroll_state scroll;
    for (const auto [width, height] : {std::pair{80, 25}, std::pair{120, 40}})
    {   auto screen = Screen::Create(Dimension::Fixed(width), Dimension::Fixed(height));
        Render(screen, detail::ram_content(mon, modules) | scroll_view(scroll) | flex);

        REQUIRE(scroll.max_offset == 0);
        REQUIRE(screen.PixelAt(width - 1, height - 1).character == "╯");
        REQUIRE(screen.ToString().find("RAM 100%") != std::string::npos);
        REQUIRE(screen.ToString().find("RAM 50%") != std::string::npos);
        REQUIRE(screen.ToString().find("60 seconds") != std::string::npos);
        REQUIRE(screen.ToString().find("height: CPU load") == std::string::npos);
    }
}
