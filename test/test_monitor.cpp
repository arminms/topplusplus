// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#include <catch2/catch_test_macros.hpp>

#include <system_error>

#include "monitor.hpp"

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
        for (const auto& history : monitors.cpu_history())
            REQUIRE(history.size() == 1);

        monitors.tick();
        monitors.tick();
        for (std::size_t i = 0; i < threads; ++i)
        {   const auto& history = monitors.cpu_history()[i];
            REQUIRE(history.size() == 3);
            REQUIRE(history[2] == monitors().cpu->per_thread[i]);
        }
    }
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
