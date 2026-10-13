#include "woodshed/capture.hpp"
#include "woodshed/stream_format.hpp"

#include <cinttypes>
#include <cmath>
#include <mutex>
#include <stdexcept>

namespace woodshed
{

static constexpr std::size_t RING_SECONDS = 4;

capture::capture(pw_session &session, capture_target target)
    : session_{session},
      ring_{static_cast<std::size_t>(SAMPLE_RATE) * 2 * RING_SECONDS}
{
    pw_properties *props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE,
        "Production", PW_KEY_NODE_NAME, "woodshed-capture", PW_KEY_NODE_DESCRIPTION,
        "Woodshed capture", nullptr);
    if (target.serial != 0)
    {
        pw_properties_setf(props, PW_KEY_TARGET_OBJECT, "%" PRIu64, target.serial);
        pw_properties_set(props, PW_KEY_NODE_DONT_RECONNECT, "true");
    }
    if (target.monitor)
        pw_properties_set(props, PW_KEY_STREAM_CAPTURE_SINK, "true");

    static const pw_stream_events events = {
        .version = PW_VERSION_STREAM_EVENTS,
        .process = on_process,
    };
    std::lock_guard guard{session_};
    stream_ = pw_stream_new(session_.core(), "woodshed-capture", props);
    if (!stream_)
        throw std::runtime_error{"PipeWire: cannot create capture stream"};
    pw_stream_add_listener(stream_, &listener_, &events, this);
    connect_stereo(stream_, PW_DIRECTION_INPUT);
}

capture::~capture()
{
    std::lock_guard guard{session_};
    spa_hook_remove(&listener_);
    pw_stream_destroy(stream_);
}

void capture::drain(audio_take &take)
{
    float buffer[4096];
    while (const std::size_t n = ring_.read(buffer, std::size(buffer)))
        take.append_interleaved(buffer, n / 2);
}

float capture::take_level()
{
    return level_.exchange(0.0f);
}

void capture::on_process(void *data)
{
    auto *self = static_cast<capture *>(data);
    pw_buffer *b = pw_stream_dequeue_buffer(self->stream_);
    if (!b)
        return;
    const spa_data &d = b->buffer->datas[0];
    if (d.data && d.chunk)
    {
        const auto *samples = reinterpret_cast<const float *>(
            static_cast<const std::uint8_t *>(d.data) + d.chunk->offset % d.maxsize);
        const std::size_t count = d.chunk->size / sizeof(float) & ~std::size_t{1};
        float peak = 0.0f;
        for (std::size_t i = 0; i < count; ++i)
            peak = std::max(peak, std::abs(samples[i]));
        if (peak > self->level_.load(std::memory_order_relaxed))
            self->level_.store(peak, std::memory_order_relaxed);
        self->ring_.write(samples, count);
    }
    pw_stream_queue_buffer(self->stream_, b);
}

}
