#ifndef WOODSHED_AUDIO_TAKE_HPP
#define WOODSHED_AUDIO_TAKE_HPP

#include "woodshed/peak_pyramid.hpp"

#include <cstddef>
#include <vector>

namespace woodshed
{

constexpr double SAMPLE_RATE = 48000.0;

struct audio_take
{
    std::vector<float> left;
    std::vector<float> right;
    peak_pyramid peaks;

    std::size_t frames() const { return left.size(); }
    double duration() const { return static_cast<double>(frames()) / SAMPLE_RATE; }
    void append_interleaved(const float *data, std::size_t frames);
};

}

#endif
