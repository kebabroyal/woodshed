#ifndef WOODSHED_ENGINE_H
#define WOODSHED_ENGINE_H

#include <gio/gio.h>

#include "woodshed-source.h"

G_BEGIN_DECLS

/**
 * WoodshedState:
 * @WOODSHED_STATE_IDLE: no take
 * @WOODSHED_STATE_RECORDING: recording
 * @WOODSHED_STATE_PLAYING: playing
 * @WOODSHED_STATE_PAUSED: take loaded, paused
 */
typedef enum
{
    WOODSHED_STATE_IDLE,
    WOODSHED_STATE_RECORDING,
    WOODSHED_STATE_PLAYING,
    WOODSHED_STATE_PAUSED,
} WoodshedState;

#define WOODSHED_TYPE_ENGINE (woodshed_engine_get_type())
G_DECLARE_FINAL_TYPE(WoodshedEngine, woodshed_engine, WOODSHED, ENGINE, GObject)

/**
 * WOODSHED_ENGINE_MIN_SPEED:
 *
 * Slowest speed.
 */
#define WOODSHED_ENGINE_MIN_SPEED 0.25

/**
 * WOODSHED_ENGINE_MAX_SPEED:
 *
 * Fastest speed.
 */
#define WOODSHED_ENGINE_MAX_SPEED 1.5

/**
 * WOODSHED_ENGINE_MAX_TRANSPOSE:
 *
 * Largest shift either way, in semitones.
 */
#define WOODSHED_ENGINE_MAX_TRANSPOSE 12

/**
 * WOODSHED_ENGINE_MAX_FINE_TUNE:
 *
 * Largest fine shift either way, in cents.
 */
#define WOODSHED_ENGINE_MAX_FINE_TUNE 50.0

/**
 * woodshed_engine_new:
 *
 * Returns: (transfer full): a new #WoodshedEngine
 */
WoodshedEngine *woodshed_engine_new(void);

/**
 * woodshed_engine_get_state:
 * @self: a #WoodshedEngine
 *
 * Returns: current state
 */
WoodshedState woodshed_engine_get_state(WoodshedEngine *self);

/**
 * woodshed_engine_get_speed:
 * @self: a #WoodshedEngine
 *
 * Returns: playback speed, 1.0 = original
 */
double woodshed_engine_get_speed(WoodshedEngine *self);

/**
 * woodshed_engine_set_speed:
 * @self: a #WoodshedEngine
 * @speed: clamped to [%WOODSHED_ENGINE_MIN_SPEED, %WOODSHED_ENGINE_MAX_SPEED]
 *
 * Pitch unchanged.
 */
void woodshed_engine_set_speed(WoodshedEngine *self, double speed);

/**
 * woodshed_engine_get_transpose:
 * @self: a #WoodshedEngine
 *
 * Returns: playback shift in semitones
 */
int woodshed_engine_get_transpose(WoodshedEngine *self);

/**
 * woodshed_engine_set_transpose:
 * @self: a #WoodshedEngine
 * @semitones: clamped to ±%WOODSHED_ENGINE_MAX_TRANSPOSE
 *
 * Speed unchanged.
 */
void woodshed_engine_set_transpose(WoodshedEngine *self, int semitones);

/**
 * woodshed_engine_get_fine_tune:
 * @self: a #WoodshedEngine
 *
 * Returns: playback shift in cents, on top of the transpose
 */
double woodshed_engine_get_fine_tune(WoodshedEngine *self);

/**
 * woodshed_engine_set_fine_tune:
 * @self: a #WoodshedEngine
 * @cents: clamped to ±%WOODSHED_ENGINE_MAX_FINE_TUNE
 */
void woodshed_engine_set_fine_tune(WoodshedEngine *self, double cents);

/**
 * woodshed_engine_get_tuning:
 * @self: a #WoodshedEngine
 *
 * Measured when recording stops.
 *
 * Returns: the take's offset from A440 equal temperament, in cents; 0 if unknown
 */
double woodshed_engine_get_tuning(WoodshedEngine *self);

/**
 * woodshed_engine_match_a440:
 * @self: a #WoodshedEngine
 *
 * Sets the fine tune so the take plays at A440.
 */
void woodshed_engine_match_a440(WoodshedEngine *self);

/**
 * woodshed_engine_get_duration:
 * @self: a #WoodshedEngine
 *
 * Returns: take length in seconds, 0 if none
 */
double woodshed_engine_get_duration(WoodshedEngine *self);

/**
 * woodshed_engine_get_position:
 * @self: a #WoodshedEngine
 *
 * Returns: playhead in seconds
 */
double woodshed_engine_get_position(WoodshedEngine *self);

