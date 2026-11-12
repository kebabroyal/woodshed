// SPDX-FileCopyrightText: 2026 kebabroyal
// SPDX-License-Identifier: GPL-3.0-or-later

#include "woodshed/spectrogram.hpp"

#include <catch2/catch.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace woodshed;

static audio_take tones(std::initializer_list<double> frequencies, float amplitude, std::size_t frames)
{
    std::vector<float> data(2 * frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        float x = 0.0f;
        for (const double f : frequencies)
            x += amplitude * static_cast<float>(
                     std::sin(2.0 * std::numbers::pi * f * static_cast<double>(i) / SAMPLE_RATE));
        data[2 * i] = data[2 * i + 1] = x;
    }
    audio_take take;
    take.append_interleaved(data.data(), frames);
    return take;
}

static double loudest_note(const spectrogram &s, std::size_t index)
{
    const std::uint8_t *column = s.column(index);
    return spectrogram::row_note(std::max_element(column, column + spectrogram::ROWS) - column);
}

static double note_of(double frequency)
{
    return 69.0 + 12.0 * std::log2(frequency / 440.0);
}

TEST_CASE("spectrogram finds the note in every register")
{
    for (const bool reassign : {false, true})
        for (const double frequency : {41.2, 110.0, 261.63, 440.0, 1046.5, 3520.0})
        {
            const audio_take take = tones({frequency}, 0.5f, 48000);
            spectrogram s{reassign};
            s.update(take, true, 1000);
            INFO("reassign " << reassign << ", " << frequency << " Hz");
            // Plain: bins are wider than a semitone below ~100 Hz.
            const double tolerance = reassign ? 0.06 : frequency < 100.0 ? 0.6 : 0.15;
            CHECK(std::abs(loudest_note(s, 20) - note_of(frequency)) < tolerance);
        }
}

TEST_CASE("spectrogram level near 0 dB for a full-scale sine")
{
    for (const bool reassign : {false, true})
    {
        const audio_take take = tones({440.0}, 1.0f, 48000);
        spectrogram s{reassign};
        s.update(take, true, 1000);
        const float db = s.loudest() / spectrogram::STEPS_PER_DB + spectrogram::DB_FLOOR;
        INFO("reassign " << reassign);
        CHECK(std::abs(db) < 1.5f);
    }
}

TEST_CASE("spectrogram column count")
{
    const audio_take take = tones({440.0}, 0.5f, 48000);
    spectrogram s{false};
    CHECK(s.update(take, false, 1000) == (48000 - 8192) / spectrogram::HOP + 1);
    CHECK(s.update(take, false, 1000) == 0);
    s.update(take, true, 1000);
    CHECK(s.columns() == (48000 + spectrogram::HOP - 1) / spectrogram::HOP);
}

TEST_CASE("spectrogram chunks")
{
    const audio_take take = tones({220.0}, 0.5f, 20000);
    spectrogram s{true};
    while (s.update(take, true, 3) > 0)
    {
    }
    spectrogram whole{true};
    whole.update(take, true, 1000);
    REQUIRE(s.columns() == whole.columns());
    CHECK(std::equal(s.column(0), s.column(0) + s.columns() * spectrogram::ROWS, whole.column(0)));
}
