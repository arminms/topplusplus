// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#include <catch2/catch_test_macros.hpp>

#include "ram_elements.hpp"

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
