// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#include <catch2/catch_test_macros.hpp>

#include <system_error>
#include <algorithm>

#include "monitor.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

TEST_CASE("Monitor samples processes on its refresh cycle")
{   monitor monitors{1};
#ifdef _WIN32
    const auto pid = static_cast<uint32_t>(GetCurrentProcessId());
#else
    const auto pid = static_cast<uint32_t>(getpid());
#endif
    const auto find_self = [pid](const auto& processes)
    {   return std::find_if(processes.begin(), processes.end(),
            [pid](const auto& process) { return process.pid == pid; });
    };
    const auto& initial = monitors().processes;
    REQUIRE(find_self(initial) != initial.end());
    const auto generation = monitors.snapshot_generation();
    monitors.tick();
    REQUIRE(monitors.snapshot_generation() == generation);
    monitors.tick();
    REQUIRE(monitors.snapshot_generation() == generation + 1);
    const auto& refreshed = monitors().processes;
    REQUIRE(find_self(refreshed) != refreshed.end());
    REQUIRE(std::is_sorted(refreshed.begin(), refreshed.end(), [](const auto& a, const auto& b)
    {   return a.cpu_percent > b.cpu_percent;
    }));
}

TEST_CASE("Monitor captures CPU load and frequencies")
{   monitor monitors{2};
    const auto& snapshot = monitors();

    REQUIRE((snapshot.cpu.has_value() || snapshot.cpu.error().code() != std::error_code{}));
    REQUIRE((snapshot.frequencies.has_value() || snapshot.frequencies.error().code() != std::error_code{}));

    if (snapshot.cpu)
    {   REQUIRE(snapshot.cpu->total >= 0.0);
        REQUIRE(snapshot.cpu->total <= 1.0);
        for (const auto load : snapshot.cpu->per_thread)
        {   REQUIRE(load >= 0.0);
            REQUIRE(load <= 1.0);
        }
    }

    monitors.tick();
    monitors.tick();
    monitors.tick();

    REQUIRE(&monitors() == &snapshot);
    REQUIRE((snapshot.cpu.has_value() || snapshot.cpu.error().code() != std::error_code{}));
    REQUIRE((snapshot.frequencies.has_value() || snapshot.frequencies.error().code() != std::error_code{}));
}

TEST_CASE("Monitor saves per-thread CPU load history")
{   monitor monitors{0};
    const auto& snapshot = monitors();

    if (!snapshot.cpu)
        SUCCEED("CPU load unavailable on this system");
    else
    {   const auto threads = snapshot.cpu->per_thread.size();
        REQUIRE(monitors.cpu_history().size() == threads);
        REQUIRE(monitors.cpu_total_history().size() == 1);
        REQUIRE(monitors.cpu_total_history()[0] == snapshot.cpu->total);
        for (const auto& history : monitors.cpu_history())
            REQUIRE(history.size() == 1);

        monitors.tick();
        monitors.tick();
        for (std::size_t i = 0; i < threads; ++i)
        {   const auto& history = monitors.cpu_history()[i];
            REQUIRE(history.size() == 3);
            REQUIRE(history[2] == monitors().cpu->per_thread[i]);
        }
        REQUIRE(monitors.cpu_total_history().size() == 3);
        REQUIRE(monitors.cpu_total_history()[2] == monitors().cpu->total);
    }
}

TEST_CASE("Monitor saves per-GPU status history")
{   monitor monitors{2};
    REQUIRE(monitors.gpu_history().size() == monitors().gpus.size());
    for (const auto& history : monitors.gpu_history())
        REQUIRE(history.size() == 1);

    monitors.tick();
    monitors.tick();
    for (const auto& history : monitors.gpu_history())
        REQUIRE(history.size() == 1);

    monitors.tick();
    REQUIRE(monitors.gpu_history().size() == monitors().gpus.size());
    for (std::size_t i = 0; i < monitors.gpu_history().size(); ++i)
    {   const auto& history = monitors.gpu_history()[i];
        REQUIRE(history.size() == 2);
        REQUIRE(history[1].has_value() == monitors().gpus[i].has_value());
    }
}

TEST_CASE("Monitor captures memory usage")
{   monitor monitors{2};
    const auto& memory = monitors().memory;

    REQUIRE((memory.has_value() || memory.error().code() != std::error_code{}));
    if (memory)
    {   REQUIRE(memory->total.value > 0);
        REQUIRE(memory->available.value <= memory->total.value);
    }
}

TEST_CASE("Monitor captures physical memory information")
{   monitor monitors{2};
    const auto& memory = monitors.memory_info();

    REQUIRE((memory.has_value() || memory.error().code() != std::error_code{}));
    if (memory)
    {   REQUIRE(memory->total.value > 0);
        if (memory->modules)
            for (const auto& module : *memory->modules)
                if (module.size)
                    REQUIRE(module.size->value > 0);
    }
}

TEST_CASE("Monitor saves memory usage history")
{   monitor monitors{2};
    REQUIRE(monitors.memory_history().size() == 1);

    monitors.tick();
    monitors.tick();
    REQUIRE(monitors.memory_history().size() == 1);

    monitors.tick();
    const auto& history = monitors.memory_history();
    REQUIRE(history.size() == 2);
    REQUIRE(history[1].has_value() == monitors().memory.has_value());
    if (history[1])
        REQUIRE(history[1]->total == monitors().memory->total);
}

TEST_CASE("Monitor memory history is capped at the buffer capacity")
{   monitor monitors{0};
    for (int i = 0; i < 100; ++i)
        monitors.tick();

    REQUIRE(monitors.memory_history().full());
    REQUIRE(monitors.memory_history().size() == monitors.memory_history().capacity());
}

TEST_CASE("Monitor history does not grow before the tick threshold")
{   monitor monitors{5};
    const auto before = monitors.cpu_history();

    monitors.tick();
    monitors.tick();

    REQUIRE(monitors.cpu_history().size() == before.size());
    for (std::size_t i = 0; i < before.size(); ++i)
        REQUIRE(monitors.cpu_history()[i].size() == before[i].size());
}

TEST_CASE("Monitor history is capped at the buffer capacity")
{   monitor monitors{0};
    for (int i = 0; i < 100; ++i)
        monitors.tick();

    for (const auto& history : monitors.cpu_history())
    {   REQUIRE(history.size() == history.capacity());
        REQUIRE(history.full());
    }
}