/**
 * woodshed_engine_seek:
 * @self: a #WoodshedEngine
 * @seconds: new playhead
 */
void woodshed_engine_seek(WoodshedEngine *self, double seconds);

/**
 * woodshed_engine_get_level:
 * @self: a #WoodshedEngine
 *
 * Returns: recent input peak, 0 to 1
 */
double woodshed_engine_get_level(WoodshedEngine *self);

/**
 * woodshed_engine_get_sources:
 * @self: a #WoodshedEngine
 *
 * Returns: (transfer none): live list of #WoodshedSource, playing apps first
 */
GListModel *woodshed_engine_get_sources(WoodshedEngine *self);

/**
 * woodshed_engine_get_source:
 * @self: a #WoodshedEngine
 *
 * Returns: (transfer none) (nullable): source to record from
 */
WoodshedSource *woodshed_engine_get_source(WoodshedEngine *self);

/**
 * woodshed_engine_set_source:
 * @self: a #WoodshedEngine
 * @source: (nullable): source, or %NULL to pick automatically
 */
void woodshed_engine_set_source(WoodshedEngine *self, WoodshedSource *source);

/**
 * woodshed_engine_record_start:
 * @self: a #WoodshedEngine
 *
 * Replaces the take.
 */
void woodshed_engine_record_start(WoodshedEngine *self);

/**
 * woodshed_engine_record_stop:
 * @self: a #WoodshedEngine
 */
void woodshed_engine_record_stop(WoodshedEngine *self);

/**
 * woodshed_engine_toggle_record:
 * @self: a #WoodshedEngine
 */
void woodshed_engine_toggle_record(WoodshedEngine *self);

/**
 * woodshed_engine_play:
 * @self: a #WoodshedEngine
 */
void woodshed_engine_play(WoodshedEngine *self);

/**
 * woodshed_engine_pause:
 * @self: a #WoodshedEngine
 */
void woodshed_engine_pause(WoodshedEngine *self);

/**
 * woodshed_engine_toggle_play:
 * @self: a #WoodshedEngine
 */
void woodshed_engine_toggle_play(WoodshedEngine *self);

/**
 * woodshed_engine_get_loop_start:
 * @self: a #WoodshedEngine
 *
 * Returns: selection start in seconds
 */
double woodshed_engine_get_loop_start(WoodshedEngine *self);

/**
 * woodshed_engine_get_loop_end:
 * @self: a #WoodshedEngine
 *
 * Returns: selection end in seconds
 */
double woodshed_engine_get_loop_end(WoodshedEngine *self);

/**
 * woodshed_engine_get_has_loop:
 * @self: a #WoodshedEngine
 *
 * Returns: whether a selection exists
 */
gboolean woodshed_engine_get_has_loop(WoodshedEngine *self);

/**
 * woodshed_engine_set_loop:
 * @self: a #WoodshedEngine
 * @start: seconds
 * @end: seconds; order does not matter
 */
void woodshed_engine_set_loop(WoodshedEngine *self, double start, double end);

/**
 * woodshed_engine_clear_loop:
 * @self: a #WoodshedEngine
 */
void woodshed_engine_clear_loop(WoodshedEngine *self);

/**
 * woodshed_engine_get_looping:
 * @self: a #WoodshedEngine
 *
 * Returns: whether playback loops the selection (or the whole take)
 */
gboolean woodshed_engine_get_looping(WoodshedEngine *self);

/**
 * woodshed_engine_set_looping:
 * @self: a #WoodshedEngine
 * @looping: on or off
 */
void woodshed_engine_set_looping(WoodshedEngine *self, gboolean looping);

/**
 * woodshed_engine_get_file:
 * @self: a #WoodshedEngine
 *
 * Takes are saved as FLAC in ~/Music/Woodshed when recording stops,
 * with their settings in a .json file beside them.
 *
 * Returns: (nullable): path of the current take
 */
const char *woodshed_engine_get_file(WoodshedEngine *self);

/**
 * woodshed_engine_open:
 * @self: a #WoodshedEngine
 * @path: audio file, 48 kHz for now
 *
 * Loads in the background, with saved settings if any. Errors go to #WoodshedEngine::error.
 */
void woodshed_engine_open(WoodshedEngine *self, const char *path);

/**
 * woodshed_engine_open_last:
 * @self: a #WoodshedEngine
 *
 * Reopens the take of the last session.
 *
 * Returns: whether there was one to open
 */
gboolean woodshed_engine_open_last(WoodshedEngine *self);

/**
 * woodshed_engine_flush:
 * @self: a #WoodshedEngine
 *
 * Waits for saves in progress and writes pending settings. Call before quitting.
 */
void woodshed_engine_flush(WoodshedEngine *self);

G_END_DECLS

#endif
