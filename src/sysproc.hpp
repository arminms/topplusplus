// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sysproc {

using time_point
=   std::chrono::time_point<std::chrono::system_clock, std::chrono::microseconds>;

enum class process_state : uint8_t
{   unknown = 0,
    running,    // Actively executing or runnable on a CPU queue
    sleeping,   // Interruptible sleep / waiting on an event
    disk_sleep, // Uninterruptible I/O wait (Linux 'D')
    stopped,    // Suspended via job control / debugger (SIGSTOP / ptrace)
    zombie,     // Terminated, awaiting parent wait() / handle closure
    idle        // Kernel idle thread / macOS SIDL
};

inline const char* to_string(process_state state) noexcept
{   switch (state)
    {   case process_state::running:    return "Running";
        case process_state::sleeping:   return "Sleeping";
        case process_state::disk_sleep: return "DiskSleep";
        case process_state::stopped:    return "Stopped";
        case process_state::zombie:     return "Zombie";
        case process_state::idle:       return "Idle";
        default:                        return "Unknown";
    }
}

struct process_info
{   uint32_t pid = 0;
    uint32_t ppid = 0;
    uint64_t start_time_raw = 0;          // Exact native tick/timestamp used for PID-reuse detection
    time_point start_time{};              // Cross-platform Unix epoch creation timestamp (UTC, microsecond precision)
    process_state state = process_state::unknown;
    uint32_t thread_count = 0;
    std::string name;
    std::string exe_path;                 // Full path to executable binary
    std::vector<std::string> args;        // Parsed argv[0..N-1] command-line arguments
    std::string username;
    uint64_t memory_bytes = 0;            // Resident Set Size (RSS) / Working Set
    uint64_t io_read_bytes = 0;           // Cumulative storage bytes read
    uint64_t io_write_bytes = 0;          // Cumulative storage bytes written
    double io_read_bytes_per_sec = 0.0;   // Populated by process_monitor
    double io_write_bytes_per_sec = 0.0;  // Populated by process_monitor
    double cpu_time_sec = 0.0;            // Cumulative user + kernel CPU seconds
    double cpu_percent = 0.0;             // Populated by process_monitor (100% = 1 full core)
};

}   // namespace sysproc

#if defined(_WIN32)
#   include "detail/sysproc_windows.hpp"
#elif defined(__APPLE__)
#   include "detail/sysproc_apple.hpp"
#elif defined(__linux__)
#   include "detail/sysproc_linux.hpp"
#else
#   error "Unsupported platform"
#endif

