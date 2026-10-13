#include "woodshed/tuning.hpp"

#include <fftw3.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace woodshed
{

static constexpr std::size_t SIZE = 8192;
static constexpr std::size_t HOP = 4096;
static constexpr double LOW_HZ = 80.0;
static constexpr double HIGH_HZ = 2000.0;
static constexpr float PEAK_RANGE = 1e-3f; // 30 dB under the frame's loudest
static constexpr std::size_t MIN_PEAKS = 20;
static constexpr double MIN_AGREEMENT = 0.5; // mean resultant length; noise gives about 1/sqrt(n)
static constexpr double TAU = 2.0 * std::numbers::pi;

double detect_tuning(const audio_take &take)
{
    if (take.frames() < SIZE)
        return 0.0;
    float *input = fftwf_alloc_real(SIZE);
    fftwf_complex *output = fftwf_alloc_complex(SIZE / 2 + 1);
    const fftwf_plan plan = fftwf_plan_dft_r2c_1d(SIZE, input, output, FFTW_ESTIMATE);

    std::vector<float> window(SIZE);
    for (std::size_t i = 0; i < SIZE; ++i)
        window[i] = static_cast<float>(0.5 - 0.5 * std::cos(TAU * i / SIZE));
    const auto first_bin = static_cast<std::size_t>(LOW_HZ * SIZE / SAMPLE_RATE);
    const auto end_bin = static_cast<std::size_t>(HIGH_HZ * SIZE / SAMPLE_RATE);
    std::vector<float> power(end_bin + 1);

    double sum_cos = 0.0;
    double sum_sin = 0.0;
    std::size_t peaks = 0;
    for (std::size_t start = 0; start + SIZE <= take.frames(); start += HOP)
    {
        for (std::size_t i = 0; i < SIZE; ++i)
            input[i] = 0.5f * (take.left[start + i] + take.right[start + i]) * window[i];
        fftwf_execute(plan);
        for (std::size_t k = first_bin - 1; k <= end_bin; ++k)
            power[k] = output[k][0] * output[k][0] + output[k][1] * output[k][1];
        const float loudest = *std::max_element(power.begin() + first_bin, power.end());
        if (loudest <= 0.0f)
            continue;

        for (std::size_t k = first_bin; k < end_bin; ++k)
        {
            const float p = power[k];
            if (p < loudest * PEAK_RANGE || p < power[k - 1] || p <= power[k + 1])
                continue;
            // Parabola through log powers: peak frequency within a bin.
            const double a = std::log(power[k - 1] + 1e-30f);
            const double m = std::log(p);
            const double c = std::log(power[k + 1] + 1e-30f);
            const double curvature = a - 2.0 * m + c;
            if (!(curvature < 0.0))
                continue;
            const double bin = k + 0.5 * (a - c) / curvature;
            const double note = 69.0 + 12.0 * std::log2(bin * SAMPLE_RATE / SIZE / 440.0);
            sum_cos += std::cos(TAU * note);
            sum_sin += std::sin(TAU * note);
            ++peaks;
        }
    }

    fftwf_destroy_plan(plan);
    fftwf_free(output);
    fftwf_free(input);
    if (peaks < MIN_PEAKS || std::hypot(sum_cos, sum_sin) / peaks < MIN_AGREEMENT)
        return 0.0;
    return std::atan2(sum_sin, sum_cos) / TAU * 100.0;
}

}
