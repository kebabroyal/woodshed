#include "woodshed/peak_pyramid.hpp"

#include <algorithm>
#include <limits>

namespace woodshed
{

static constexpr peak EMPTY_PEAK{std::numeric_limits<float>::infinity(),
                                 -std::numeric_limits<float>::infinity()};

static peak merge(peak a, peak b)
{
    return {std::min(a.min, b.min), std::max(a.max, b.max)};
}

std::size_t peak_pyramid::frames_per_bin(std::size_t level)
{
    std::size_t n = BASE_BIN;
    for (std::size_t i = 0; i < level; ++i)
        n *= FAN_IN;
    return n;
}

void peak_pyramid::append(const float *const *channels, std::size_t channel_count,
                          std::size_t frames)
{
    if (frames == 0 || channel_count == 0)
        return;
    if (levels_.empty())
        levels_.emplace_back();

    std::size_t done = 0;
    while (done < frames)
    {
        // propagate() may reallocate levels_.
        std::vector<peak> &base = levels_[0];
        const std::size_t offset = frames_ % BASE_BIN;
        if (offset == 0)
            base.push_back(EMPTY_PEAK);
        const std::size_t n = std::min(frames - done, BASE_BIN - offset);
        peak p = base.back();
        for (std::size_t c = 0; c < channel_count; ++c)
        {
            const auto [lo, hi] = std::minmax_element(channels[c] + done, channels[c] + done + n);
            p = merge(p, {*lo, *hi});
        }
        base.back() = p;
        frames_ += n;
        done += n;
        propagate(base.size() - 1);
    }
}

void peak_pyramid::propagate(std::size_t bin)
{
    for (std::size_t level = 1;; ++level)
    {
        if (levels_[level - 1].size() <= 1)
            break;
        if (level == levels_.size())
            levels_.emplace_back();
        const std::vector<peak> &below = levels_[level - 1];
        const std::size_t parent = bin / FAN_IN;
        const std::size_t first = parent * FAN_IN;
        const std::size_t last = std::min(first + FAN_IN, below.size());
        peak p = EMPTY_PEAK;
        for (std::size_t i = first; i < last; ++i)
            p = merge(p, below[i]);
        std::vector<peak> &here = levels_[level];
        if (parent == here.size())
            here.push_back(p);
        else
            here[parent] = p;
        bin = parent;
    }
}

peak peak_pyramid::range(std::size_t begin, std::size_t end) const
{
    end = std::min(end, frames_);
    if (begin >= end)
        return {0.0f, 0.0f};

    std::size_t b0 = begin / BASE_BIN;
    std::size_t b1 = (end + BASE_BIN - 1) / BASE_BIN;
    peak p = EMPTY_PEAK;
    for (std::size_t level = 0; b0 < b1; ++level)
    {
        const std::vector<peak> &bins = levels_[level];
        if (level + 1 == levels_.size() || b1 - b0 < FAN_IN)
        {
            for (std::size_t i = b0; i < b1; ++i)
                p = merge(p, bins[i]);
            break;
        }
        while (b0 % FAN_IN != 0 && b0 < b1)
            p = merge(p, bins[b0++]);
        while (b1 % FAN_IN != 0 && b1 > b0 && b1 != bins.size())
            p = merge(p, bins[--b1]);
        b0 /= FAN_IN;
        b1 = (b1 + FAN_IN - 1) / FAN_IN;
    }
    return p;
}

}
