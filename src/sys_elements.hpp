// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#include <array>
#include <format>
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
};

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
{   for (std::size_t i = 0; i < nodes.size(); ++i)
    {   const bool last = i + 1 == nodes.size() && !has_following;
        rows.push_back(ftxui::text(prefix + (last ? "└── " : "├── ") + nodes[i].label));
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
        {   {"Vendor: " + sys_value(computer->vendor)}
        ,   {"Model: " + sys_value(computer->model)}
        ,   {"Family: " + sys_value(computer->family)}
        ,   {"Version: " + sys_value(computer->version)}
        ,   {"SKU: " + sys_value(computer->sku)}
        ,   {"Serial number: " + sys_value(computer->serial_number)}
        ,   {"UUID: " + (computer->uuid ? hwinfo::to_string(*computer->uuid) : "Unavailable")}
        ,   {"Chassis: " + std::string(hwinfo::to_string(computer->chassis))}
        };
        if (computer->chassis_code)
            nodes.push_back({"Chassis code: " + std::to_string(*computer->chassis_code)});
    }
    else if (!computer)
        nodes.push_back({std::format("Computer error: {}", computer.error())});
    if (computer && virtualization && (virtualization->vm || virtualization->container)
        && computer->boot_time)
        nodes.push_back({std::format("Boot time (UTC): {:%F %T}", *computer->boot_time)});
    if (!virtualization)
        nodes.push_back({std::format("Virtualization error: {}", virtualization.error())});
    else
    {   if (virtualization->vm)
        {   const auto& vm = *virtualization->vm;
            nodes.push_back
            (   {"Virtual machine"
                ,   {   {"Hypervisor: " + std::string(hwinfo::to_string(vm.hypervisor))}
                    ,   {"Vendor ID: " + sys_value(vm.vendor_id)}
                    }
                }
            );
        }
        if (virtualization->container)
        {   const auto& container = *virtualization->container;
            nodes.push_back
            (   {"Container"
                ,   {   {"Runtime: " + std::string(hwinfo::to_string(container.runtime))}
                    ,   {std::string("Kubernetes: ") + (container.kubernetes ? "Yes" : "No")}
                    }
                }
            );
        }
    }
    return nodes;
}

inline std::vector<sys_node> sys_os_nodes(hwinfo::result<hwinfo::Os> const& os)
{   if (!os)
        return {{std::format("Error: {}", os.error())}};
    return
    {   {"Name: " + sys_value(os->name)}
    ,   {"Family: " + std::string(hwinfo::to_string(os->family))}
    ,   {"Marketing name: " + sys_value(os->marketing_name)}
    ,   {"Version: " + sys_value(os->version)}
    ,   {"Build: " + sys_value(os->build)}
    ,   {"Kernel: " + sys_value(os->kernel)}
    ,   {"Architecture: " + sys_value(os->architecture)}
    ,   {"Bits: " + (os->bits ? std::to_string(os->bits) : "Unavailable")}
    };
}

inline std::vector<sys_node> sys_disk_nodes
(   hwinfo::result<std::vector<hwinfo::Disk>> const& disks
)
{   if (!disks)
        return {{std::format("Error: {}", disks.error())}};
    if (disks->empty())
        return {{"No disks detected"}};
    std::vector<sys_node> nodes;
    for (const auto& disk : *disks)
    {   sys_node node
        {   std::format("Disk {} - {}", disk.index, sys_value(disk.model))
        ,   {   {"Vendor: " + sys_value(disk.vendor)}
            ,   {"Model: " + sys_value(disk.model)}
            ,   {"Serial number: " + sys_value(disk.serial_number)}
            ,   {std::format("Size: {:.2GiB}", disk.size)}
            ,   {"Bus: " + std::string(hwinfo::to_string(disk.bus))}
            }
        };
        if (disk.link_speed)
            node.children.push_back({std::format("Link speed: {}", *disk.link_speed)});
        for (const auto& mount : disk.mount_points)
            node.children.push_back
            (   {"Mount: " + mount.path.string()
                ,   {   {"Filesystem: " + sys_value(mount.filesystem)}
                    ,   {"Source: " + sys_value(mount.source)}
                    ,   {"Volume: " + sys_value(mount.volume)}
                    ,   {"Space ID: " + sys_value(mount.space_id)}
                    ,   {"Encrypted: " + std::string
                        (   mount.encrypted
                            ?   (*mount.encrypted ? "Yes" : "No")
                            :   "Unavailable"
                        )}
                    }
                }
            );
        nodes.push_back(std::move(node));
    }
    return nodes;
}

inline std::vector<sys_node> sys_network_nodes
(   hwinfo::result<std::vector<hwinfo::NetworkInterface>> const& interfaces
)
{   if (!interfaces)
        return {{std::format("Error: {}", interfaces.error())}};
    if (interfaces->empty())
        return {{"No network interfaces detected"}};
    std::vector<sys_node> nodes;
    for (const auto& nic : *interfaces)
    {   sys_node node
        {   sys_value(nic.name)
        ,   {   {"Index: " + std::to_string(nic.index)}
            ,   {"Description: " + sys_value(nic.description)}
            ,   {"MAC: " + sys_value(nic.mac)}
            ,   {std::string("Status: ") + (nic.is_up ? "Up" : "Down")}
            ,   {std::string("Loopback: ") + (nic.is_loopback ? "Yes" : "No")}
            ,   {"IPv4: " + (nic.ipv4.empty() ? "Unavailable" : join(nic.ipv4, ", "))}
            ,   {"IPv6: " + (nic.ipv6.empty() ? "Unavailable" : join(nic.ipv6, ", "))}
            }
        };
        nodes.push_back(std::move(node));
    }
    return nodes;
}

struct sys_state
{   std::array<bool, 4> expanded{true, false, false, false};
    std::array<ftxui::Box, 4> headers;
};

inline ftxui::Component sys_content
(   hwinfo::result<hwinfo::Computer> const& computer
,   hwinfo::result<hwinfo::Virtualization> const& virtualization
,   hwinfo::result<hwinfo::Os> const& os
,   hwinfo::result<std::vector<hwinfo::Disk>> const& disks
,   hwinfo::result<std::vector<hwinfo::NetworkInterface>> const& networks
,   sys_state& state
)
{   using namespace ftxui;
    std::array<std::vector<sys_node>, 4> nodes
    {   sys_computer_nodes(computer, virtualization)
    ,   sys_os_nodes(os), sys_disk_nodes(disks), sys_network_nodes(networks)
    };
    std::array<std::string, 4> labels
    {   virtualization && virtualization->vm ? "Virtual Machine" : "Computer"
    ,   "Operating System", "Disks", "Networks"
    };
    Components controls;
    for (std::size_t i = 0; i < labels.size(); ++i)
    {   CheckboxOption option;
        option.transform = [i](EntryState entry)
        {   auto label = text(entry.label);
            if (entry.active) label |= bold;
            if (entry.focused) label |= inverted;
            return hbox
            (   {   text(i ? (i == 3 ? "└── " : "├── ") : "")
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
                for (std::size_t i = 1; i < 4; ++i)
                {   rows.push_back(container->ChildAt(i)->Render() | reflect(state.headers[i]));
                    if (state.expanded[i])
                    {   Elements children;
                        sys_tree_rows(children, nodes[i], i == 3 ? "    " : "│   ");
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
    const int count = container->ChildAt(1)->Focusable() ? 4 : 1;
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
