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
