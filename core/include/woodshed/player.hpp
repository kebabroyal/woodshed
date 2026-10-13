#ifndef WOODSHED_PLAYER_HPP
#define WOODSHED_PLAYER_HPP

#include "woodshed/audio_take.hpp"
#include "woodshed/pw_session.hpp"
#include "woodshed/render.hpp"
#include "woodshed/spsc_ring.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

namespace woodshed
{

// Plays while alive. The take must not change meanwhile.
// A worker stretches about 100 ms ahead; the real-time callback only copies.
class player
{
public:
    player(pw_session &session, std::shared_ptr<const audio_take> take, std::size_t position,
           loop_region loop, double speed, double pitch);
    ~player();
    player(const player &) = delete;
    player &operator=(const player &) = delete;

    // What is heard now.
    std::size_t position() const { return position_.load(std::memory_order_relaxed); }
    bool finished() const { return finished_.load(std::memory_order_relaxed); }
    void seek(std::size_t position);
    void set_loop(loop_region loop);
    void set_speed(double speed);
    // Frequency ratio.
    void set_pitch(double scale);

private:
    struct stamp
    {
        std::uint32_t generation;
        bool end;
        std::size_t position;
    };

    static void on_process(void *data);
    void run(std::size_t position, double speed, double pitch);
    void wake();

    pw_session &session_;
    std::shared_ptr<const audio_take> take_;
    pw_stream *stream_ = nullptr;
    spa_hook listener_{};

    spsc_ring<float> audio_;
    spsc_ring<stamp> stamps_;
    std::atomic<std::size_t> position_;
    std::atomic<bool> finished_{false};
    std::atomic<std::uint32_t> generation_{0};
    std::atomic<std::size_t> seek_to_{0};
    std::atomic<double> speed_;
    std::atomic<double> pitch_;
    std::atomic<std::size_t> loop_start_;
    std::atomic<std::size_t> loop_end_;
    std::atomic<bool> loop_enabled_;

    // Real-time thread only.
    stamp current_{};
    std::size_t chunk_left_ = 0;

    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false;
    bool changed_ = false;
    std::thread worker_;
};

}

#endif
