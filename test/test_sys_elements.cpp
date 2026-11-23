// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#include <catch2/catch_test_macros.hpp>

#include "sys_elements.hpp"

namespace {

std::string render(ftxui::Component const& component)
{   auto screen = ftxui::Screen::Create
    (   ftxui::Dimension::Fixed(100), ftxui::Dimension::Fixed(80)
    );
    ftxui::Render(screen, component->Render());
    return screen.ToString();
}

}

TEST_CASE("SYS tree has one column of padding inside its border")
{   using namespace ftxui;
    detail::sys_state state;
    auto system = detail::sys_content
    (   hwinfo::Computer{}, hwinfo::Virtualization{}, hwinfo::Os{}
    ,   std::vector<hwinfo::Disk>{}, std::vector<hwinfo::NetworkInterface>{}, state
    );
    auto screen = Screen::Create(Dimension::Fixed(50), Dimension::Fixed(20));
    Render(screen, system->Render());
    REQUIRE(screen.PixelAt(1, 1).character == " ");
    REQUIRE(screen.PixelAt(2, 1).character == "▼");
    REQUIRE(screen.PixelAt(2, 2).character == "├");
    REQUIRE(screen.PixelAt(2, state.headers[1].y_min).character == "├");
    REQUIRE(screen.PixelAt(6, state.headers[1].y_min).character == "▶");

    state.expanded[0] = false;
    Render(screen, system->Render());
    REQUIRE(screen.PixelAt(2, 1).character == "▶");
}

TEST_CASE("SYS tree expands sections and retains their state")
{   using namespace ftxui;
    hwinfo::Computer computer;
    computer.vendor = "System vendor";
    computer.family = "Family A";
    hwinfo::Os os;
    os.name = "Test OS";
    hwinfo::Disk disk;
    disk.model = "Test disk";
    disk.mount_points.push_back(hwinfo::MountPoint{.path = "/data", .filesystem = "ext4"});
    hwinfo::NetworkInterface network;
    network.name = "test0";
    network.ipv4 = {"192.0.2.1"};
    detail::sys_state state;
    scroll_state scroll;
    auto system = detail::sys_content
    (   computer, hwinfo::Virtualization{}, os
    ,   std::vector{disk}, std::vector{network}, state
    );
    auto text = render(system);
    REQUIRE(text.find("Computer") != std::string::npos);
    REQUIRE(text.find("System vendor") != std::string::npos);
    REQUIRE(text.find("Family A") != std::string::npos);
    REQUIRE(text.find("Operating System") != std::string::npos);
    REQUIRE(text.find("└──") != std::string::npos);
    REQUIRE(text.find("Networks") != std::string::npos);
    REQUIRE(text.find("Test OS") == std::string::npos);

    std::vector<std::string> entries{"SYS"};
    int selected = 0;
    auto menu = Menu(&entries, &selected);
    auto root = Container::Vertical({menu, system});
    auto send = [&](Event event)
    {   return detail::sys_navigation(event, system, menu, state, scroll)
            || root->OnEvent(event);
    };
    REQUIRE(send(Event::Tab));
    REQUIRE(send(Event::Tab));
    REQUIRE(send(Event::Return));
    REQUIRE(send(Event::Tab));
    REQUIRE(send(Event::Return));
    REQUIRE(send(Event::Tab));
    REQUIRE(send(Event::Return));
    text = render(system);
    REQUIRE(text.find("Test OS") != std::string::npos);
    REQUIRE(text.find("Test disk") != std::string::npos);
    REQUIRE(text.find("/data") != std::string::npos);
    REQUIRE(text.find("ext4") != std::string::npos);
    REQUIRE(text.find("192.0.2.1") != std::string::npos);
    REQUIRE(send(Event::Tab));
    REQUIRE(menu->Focused());
    REQUIRE(send(Event::Tab));
    REQUIRE(send(Event::Return));
    REQUIRE(render(system).find("Test OS") == std::string::npos);
    REQUIRE(send(Event::Return));
    REQUIRE(render(system).find("Test OS") != std::string::npos);
}

