// SPDX-FileCopyrightText: 2026 kebabroyal
// SPDX-License-Identifier: GPL-3.0-or-later

#include "woodshed/playback_feed.hpp"

#include <catch2/catch.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace woodshed;

static std::shared_ptr<audio_take> tone(double frequency, std::size_t frames)
{
    std::vector<float> data(2 * frames);
    for (std::size_t i = 0; i < frames; ++i)
        data[2 * i] = data[2 * i + 1] = 0.5f * static_cast<float>(std::sin(
            2.0 * std::numbers::pi * frequency * static_cast<double>(i) / SAMPLE_RATE));
    auto take = std::make_shared<audio_take>();
    take->append_interleaved(data.data(), frames);
    return take;
}

struct played
{
    std::vector<float> left;
    std::vector<std::size_t> positions;
};

static played play(playback_feed &feed, loop_region loop, std::size_t max_chunks)
{
    played out;
    float chunk[2 * playback_feed::CHUNK];
    std::size_t position = 0;
    for (std::size_t c = 0; c < max_chunks && feed.next(loop, chunk, position); ++c)
    {
        for (std::size_t i = 0; i < playback_feed::CHUNK; ++i)
            out.left.push_back(chunk[2 * i]);
        out.positions.push_back(position);
    }
    return out;
}

// Upward zero crossings per second, over the middle half.
static double frequency_of(const std::vector<float> &x)
{
    const std::size_t begin = x.size() / 4;
    const std::size_t end = 3 * x.size() / 4;
    int crossings = 0;
    for (std::size_t i = begin + 1; i < end; ++i)
        crossings += x[i - 1] < 0.0f && x[i] >= 0.0f;
    return crossings * SAMPLE_RATE / static_cast<double>(end - begin);
}

TEST_CASE("feed at 1 is the take")
{
    const auto take = tone(440.0, 10000);
    playback_feed feed{take, 0, 1.0, 1.0};
    const played out = play(feed, {}, 1000);
    REQUIRE(out.left.size() >= take->frames());
    for (std::size_t i = 0; i < take->frames(); ++i)
        REQUIRE(out.left[i] == take->left[i]);
    CHECK(out.positions[3] == 3 * playback_feed::CHUNK);
}

TEST_CASE("feed at half speed: twice as long, same pitch")
{
    const auto take = tone(440.0, 48000);
    playback_feed feed{take, 0, 0.5, 1.0};
    const played out = play(feed, {}, 10000);
    // Plus a silent tail.
    CHECK(out.left.size() >= 2 * take->frames() - playback_feed::CHUNK);
    CHECK(out.left.size() < 2 * take->frames() + 12000);
    CHECK(std::abs(frequency_of(out.left) - 440.0) < 5.0);
    // Positions advance at half rate.
    const std::size_t at = out.positions[48000 / playback_feed::CHUNK];
    CHECK(std::abs(static_cast<double>(at) - 24000.0) < 512.0);
}

TEST_CASE("feed loops inside the region while slowed")
{
    const auto take = tone(330.0, 96000);
    const loop_region loop{24000, 36000, true};
    playback_feed feed{take, 24000, 0.6, 1.0};
    const played out = play(feed, loop, 2000);
    REQUIRE(out.positions.size() == 2000);
    for (const std::size_t p : out.positions)
    {
        REQUIRE(p >= loop.start);
        REQUIRE(p < loop.end);
    }
    CHECK(std::abs(frequency_of(out.left) - 330.0) < 5.0);
}

TEST_CASE("feed switches engine mid-play")
{
    const auto take = tone(440.0, 96000);
    playback_feed feed{take, 0, 1.0, 1.0};
    play(feed, {}, 50);
    feed.set_rate(0.75, 1.0);
    const played slow = play(feed, {}, 50);
    CHECK(slow.positions.front() > 50 * playback_feed::CHUNK - playback_feed::BLOCK);
    CHECK(slow.positions.back() > slow.positions.front());
}

TEST_CASE("feed transposes, keeps time")
{
    const auto take = tone(440.0, 48000);
    const double up = std::exp2(2.0 / 12.0);
    playback_feed feed{take, 0, 1.0, up};
    const played out = play(feed, {}, 10000);
    CHECK(out.left.size() < take->frames() + 12000);
    CHECK(std::abs(frequency_of(out.left) - 440.0 * up) < 5.0);
    const std::size_t at = out.positions[24000 / playback_feed::CHUNK];
    CHECK(std::abs(static_cast<double>(at) - 24000.0) < 512.0);
}

TEST_CASE("feed slows and transposes together")
{
    const auto take = tone(440.0, 48000);
    const double down = std::exp2(-0.32 / 12.0); // -32 cents
    playback_feed feed{take, 0, 0.5, down};
    const played out = play(feed, {}, 10000);
    CHECK(out.left.size() >= 2 * take->frames() - playback_feed::CHUNK);
    CHECK(std::abs(frequency_of(out.left) - 440.0 * down) < 3.0);
}
