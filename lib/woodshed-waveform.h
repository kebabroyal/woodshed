// SPDX-FileCopyrightText: 2026 kebabroyal
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WOODSHED_WAVEFORM_H
#define WOODSHED_WAVEFORM_H

#include <gtk/gtk.h>

#include "woodshed-engine.h"

G_BEGIN_DECLS

/**
 * WoodshedDisplay:
 * @WOODSHED_DISPLAY_WAVEFORM: amplitude
 * @WOODSHED_DISPLAY_SPECTROGRAM: pitch over time as sharp lines, piano keys on the left
 */
typedef enum
{
    WOODSHED_DISPLAY_WAVEFORM,
    WOODSHED_DISPLAY_SPECTROGRAM,
} WoodshedDisplay;

#define WOODSHED_TYPE_WAVEFORM (woodshed_waveform_get_type())
G_DECLARE_FINAL_TYPE(WoodshedWaveform, woodshed_waveform, WOODSHED, WAVEFORM, GtkWidget)

/**
 * woodshed_waveform_new:
 * @engine: engine to show
 *
 * Drag: select a loop. Tap: seek. Double tap: clear. Pinch or Ctrl+scroll: zoom.
 *
 * Returns: (transfer floating): a new #WoodshedWaveform
 */
GtkWidget *woodshed_waveform_new(WoodshedEngine *engine);

/**
 * woodshed_waveform_get_engine:
 * @self: a #WoodshedWaveform
 *
 * Returns: (transfer none) (nullable): the engine
 */
WoodshedEngine *woodshed_waveform_get_engine(WoodshedWaveform *self);

/**
 * woodshed_waveform_set_engine:
 * @self: a #WoodshedWaveform
 * @engine: (nullable): engine to show
 */
void woodshed_waveform_set_engine(WoodshedWaveform *self, WoodshedEngine *engine);

/**
 * woodshed_waveform_get_display:
 * @self: a #WoodshedWaveform
 *
 * Returns: what is drawn
 */
WoodshedDisplay woodshed_waveform_get_display(WoodshedWaveform *self);

/**
 * woodshed_waveform_set_display:
 * @self: a #WoodshedWaveform
 * @display: what to draw
 *
 * The spectrogram is computed while shown, in idle time.
 */
void woodshed_waveform_set_display(WoodshedWaveform *self, WoodshedDisplay display);

/**
 * woodshed_waveform_zoom_to_fit:
 * @self: a #WoodshedWaveform
 */
void woodshed_waveform_zoom_to_fit(WoodshedWaveform *self);

G_END_DECLS

#endif
