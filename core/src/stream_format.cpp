#include "woodshed/stream_format.hpp"
#include "woodshed/audio_take.hpp"

#include <spa/param/audio/format-utils.h>

#include <cstdint>

namespace woodshed
{

void connect_stereo(pw_stream *stream, pw_direction direction)
{
    std::uint8_t buffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.rate = static_cast<std::uint32_t>(SAMPLE_RATE);
    info.channels = 2;
    info.position[0] = SPA_AUDIO_CHANNEL_FL;
    info.position[1] = SPA_AUDIO_CHANNEL_FR;
    const spa_pod *params[1] = {spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info)};
    pw_stream_connect(stream, direction, PW_ID_ANY,
                      static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT |
                                                   PW_STREAM_FLAG_MAP_BUFFERS |
                                                   PW_STREAM_FLAG_RT_PROCESS),
                      params, 1);
}

}
