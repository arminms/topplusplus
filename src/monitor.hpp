// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#include <cmath>
#include <memory>
#include <optional>
#include <vector>

#include <hwinfo/hwinfo.h>

#include "circular_buffer.hpp"
#include "sysproc.hpp"

struct monitor
{   struct snapshot
    {   hwinfo::result<hwinfo::CpuLoad> cpu;
        hwinfo::result<std::vector<hwinfo::Hertz>> frequencies;
        std::vector<hwinfo::result<hwinfo::GpuStatus>> gpus;
        hwinfo::result<hwinfo::MemoryUsage> memory;
        std::vector<sysproc::process_info> processes;
        // std::vector<std::pair<std::filesystem::path, hwinfo::result<hwinfo::DiskSpace>>> disks;
    };

    monitor(std::size_t tick_max = 60)
    :   tick_max_(tick_max)
    ,   tick_count_(0)
    ,   cpu_sampler_(std::make_unique<hwinfo::CpuSampler>())
    ,   memory_info_(hwinfo::memory())
    {   const auto gpus = hwinfo::gpus();
        if (gpus)
            for (const auto& gpu : *gpus)
            {   gpu_samplers_.emplace_back(gpu);
                gpu_types_.push_back(gpu.type);
            }
        last_snapshot_ = take_snapshot();
        record_history();
    }

    void tick()
    {   tick_count_++;
        if (tick_count_ > tick_max_)
        {   tick_count_ = 0;
            last_snapshot_ = take_snapshot();
            record_history();
            ++snapshot_generation_;
        }
    }

    const snapshot& operator()() const
    {   return last_snapshot_;
    }

    std::size_t snapshot_generation() const
    {   return snapshot_generation_;
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

    std::optional<uint32_t> selected_process_pid() const
    {   return selected_process_pid_;
    }

    void select_process(uint32_t pid)
    {   for (const auto& process : last_snapshot_.processes)
    {   if (process.pid != pid)
            continue;
        if (selected_process_pid_ == pid
            && selected_process_start_time_raw_ == process.start_time_raw)
            return;
        if (selected_process_pid_ != pid
            || selected_process_start_time_raw_ != process.start_time_raw)
        {   selected_process_pid_ = pid;
            selected_process_start_time_raw_ = process.start_time_raw;
            process_cpu_history_ = {};
            process_ram_history_ = {};
        }
        record_selected_process_history();
        return;
    }
        if (!last_snapshot_.processes.empty())
        {   selected_process_pid_ = last_snapshot_.processes.front().pid;
            selected_process_start_time_raw_
                = last_snapshot_.processes.front().start_time_raw;
            process_cpu_history_ = {};
            process_ram_history_ = {};
            record_selected_process_history();
        }
    }

    const circular_buffer_64<std::optional<double>>& process_cpu_history() const
    {   return process_cpu_history_; }

    const circular_buffer_64<std::optional<double>>& process_ram_history() const
    {   return process_ram_history_; }

    const circular_buffer_64<std::optional<double>>& selected_process_cpu_history() const
    {   return process_cpu_history_; }

    const circular_buffer_64<std::optional<double>>& selected_process_ram_history() const
    {   return process_ram_history_; }

    const hwinfo::result<hwinfo::Memory>& memory_info() const
    {   return memory_info_;
    }

    snapshot take_snapshot()
    {   snapshot current
        {   .cpu = cpu_sampler_->sample()
        ,   .frequencies = hwinfo::cpu_frequencies()
        ,   .memory = hwinfo::memory_usage()
        ,   .processes = process_sampler_.sample()
        };
        sysproc::sort_processes(current.processes, sysproc::sort_by::cpu_percent);
        for (std::size_t i = 0; i < gpu_samplers_.size(); ++i)
        {   auto status = gpu_samplers_[i].sample();
            // Non-discrete GPUs use system RAM, so report that instead
            if (status && gpu_types_[i] != hwinfo::GpuType::discrete)
            {   if (current.memory
                    && current.memory->available.value <= current.memory->total.value)
                {   status->memory_total = current.memory->total;
                    status->memory_used = hwinfo::Bytes
                    {   current.memory->total.value - current.memory->available.value
                    };
                }
                else
                {   status->memory_total.reset();
                    status->memory_used.reset();
                }
            }
            current.gpus.push_back(std::move(status));
        }
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

        if (!selected_process_pid_ && !last_snapshot_.processes.empty())
        {   selected_process_pid_ = last_snapshot_.processes.front().pid;
            selected_process_start_time_raw_
                = last_snapshot_.processes.front().start_time_raw;
        }
        if (selected_process_pid_)
        {   bool found = false;
            for (const auto& process : last_snapshot_.processes)
            {   if (process.pid != *selected_process_pid_)
                    continue;
                if (selected_process_start_time_raw_ != process.start_time_raw)
                {   selected_process_start_time_raw_ = process.start_time_raw;
                    process_cpu_history_ = {};
                    process_ram_history_ = {};
                }
                found = true;
                break;
            }
            if (!found)
            {   process_cpu_history_ = {};
                process_ram_history_ = {};
                selected_process_pid_.reset();
                selected_process_start_time_raw_ = 0;
            }
        }
        if (!selected_process_pid_ && !last_snapshot_.processes.empty())
        {   selected_process_pid_ = last_snapshot_.processes.front().pid;
            selected_process_start_time_raw_
                = last_snapshot_.processes.front().start_time_raw;
            process_cpu_history_ = {};
            process_ram_history_ = {};
        }
        record_selected_process_history();
    }

    void record_selected_process_history()
    {   if (!selected_process_pid_)
            return;
        for (const auto& process : last_snapshot_.processes)
        {   if (process.pid != *selected_process_pid_)
                continue;
            process_cpu_history_.push
            (   std::isfinite(process.cpu_percent)
                    ? std::optional<double>(process.cpu_percent / 100.0)
                    : std::nullopt
            );
            const auto total_memory = last_snapshot_.memory
                    ? last_snapshot_.memory->total.value
                    : 0;
            process_ram_history_.push
            (   total_memory > 0
                    ? std::optional<double>
                        (static_cast<double>(process.memory_bytes)
                         / static_cast<double>(total_memory))
                    : std::nullopt
            );
            return;
        }
    }

    std::size_t tick_max_;
    std::size_t tick_count_;
    std::size_t snapshot_generation_ = 0;
    std::unique_ptr<hwinfo::CpuSampler> cpu_sampler_;
    sysproc::process_monitor process_sampler_;
    hwinfo::result<hwinfo::Memory> memory_info_;
    std::vector<hwinfo::GpuSampler> gpu_samplers_;
    std::vector<hwinfo::GpuType> gpu_types_;
    snapshot last_snapshot_;
    std::vector<circular_buffer_64<double>> cpu_history_;
    circular_buffer_64<std::optional<double>> cpu_total_history_;
    std::vector<circular_buffer_64<hwinfo::result<hwinfo::GpuStatus>>> gpu_history_;
    circular_buffer_64<hwinfo::result<hwinfo::MemoryUsage>> memory_history_;
    std::optional<uint32_t> selected_process_pid_;
    uint64_t selected_process_start_time_raw_ = 0;
    circular_buffer_64<std::optional<double>> process_cpu_history_;
    circular_buffer_64<std::optional<double>> process_ram_history_;
};
