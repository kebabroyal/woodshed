#include "woodshed/spectrogram.hpp"

#include <fftw3.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace woodshed
{

static constexpr double TAU = 2.0 * std::numbers::pi;
static constexpr double MAX_SHIFT_BINS = 1.5; // farther: transient or noise, dropped
static constexpr float BACKDROP = 0.016f;     // plain view under focused lines: -18 dB
static constexpr std::uint8_t SPREAD_STEPS = 6; // focused lines: neighbours 3 dB down

struct band_spec
{
    std::size_t size;
    int top_note;
};

// Window per register: about 3 bins per semitone at the bottom of each.
static constexpr band_spec BANDS[] = {{16384, 50}, {8192, 62}, {4096, 74}, {2048, 108}};

static double note_frequency(double note)
{
    return 440.0 * std::exp2((note - 69.0) / 12.0);
}

static double frequency_note(double frequency)
{
    return 69.0 + 12.0 * std::log2(frequency / 440.0);
}

double spectrogram::row_note(std::size_t row)
{
    return LOWEST_NOTE - 0.5 + (row + 0.5) / ROWS_PER_NOTE;
}

static std::size_t note_row(double note)
{
    return static_cast<std::size_t>(std::floor((note - (spectrogram::LOWEST_NOTE - 0.5)) *
                                               spectrogram::ROWS_PER_NOTE));
}

struct spectrogram::band
{
    struct row_bins
    {
        std::size_t first;
        std::size_t last;
        double centre;
    };

    std::size_t size;
    std::size_t first_row;
    std::size_t end_row;
    std::size_t first_bin;
    std::size_t end_bin;
    std::vector<float> window;
    std::vector<float> slope; // window derivative, per sample
    float power_scale;
    std::vector<row_bins> rows;
    float *input;
    fftwf_complex *output;
    fftwf_plan plan;
    float *slope_input = nullptr;
    fftwf_complex *slope_output = nullptr;
    fftwf_plan slope_plan = nullptr;

    band(std::size_t size_, std::size_t first_row_, std::size_t end_row_, bool reassign)
        : size{size_},
          first_row{first_row_},
          end_row{end_row_},
          window(size),
          slope(size),
          input{fftwf_alloc_real(size)},
          output{fftwf_alloc_complex(size / 2 + 1)},
          plan{fftwf_plan_dft_r2c_1d(static_cast<int>(size), input, output, FFTW_ESTIMATE)}
    {
        double sum = 0.0;
        for (std::size_t i = 0; i < size; ++i)
        {
            const double phase = TAU * i / size;
            window[i] = static_cast<float>(0.5 - 0.5 * std::cos(phase));
            slope[i] = static_cast<float>(0.5 * TAU / size * std::sin(phase));
            sum += window[i];
        }
        // A full-scale sine reads 0 dB.
        power_scale = static_cast<float>(4.0 / (sum * sum));

        const double bins_per_hz = size / SAMPLE_RATE;
        const double half_row = 0.5 / ROWS_PER_NOTE;
        for (std::size_t row = first_row; row < end_row; ++row)
        {
            const double note = row_note(row);
            rows.push_back({static_cast<std::size_t>(std::ceil(note_frequency(note - half_row) * bins_per_hz)),
                            static_cast<std::size_t>(std::floor(note_frequency(note + half_row) * bins_per_hz)),
                            note_frequency(note) * bins_per_hz});
        }
        first_bin = static_cast<std::size_t>(rows.front().centre) - 2;
        end_bin = std::min(size / 2, static_cast<std::size_t>(rows.back().centre) + 3);

        if (reassign)
        {
            slope_input = fftwf_alloc_real(size);
            slope_output = fftwf_alloc_complex(size / 2 + 1);
            slope_plan = fftwf_plan_dft_r2c_1d(static_cast<int>(size), slope_input, slope_output,
                                               FFTW_ESTIMATE);
        }
    }

    ~band()
    {
        if (slope_plan)
        {
            fftwf_destroy_plan(slope_plan);
            fftwf_free(slope_output);
            fftwf_free(slope_input);
        }
        fftwf_destroy_plan(plan);
        fftwf_free(output);
        fftwf_free(input);
    }

    band(const band &) = delete;
    band &operator=(const band &) = delete;

    float power(std::size_t k) const
    {
        return (output[k][0] * output[k][0] + output[k][1] * output[k][1]) * power_scale;
    }

    // Measured frequency of bin k, in bins.
    double reassigned(std::size_t k) const
    {
        const double re = output[k][0];
        const double im = output[k][1];
        const double slope_re = slope_output[k][0];
        const double slope_im = slope_output[k][1];
        const double cross = slope_im * re - slope_re * im;
        return k - cross / (re * re + im * im) * size / TAU;
    }
};

spectrogram::spectrogram(bool reassign)
    : reassign_{reassign}, row_power_(ROWS), focus_power_(ROWS), focused_(ROWS)
{
    std::size_t first_row = 0;
    for (const band_spec &spec : BANDS)
    {
        const std::size_t end_row = (spec.top_note - LOWEST_NOTE + 1) * ROWS_PER_NOTE;
        bands_.push_back(std::make_unique<band>(spec.size, first_row, end_row, reassign));
        first_row = end_row;
    }
}

spectrogram::~spectrogram() = default;

void spectrogram::clear()
{
    values_.clear();
    loudest_ = 0;
}

std::size_t spectrogram::lookahead() const
{
    return bands_.front()->size / 2;
}

void spectrogram::analyze(const audio_take &take, std::size_t index)
{
    const std::ptrdiff_t centre = static_cast<std::ptrdiff_t>(index * HOP);
    const std::ptrdiff_t frames = static_cast<std::ptrdiff_t>(take.frames());
    std::fill(row_power_.begin(), row_power_.end(), 0.0f);
    std::fill(focus_power_.begin(), focus_power_.end(), 0.0f);

    for (const auto &b : bands_)
    {
        const std::ptrdiff_t first = centre - static_cast<std::ptrdiff_t>(b->size / 2);
        for (std::size_t i = 0; i < b->size; ++i)
        {
            const std::ptrdiff_t at = first + static_cast<std::ptrdiff_t>(i);
            const float x = at >= 0 && at < frames ? 0.5f * (take.left[at] + take.right[at]) : 0.0f;
            b->input[i] = x * b->window[i];
            if (b->slope_input)
                b->slope_input[i] = x * b->slope[i];
        }
        fftwf_execute(b->plan);
        if (b->slope_plan)
            fftwf_execute(b->slope_plan);

        if (reassign_)
        {
            // A sine's main lobe sums to 1.5 times its peak bin (Hann).
            for (std::size_t k = b->first_bin; k < b->end_bin; ++k)
            {
                const float p = b->power(k);
                if (p < 1e-14f)
                    continue;
                const double bin = b->reassigned(k);
                if (std::abs(bin - static_cast<double>(k)) > MAX_SHIFT_BINS)
                    continue;
                const double note = frequency_note(bin * SAMPLE_RATE / b->size);
                const std::size_t row = note > LOWEST_NOTE - 0.5 ? note_row(note) : ROWS;
                if (row >= b->first_row && row < b->end_row)
                    focus_power_[row] += p / 1.5f;
            }
        }
        {
            for (std::size_t r = 0; r < b->rows.size(); ++r)
            {
                const band::row_bins &bins = b->rows[r];
                float p;
                if (bins.last >= bins.first)
                {
                    p = 0.0f;
                    for (std::size_t k = bins.first; k <= bins.last; ++k)
                        p = std::max(p, b->power(k));
                }
                else
                {
                    // Narrower than a bin: parabola through log powers, near-exact for a Hann lobe.
                    const auto k = static_cast<std::size_t>(std::lround(bins.centre));
                    const double x = bins.centre - k;
                    const double a = std::log(b->power(k - 1) + 1e-30f);
                    const double m = std::log(b->power(k) + 1e-30f);
                    const double c = std::log(b->power(k + 1) + 1e-30f);
                    p = static_cast<float>(std::exp(m + 0.5 * x * (c - a) + 0.5 * x * x * (a - 2.0 * m + c)));
                }
                row_power_[b->first_row + r] = p;
            }
        }
    }

    const auto quantize = [](float power)
    {
        const float db = 10.0f * std::log10(std::max(power, 1e-20f));
        return static_cast<std::uint8_t>(std::clamp(std::round((db - DB_FLOOR) * STEPS_PER_DB), 0.0f, 255.0f));
    };
    const std::size_t first = values_.size();
    if (reassign_)
    {
        for (std::size_t r = 0; r < ROWS; ++r)
            focused_[r] = quantize(focus_power_[r]);
        for (std::size_t r = 0; r < ROWS; ++r)
        {
            std::uint8_t v = std::max(focused_[r], quantize(row_power_[r] * BACKDROP));
            // Thicken lines to stay visible when scaled down.
            for (const std::size_t n : {r - 1, r + 1})
                if (n < ROWS && focused_[n] > SPREAD_STEPS)
                    v = std::max<std::uint8_t>(v, focused_[n] - SPREAD_STEPS);
            values_.push_back(v);
        }
    }
    else
    {
        for (const float power : row_power_)
            values_.push_back(quantize(power));
    }
    loudest_ = std::max(loudest_, *std::max_element(values_.begin() + first, values_.end()));
}

std::size_t spectrogram::update(const audio_take &take, bool final, std::size_t max_columns)
{
    const std::size_t frames = take.frames();
    const std::size_t ahead = lookahead();
    const std::size_t available = final ? (frames + HOP - 1) / HOP
                                  : frames >= ahead ? (frames - ahead) / HOP + 1
                                                    : 0;
    const std::size_t first = columns();
    const std::size_t end = std::min(available, first + max_columns);
    for (std::size_t index = first; index < end; ++index)
        analyze(take, index);
    return end > first ? end - first : 0;
}

}
