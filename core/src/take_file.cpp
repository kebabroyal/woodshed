#include "woodshed/take_file.hpp"

#include <nlohmann/json.hpp>
#include <sndfile.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

namespace woodshed
{

static constexpr sf_count_t BLOCK = 65536;

// Readable numbers: 0.1 cent, 1 ms.
static double rounded(double value, double step)
{
    return std::round(value / step) * step;
}

std::string to_json(const take_state &state)
{
    const nlohmann::ordered_json json = {
        {"format", take_state::FORMAT},
        {"source", state.source},
        {"recorded", state.recorded},
        {"tuning_cents", rounded(state.tuning, 0.1)},
        {"playback",
         {{"speed", state.speed},
          {"transpose_semitones", state.transpose},
          {"fine_tune_cents", rounded(state.fine_tune, 0.1)}}},
        {"loop",
         {{"start", rounded(state.loop_start, 0.001)},
          {"end", rounded(state.loop_end, 0.001)},
          {"enabled", state.looping}}},
        {"position", rounded(state.position, 0.001)},
    };
    return json.dump(2) + "\n";
}

take_state parse_take_state(const std::string &text)
{
    const nlohmann::json json = nlohmann::json::parse(text);
    if (json.value("format", 0) > take_state::FORMAT)
        throw std::runtime_error{"Saved by a newer Woodshed"};
    take_state state;
    state.source = json.value("source", state.source);
    state.recorded = json.value("recorded", state.recorded);
    state.tuning = json.value("tuning_cents", state.tuning);
    const nlohmann::json playback = json.value("playback", nlohmann::json::object());
    state.speed = playback.value("speed", state.speed);
    state.transpose = playback.value("transpose_semitones", state.transpose);
    state.fine_tune = playback.value("fine_tune_cents", state.fine_tune);
    const nlohmann::json loop = json.value("loop", nlohmann::json::object());
    state.loop_start = loop.value("start", state.loop_start);
    state.loop_end = loop.value("end", state.loop_end);
    state.looping = loop.value("enabled", state.looping);
    state.position = json.value("position", state.position);
    return state;
}

struct sndfile_closer
{
    void operator()(SNDFILE *file) const { sf_close(file); }
};

using sndfile_ptr = std::unique_ptr<SNDFILE, sndfile_closer>;

void save_flac(const audio_take &take, const std::string &path)
{
    const std::string part = path + ".part";
    SF_INFO info{};
    info.samplerate = static_cast<int>(SAMPLE_RATE);
    info.channels = 2;
    info.format = SF_FORMAT_FLAC | SF_FORMAT_PCM_24;
    sndfile_ptr file{sf_open(part.c_str(), SFM_WRITE, &info)};
    if (!file)
        throw std::runtime_error{std::string{"Cannot save: "} + sf_strerror(nullptr)};

    std::vector<float> block(2 * BLOCK);
    for (std::size_t first = 0; first < take.frames(); first += BLOCK)
    {
        const std::size_t n = std::min<std::size_t>(BLOCK, take.frames() - first);
        for (std::size_t i = 0; i < n; ++i)
        {
            block[2 * i] = take.left[first + i];
            block[2 * i + 1] = take.right[first + i];
        }
        if (sf_writef_float(file.get(), block.data(), static_cast<sf_count_t>(n)) !=
            static_cast<sf_count_t>(n))
        {
            const std::string error = sf_strerror(file.get());
            file.reset();
            std::remove(part.c_str());
            throw std::runtime_error{"Cannot save: " + error};
        }
    }
    file.reset();
    if (std::rename(part.c_str(), path.c_str()) != 0)
    {
        std::remove(part.c_str());
        throw std::runtime_error{"Cannot save " + path};
    }
}

audio_take load_audio(const std::string &path)
{
    SF_INFO info{};
    sndfile_ptr file{sf_open(path.c_str(), SFM_READ, &info)};
    if (!file)
        throw std::runtime_error{"Cannot open " + path + ": " + sf_strerror(nullptr)};
    if (info.samplerate != static_cast<int>(SAMPLE_RATE))
        throw std::runtime_error{"Only 48 kHz files for now"};
    if (info.channels < 1 || info.channels > 2)
        throw std::runtime_error{"Only mono or stereo files"};

    audio_take take;
    take.left.reserve(static_cast<std::size_t>(info.frames));
    take.right.reserve(static_cast<std::size_t>(info.frames));
    std::vector<float> block(static_cast<std::size_t>(info.channels * BLOCK));
    std::vector<float> stereo(2 * BLOCK);
    for (;;)
    {
        const sf_count_t n = sf_readf_float(file.get(), block.data(), BLOCK);
        if (n <= 0)
            break;
        const float *data = block.data();
        if (info.channels == 1)
        {
            for (sf_count_t i = 0; i < n; ++i)
                stereo[2 * i] = stereo[2 * i + 1] = block[i];
            data = stereo.data();
        }
        take.append_interleaved(data, static_cast<std::size_t>(n));
    }
    return take;
}

}
