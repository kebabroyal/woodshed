#include "woodshed/audio_take.hpp"

namespace woodshed
{

void audio_take::append_interleaved(const float *data, std::size_t frames)
{
    const std::size_t first = left.size();
    left.resize(first + frames);
    right.resize(first + frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        left[first + i] = data[2 * i];
        right[first + i] = data[2 * i + 1];
    }
    const float *channels[2] = {left.data() + first, right.data() + first};
    peaks.append(channels, 2, frames);
}

}
