// SPDX-FileCopyrightText: 2026 kebabroyal
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WOODSHED_RENDER_HPP
#define WOODSHED_RENDER_HPP

#include "woodshed/audio_take.hpp"

#include <cstddef>

namespace woodshed
{

constexpr std::size_t CROSSFADE_FRAMES = 256;

struct loop_region
{
    std::size_t start = 0;
    std::size_t end = 0;
    bool enabled = false;
};

struct render_result
{
    std::size_t position;
    std::size_t frames;
};

// Interleaved stereo out. Fewer frames than asked: the take ended.
render_result render(const audio_take &take, std::size_t position, loop_region loop, float *out,
                     std::size_t frames);

}

#endif
