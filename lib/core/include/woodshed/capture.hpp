// SPDX-FileCopyrightText: 2026 kebabroyal
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WOODSHED_CAPTURE_HPP
#define WOODSHED_CAPTURE_HPP

#include "woodshed/audio_take.hpp"
#include "woodshed/pw_session.hpp"
#include "woodshed/spsc_ring.hpp"

#include <atomic>
#include <cstdint>

namespace woodshed
{

struct capture_target
{
    std::uint64_t serial = 0; // 0: default output
    bool monitor = true;      // record what a sink plays
};

// Records while alive.
class capture
{
public:
    capture(pw_session &session, capture_target target);
    ~capture();
    capture(const capture &) = delete;
    capture &operator=(const capture &) = delete;

    // Main thread.
    void drain(audio_take &take);
    float take_level();

private:
    static void on_process(void *data);

    pw_session &session_;
    pw_stream *stream_ = nullptr;
    spa_hook listener_{};
    spsc_ring<float> ring_;
    std::atomic<float> level_{0.0f};
};

}

#endif
