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
    (   hwinfo::Computer{}, hwinfo::Virtualization{}, hwinfo::Mainboard{}, hwinfo::Os{}
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

TEST_CASE("SYS tree aligns dimmed titles and undimmed values")
{   using namespace ftxui;
    std::vector<detail::sys_node> nodes
    {   detail::sys_field("Name", "alpha:beta")
    ,   detail::sys_field("Serial number", "serial")
    ,   {"Group", {detail::sys_field("IPv6", "fe80::1")}}
    ,   detail::sys_field("Virtualization error", "I/O error")
    };
    auto screen = Screen::Create(Dimension::Fixed(80), Dimension::Fixed(6));
    Render(screen, detail::sys_tree(nodes));
    REQUIRE(screen.PixelAt(4, 0).character == "N");
    REQUIRE(screen.PixelAt(4, 0).dim);
    REQUIRE(screen.PixelAt(8, 0).character != ":");
    REQUIRE(screen.PixelAt(25, 0).character == "a");
    REQUIRE_FALSE(screen.PixelAt(25, 0).dim);
    REQUIRE(screen.PixelAt(25, 1).character == "s");
    REQUIRE(screen.PixelAt(8, 3).character == "I");
    REQUIRE(screen.PixelAt(8, 3).dim);
    REQUIRE(screen.PixelAt(29, 3).character == "f");
    REQUIRE_FALSE(screen.PixelAt(29, 3).dim);
    REQUIRE(screen.PixelAt(23, 4).character == "r");
    REQUIRE(screen.PixelAt(25, 4).character == "I");
    REQUIRE(screen.ToString().find("alpha:beta") != std::string::npos);
    REQUIRE(screen.ToString().find("fe80::1") != std::string::npos);
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
    (   computer, hwinfo::Virtualization{}, hwinfo::Mainboard{}, os
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
    hwinfo::Computer computer;
    computer.boot_time = std::chrono::sys_seconds{std::chrono::seconds{0}};
    const auto nodes = detail::sys_computer_nodes(computer, virtualization);
    REQUIRE(nodes.size() == 4);
    REQUIRE(nodes[0].label == "Hypervisor");
    REQUIRE(nodes[1].label == "Vendor ID");
    REQUIRE(nodes[2].label == "Boot time (UTC)");
    REQUIRE(nodes[3].label == "Container");
    detail::sys_state state;
    auto system = detail::sys_content
    (   computer, virtualization, hwinfo::Mainboard{}, hwinfo::Os{}
    ,   std::vector<hwinfo::Disk>{}, std::vector<hwinfo::NetworkInterface>{}, state
    );
    const auto text = render(system);
    REQUIRE(text.find("Virtual Machine") != std::string::npos);
    REQUIRE(text.find("Hypervisor") != std::string::npos);
    REQUIRE(text.find("Virtual machine") == std::string::npos);
    REQUIRE(text.find("KVM") != std::string::npos);
    REQUIRE(text.find("VM vendor") != std::string::npos);
    REQUIRE(text.find("Docker") != std::string::npos);
    REQUIRE(text.find("Kubernetes") != std::string::npos);
    REQUIRE(text.find("Yes") != std::string::npos);
}

TEST_CASE("SYS mainboard section is available on physical systems")
{   using namespace ftxui;
    hwinfo::Computer computer;
    computer.boot_time = std::chrono::sys_seconds{std::chrono::seconds{0}};
    hwinfo::Mainboard mainboard
    {   .vendor = "Board vendor"
    ,   .name = "Board name"
    ,   .version = "Board version"
    ,   .serial_number = "Board serial"
    ,   .firmware_vendor = "Firmware maker"
    ,   .firmware_version = "Firmware release"
    ,   .firmware_date = std::chrono::year{2024}/3/14
    };
    detail::sys_state state;
    scroll_state scroll;
    auto system = detail::sys_content
    (   computer, hwinfo::Virtualization{}, mainboard, hwinfo::Os{}
    ,   std::vector<hwinfo::Disk>{}, std::vector<hwinfo::NetworkInterface>{}, state
    );
    std::vector<std::string> entries{"SYS"};
    int selected = 0;
    auto menu = Menu(&entries, &selected);
    auto root = Container::Vertical({menu, system});
    auto send = [&](Event event)
    {   return detail::sys_navigation(event, system, menu, state, scroll)
            || root->OnEvent(event);
    };
    auto text = render(system);
    REQUIRE(text.find("Boot time (UTC)") < text.find("Mainboard"));
    REQUIRE(text.find("Mainboard") < text.find("Operating System"));
    REQUIRE(text.find("Board vendor") == std::string::npos);
    REQUIRE(send(Event::Tab));
    REQUIRE(send(Event::Tab));
    REQUIRE(send(Event::Return));
    text = render(system);
    REQUIRE(text.find("Board vendor") != std::string::npos);
    REQUIRE(text.find("Board name") != std::string::npos);
    REQUIRE(text.find("Board version") != std::string::npos);
    REQUIRE(text.find("Board serial") != std::string::npos);
    REQUIRE(text.find("Firmware maker") != std::string::npos);
    REQUIRE(text.find("Firmware release") != std::string::npos);
    REQUIRE(text.find("2024-03-14") != std::string::npos);
    REQUIRE(send(Event::Tab));
    REQUIRE(system->ChildAt(0)->ChildAt(2)->Focused());
    REQUIRE(send(Event::TabReverse));
    REQUIRE(send(Event::Return));
    REQUIRE(render(system).find("Board vendor") == std::string::npos);
    const auto unavailable = detail::sys_mainboard_nodes(hwinfo::Mainboard{});
    REQUIRE(unavailable.back().label == "Firmware date");
    REQUIRE(unavailable.back().value == "Unavailable");
}

TEST_CASE("SYS mainboard errors are shown only on physical systems")
{   auto failure = std::unexpected(hwinfo::error(hwinfo::errc::permission_denied, "mainboard"));
    detail::sys_state state;
    state.expanded[1] = true;
    auto draw = [&](hwinfo::result<hwinfo::Virtualization> const& virtualization)
    {   return render(detail::sys_content
        (   hwinfo::Computer{}, virtualization, failure, hwinfo::Os{}
        ,   std::vector<hwinfo::Disk>{}, std::vector<hwinfo::NetworkInterface>{}, state
        ));
    };
    const auto physical = draw(hwinfo::Virtualization{});
    REQUIRE(physical.find("Mainboard") != std::string::npos);
    REQUIRE(physical.find("mainboard: permission denied") != std::string::npos);
    for (const auto& virtualization :
        {   hwinfo::Virtualization{.vm = hwinfo::VirtualMachine{}}
        ,   hwinfo::Virtualization{.container = hwinfo::Container{}}
        })
    {   const auto text = draw(virtualization);
        REQUIRE(text.find("Mainboard") == std::string::npos);
        REQUIRE(text.find("mainboard: permission denied") == std::string::npos);
    }
    const auto unknown = draw
    (   std::unexpected(hwinfo::error(hwinfo::errc::io_error, "virtualization"))
    );
    REQUIRE(unknown.find("Mainboard") == std::string::npos);
}

TEST_CASE("SYS shows computer details only on non-virtualized systems")
{   hwinfo::Computer computer;
    computer.vendor = "Hardware vendor";
    computer.model = "Hardware model";
    computer.family = "Hardware family";
    computer.version = "Hardware version";
    computer.sku = "Hardware SKU";
    computer.serial_number = "Hardware serial";
    computer.chassis = hwinfo::ChassisType::desktop;
    computer.chassis_code = 3;
    computer.boot_time = std::chrono::sys_seconds{std::chrono::seconds{0}};
    detail::sys_state state;
    auto draw = [&](hwinfo::result<hwinfo::Virtualization> const& virtualization)
    {   return render(detail::sys_content
        (   computer, virtualization, hwinfo::Mainboard{}, hwinfo::Os{}
        ,   std::vector<hwinfo::Disk>{}, std::vector<hwinfo::NetworkInterface>{}, state
        ));
    };

    const auto bare = draw(hwinfo::Virtualization{});
    REQUIRE(bare.find("Hardware vendor") != std::string::npos);
    REQUIRE(bare.find("Hardware model") != std::string::npos);
    REQUIRE(bare.find("Hardware family") != std::string::npos);
    REQUIRE(bare.find("Hardware version") != std::string::npos);
    REQUIRE(bare.find("Hardware SKU") != std::string::npos);
    REQUIRE(bare.find("Hardware serial") != std::string::npos);
    REQUIRE(bare.find("Chassis") != std::string::npos);
    REQUIRE(bare.find("desktop") != std::string::npos);
    REQUIRE(bare.find("Chassis code") != std::string::npos);
    REQUIRE(bare.find("Boot time (UTC)") != std::string::npos);
    REQUIRE(bare.find("1970-01-01 00:00:00") != std::string::npos);

    for (const auto& virtualization :
        {   hwinfo::Virtualization{.vm = hwinfo::VirtualMachine{}}
        ,   hwinfo::Virtualization{.container = hwinfo::Container{}}
        })
    {   const auto text = draw(virtualization);
        REQUIRE(text.find("Hardware vendor") == std::string::npos);
        REQUIRE(text.find("Hardware model") == std::string::npos);
        REQUIRE(text.find("Hardware family") == std::string::npos);
        REQUIRE(text.find("Hardware version") == std::string::npos);
        REQUIRE(text.find("Hardware SKU") == std::string::npos);
        REQUIRE(text.find("Hardware serial") == std::string::npos);
        REQUIRE(text.find("UUID") == std::string::npos);
        REQUIRE(text.find("Chassis") == std::string::npos);
        REQUIRE(text.find("Boot time (UTC)") != std::string::npos);
        REQUIRE(text.find("1970-01-01 00:00:00") != std::string::npos);
    }

    const auto unknown = draw
    (   std::unexpected(hwinfo::error(hwinfo::errc::io_error, "virtualization"))
    );
    REQUIRE(unknown.find("Virtualization error") != std::string::npos);
    REQUIRE(unknown.find("virtualization: I/O error") != std::string::npos);
    REQUIRE(unknown.find("Hardware vendor") == std::string::npos);
    REQUIRE(unknown.find("Boot time") == std::string::npos);

    computer.boot_time.reset();
    REQUIRE(draw(hwinfo::Virtualization{}).find("Boot time") == std::string::npos);
    REQUIRE(draw(hwinfo::Virtualization{.vm = hwinfo::VirtualMachine{}}).find("Boot time")
            == std::string::npos);
}

TEST_CASE("SYS navigation scrolls focused sections into view")
{   using namespace ftxui;
    detail::sys_state state;
    scroll_state scroll;
    hwinfo::Virtualization virtualization;
    virtualization.container = hwinfo::Container{.runtime = hwinfo::ContainerRuntime::podman};
    auto system = detail::sys_content
    (   hwinfo::Computer{}, virtualization, hwinfo::Mainboard{}, hwinfo::Os{}
    ,   std::vector<hwinfo::Disk>{}, std::vector<hwinfo::NetworkInterface>{}, state
    );
    REQUIRE(render(system).find("Computer") != std::string::npos);
    REQUIRE(render(system).find("Podman") != std::string::npos);
    auto sys_tab = Renderer(system, [&] { return system->Render() | scroll_view(scroll) | flex; });
    std::vector<std::string> entries{"SYS"};
    int selected = 0;
    auto menu = Menu(&entries, &selected);
    auto tabs = Container::Tab({sys_tab}, &selected);
    auto root = Container::Vertical({menu, tabs});
    auto draw = [&]
    {   auto screen = Screen::Create(Dimension::Fixed(50), Dimension::Fixed(6));
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
    (   failure, failure, failure, failure
    ,   failure, std::vector<hwinfo::NetworkInterface>{}, state
    );
    auto text = render(system);
    REQUIRE(text.find("Computer error") != std::string::npos);
    REQUIRE(text.find("Virtualization error") != std::string::npos);
    REQUIRE(text.find("test query: permission denied") != std::string::npos);
    auto controls = system->ChildAt(0);
    controls->ChildAt(1)->TakeFocus();
    REQUIRE(system->OnEvent(ftxui::Event::Return));
    controls->ChildAt(2)->TakeFocus();
    REQUIRE(system->OnEvent(ftxui::Event::Return));
    controls->ChildAt(3)->TakeFocus();
    REQUIRE(system->OnEvent(ftxui::Event::Return));
    text = render(system);
    REQUIRE(text.find("test query: permission denied") != std::string::npos);
    REQUIRE(text.find("No network interfaces detected") != std::string::npos);
}
