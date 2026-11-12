// SPDX-FileCopyrightText: 2026 kebabroyal
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WOODSHED_SOURCE_H
#define WOODSHED_SOURCE_H

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * WoodshedSourceKind:
 * @WOODSHED_SOURCE_KIND_APP: an application's output
 * @WOODSHED_SOURCE_KIND_OUTPUT: everything a speaker plays
 * @WOODSHED_SOURCE_KIND_INPUT: a microphone or line in
 */
typedef enum
{
    WOODSHED_SOURCE_KIND_APP,
    WOODSHED_SOURCE_KIND_OUTPUT,
    WOODSHED_SOURCE_KIND_INPUT,
} WoodshedSourceKind;

#define WOODSHED_TYPE_SOURCE (woodshed_source_get_type())
G_DECLARE_FINAL_TYPE(WoodshedSource, woodshed_source, WOODSHED, SOURCE, GObject)

/**
 * woodshed_source_get_name:
 * @self: a #WoodshedSource
 *
 * Returns: display name
 */
const char *woodshed_source_get_name(WoodshedSource *self);

/**
 * woodshed_source_get_detail:
 * @self: a #WoodshedSource
 *
 * Returns: second line, e.g. the track
 */
const char *woodshed_source_get_detail(WoodshedSource *self);

/**
 * woodshed_source_get_icon_name:
 * @self: a #WoodshedSource
 *
 * Returns: icon name
 */
const char *woodshed_source_get_icon_name(WoodshedSource *self);

/**
 * woodshed_source_get_kind:
 * @self: a #WoodshedSource
 *
 * Returns: kind
 */
WoodshedSourceKind woodshed_source_get_kind(WoodshedSource *self);

/**
 * woodshed_source_get_playing:
 * @self: a #WoodshedSource
 *
 * Returns: whether audio flows now
 */
gboolean woodshed_source_get_playing(WoodshedSource *self);

/**
 * woodshed_source_get_binary:
 * @self: a #WoodshedSource
 *
 * Returns: (nullable): executable name of an app, e.g. "chrome"
 */
const char *woodshed_source_get_binary(WoodshedSource *self);

/**
 * woodshed_source_get_app_id:
 * @self: a #WoodshedSource
 *
 * Returns: (nullable): Flatpak or app id, e.g. "com.spotify.Client"
 */
const char *woodshed_source_get_app_id(WoodshedSource *self);

/**
 * woodshed_source_get_process_id:
 * @self: a #WoodshedSource
 *
 * Inside a sandbox this is the sandbox's own id.
 *
 * Returns: process id of an app, 0 if unknown
 */
guint woodshed_source_get_process_id(WoodshedSource *self);

G_END_DECLS

#endif
