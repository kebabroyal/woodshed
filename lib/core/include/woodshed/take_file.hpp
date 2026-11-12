// SPDX-FileCopyrightText: 2026 kebabroyal
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WOODSHED_TAKE_FILE_HPP
#define WOODSHED_TAKE_FILE_HPP

#include "woodshed/audio_take.hpp"

#include <string>

namespace woodshed
{

// Settings saved next to a take, as JSON. Times in seconds.
struct take_state
{
    static constexpr int FORMAT = 1;

    std::string source;   // app name
    std::string recorded; // ISO 8601, local time
    double tuning = 0.0;  // cents from A440, 0 if unknown
    double speed = 1.0;
    int transpose = 0;      // semitones
    double fine_tune = 0.0; // cents
    double loop_start = 0.0;
    double loop_end = 0.0; // equal to start: no selection
    bool looping = false;
    double position = 0.0;
};

std::string to_json(const take_state &state);
// Missing fields keep their defaults. Throws on bad JSON or a newer format.
take_state parse_take_state(const std::string &json);

// FLAC, 24 bit, stereo at SAMPLE_RATE. Writes a .part file, then renames. Throws.
void save_flac(const audio_take &take, const std::string &path);
// Any format libsndfile reads, at SAMPLE_RATE. Mono is doubled. Throws.
audio_take load_audio(const std::string &path);

}

#endif
