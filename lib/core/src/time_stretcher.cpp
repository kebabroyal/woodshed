// SPDX-FileCopyrightText: 2026 kebabroyal
// SPDX-License-Identifier: GPL-3.0-or-later

#include "woodshed/time_stretcher.hpp"
#include "woodshed/audio_take.hpp"

#include <rubberband/RubberBandStretcher.h>

#include <algorithm>
#include <vector>

namespace woodshed
{

class passthrough final : public time_stretcher
{
public:
    void set_speed(double) override {}
    void set_pitch(double) override {}

    void reset() override
    {
        for (auto &channel : buffer_)
            channel.clear();
        read_ = 0;
    }

    void push(const float *const *input, std::size_t frames, bool) override
    {
        if (frames == 0)
            return;
        if (read_ > 0)
        {
            for (auto &channel : buffer_)
                channel.erase(channel.begin(), channel.begin() + read_);
            read_ = 0;
        }
        for (std::size_t c = 0; c < 2; ++c)
            buffer_[c].insert(buffer_[c].end(), input[c], input[c] + frames);
    }

    std::size_t available() const override { return buffer_[0].size() - read_; }

    std::size_t pull(float *const *output, std::size_t frames) override
    {
        frames = std::min(frames, available());
        for (std::size_t c = 0; c < 2; ++c)
            std::copy_n(buffer_[c].begin() + read_, frames, output[c]);
        read_ += frames;
        return frames;
    }

private:
    std::vector<float> buffer_[2];
    std::size_t read_ = 0;
};

class rubberband final : public time_stretcher
{
public:
    rubberband(double speed, double pitch, std::size_t max_block)
        : stretcher_{static_cast<std::size_t>(SAMPLE_RATE), 2,
                     RubberBand::RubberBandStretcher::OptionProcessRealTime |
                         RubberBand::RubberBandStretcher::OptionEngineFiner |
                         RubberBand::RubberBandStretcher::OptionChannelsTogether |
                         RubberBand::RubberBandStretcher::OptionPitchHighQuality,
                     1.0 / speed, pitch},
          max_block_{max_block},
          scratch_{std::vector<float>(max_block), std::vector<float>(max_block)}
    {
        stretcher_.setMaxProcessSize(max_block);
        reset();
    }

    void set_speed(double speed) override { stretcher_.setTimeRatio(1.0 / speed); }
    void set_pitch(double scale) override { stretcher_.setPitchScale(scale); }

    void reset() override
    {
        stretcher_.reset();
        std::fill(scratch_[0].begin(), scratch_[0].end(), 0.0f);
        std::fill(scratch_[1].begin(), scratch_[1].end(), 0.0f);
        const float *silence[2] = {scratch_[0].data(), scratch_[1].data()};
        for (std::size_t pad = stretcher_.getPreferredStartPad(); pad > 0;)
        {
            const std::size_t n = std::min(pad, max_block_);
            stretcher_.process(silence, n, false);
            pad -= n;
        }
        delay_ = stretcher_.getStartDelay();
    }

    void push(const float *const *input, std::size_t frames, bool final) override
    {
        const float *none[2] = {nullptr, nullptr};
        stretcher_.process(frames ? input : none, frames, final);
    }

    std::size_t available() const override
    {
        const int n = stretcher_.available();
        return n > 0 && static_cast<std::size_t>(n) > delay_ ? n - delay_ : 0;
    }

    std::size_t pull(float *const *output, std::size_t frames) override
    {
        while (delay_ > 0 && stretcher_.available() > 0)
        {
            float *trash[2] = {scratch_[0].data(), scratch_[1].data()};
            delay_ -= stretcher_.retrieve(trash, std::min(delay_, max_block_));
        }
        return delay_ > 0 ? 0 : stretcher_.retrieve(output, frames);
    }

private:
    RubberBand::RubberBandStretcher stretcher_;
    std::size_t max_block_;
    std::vector<float> scratch_[2];
    std::size_t delay_ = 0;
};

std::unique_ptr<time_stretcher> make_passthrough()
{
    return std::make_unique<passthrough>();
}

std::unique_ptr<time_stretcher> make_rubberband(double speed, double pitch, std::size_t max_block)
{
    return std::make_unique<rubberband>(speed, pitch, max_block);
}

}