namespace sysproc {

namespace detail {

struct prev_sample
{   uint64_t start_time_raw = 0;
    double cpu_time_sec = 0.0;
    uint64_t io_read_bytes = 0;
    uint64_t io_write_bytes = 0;
};

inline
void update_rates(process_info& process, const prev_sample& previous, double elapsed_sec)
{   if
    (   elapsed_sec <= 0.0 || process.start_time_raw == 0
    ||  process.start_time_raw != previous.start_time_raw
    )   return;
    if (process.cpu_time_sec >= previous.cpu_time_sec)
        process.cpu_percent
        =   (process.cpu_time_sec - previous.cpu_time_sec)
        /   elapsed_sec * 100.0
        ;
    if (process.io_read_bytes >= previous.io_read_bytes)
        process.io_read_bytes_per_sec
        =   static_cast<double>(process.io_read_bytes - previous.io_read_bytes)
        /   elapsed_sec;
    if (process.io_write_bytes >= previous.io_write_bytes)
        process.io_write_bytes_per_sec
        =   static_cast<double>(process.io_write_bytes - previous.io_write_bytes)
        /   elapsed_sec;
}

} // namespace detail

// Stateful sampler for computing live CPU percentage and disk I/O throughput while guarding against PID reuse
class process_monitor
{
public:
    std::vector<process_info> sample()
    {   auto now = std::chrono::steady_clock::now();
        auto procs = list_processes();

        std::unordered_map<uint32_t, detail::prev_sample> current_samples;
        current_samples.reserve(procs.size());

        double elapsed_sec = has_prev_
            ? std::chrono::duration<double>(now - prev_time_).count()
            : 0.0;

        for (auto& p : procs)
        {   current_samples[p.pid] = detail::prev_sample
            {   p.start_time_raw,
                p.cpu_time_sec,
                p.io_read_bytes,
                p.io_write_bytes
            };

            if (has_prev_ && elapsed_sec > 0.0)
            {   auto it = prev_samples_.find(p.pid);
                if (it != prev_samples_.end())
                    detail::update_rates(p, it->second, elapsed_sec);
            }
        }

        prev_samples_ = std::move(current_samples);
        prev_time_ = now;
        has_prev_ = true;
        return procs;
    }

private:
    bool has_prev_ = false;
    std::chrono::steady_clock::time_point prev_time_;
    std::unordered_map<uint32_t, detail::prev_sample> prev_samples_;
};

enum class sort_by : uint8_t
{   pid,
    name,
    cpu_percent,
    cpu_time,
    memory,
    io_read_rate,
    io_write_rate,
    threads,
    start_time
};

inline
void sort_processes
(   std::vector<process_info>& procs
,   sort_by field
,   bool descending = true
)
{   auto cmp = [field](const process_info& a, const process_info& b) -> bool
    {   switch (field)
        {   case sort_by::pid:         return a.pid < b.pid;
            case sort_by::name:
            {   // Case-insensitive name comparison with PID tie-breaker
                size_t len = std::min(a.name.size(), b.name.size());
                for (size_t i = 0; i < len; ++i)
                {   unsigned char ca = static_cast<unsigned char>
                        (std::tolower(static_cast<unsigned char>(a.name[i])));
                    unsigned char cb = static_cast<unsigned char>
                        (std::tolower(static_cast<unsigned char>(b.name[i])));
                    if (ca != cb) return ca < cb;
                }
                if (a.name.size() != b.name.size())
                    return a.name.size() < b.name.size();
                return a.pid < b.pid;
            }
            case sort_by::cpu_percent:
                return a.cpu_percent < b.cpu_percent;
            case sort_by::cpu_time:
                return a.cpu_time_sec < b.cpu_time_sec;
            case sort_by::memory:
                return a.memory_bytes < b.memory_bytes;
            case sort_by::io_read_rate:
                return a.io_read_bytes_per_sec < b.io_read_bytes_per_sec;
            case sort_by::io_write_rate:
                return a.io_write_bytes_per_sec < b.io_write_bytes_per_sec;
            case sort_by::threads:
                return a.thread_count < b.thread_count;
            case sort_by::start_time:
                return a.start_time_raw < b.start_time_raw;
        }
        return a.pid < b.pid;
    };

    if (descending)
    {   std::sort
        (   procs.begin()
        ,   procs.end()
        ,   [&cmp](const process_info& a, const process_info& b)
            {   return cmp(b, a);
            }
        );
    }
    else
    {   std::sort(procs.begin(), procs.end(), cmp);
    }
}

template <typename Predicate>
inline
std::vector<process_info> filter_processes
(   const std::vector<process_info>& procs
,   Predicate&& pred
)
{   std::vector<process_info> out;
    for (const auto& p : procs)
        if (pred(p))
            out.push_back(p);
    return out;
}

inline
std::vector<process_info> find_by_name
(   const std::vector<process_info>& procs
,   std::string_view substr
,   bool case_sensitive = false
)
{   return filter_processes(procs, [substr, case_sensitive](const process_info& p)
    {   if (substr.empty()) return true;
        if (case_sensitive)
            return p.name.find(substr) != std::string::npos;
        auto it = std::search
        (   p.name.begin(), p.name.end(),
            substr.begin(), substr.end(),
            [](char ch1, char ch2)
            {   return std::tolower(static_cast<unsigned char>(ch1)) ==
                       std::tolower(static_cast<unsigned char>(ch2));
            }
        );
        return it != p.name.end();
    });
}

// Index-backed tree representation (avoids pointer invalidation when vectors reallocate)
struct process_node
{   size_t proc_index = 0;               // Index into the source std::vector<process_info>
    std::vector<process_node> children;
};

inline
std::vector<process_node> build_process_tree
(   const std::vector<process_info>& procs
)
{   std::unordered_map<uint32_t, size_t> pid_to_idx;
    pid_to_idx.reserve(procs.size());
    for (size_t i = 0; i < procs.size(); ++i)
        pid_to_idx[procs[i].pid] = i;

    std::vector<std::vector<size_t>> adj(procs.size());
    std::vector<size_t> root_indices;

    for (size_t i = 0; i < procs.size(); ++i)
    {   const auto& child = procs[i];
        bool has_valid_parent = false;

        if (child.ppid != 0 && child.ppid != child.pid)
        {   auto it = pid_to_idx.find(child.ppid);
            if (it != pid_to_idx.end())
            {   const auto& parent = procs[it->second];
                // Reject recycled parent PIDs that were created after
                // the child started
                const bool timestamps_valid =
                (   parent.start_time_raw == 0
                ||  child.start_time_raw == 0
                ||  parent.start_time_raw <= child.start_time_raw
                );
                if (timestamps_valid)
                {   adj[it->second].push_back(i);
                    has_valid_parent = true;
                }
            }
        }

        if (!has_valid_parent)
            root_indices.push_back(i);
    }

    std::vector<uint8_t> visited(procs.size(), 0);
    std::function<process_node(size_t)> build_node
    =   [&](size_t idx) -> process_node
    {   visited[idx] = 1;
        process_node node;
        node.proc_index = idx;
        node.children.reserve(adj[idx].size());
        for (size_t child_idx : adj[idx])
        {   if (!visited[child_idx])
                node.children.push_back(build_node(child_idx));
        }
        return node;
    };

    std::vector<process_node> roots;
    roots.reserve(root_indices.size());
    for (size_t root_idx : root_indices)
    {   if (!visited[root_idx])
            roots.push_back(build_node(root_idx));
    }

    // Catch any disconnected cycles in malformed snapshots
    for (size_t i = 0; i < procs.size(); ++i)
    {   if (!visited[i])
            roots.push_back(build_node(i));
    }

    return roots;
}

// Depth-first traversal helper passing (const process_info&, size_t depth)
template <typename Visitor>
inline
void walk_process_tree
(   const std::vector<process_info>& procs
,   const std::vector<process_node>& roots
,   Visitor&& fn
)
{   std::function<void(const process_node&, size_t)> dfs
    =   [&](const process_node& node, size_t depth)
    {   fn(procs[node.proc_index], depth);
        for (const auto& child : node.children)
            dfs(child, depth + 1);
    };
    for (const auto& root : roots)
        dfs(root, 0);
}

} // namespace sysproc
