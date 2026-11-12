// SPDX-FileCopyrightText: 2026 kebabroyal
// SPDX-License-Identifier: GPL-3.0-or-later

#include "woodshed/render.hpp"

#include <algorithm>

namespace woodshed
{

render_result render(const audio_take &take, std::size_t position, loop_region loop, float *out,
                     std::size_t frames)
{
    const std::size_t total = take.frames();
    const std::size_t loop_end = std::min(loop.end, total);
    const bool looping = loop.enabled && loop_end > loop.start;
    const std::size_t start = looping ? loop.start : 0;
    const std::size_t end = looping ? loop_end : total;
    const std::size_t fade = looping ? std::min(CROSSFADE_FRAMES, (end - start) / 4) : 0;
    if (looping && (position < start || position >= end))
        position = start;

    std::size_t i = 0;
    for (; i < frames; ++i)
    {
        if (position >= end)
        {
            if (!looping)
                break;
            position = start;
        }
        float l = take.left[position];
        float r = take.right[position];
        // Tail fades into what precedes the loop start.
        if (position + fade >= end)
        {
            const std::size_t k = position + fade - end;
            const float g = (static_cast<float>(k) + 0.5f) / static_cast<float>(fade);
            const bool before_take = start + k < fade;
            const std::size_t src = before_take ? 0 : start + k - fade;
            const float l2 = before_take ? 0.0f : take.left[src];
            const float r2 = before_take ? 0.0f : take.right[src];
            l = l * (1.0f - g) + l2 * g;
            r = r * (1.0f - g) + r2 * g;
        }
        out[2 * i] = l;
        out[2 * i + 1] = r;
        ++position;
    }
    return {position, i};
}

}
