// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#include <array>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/ftxui.hpp>
#include <hwinfo/hwinfo.h>

#include "commons.hpp"
#include "scroll_view.hpp"

namespace detail {

struct sys_node
{   std::string label;
    std::vector<sys_node> children;
    std::optional<std::string> value;
};

inline sys_node sys_field
(   std::string label, std::string value, std::vector<sys_node> children = {}
)
{   return {std::move(label), std::move(children), std::move(value)};
}

inline std::string sys_value(std::string const& value)
{   return value.empty() ? "Unavailable" : value;
}

inline std::string sys_value(std::optional<std::string> const& value)
{   return value ? sys_value(*value) : "Unavailable";
}

inline void sys_tree_rows
(   ftxui::Elements& rows
,   std::vector<sys_node> const& nodes
,   std::string const& prefix
,   bool has_following = false
)
{   using namespace ftxui;
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {   const bool last = i + 1 == nodes.size() && !has_following;
        auto branch = text(prefix + (last ? "└── " : "├── "));
        if (nodes[i].value)
            rows.push_back
            (   hbox
                (   {   std::move(branch)
                    ,   text(nodes[i].label) | dim | size(WIDTH, EQUAL, 20)
                    ,   text(" " + *nodes[i].value)
                    }
                )
            );
        else
            rows.push_back(hbox({std::move(branch), text(nodes[i].label)}));
        sys_tree_rows(rows, nodes[i].children, prefix + (last ? "    " : "│   "));
    }
}

inline ftxui::Element sys_tree(std::vector<sys_node> const& nodes)
{   ftxui::Elements rows;
    sys_tree_rows(rows, nodes, "", true);
    return ftxui::vbox(std::move(rows));
}

inline std::vector<sys_node> sys_computer_nodes
(   hwinfo::result<hwinfo::Computer> const& computer
,   hwinfo::result<hwinfo::Virtualization> const& virtualization
)
{   std::vector<sys_node> nodes;
    if (computer && virtualization
        && !virtualization->vm && !virtualization->container)
    {   nodes =
        {   sys_field("Vendor", sys_value(computer->vendor))
        ,   sys_field("Model", sys_value(computer->model))
        ,   sys_field("Family", sys_value(computer->family))
        ,   sys_field("Version", sys_value(computer->version))
        ,   sys_field("SKU", sys_value(computer->sku))
        ,   sys_field("Serial number", sys_value(computer->serial_number))
        ,   sys_field("UUID", computer->uuid ? hwinfo::to_string(*computer->uuid) : "Unavailable")
        ,   sys_field("Chassis", std::string(hwinfo::to_string(computer->chassis)))
        };
        if (computer->chassis_code)
            nodes.push_back(sys_field("Chassis code", std::to_string(*computer->chassis_code)));
    }
    else if (!computer)
        nodes.push_back(sys_field("Computer error", computer.error().message()));
    if (virtualization && virtualization->vm)
    {   const auto& vm = *virtualization->vm;
        nodes.push_back(sys_field("Hypervisor", std::string(hwinfo::to_string(vm.hypervisor))));
        nodes.push_back(sys_field("Vendor ID", sys_value(vm.vendor_id)));
    }
    if (computer && virtualization && computer->boot_time)
        nodes.push_back
        (   sys_field("Boot time (UTC)", std::format("{:%F %T}", *computer->boot_time))
        );
    if (!virtualization)
        nodes.push_back(sys_field("Virtualization error", virtualization.error().message()));
    else
    {   if (virtualization->container)
        {   const auto& container = *virtualization->container;
            nodes.push_back
            (   {"Container"
                ,   {   sys_field("Runtime", std::string(hwinfo::to_string(container.runtime)))
                    ,   sys_field("Kubernetes", container.kubernetes ? "Yes" : "No")
                    }
                }
            );
        }
    }
    return nodes;
}

inline std::vector<sys_node> sys_os_nodes(hwinfo::result<hwinfo::Os> const& os)
{   if (!os)
        return {sys_field("Error", os.error().message())};
    return
    {   sys_field("Name", sys_value(os->name))
    ,   sys_field("Family", std::string(hwinfo::to_string(os->family)))
    ,   sys_field("Marketing name", sys_value(os->marketing_name))
    ,   sys_field("Version", sys_value(os->version))
    ,   sys_field("Build", sys_value(os->build))
    ,   sys_field("Kernel", sys_value(os->kernel))
    ,   sys_field("Architecture", sys_value(os->architecture))
    ,   sys_field("Bits", os->bits ? std::to_string(os->bits) : "Unavailable")
    };
}

inline std::vector<sys_node> sys_mainboard_nodes
(   hwinfo::result<hwinfo::Mainboard> const& mainboard
)
{   if (!mainboard)
        return {sys_field("Error", mainboard.error().message())};
    return
    {   sys_field("Vendor", sys_value(mainboard->vendor))
    ,   sys_field("Name", sys_value(mainboard->name))
    ,   sys_field("Version", sys_value(mainboard->version))
    ,   sys_field("Serial number", sys_value(mainboard->serial_number))
    ,   sys_field("Firmware vendor", sys_value(mainboard->firmware_vendor))
    ,   sys_field("Firmware version", sys_value(mainboard->firmware_version))
    ,   sys_field
        (   "Firmware date"
        ,   mainboard->firmware_date
            ?   std::format("{:%F}", *mainboard->firmware_date)
            :   "Unavailable"
        )
    };
}

inline std::vector<sys_node> sys_disk_nodes
(   hwinfo::result<std::vector<hwinfo::Disk>> const& disks
)
{   if (!disks)
        return {sys_field("Error", disks.error().message())};
    if (disks->empty())
        return {{"No disks detected"}};
    std::vector<sys_node> nodes;
    for (const auto& disk : *disks)
    {   sys_node node
        {   std::format("Disk {} - {}", disk.index, sys_value(disk.model))
        ,   {   sys_field("Vendor", sys_value(disk.vendor))
            ,   sys_field("Model", sys_value(disk.model))
            ,   sys_field("Serial number", sys_value(disk.serial_number))
            ,   sys_field("Size", std::format("{:.2GiB}", disk.size))
            ,   sys_field("Bus", std::string(hwinfo::to_string(disk.bus)))
            }
        };
        if (disk.link_speed)
            node.children.push_back(sys_field("Link speed", std::format("{}", *disk.link_speed)));
        for (const auto& mount : disk.mount_points)
            node.children.push_back
            (   sys_field
                (   "Mount", mount.path.string()
                ,   {   sys_field("Filesystem", sys_value(mount.filesystem))
                    ,   sys_field("Source", sys_value(mount.source))
                    ,   sys_field("Volume", sys_value(mount.volume))
                    ,   sys_field("Space ID", sys_value(mount.space_id))
                    ,   sys_field
                        (   "Encrypted"
                        ,   mount.encrypted
                            ?   (*mount.encrypted ? "Yes" : "No")
                            :   "Unavailable"
                        )
                    }
                )
            );
        nodes.push_back(std::move(node));
    }
    return nodes;
}

inline std::vector<sys_node> sys_network_nodes
(   hwinfo::result<std::vector<hwinfo::NetworkInterface>> const& interfaces
)
{   if (!interfaces)
        return {sys_field("Error", interfaces.error().message())};
    if (interfaces->empty())
        return {{"No network interfaces detected"}};
    std::vector<sys_node> nodes;
    for (const auto& nic : *interfaces)
    {   sys_node node
        {   sys_value(nic.name)
        ,   {   sys_field("Index", std::to_string(nic.index))
            ,   sys_field("Description", sys_value(nic.description))
            ,   sys_field("MAC", sys_value(nic.mac))
            ,   sys_field("Status", nic.is_up ? "Up" : "Down")
            ,   sys_field("Loopback", nic.is_loopback ? "Yes" : "No")
            ,   sys_field("IPv4", nic.ipv4.empty() ? "Unavailable" : join(nic.ipv4, ", "))
            ,   sys_field("IPv6", nic.ipv6.empty() ? "Unavailable" : join(nic.ipv6, ", "))
            }
        };
        nodes.push_back(std::move(node));
    }
    return nodes;
}

struct sys_state
{   std::array<bool, 5> expanded{true, false, false, false, false};
    std::array<ftxui::Box, 5> headers;
};

inline ftxui::Component sys_content
(   hwinfo::result<hwinfo::Computer> const& computer
,   hwinfo::result<hwinfo::Virtualization> const& virtualization
,   hwinfo::result<hwinfo::Mainboard> const& mainboard
,   hwinfo::result<hwinfo::Os> const& os
,   hwinfo::result<std::vector<hwinfo::Disk>> const& disks
,   hwinfo::result<std::vector<hwinfo::NetworkInterface>> const& networks
,   sys_state& state
)
{   using namespace ftxui;
    std::vector<std::vector<sys_node>> nodes
    {   sys_computer_nodes(computer, virtualization) };
    std::vector<std::string> labels
    {   virtualization && virtualization->vm ? "Virtual Machine" : "Computer" };
    if (virtualization && !virtualization->vm && !virtualization->container)
    {   labels.push_back("Mainboard");
        nodes.push_back(sys_mainboard_nodes(mainboard));
    }
    labels.insert(labels.end(), {"Operating System", "Disks", "Networks"});
    nodes.push_back(sys_os_nodes(os));
    nodes.push_back(sys_disk_nodes(disks));
    nodes.push_back(sys_network_nodes(networks));
    Components controls;
    for (std::size_t i = 0; i < labels.size(); ++i)
    {   CheckboxOption option;
        option.transform = [i, last = labels.size() - 1](EntryState entry)
        {   auto label = text(entry.label);
            if (entry.active) label |= bold;
            if (entry.focused) label |= inverted;
            return hbox
            (   {   text(i ? (i == last ? "└── " : "├── ") : "")
                ,   text(entry.state ? "▼ " : "▶ "), std::move(label)
                }
            );
        };
        auto checkbox = Checkbox(labels[i], &state.expanded[i], option);
        controls.push_back(i ? Maybe(checkbox, &state.expanded[0]) : checkbox);
    }
    auto container = Container::Vertical(std::move(controls));
    return Renderer
    (   container
    ,   [container, &state, nodes = std::move(nodes)]
        {   Elements rows
            {   container->ChildAt(0)->Render() | reflect(state.headers[0])
            };
            if (state.expanded[0])
            {   rows.push_back(sys_tree(nodes[0]));
                for (std::size_t i = 1; i < nodes.size(); ++i)
                {   rows.push_back(container->ChildAt(i)->Render() | reflect(state.headers[i]));
                    if (state.expanded[i])
                    {   Elements children;
                        sys_tree_rows
                        (   children, nodes[i], i + 1 == nodes.size() ? "    " : "│   "
                        );
                        rows.push_back(vbox(std::move(children)));
                    }
                }
            }
            return hbox({text(" "), vbox(std::move(rows))}) | border;
        }
    );
}

inline bool sys_navigation
(   ftxui::Event event
,   ftxui::Component const& sys
,   ftxui::Component const& menu
,   sys_state const& state
,   scroll_state& scroll
)
{   using namespace ftxui;
    if (event != Event::Tab && event != Event::TabReverse)
        return false;
    const auto container = sys->ChildAt(0);
    const bool forward = event == Event::Tab;
    if (!menu->Focused() && !sys->Focused())
        return false;
    const int count = state.expanded[0] ? container->ChildCount() : 1;
    int next = forward ? 0 : count - 1;
    if (sys->Focused())
    {   int current = 0;
        for (int i = 0; i < count; ++i)
            if (container->ChildAt(i)->Focused())
                current = i;
        next = current + (forward ? 1 : -1);
    }
    if (next < 0 || next >= count)
        menu->TakeFocus();
    else
    {   const auto& header = state.headers[next];
        if (header.y_max < scroll.view.y_min)
            scroll.offset -= scroll.view.y_min - header.y_max;
        else if (header.y_min > scroll.view.y_max)
            scroll.offset += header.y_min - scroll.view.y_max;
        container->ChildAt(next)->TakeFocus();
    }
    return true;
}

}   // namespace detail
