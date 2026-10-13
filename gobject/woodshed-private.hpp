#ifndef WOODSHED_PRIVATE_HPP
#define WOODSHED_PRIVATE_HPP

#include "woodshed-engine.h"
#include "woodshed-source.h"

#include "woodshed/audio_take.hpp"

#include <cstdint>

struct source_info
{
    std::uint32_t id;      // 0: default output
    std::uint64_t serial;  // 0: default output
    WoodshedSourceKind kind;
    const char *name;
    const char *detail;
    const char *icon_name;
    const char *binary;
    const char *app_id;
    std::uint32_t pid;
    bool playing;
};

WoodshedSource *woodshed_source_new(const source_info &info);
void woodshed_source_update(WoodshedSource *self, const source_info &info);
std::uint32_t woodshed_source_get_id(WoodshedSource *self);
std::uint64_t woodshed_source_get_serial(WoodshedSource *self);

// Null without a take.
const woodshed::audio_take *woodshed_engine_peek_take(WoodshedEngine *self);

#endif
