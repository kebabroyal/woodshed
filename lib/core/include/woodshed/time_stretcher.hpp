// SPDX-FileCopyrightText: 2026 kebabroyal
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WOODSHED_TIME_STRETCHER_HPP
#define WOODSHED_TIME_STRETCHER_HPP

#include <cstddef>
#include <memory>

namespace woodshed
{

// Changes speed and pitch independently. Planar stereo. One thread only.
// After reset, output lines up with the next input: no start delay.
class time_stretcher
{
public:
    virtual ~time_stretcher() = default;
    virtual void set_speed(double speed) = 0;
    // Frequency ratio: 2 is an octave up.
    virtual void set_pitch(double scale) = 0;
    virtual void reset() = 0;
    virtual void push(const float *const *input, std::size_t frames, bool final) = 0;
    virtual std::size_t available() const = 0;
    virtual std::size_t pull(float *const *output, std::size_t frames) = 0;
};

std::unique_ptr<time_stretcher> make_passthrough();
// Rubber Band R3.
std::unique_ptr<time_stretcher> make_rubberband(double speed, double pitch, std::size_t max_block);

}

#endif
