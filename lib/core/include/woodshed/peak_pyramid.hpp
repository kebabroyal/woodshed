// SPDX-FileCopyrightText: 2026 kebabroyal
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WOODSHED_PEAK_PYRAMID_HPP
#define WOODSHED_PEAK_PYRAMID_HPP

#include <cstddef>
#include <vector>

namespace woodshed
{

struct peak
{
    float min;
    float max;
};

// Waveform min/max at every zoom level. Channels folded together.
class peak_pyramid
{
public:
    static constexpr std::size_t BASE_BIN = 256;
    static constexpr std::size_t FAN_IN = 4;

    void append(const float *const *channels, std::size_t channel_count, std::size_t frames);

    std::size_t frames() const { return frames_; }
    std::size_t level_count() const { return levels_.size(); }
    const std::vector<peak> &level(std::size_t index) const { return levels_[index]; }
    static std::size_t frames_per_bin(std::size_t level);

    // Over every bin touching [begin, end); {0, 0} if empty.
    peak range(std::size_t begin, std::size_t end) const;

private:
    void propagate(std::size_t level0_bin);

    std::vector<std::vector<peak>> levels_;
    std::size_t frames_ = 0;
};

}

#endif
