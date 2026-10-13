#include "woodshed/take_file.hpp"

#include <catch2/catch.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

using namespace woodshed;

TEST_CASE("take state round trip")
{
    take_state state;
    state.source = "Spotify";
    state.recorded = "2026-10-13T21:34:12+02:00";
    state.tuning = -31.1;
    state.speed = 0.75;
    state.transpose = -2;
    state.fine_tune = 31.1;
    state.loop_start = 12.5;
    state.loop_end = 14.25;
    state.looping = true;
    state.position = 13.0;
    const take_state back = parse_take_state(to_json(state));
    CHECK(back.source == state.source);
    CHECK(back.recorded == state.recorded);
    CHECK(back.tuning == Approx(state.tuning));
    CHECK(back.speed == state.speed);
    CHECK(back.transpose == state.transpose);
    CHECK(back.fine_tune == Approx(state.fine_tune));
    CHECK(back.loop_start == state.loop_start);
    CHECK(back.loop_end == state.loop_end);
    CHECK(back.looping == state.looping);
    CHECK(back.position == state.position);
}

TEST_CASE("take state defaults and errors")
{
    const take_state partial = parse_take_state(R"({"format": 1, "playback": {"speed": 0.5}})");
    CHECK(partial.speed == 0.5);
    CHECK(partial.transpose == 0);
    CHECK_FALSE(partial.looping);
    CHECK_THROWS(parse_take_state("{nope"));
    CHECK_THROWS(parse_take_state(R"({"format": 99})"));
}

TEST_CASE("flac round trip")
{
    const std::size_t frames = 30000;
    std::vector<float> data(2 * frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        data[2 * i] = 0.5f * static_cast<float>(std::sin(i * 0.01));
        data[2 * i + 1] = -0.25f * static_cast<float>(std::cos(i * 0.003));
    }
    audio_take take;
    take.append_interleaved(data.data(), frames);

    const std::string path =
        (std::filesystem::temp_directory_path() / "woodshed-test-take.flac").string();
    save_flac(take, path);
    CHECK_FALSE(std::filesystem::exists(path + ".part"));
    const audio_take back = load_audio(path);
    std::remove(path.c_str());

    REQUIRE(back.frames() == frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        REQUIRE(std::abs(back.left[i] - take.left[i]) < 1e-6f);
        REQUIRE(std::abs(back.right[i] - take.right[i]) < 1e-6f);
    }
    CHECK(back.peaks.frames() == frames);
    CHECK_THROWS(load_audio(path));
}
