// SPDX-FileCopyrightText: 2026 kebabroyal
// SPDX-License-Identifier: GPL-3.0-or-later

#include "woodshed/render.hpp"

#include <catch2/catch.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace woodshed;

static audio_take sine_take(std::size_t frames, double frequency)
{
    std::vector<float> data(2 * frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        const auto x = static_cast<float>(
            std::sin(2.0 * std::numbers::pi * frequency * static_cast<double>(i) / SAMPLE_RATE));
        data[2 * i] = x;
        data[2 * i + 1] = x;
    }
    audio_take take;
    take.append_interleaved(data.data(), frames);
    return take;
}

static float max_step(const std::vector<float> &out, std::size_t frames)
{
    float step = 0.0f;
    for (std::size_t i = 1; i < frames; ++i)
        step = std::max(step, std::abs(out[2 * i] - out[2 * (i - 1)]));
    return step;
}

TEST_CASE("render stops at the end without a loop")
{
    const audio_take take = sine_take(1000, 440.0);
    std::vector<float> out(2 * 600);
    const render_result first = render(take, 0, {}, out.data(), 600);
    REQUIRE(first.frames == 600);
    const render_result second = render(take, first.position, {}, out.data(), 600);
    REQUIRE(second.frames == 400);
    REQUIRE(second.position == 1000);
}

TEST_CASE("render loops without clicks")
{
    const audio_take take = sine_take(48000, 440.0);
    // Odd boundaries: phases mismatch at the wrap.
    const loop_region loop{10000, 20037, true};
    constexpr std::size_t FRAMES = 50000;
    std::vector<float> out(2 * FRAMES);
    const render_result result = render(take, 0, loop, out.data(), FRAMES);
    REQUIRE(result.frames == FRAMES);
    REQUIRE(result.position >= loop.start);
    REQUIRE(result.position < loop.end);

    const float natural = static_cast<float>(2.0 * std::numbers::pi * 440.0 / SAMPLE_RATE);
    REQUIRE(max_step(out, FRAMES) < 2.0f * natural);
    REQUIRE(out[0] == take.left[loop.start]);
}

TEST_CASE("render jumps into the loop from outside")
{
    const audio_take take = sine_take(4000, 440.0);
    std::vector<float> out(2);
    const render_result result = render(take, 3500, {1000, 2000, true}, out.data(), 1);
    REQUIRE(result.position == 1001);
}

TEST_CASE("loop starting at zero fades from silence")
{
    const audio_take take = sine_take(4000, 440.0);
    std::vector<float> out(2 * 3000);
    const render_result result = render(take, 0, {0, 2000, true}, out.data(), 3000);
    REQUIRE(result.frames == 3000);
    REQUIRE(result.position == 1000);
}
