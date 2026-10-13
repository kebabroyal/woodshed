#include "woodshed/tuning.hpp"

#include <catch2/catch.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace woodshed;

static audio_take chord(double a4, std::size_t frames)
{
    // A major triad, an octave down, with two harmonics.
    const double notes[] = {0.0, 4.0, 7.0};
    std::vector<float> data(2 * frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        double x = 0.0;
        for (const double n : notes)
            for (int h = 1; h <= 3; ++h)
                x += 0.1 / h * std::sin(2.0 * std::numbers::pi * h * a4 / 2 * std::exp2(n / 12.0) *
                                        static_cast<double>(i) / SAMPLE_RATE);
        data[2 * i] = data[2 * i + 1] = static_cast<float>(x);
    }
    audio_take take;
    take.append_interleaved(data.data(), frames);
    return take;
}

TEST_CASE("tuning detected from a chord")
{
    for (const double a4 : {440.0, 432.0, 446.0, 452.0})
    {
        INFO(a4 << " Hz");
        const double cents = 1200.0 * std::log2(a4 / 440.0);
        const double expected = cents - 100.0 * std::round(cents / 100.0);
        CHECK(std::abs(detect_tuning(chord(a4, 5 * 48000)) - expected) < 3.0);
    }
}

TEST_CASE("tuning of noise is 0")
{
    std::vector<float> data(2 * 5 * 48000);
    std::uint32_t seed = 1;
    for (float &x : data)
    {
        seed = seed * 1664525u + 1013904223u;
        x = static_cast<float>(seed >> 8) / 16777216.0f - 0.5f;
    }
    audio_take take;
    take.append_interleaved(data.data(), data.size() / 2);
    CHECK(detect_tuning(take) == 0.0);
}

TEST_CASE("tuning of silence is 0")
{
    CHECK(detect_tuning(chord(432.0, 4000)) == 0.0);
    audio_take silent;
    const std::vector<float> zeros(2 * 48000);
    silent.append_interleaved(zeros.data(), 48000);
    CHECK(detect_tuning(silent) == 0.0);
}
