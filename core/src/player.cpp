#include "woodshed/player.hpp"
#include "woodshed/playback_feed.hpp"
#include "woodshed/stream_format.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace woodshed
{

static constexpr std::size_t STRIDE = 2 * sizeof(float);
static constexpr std::size_t CHUNK = playback_feed::CHUNK;
static constexpr std::size_t AHEAD_FRAMES = 4800;
static constexpr std::size_t RING_FRAMES = 8192;
static constexpr auto REFILL = std::chrono::milliseconds{20};

player::player(pw_session &session, std::shared_ptr<const audio_take> take, std::size_t position,
               loop_region loop, double speed, double pitch)
    : session_{session},
      take_{std::move(take)},
      audio_{2 * RING_FRAMES},
      stamps_{RING_FRAMES / CHUNK},
      position_{position},
      speed_{speed},
      pitch_{pitch},
      loop_start_{loop.start},
      loop_end_{loop.end},
      loop_enabled_{loop.enabled}
{
    worker_ = std::thread{&player::run, this, position, speed, pitch};

    pw_properties *props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback", PW_KEY_MEDIA_ROLE, "Music",
        PW_KEY_NODE_NAME, "woodshed-playback", PW_KEY_NODE_DESCRIPTION, "Woodshed", nullptr);
    static const pw_stream_events events = {
        .version = PW_VERSION_STREAM_EVENTS,
        .process = on_process,
    };
    std::lock_guard guard{session_};
    stream_ = pw_stream_new(session_.core(), "woodshed-playback", props);
    if (!stream_)
    {
        {
            std::lock_guard lock{mutex_};
            stop_ = true;
        }
        wake_.notify_one();
        worker_.join();
        throw std::runtime_error{"PipeWire: cannot create playback stream"};
    }
    pw_stream_add_listener(stream_, &listener_, &events, this);
    connect_stereo(stream_, PW_DIRECTION_OUTPUT);
}

player::~player()
{
    {
        std::lock_guard guard{session_};
        spa_hook_remove(&listener_);
        pw_stream_destroy(stream_);
    }
    {
        std::lock_guard lock{mutex_};
        stop_ = true;
    }
    wake_.notify_one();
    worker_.join();
}

void player::wake()
{
    {
        std::lock_guard lock{mutex_};
        changed_ = true;
    }
    wake_.notify_one();
}

void player::seek(std::size_t position)
{
    seek_to_.store(position);
    generation_.fetch_add(1);
    position_.store(position, std::memory_order_relaxed);
    wake();
}

void player::set_loop(loop_region loop)
{
    loop_start_.store(loop.start);
    loop_end_.store(loop.end);
    loop_enabled_.store(loop.enabled);
}

void player::set_speed(double speed)
{
    speed_.store(speed);
    wake();
}

void player::set_pitch(double scale)
{
    pitch_.store(scale);
    wake();
}

void player::run(std::size_t position, double speed, double pitch)
{
    playback_feed feed{take_, position, speed, pitch};
    std::uint32_t generation = 0;
    bool ended = false;
    float chunk[2 * CHUNK];

    for (;;)
    {
        {
            std::unique_lock lock{mutex_};
            wake_.wait_for(lock, REFILL, [this] { return stop_ || changed_; });
            if (stop_)
                return;
            changed_ = false;
        }

        if (const std::uint32_t g = generation_.load(); g != generation)
        {
            generation = g;
            feed.seek(seek_to_.load());
            ended = false;
        }
        const double new_speed = speed_.load();
        const double new_pitch = pitch_.load();
        if (new_speed != speed || new_pitch != pitch)
        {
            // Engine switch restarts the feed; drop what is queued.
            const bool restart = (new_speed == 1.0 && new_pitch == 1.0) != (speed == 1.0 && pitch == 1.0);
            speed = new_speed;
            pitch = new_pitch;
            feed.set_rate(speed, pitch);
            if (restart)
            {
                generation = generation_.fetch_add(1) + 1;
                feed.seek(position_.load());
                ended = false;
            }
        }

        while (!ended && audio_.readable() < 2 * AHEAD_FRAMES && audio_.writable() >= 2 * CHUNK)
        {
            const loop_region loop{loop_start_.load(), loop_end_.load(), loop_enabled_.load()};
            std::size_t at = 0;
            ended = !feed.next(loop, chunk, at);
            if (!ended)
                audio_.write(chunk, 2 * CHUNK);
            const stamp s{generation, ended, at};
            stamps_.write(&s, 1);
        }
    }
}

void player::on_process(void *data)
{
    auto *self = static_cast<player *>(data);
    pw_buffer *b = pw_stream_dequeue_buffer(self->stream_);
    if (!b)
        return;
    spa_data &d = b->buffer->datas[0];
    auto *out = static_cast<float *>(d.data);
    if (!out)
    {
        pw_stream_queue_buffer(self->stream_, b);
        return;
    }
    std::size_t frames = d.maxsize / STRIDE;
    if (b->requested)
        frames = std::min<std::size_t>(frames, b->requested);

    const std::uint32_t generation = self->generation_.load();
    std::size_t done = 0;
    while (done < frames)
    {
        if (self->chunk_left_ > 0 && self->current_.generation != generation)
        {
            self->audio_.skip(2 * self->chunk_left_);
            self->chunk_left_ = 0;
        }
        if (self->chunk_left_ == 0)
        {
            stamp s;
            if (!self->stamps_.read(&s, 1))
                break;
            if (s.generation != generation)
            {
                if (!s.end)
                    self->audio_.skip(2 * CHUNK);
                continue;
            }
            if (s.end)
            {
                self->finished_.store(true);
                break;
            }
            self->current_ = s;
            self->chunk_left_ = CHUNK;
            self->position_.store(s.position, std::memory_order_relaxed);
        }
        const std::size_t n = std::min(frames - done, self->chunk_left_);
        self->audio_.read(out + 2 * done, 2 * n);
        self->chunk_left_ -= n;
        done += n;
    }
    std::fill(out + 2 * done, out + 2 * frames, 0.0f);

    d.chunk->offset = 0;
    d.chunk->stride = STRIDE;
    d.chunk->size = static_cast<std::uint32_t>(frames * STRIDE);
    pw_stream_queue_buffer(self->stream_, b);
}

}
