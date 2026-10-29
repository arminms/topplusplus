// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#include <algorithm>
#include <format>
#include <string>

#include <hwinfo/hwinfo.h>

namespace detail {

inline auto count_cores(hwinfo::Cpu const& cpu, hwinfo::CoreType type)
{   return std::ranges::count(cpu.cores, type, &hwinfo::Core::type);
}

inline std::string core_count_string(hwinfo::Cpu const& cpu)
{   using hwinfo::CoreType;
    return cpu.is_hybrid()
    ?   std::format
        (   "{} cores ({} Performance + {} Efficiency, {} threads)"
        ,   cpu.physical_cores
        ,   count_cores(cpu, CoreType::performance)
        ,   count_cores(cpu, CoreType::efficiency)
        ,   cpu.logical_cores
        )
    :   std::format
        (   "{} cores ({} threads)"
        ,   cpu.physical_cores
        ,   cpu.logical_cores
        )
    ;
}

}   // namespace detail
