// SPDX-FileCopyrightText: 2026 kebabroyal
// SPDX-License-Identifier: GPL-3.0-or-later

#include "woodshed/playback_feed.hpp"

#include <algorithm>

namespace woodshed
{

static bool untouched(double speed, double pitch)
{
    return speed == 1.0 && pitch == 1.0;
}

static std::unique_ptr<time_stretcher> stretcher_for(double speed, double pitch)
{
    return untouched(speed, pitch) ? make_passthrough()
                                   : make_rubberband(speed, pitch, playback_feed::BLOCK);
}

playback_feed::playback_feed(std::shared_ptr<const audio_take> take, std::size_t position,
                             double speed, double pitch)
    : take_{std::move(take)},
      stretcher_{stretcher_for(speed, pitch)},
      speed_{speed},
      pitch_{pitch},
      position_{position},
      interleaved_(2 * BLOCK),
      planar_{std::vector<float>(BLOCK), std::vector<float>(BLOCK)}
{
}

void playback_feed::seek(std::size_t position)
{
    stretcher_->reset();
    position_ = position;
    input_done_ = false;
    input_count_ = cursor_ = 0.0;
    segments_.clear();
}

void playback_feed::set_rate(double speed, double pitch)
{
    if (speed == speed_ && pitch == pitch_)
        return;
    // Pass-through when untouched: switching engines restarts from what is heard.
    const bool same_engine = untouched(speed, pitch) == untouched(speed_, pitch_);
    speed_ = speed;
    pitch_ = pitch;
    if (same_engine)
    {
        stretcher_->set_speed(speed);
        stretcher_->set_pitch(pitch);
        return;
    }
    const std::size_t at = heard();
    stretcher_ = stretcher_for(speed, pitch);
    seek(at);
}

void playback_feed::feed(loop_region loop)
{
    const std::size_t total = take_->frames();
    const std::size_t loop_end = std::min(loop.end, total);
    const bool looping = loop.enabled && loop_end > loop.start;
    if (looping && (position_ < loop.start || position_ >= loop_end))
        position_ = loop.start;
    const std::size_t limit = (looping ? loop_end : total) - std::min(position_, total);
    if (limit == 0)
    {
        stretcher_->push(nullptr, 0, true);
        input_done_ = true;
        return;
    }
    const std::size_t n = std::min(BLOCK, limit);
    render(*take_, position_, loop, interleaved_.data(), n);
    for (std::size_t i = 0; i < n; ++i)
    {
        planar_[0][i] = interleaved_[2 * i];
        planar_[1][i] = interleaved_[2 * i + 1];
    }
    const float *input[2] = {planar_[0].data(), planar_[1].data()};
    stretcher_->push(input, n, false);
    segments_.push_back({input_count_, position_, n});
    input_count_ += static_cast<double>(n);
    position_ += n;
}

// Take position of the input now leaving the stretcher.
std::size_t playback_feed::heard()
{
    while (segments_.size() > 1 && segments_[1].input <= cursor_)
        segments_.pop_front();
    if (segments_.empty())
        return position_;
    const segment &s = segments_.front();
    const double offset = std::clamp(cursor_ - s.input, 0.0, static_cast<double>(s.frames));
    return s.position + static_cast<std::size_t>(offset);
}

bool playback_feed::next(loop_region loop, float *out, std::size_t &position)
{
    while (stretcher_->available() < CHUNK && !input_done_)
        feed(loop);
    if (stretcher_->available() == 0)
        return false;

    float left[CHUNK];
    float right[CHUNK];
    float *output[2] = {left, right};
    const std::size_t got = stretcher_->pull(output, std::min(CHUNK, stretcher_->available()));
    for (std::size_t i = 0; i < CHUNK; ++i)
    {
        out[2 * i] = i < got ? left[i] : 0.0f;
        out[2 * i + 1] = i < got ? right[i] : 0.0f;
    }
    position = heard();
    cursor_ += static_cast<double>(got) * speed_;
    return true;
}

}
