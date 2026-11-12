// SPDX-FileCopyrightText: 2026 kebabroyal
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WOODSHED_SPECTROGRAM_HPP
#define WOODSHED_SPECTROGRAM_HPP

#include "woodshed/audio_take.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace woodshed
{

// Log-frequency magnitudes, A0 to C8, rows on an A440 equal-tempered grid.
// Longer windows for lower notes. Values: 0.5 dB steps above DB_FLOOR.
class spectrogram
{
public:
    static constexpr std::size_t HOP = 1024;
    static constexpr int LOWEST_NOTE = 21;
    static constexpr int HIGHEST_NOTE = 108;
    static constexpr int ROWS_PER_NOTE = 9; // odd: a row centred on each note
    static constexpr std::size_t ROWS = (HIGHEST_NOTE - LOWEST_NOTE + 1) * ROWS_PER_NOTE;
    static constexpr float DB_FLOOR = -120.0f;
    static constexpr float STEPS_PER_DB = 2.0f;

    // reassign: move energy to each bin's measured frequency. Sharper lines.
    explicit spectrogram(bool reassign);
    ~spectrogram();
    spectrogram(const spectrogram &) = delete;
    spectrogram &operator=(const spectrogram &) = delete;

    // Adds up to max_columns. Without final, waits for full windows.
    std::size_t update(const audio_take &take, bool final, std::size_t max_columns);
    void clear();

    std::size_t columns() const { return values_.size() / ROWS; }
    // Row 0 is the lowest.
    const std::uint8_t *column(std::size_t index) const { return values_.data() + index * ROWS; }
    std::uint8_t loudest() const { return loudest_; }

    static double column_time(std::size_t index) { return index * HOP / SAMPLE_RATE; }
    // MIDI note at the centre of a row.
    static double row_note(std::size_t row);

private:
    struct band;

    void analyze(const audio_take &take, std::size_t index);
    std::size_t lookahead() const;

    bool reassign_;
    std::vector<std::unique_ptr<band>> bands_;
    std::vector<float> row_power_;
    std::vector<float> focus_power_;
    std::vector<std::uint8_t> focused_;
    std::vector<std::uint8_t> values_;
    std::uint8_t loudest_ = 0;
};

}

#endif
