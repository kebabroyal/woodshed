// SPDX-FileCopyrightText: 2026 kebabroyal
// SPDX-License-Identifier: GPL-3.0-or-later

#include "woodshed/peak_pyramid.hpp"

#include <catch2/catch.hpp>

#include <algorithm>
#include <random>
#include <vector>

using woodshed::peak;
using woodshed::peak_pyramid;

// Brute force over every bin touching the range.
static peak reference(const std::vector<std::vector<float>> &channels, std::size_t begin,
                      std::size_t end)
{
    const std::size_t total = channels[0].size();
    end = std::min(end, total);
    if (begin >= end)
        return {0.0f, 0.0f};
    const std::size_t first = begin / peak_pyramid::BASE_BIN * peak_pyramid::BASE_BIN;
    const std::size_t last = std::min(
        (end + peak_pyramid::BASE_BIN - 1) / peak_pyramid::BASE_BIN * peak_pyramid::BASE_BIN,
        total);
    peak p{channels[0][first], channels[0][first]};
    for (const auto &channel : channels)
        for (std::size_t i = first; i < last; ++i)
        {
            p.min = std::min(p.min, channel[i]);
            p.max = std::max(p.max, channel[i]);
        }
    return p;
}

TEST_CASE("peak_pyramid matches brute force for random ranges")
{
    std::mt19937 rng{42};
    std::uniform_real_distribution<float> sample{-1.0f, 1.0f};
    constexpr std::size_t FRAMES = 300'001;
    std::vector<std::vector<float>> channels(2, std::vector<float>(FRAMES));
    for (auto &channel : channels)
        for (float &x : channel)
            x = sample(rng);

    // Uneven chunks, like a capture.
    peak_pyramid pyramid;
    std::uniform_int_distribution<std::size_t> chunk{1, 5000};
    std::size_t fed = 0;
    while (fed < FRAMES)
    {
        const std::size_t n = std::min(chunk(rng), FRAMES - fed);
        const float *ptrs[2] = {channels[0].data() + fed, channels[1].data() + fed};
        pyramid.append(ptrs, 2, n);
        fed += n;
    }
    REQUIRE(pyramid.frames() == FRAMES);
    REQUIRE(pyramid.level_count() > 3);

    std::uniform_int_distribution<std::size_t> position{0, FRAMES + 1000};
    for (int i = 0; i < 2000; ++i)
    {
        std::size_t a = position(rng);
        std::size_t b = position(rng);
        if (a > b)
            std::swap(a, b);
        const peak expected = reference(channels, a, b);
        const peak got = pyramid.range(a, b);
        INFO("range [" << a << ", " << b << ")");
        REQUIRE(got.min == expected.min);
        REQUIRE(got.max == expected.max);
    }
}

TEST_CASE("peak_pyramid handles empty input and empty ranges")
{
    peak_pyramid pyramid;
    REQUIRE(pyramid.range(0, 100).max == 0.0f);
    const float x[3] = {0.5f, -0.25f, 0.1f};
    const float *ptr = x;
    pyramid.append(&ptr, 1, 3);
    REQUIRE(pyramid.range(2, 2).max == 0.0f);
    const peak p = pyramid.range(0, 3);
    REQUIRE(p.min == -0.25f);
    REQUIRE(p.max == 0.5f);
}
