// SPDX-FileCopyrightText: 2026 kebabroyal
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WOODSHED_PLAYBACK_FEED_HPP
#define WOODSHED_PLAYBACK_FEED_HPP

#include "woodshed/audio_take.hpp"
#include "woodshed/render.hpp"
#include "woodshed/time_stretcher.hpp"

#include <cstddef>
#include <deque>
#include <memory>
#include <vector>

namespace woodshed
{

// The take, looped, stretched and shifted, in fixed chunks. No threads.
class playback_feed
{
public:
    static constexpr std::size_t CHUNK = 256;
    static constexpr std::size_t BLOCK = 1024;

    playback_feed(std::shared_ptr<const audio_take> take, std::size_t position, double speed,
                  double pitch);

    void seek(std::size_t position);
    // Pitch: frequency ratio.
    void set_rate(double speed, double pitch);

    // CHUNK interleaved frames into out, and the take position they start at.
    // False once the take has ended and everything is out.
    bool next(loop_region loop, float *out, std::size_t &position);

private:
    struct segment
    {
        double input;
        std::size_t position;
        std::size_t frames;
    };

    void feed(loop_region loop);
    std::size_t heard();

    std::shared_ptr<const audio_take> take_;
    std::unique_ptr<time_stretcher> stretcher_;
    double speed_;
    double pitch_;
    std::size_t position_;
    bool input_done_ = false;
    double input_count_ = 0.0;
    double cursor_ = 0.0;
    std::deque<segment> segments_;
    std::vector<float> interleaved_;
    std::vector<float> planar_[2];
};

}

#endif