TEST_CASE("SYS displays VM and container information together")
{   hwinfo::Virtualization virtualization;
    virtualization.vm = hwinfo::VirtualMachine
    {   .hypervisor = hwinfo::Hypervisor::kvm, .vendor_id = "VM vendor"
    };
    virtualization.container = hwinfo::Container
    {   .runtime = hwinfo::ContainerRuntime::docker, .kubernetes = true
    };
    detail::sys_state state;
    auto system = detail::sys_content
    (   hwinfo::Computer{}, virtualization, hwinfo::Os{}
    ,   std::vector<hwinfo::Disk>{}, std::vector<hwinfo::NetworkInterface>{}, state
    );
    const auto text = render(system);
    REQUIRE(text.find("Virtual Machine") != std::string::npos);
    REQUIRE(text.find("Hypervisor: KVM") != std::string::npos);
    REQUIRE(text.find("Vendor ID: VM vendor") != std::string::npos);
    REQUIRE(text.find("Runtime: Docker") != std::string::npos);
    REQUIRE(text.find("Kubernetes: Yes") != std::string::npos);
}

TEST_CASE("SYS navigation scrolls focused sections into view")
{   using namespace ftxui;
    detail::sys_state state;
    scroll_state scroll;
    hwinfo::Virtualization virtualization;
    virtualization.container = hwinfo::Container{.runtime = hwinfo::ContainerRuntime::podman};
    auto system = detail::sys_content
    (   hwinfo::Computer{}, virtualization, hwinfo::Os{}
    ,   std::vector<hwinfo::Disk>{}, std::vector<hwinfo::NetworkInterface>{}, state
    );
    REQUIRE(render(system).find("Computer") != std::string::npos);
    REQUIRE(render(system).find("Runtime: Podman") != std::string::npos);
    auto sys_tab = Renderer(system, [&] { return system->Render() | scroll_view(scroll) | flex; });
    std::vector<std::string> entries{"SYS"};
    int selected = 0;
    auto menu = Menu(&entries, &selected);
    auto tabs = Container::Tab({sys_tab}, &selected);
    auto root = Container::Vertical({menu, tabs});
    auto draw = [&]
    {   auto screen = Screen::Create(Dimension::Fixed(50), Dimension::Fixed(8));
        Render(screen, sys_tab->Render());
        return screen.ToString();
    };
    auto send = [&](Event event)
    {   return detail::sys_navigation(event, system, menu, state, scroll)
            || root->OnEvent(event);
    };
    REQUIRE(draw().find("Computer") != std::string::npos);
    REQUIRE(send(Event::TabReverse));
    REQUIRE(scroll.offset > 0);
    REQUIRE(draw().find("Networks") != std::string::npos);
    REQUIRE(send(Event::Return));
    scroll.offset += 2;
    REQUIRE(draw().find("No network interfaces detected") != std::string::npos);
    REQUIRE(send(Event::Tab));
    REQUIRE(menu->Focused());
    REQUIRE(send(Event::Tab));
    REQUIRE(draw().find("Computer") != std::string::npos);
    REQUIRE(scroll.offset <= 1);
}

TEST_CASE("SYS shows hardware query failures and empty inventories")
{   auto failure = std::unexpected(hwinfo::error(hwinfo::errc::permission_denied, "test query"));
    detail::sys_state state;
    auto system = detail::sys_content
    (   failure, failure, failure
    ,   failure, std::vector<hwinfo::NetworkInterface>{}, state
    );
    auto text = render(system);
    REQUIRE(text.find("Computer error: test query: permission denied") != std::string::npos);
    REQUIRE(text.find("Virtualization error: test query: permission denied") != std::string::npos);
    auto controls = system->ChildAt(0);
    controls->ChildAt(1)->TakeFocus();
    REQUIRE(system->OnEvent(ftxui::Event::Return));
    controls->ChildAt(2)->TakeFocus();
    REQUIRE(system->OnEvent(ftxui::Event::Return));
    controls->ChildAt(3)->TakeFocus();
    REQUIRE(system->OnEvent(ftxui::Event::Return));
    text = render(system);
    REQUIRE(text.find("Error: test query: permission denied") != std::string::npos);
    REQUIRE(text.find("No network interfaces detected") != std::string::npos);
}
