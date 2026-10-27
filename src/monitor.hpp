// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#include <memory>

#include <hwinfo/hwinfo.h>

struct monitor
{   struct snapshot
    {   hwinfo::result<hwinfo::CpuLoad> cpu;
        hwinfo::result<std::vector<hwinfo::Hertz>> frequencies;
        // std::vector<hwinfo::result<hwinfo::GpuStatus>> gpus;
        // hwinfo::result<hwinfo::MemoryUsage> memory;
        // std::vector<std::pair<std::filesystem::path, hwinfo::result<hwinfo::DiskSpace>>> disks;
    };

    monitor(std::size_t tick_max = 60)
    :   tick_max_(tick_max)
    ,   tick_count_(0)
    ,   cpu_sampler_(std::make_unique<hwinfo::CpuSampler>())
    ,   last_snapshot_(take_snapshot())
    {}

    void tick()
    {   tick_count_++;
        if (tick_count_ > tick_max_)
        {   tick_count_ = 0;
            last_snapshot_ = take_snapshot();
        }
    }

    const snapshot& operator()() const
    {   return last_snapshot_;
    }

    snapshot take_snapshot()
    {   return snapshot
        {   .cpu = cpu_sampler_->sample()
        ,   .frequencies = hwinfo::cpu_frequencies()
            // ,   .gpus = std::ranges::to<std::vector>
            //     (   gpu_samplers
            //     |   std::views::transform([](auto& gpu){ return gpu.sample(); })
            //     )
            // ,   .memory = hwinfo::memory_usage()
            // ,   .disks = std::ranges::to<std::vector>
            //     (   mount_points
            //     |   std::views::transform
            //         (   [](const auto& path)
            //             {   return std::pair{path, hwinfo::disk_space(path)};
            //             }
            //         )
            //     )
        };
    }

private:
    std::size_t tick_max_;
    std::size_t tick_count_;
    std::unique_ptr<hwinfo::CpuSampler> cpu_sampler_;
    snapshot last_snapshot_;
};
