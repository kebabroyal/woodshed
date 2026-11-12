// SPDX-FileCopyrightText: 2026 kebabroyal
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WOODSHED_STREAM_FORMAT_HPP
#define WOODSHED_STREAM_FORMAT_HPP

#include <pipewire/pipewire.h>

namespace woodshed
{

// F32 stereo at SAMPLE_RATE. Caller holds the loop lock.
void connect_stereo(pw_stream *stream, pw_direction direction);

}

#endif
