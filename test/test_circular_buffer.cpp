// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <utility>

#include "circular_buffer.hpp"

static_assert(std::random_access_iterator<circular_buffer_64<int>::iterator>);
static_assert(std::random_access_iterator<circular_buffer_64<int>::const_iterator>);

TEST_CASE("A new circular buffer is empty")
{   const circular_buffer_64<int> buffer;

    REQUIRE(buffer.empty());
    REQUIRE_FALSE(buffer.full());
    REQUIRE(buffer.size() == 0);
    REQUIRE(buffer.capacity() == 64);
    REQUIRE(buffer.begin() == buffer.end());
}

TEST_CASE("Pushing and popping preserves FIFO order")
{   circular_buffer_64<int> buffer;
    const int first = 10;

    buffer.push(first);
    buffer.push(20);

    REQUIRE_FALSE(buffer.empty());
    REQUIRE(buffer.size() == 2);
    REQUIRE(buffer[0] == 10);
    REQUIRE(buffer[1] == 20);
    REQUIRE(buffer.pop() == 10);
    REQUIRE(buffer.pop() == 20);
    REQUIRE(buffer.empty());
    REQUIRE_THROWS_AS(buffer.pop(), std::underflow_error);
}

TEST_CASE("Pushing into a full buffer replaces its oldest value")
{   circular_buffer_64<int> buffer;
    for (int value = 0; value < 64; ++value)
        buffer.push(value);

    REQUIRE(buffer.full());
    REQUIRE(buffer.size() == buffer.capacity());

    buffer.push(64);

    REQUIRE(buffer.full());
    REQUIRE(buffer.size() == 64);
    REQUIRE(buffer[0] == 1);
    REQUIRE(buffer[63] == 64);
    REQUIRE(buffer.pop() == 1);
}

TEST_CASE("The buffer wraps around while preserving logical order")
{   circular_buffer_64<int> buffer;
    for (int value = 0; value < 60; ++value)
        buffer.push(value);
    for (int value = 0; value < 50; ++value)
        REQUIRE(buffer.pop() == value);
    for (int value = 60; value < 80; ++value)
        buffer.push(value);

    REQUIRE(buffer.size() == 30);
    for (int index = 0; index < 30; ++index)
        REQUIRE(buffer[static_cast<std::size_t>(index)] == index + 50);
}

TEST_CASE("Iterators traverse and modify elements in logical order")
{   circular_buffer_64<int> buffer;
    for (int value = 0; value < 5; ++value)
        buffer.push(value);

    auto first = buffer.begin();
    auto third = first + 2;
    REQUIRE(*third == 2);
    REQUIRE(*(2 + first) == 2);
    REQUIRE(third - first == 2);
    REQUIRE(third[1] == 3);

    *third = 20;
    std::ranges::sort(buffer);

    const auto& const_buffer = buffer;
    REQUIRE(std::ranges::equal(const_buffer, std::array{0, 1, 3, 4, 20}));
    REQUIRE(std::distance(const_buffer.cbegin(), const_buffer.cend()) == 5);
}

TEST_CASE("Move assignment failures from push are propagated")
{   struct throwing_move_assignment
    {
        throwing_move_assignment& operator=(throwing_move_assignment&&)
        {
            throw std::runtime_error("move assignment failed");
        }
    };

    circular_buffer_64<throwing_move_assignment> buffer;
    throwing_move_assignment value;

    REQUIRE_THROWS_AS(buffer.push(std::move(value)), std::runtime_error);
    REQUIRE(buffer.empty());
}
