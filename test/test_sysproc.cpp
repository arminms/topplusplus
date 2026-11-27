// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "sysproc.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

TEST_CASE("Process sorting and name filtering")
{   std::vector<sysproc::process_info> processes(3);
    processes[0].pid = 3;
    processes[0].name = "worker";
    processes[0].cpu_percent = 15;
    processes[1].pid = 1;
    processes[1].name = "Alpha";
    processes[1].cpu_percent = 5;
    processes[2].pid = 2;
    processes[2].name = "alpha";
    processes[2].cpu_percent = 10;

    sysproc::sort_processes(processes, sysproc::sort_by::cpu_percent);
    REQUIRE(processes[0].pid == 3);
    REQUIRE(processes[1].pid == 2);
    REQUIRE(processes[2].pid == 1);
    sysproc::sort_processes(processes, sysproc::sort_by::name, false);
    REQUIRE(processes[0].pid == 1);
    REQUIRE(processes[1].pid == 2);
    REQUIRE(processes[2].pid == 3);

    REQUIRE(sysproc::find_by_name(processes, "ALP").size() == 2);
    REQUIRE(sysproc::find_by_name(processes, "Alp", true).size() == 1);
    REQUIRE(sysproc::find_by_name(processes, "").size() == processes.size());
    REQUIRE(sysproc::filter_processes(processes, [](const auto& p) { return p.pid > 1; }).size() == 2);
}

TEST_CASE("Process tree handles orphans recycled parents and cycles")
{   std::vector<sysproc::process_info> processes(6);
    processes[0].pid = 1;
    processes[0].start_time_raw = 10;
    processes[1].pid = 2;
    processes[1].ppid = 1;
    processes[1].start_time_raw = 20;
    processes[2].pid = 3;
    processes[2].ppid = 1;
    processes[2].start_time_raw = 5;
    processes[3].pid = 4;
    processes[3].ppid = 99;
    processes[4].pid = 5;
    processes[4].ppid = 6;
    processes[5].pid = 6;
    processes[5].ppid = 5;

    const auto roots = sysproc::build_process_tree(processes);
    std::vector<std::pair<uint32_t, std::size_t>> visited;
    sysproc::walk_process_tree(processes, roots, [&](const auto& p, std::size_t depth)
    {   visited.emplace_back(p.pid, depth);
    });
    const std::vector<std::pair<uint32_t, std::size_t>> expected
    {   {1, 0}, {2, 1}, {3, 0}, {4, 0}, {5, 0}, {6, 1}
    };
    REQUIRE(visited == expected);
}

TEST_CASE("Process rates require matching creation times and monotonic counters")
{   const sysproc::detail::prev_sample previous{42, 2.0, 100, 200};
    sysproc::process_info process;
    process.start_time_raw = 42;
    process.cpu_time_sec = 3.0;
    process.io_read_bytes = 400;
    process.io_write_bytes = 100;
    sysproc::detail::update_rates(process, previous, 2.0);
    REQUIRE(process.cpu_percent == 50.0);
    REQUIRE(process.io_read_bytes_per_sec == 150.0);
    REQUIRE(process.io_write_bytes_per_sec == 0.0);

    process.cpu_percent = 0;
    process.io_read_bytes_per_sec = 0;
    process.start_time_raw = 43;
    sysproc::detail::update_rates(process, previous, 2.0);
    REQUIRE(process.cpu_percent == 0.0);
    REQUIRE(process.io_read_bytes_per_sec == 0.0);
    process.start_time_raw = 0;
    sysproc::detail::update_rates(process, {}, 2.0);
    REQUIRE(process.cpu_percent == 0.0);
    REQUIRE(process.io_read_bytes_per_sec == 0.0);
    process.start_time_raw = 42;
    sysproc::detail::update_rates(process, previous, 0.0);
    REQUIRE(process.cpu_percent == 0.0);
}

TEST_CASE("Process monitor captures the current process")
{   sysproc::process_monitor monitor;
    const auto initial = monitor.sample();
#ifdef _WIN32
    const auto pid = static_cast<uint32_t>(GetCurrentProcessId());
#else
    const auto pid = static_cast<uint32_t>(getpid());
#endif
    const auto find_self = [pid](const auto& processes)
    {   return std::find_if(processes.begin(), processes.end(),
            [pid](const auto& p) { return p.pid == pid; });
    };
    const auto first = find_self(initial);
    REQUIRE(first != initial.end());
    REQUIRE_FALSE(first->name.empty());
    REQUIRE(first->start_time_raw != 0);
    REQUIRE(first->thread_count > 0);
    REQUIRE(first->memory_bytes > 0);
    REQUIRE(first->cpu_percent == 0.0);
    REQUIRE(first->start_time.time_since_epoch() > std::chrono::microseconds::zero());
    REQUIRE(first->start_time <= std::chrono::system_clock::now());
#if defined(__linux__)
    REQUIRE(first->ppid == static_cast<uint32_t>(getppid()));
    REQUIRE_FALSE(first->exe_path.empty());
    REQUIRE_FALSE(first->args.empty());
#endif

    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (std::chrono::steady_clock::now() < until)
    {   // Keep this process busy to exercise CPU rate calculation.
    }

    const auto next = monitor.sample();
    const auto second = find_self(next);
    REQUIRE(second != next.end());
    REQUIRE(second->start_time_raw == first->start_time_raw);
    REQUIRE(second->cpu_time_sec >= first->cpu_time_sec);
    REQUIRE(second->cpu_percent > 0.0);
    REQUIRE(second->io_read_bytes_per_sec >= 0.0);
    REQUIRE(second->io_write_bytes_per_sec >= 0.0);
}

#ifdef _WIN32
TEST_CASE("Windows process strings convert UTF-16 to UTF-8")
{   REQUIRE(sysproc::detail::wide_to_utf8(L"hello") == "hello");
    REQUIRE(sysproc::detail::wide_to_utf8(L"\u00e9") == "\xc3\xa9");
    REQUIRE(sysproc::detail::wide_to_utf8(L"hello", 3) == "hel");
}
#endif
