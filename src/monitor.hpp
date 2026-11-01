// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <hwinfo/hwinfo.h>

#include "circular_buffer.hpp"

struct monitor
{   struct snapshot
    {   hwinfo::result<hwinfo::CpuLoad> cpu;
        hwinfo::result<std::vector<hwinfo::Hertz>> frequencies;
        std::vector<hwinfo::result<hwinfo::GpuStatus>> gpus;
        hwinfo::result<hwinfo::MemoryUsage> memory;
        // std::vector<std::pair<std::filesystem::path, hwinfo::result<hwinfo::DiskSpace>>> disks;
    };

    monitor(std::size_t tick_max = 60)
    :   tick_max_(tick_max)
    ,   tick_count_(0)
    ,   cpu_sampler_(std::make_unique<hwinfo::CpuSampler>())
    {   const auto gpus = hwinfo::gpus();
        if (gpus)
            for (const auto& gpu : *gpus)
                gpu_samplers.emplace_back(gpu);
        last_snapshot_ = take_snapshot();
        record_history();
    }

    void tick()
    {   tick_count_++;
        if (tick_count_ > tick_max_)
        {   tick_count_ = 0;
            last_snapshot_ = take_snapshot();
            record_history();
        }
    }

    const snapshot& operator()() const
    {   return last_snapshot_;
    }

    // Load history (one buffer per CPU thread), oldest sample first.
    const std::vector<circular_buffer_64<double>>& cpu_history() const
    {   return cpu_history_;
    }

    // Total CPU load history, oldest sample first.
    const circular_buffer_64<std::optional<double>>& cpu_total_history() const
    {   return cpu_total_history_;
    }

    // GPU status history (one buffer per GPU), oldest sample first.
    const std::vector<circular_buffer_64<hwinfo::result<hwinfo::GpuStatus>>>& gpu_history() const
    {   return gpu_history_;
    }

    // Memory usage history, oldest sample first.
    const circular_buffer_64<hwinfo::result<hwinfo::MemoryUsage>>& memory_history() const
    {   return memory_history_;
    }

    snapshot take_snapshot()
    {   snapshot current
        {   .cpu = cpu_sampler_->sample()
        ,   .frequencies = hwinfo::cpu_frequencies()
        ,   .memory = hwinfo::memory_usage()
        };
        for (auto& sampler : gpu_samplers)
            current.gpus.push_back(sampler.sample());
        return current;
    }

private:
    void record_history()
    {   cpu_total_history_.push
        (   last_snapshot_.cpu
            ?   std::optional<double>(last_snapshot_.cpu->total)
            :   std::nullopt
        );
        if (last_snapshot_.cpu)
        {   const auto& loads = last_snapshot_.cpu->per_thread;
            if (cpu_history_.size() < loads.size())
                cpu_history_.resize(loads.size());
            for (std::size_t i = 0; i < loads.size(); ++i)
                cpu_history_[i].push(loads[i]);
        }

        if (gpu_history_.size() < last_snapshot_.gpus.size())
            gpu_history_.resize(last_snapshot_.gpus.size());
        for (std::size_t i = 0; i < last_snapshot_.gpus.size(); ++i)
            gpu_history_[i].push(last_snapshot_.gpus[i]);

        memory_history_.push(last_snapshot_.memory);
    }

    std::size_t tick_max_;
    std::size_t tick_count_;
    std::unique_ptr<hwinfo::CpuSampler> cpu_sampler_;
    std::vector<hwinfo::GpuSampler> gpu_samplers;
    snapshot last_snapshot_;
    std::vector<circular_buffer_64<double>> cpu_history_;
    circular_buffer_64<std::optional<double>> cpu_total_history_;
    std::vector<circular_buffer_64<hwinfo::result<hwinfo::GpuStatus>>> gpu_history_;
    circular_buffer_64<hwinfo::result<hwinfo::MemoryUsage>> memory_history_;
};
