// SPDX-FileCopyrightText: 2026 kebabroyal
// SPDX-License-Identifier: GPL-3.0-or-later

#include "woodshed-engine.h"
#include "woodshed-enums.h"
#include "woodshed-private.hpp"

#include "woodshed/capture.hpp"
#include "woodshed/player.hpp"
#include "woodshed/pw_session.hpp"
#include "woodshed/take_file.hpp"
#include "woodshed/tuning.hpp"

#include <gio/gio.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

static constexpr guint TICK_MS = 33;
static constexpr guint STATE_SAVE_SECONDS = 2;

struct engine_impl
{
    std::unique_ptr<woodshed::pw_session> session;
    std::shared_ptr<woodshed::audio_take> take;
    std::unique_ptr<woodshed::capture> capture;
    std::unique_ptr<woodshed::player> player;
    GListStore *sources = nullptr;
    std::map<std::uint32_t, WoodshedSource *> by_id;
    std::atomic<bool> nodes_pending{false};
    guint tick = 0;

    std::string file; // take path; empty until recorded or opened
    std::string recorded;
    std::string source_name;
    guint state_save = 0;
    unsigned saving = 0;
    unsigned load_generation = 0;
};

struct _WoodshedEngine
{
    GObject parent_instance;
    WoodshedState state;
    double speed;
    int transpose;
    double fine_tune;
    double tuning;
    std::size_t position;
    std::size_t loop_start;
    std::size_t loop_end;
    gboolean looping;
    double level;
    WoodshedSource *source;
    engine_impl *impl;
};

G_DEFINE_FINAL_TYPE(WoodshedEngine, woodshed_engine, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_STATE,
    PROP_SPEED,
    PROP_TRANSPOSE,
    PROP_FINE_TUNE,
    PROP_TUNING,
    PROP_FILE,
    PROP_DURATION,
    PROP_POSITION,
    PROP_LEVEL,
    PROP_SOURCES,
    PROP_SOURCE,
    PROP_LOOP_START,
    PROP_LOOP_END,
    PROP_HAS_LOOP,
    PROP_LOOPING,
    N_PROPS
};

enum
{
    SIGNAL_ERROR,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

static double to_seconds(std::size_t frames)
{
    return static_cast<double>(frames) / woodshed::SAMPLE_RATE;
}

static std::size_t frames_of(WoodshedEngine *self)
{
    return self->impl->take ? self->impl->take->frames() : 0;
}

static std::size_t to_frames(WoodshedEngine *self, double seconds)
{
    const double frames = std::max(0.0, seconds) * woodshed::SAMPLE_RATE;
    return std::min(static_cast<std::size_t>(frames), frames_of(self));
}

static void notify(WoodshedEngine *self, int prop)
{
    g_object_notify_by_pspec(G_OBJECT(self), properties[prop]);
}

static void fail(WoodshedEngine *self, const char *message)
{
    g_signal_emit(self, signals[SIGNAL_ERROR], 0, message);
}

static void set_state(WoodshedEngine *self, WoodshedState state)
{
    if (self->state == state)
        return;
    self->state = state;
    notify(self, PROP_STATE);
}

static woodshed::loop_region current_loop(WoodshedEngine *self)
{
    if (self->loop_end > self->loop_start)
        return {self->loop_start, self->loop_end, static_cast<bool>(self->looping)};
    return {0, frames_of(self), static_cast<bool>(self->looping)};
}

static void schedule_state_save(WoodshedEngine *self);

static void push_loop(WoodshedEngine *self)
{
    if (self->impl->player)
        self->impl->player->set_loop(current_loop(self));
}

// Sources

struct source_entry
{
    std::uint32_t id;
    std::uint64_t serial;
    WoodshedSourceKind kind;
    std::string name;
    std::string detail;
    std::string icon_name;
    bool playing;
    int rank;
    std::string binary = {};
    std::string app_id = {};
    std::uint32_t pid = 0;

    source_info info() const
    {
        return {id,    serial, kind, name.c_str(), detail.c_str(), icon_name.c_str(), binary.c_str(),
                app_id.c_str(), pid, playing};
    }
};

static const std::string &first_of(const std::string &a, const std::string &b)
{
    return a.empty() ? b : a;
}

static source_entry entry_of(const woodshed::audio_node &node)
{
    switch (node.kind)
    {
    case woodshed::node_kind::app_output:
        return {node.id, node.serial, WOODSHED_SOURCE_KIND_APP,
                first_of(node.app_name, first_of(node.description, node.node_name)),
                node.media_name, first_of(node.icon_name, "audio-x-generic-symbolic"),
                node.running, node.running ? 0 : 1, node.binary, node.app_id, node.pid};
    case woodshed::node_kind::sink:
        return {node.id, node.serial, WOODSHED_SOURCE_KIND_OUTPUT,
                first_of(node.nick, node.description), "Output", "audio-speakers-symbolic",
                node.running, 3};
    case woodshed::node_kind::source:
        break;
    }
    return {node.id, node.serial, WOODSHED_SOURCE_KIND_INPUT,
            first_of(node.nick, node.description), "Input",
            "audio-input-microphone-symbolic", node.running, 4};
}

static void refresh_sources(WoodshedEngine *self)
{
    engine_impl *impl = self->impl;
    std::vector<source_entry> entries;
    entries.push_back({0, 0, WOODSHED_SOURCE_KIND_OUTPUT, "System sound", "Everything you hear",
                       "audio-speakers-symbolic", false, 2});
    if (impl->session)
        for (const woodshed::audio_node &node : impl->session->nodes())
            entries.push_back(entry_of(node));
    std::stable_sort(entries.begin(), entries.end(),
                     [](const source_entry &a, const source_entry &b) { return a.rank < b.rank; });

    std::map<std::uint32_t, WoodshedSource *> by_id;
    std::vector<gpointer> items;
    for (const source_entry &entry : entries)
    {
        WoodshedSource *source;
        const auto it = impl->by_id.find(entry.id);
        if (it != impl->by_id.end())
        {
            source = it->second;
            impl->by_id.erase(it);
            woodshed_source_update(source, entry.info());
        }
        else
            source = woodshed_source_new(entry.info());
        by_id[entry.id] = source;
        items.push_back(source);
    }
    for (auto &[id, gone] : impl->by_id)
        g_object_unref(gone);
    impl->by_id = std::move(by_id);

    // Splice only when the order changed: keeps list selections stable.
    GListModel *model = G_LIST_MODEL(impl->sources);
    const guint old_count = g_list_model_get_n_items(model);
    bool same = old_count == items.size();
    for (guint i = 0; same && i < old_count; ++i)
    {
        gpointer item = g_list_model_get_item(model, i);
        same = item == items[i];
        g_object_unref(item);
    }
    if (!same)
        g_list_store_splice(impl->sources, 0, old_count, items.data(),
                            static_cast<guint>(items.size()));
}

static gboolean on_nodes_idle(gpointer data)
{
    WoodshedEngine *self = WOODSHED_ENGINE(data);
    self->impl->nodes_pending.store(false);
    refresh_sources(self);
    return G_SOURCE_REMOVE;
}

static bool is_listed(WoodshedEngine *self, WoodshedSource *source)
{
    const auto it = self->impl->by_id.find(woodshed_source_get_id(source));
    return it != self->impl->by_id.end() && it->second == source;
}

// Explicit choice if still there, else the same app, else the first app (playing ones
// come first), else system sound.
static WoodshedSource *resolve_source(WoodshedEngine *self)
{
    WoodshedSource *chosen = self->source;
    if (chosen && is_listed(self, chosen))
        return chosen;
    GListModel *model = G_LIST_MODEL(self->impl->sources);
    WoodshedSource *same_app = nullptr;
    WoodshedSource *first_app = nullptr;
    WoodshedSource *system = nullptr;
    for (guint i = 0; i < g_list_model_get_n_items(model); ++i)
    {
        auto *source = WOODSHED_SOURCE(g_list_model_get_item(model, i));
        g_object_unref(source);
        if (woodshed_source_get_id(source) == 0)
            system = source;
        if (woodshed_source_get_kind(source) != WOODSHED_SOURCE_KIND_APP)
            continue;
        if (!first_app)
            first_app = source;
        if (!same_app && chosen &&
            g_strcmp0(woodshed_source_get_name(source), woodshed_source_get_name(chosen)) == 0)
            same_app = source;
    }
    if (same_app)
        return same_app;
    return first_app ? first_app : system;
}

// Ticking: only while recording or playing.

static gboolean on_tick(gpointer data);

static void update_tick(WoodshedEngine *self)
{
    const bool busy = self->state == WOODSHED_STATE_RECORDING || self->state == WOODSHED_STATE_PLAYING;
    if (busy && !self->impl->tick)
        self->impl->tick = g_timeout_add(TICK_MS, on_tick, self);
    else if (!busy && self->impl->tick)
        g_clear_handle_id(&self->impl->tick, g_source_remove);
}

static void stop_player(WoodshedEngine *self)
{
    self->position = self->impl->player->position();
    self->impl->player.reset();
    set_state(self, WOODSHED_STATE_PAUSED);
    update_tick(self);
    schedule_state_save(self);
    notify(self, PROP_POSITION);
}

static gboolean on_tick(gpointer data)
{
    WoodshedEngine *self = WOODSHED_ENGINE(data);
    engine_impl *impl = self->impl;
    if (impl->capture)
    {
        impl->capture->drain(*impl->take);
        self->level = impl->capture->take_level();
        notify(self, PROP_DURATION);
        notify(self, PROP_LEVEL);
    }
    if (impl->player)
    {
        if (impl->player->finished())
        {
            impl->player.reset();
            self->position = self->loop_end > self->loop_start ? self->loop_start : 0;
            set_state(self, WOODSHED_STATE_PAUSED);
            notify(self, PROP_POSITION);
            impl->tick = 0;
            return G_SOURCE_REMOVE;
        }
        self->position = impl->player->position();
        notify(self, PROP_POSITION);
    }
    return G_SOURCE_CONTINUE;
}

// Saving: each take as FLAC in the library, its settings in a JSON file beside it.

static std::string take_string(char *owned)
{
    std::string out = owned ? owned : "";
    g_free(owned);
    return out;
}

static std::string library_dir()
{
    const char *music = g_get_user_special_dir(G_USER_DIRECTORY_MUSIC);
    return take_string(music ? g_build_filename(music, "Woodshed", nullptr)
                             : g_build_filename(g_get_home_dir(), "Music", "Woodshed", nullptr));
}

static std::string last_take_pointer()
{
    return take_string(g_build_filename(g_get_user_state_dir(), "woodshed", "last-take", nullptr));
}

static std::string sidecar_of(const std::string &take)
{
    const std::size_t dot = take.rfind('.');
    return (dot == std::string::npos ? take : take.substr(0, dot)) + ".json";
}

static std::string new_take_path(const std::string &app)
{
    GDateTime *now = g_date_time_new_now_local();
    std::string stem = take_string(g_date_time_format(now, "%Y-%m-%d %H.%M"));
    g_date_time_unref(now);
    std::string name = app.empty() ? "Recording" : app;
    std::replace_if(name.begin(), name.end(), [](char c) { return c == '/' || c == '\\'; }, '-');
    stem += " " + name;
    const std::string dir = library_dir();
    std::string path = take_string(g_build_filename(dir.c_str(), (stem + ".flac").c_str(), nullptr));
    for (int n = 2; g_file_test(path.c_str(), G_FILE_TEST_EXISTS); ++n)
        path = take_string(g_build_filename(
            dir.c_str(), (stem + " " + std::to_string(n) + ".flac").c_str(), nullptr));
    return path;
}

static bool write_file(WoodshedEngine *self, const std::string &path, const std::string &text)
{
    char *dir = g_path_get_dirname(path.c_str());
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);
    GError *error = nullptr;
    if (g_file_set_contents(path.c_str(), text.c_str(), static_cast<gssize>(text.size()), &error))
        return true;
    fail(self, error->message);
    g_error_free(error);
    return false;
}

static woodshed::take_state state_of(WoodshedEngine *self)
{
    woodshed::take_state state;
    state.source = self->impl->source_name;
    state.recorded = self->impl->recorded;
    state.tuning = self->tuning;
    state.speed = self->speed;
    state.transpose = self->transpose;
    state.fine_tune = self->fine_tune;
    state.loop_start = to_seconds(self->loop_start);
    state.loop_end = to_seconds(self->loop_end);
    state.looping = self->looping;
    state.position = to_seconds(self->impl->player ? self->impl->player->position() : self->position);
    return state;
}

static void save_state_now(WoodshedEngine *self)
{
    engine_impl *impl = self->impl;
    g_clear_handle_id(&impl->state_save, g_source_remove);
    if (!impl->file.empty())
        write_file(self, sidecar_of(impl->file), woodshed::to_json(state_of(self)));
}

static gboolean on_state_save(gpointer data)
{
    WoodshedEngine *self = WOODSHED_ENGINE(data);
    self->impl->state_save = 0;
    save_state_now(self);
    return G_SOURCE_REMOVE;
}

// Called on every user change; writes once things settle.
static void schedule_state_save(WoodshedEngine *self)
{
    engine_impl *impl = self->impl;
    if (impl->file.empty())
        return;
    g_clear_handle_id(&impl->state_save, g_source_remove);
    impl->state_save = g_timeout_add_seconds(STATE_SAVE_SECONDS, on_state_save, self);
}

static void remember_last(WoodshedEngine *self, const std::string &path)
{
    write_file(self, last_take_pointer(), path + "\n");
}

struct save_job
{
    std::shared_ptr<const woodshed::audio_take> take;
    std::string path;
};

static void save_in_thread(GTask *task, gpointer, gpointer data, GCancellable *)
{
    const auto *job = static_cast<save_job *>(data);
    try
    {
        woodshed::save_flac(*job->take, job->path);
        g_task_return_boolean(task, TRUE);
    }
    catch (const std::exception &error)
    {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", error.what());
    }
}

static void on_saved(GObject *object, GAsyncResult *result, gpointer)
{
    WoodshedEngine *self = WOODSHED_ENGINE(object);
    --self->impl->saving;
    GError *error = nullptr;
    if (!g_task_propagate_boolean(G_TASK(result), &error))
    {
        fail(self, error->message);
        g_error_free(error);
        return;
    }
    remember_last(self, static_cast<save_job *>(g_task_get_task_data(G_TASK(result)))->path);
}

static void save_take(WoodshedEngine *self)
{
    engine_impl *impl = self->impl;
    const WoodshedSource *source = self->source;
    impl->source_name = source ? woodshed_source_get_name(self->source) : "";
    GDateTime *now = g_date_time_new_now_local();
    impl->recorded = take_string(g_date_time_format(now, "%Y-%m-%dT%H:%M:%S%:z"));
    g_date_time_unref(now);
    impl->file = new_take_path(impl->source_name);
    notify(self, PROP_FILE);

    char *dir = g_path_get_dirname(impl->file.c_str());
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);
    save_state_now(self);
    GTask *task = g_task_new(self, nullptr, on_saved, nullptr);
    g_task_set_task_data(task, new save_job{impl->take, impl->file},
                         [](gpointer data) { delete static_cast<save_job *>(data); });
    ++impl->saving;
    g_task_run_in_thread(task, save_in_thread);
    g_object_unref(task);
}

struct load_job
{
    std::string path;
    unsigned generation;
    std::shared_ptr<woodshed::audio_take> take;
    std::optional<woodshed::take_state> state;
};

static void load_in_thread(GTask *task, gpointer, gpointer data, GCancellable *)
{
    auto *job = static_cast<load_job *>(data);
    try
    {
        job->take = std::make_shared<woodshed::audio_take>(woodshed::load_audio(job->path));
        char *text = nullptr;
        if (g_file_get_contents(sidecar_of(job->path).c_str(), &text, nullptr, nullptr))
        {
            try
            {
                job->state = woodshed::parse_take_state(text);
            }
            catch (const std::exception &)
            {
                // Unreadable settings: open the audio alone.
            }
            g_free(text);
        }
        if (!job->state || job->state->tuning == 0.0)
        {
            if (!job->state)
                job->state.emplace();
            job->state->tuning = woodshed::detect_tuning(*job->take);
        }
        g_task_return_boolean(task, TRUE);
    }
    catch (const std::exception &error)
    {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", error.what());
    }
}

static void on_loaded(GObject *object, GAsyncResult *result, gpointer)
{
    WoodshedEngine *self = WOODSHED_ENGINE(object);
    engine_impl *impl = self->impl;
    auto *job = static_cast<load_job *>(g_task_get_task_data(G_TASK(result)));
    GError *error = nullptr;
    const bool ok = g_task_propagate_boolean(G_TASK(result), &error);
    // A recording or another open came first.
    if (job->generation != impl->load_generation || self->state == WOODSHED_STATE_RECORDING)
    {
        g_clear_error(&error);
        return;
    }
    if (!ok)
    {
        fail(self, error->message);
        g_error_free(error);
        return;
    }

    if (impl->state_save)
        save_state_now(self);
    impl->player.reset();
    impl->take = job->take;
    impl->file = job->path;
    const woodshed::take_state &state = *job->state;
    impl->source_name = state.source;
    impl->recorded = state.recorded;
    self->tuning = state.tuning;
    self->speed = std::clamp(state.speed, WOODSHED_ENGINE_MIN_SPEED, WOODSHED_ENGINE_MAX_SPEED);
    self->transpose = std::clamp(state.transpose, -WOODSHED_ENGINE_MAX_TRANSPOSE,
                                 WOODSHED_ENGINE_MAX_TRANSPOSE);
    self->fine_tune = std::clamp(state.fine_tune, -WOODSHED_ENGINE_MAX_FINE_TUNE,
                                 WOODSHED_ENGINE_MAX_FINE_TUNE);
    self->loop_start = to_frames(self, state.loop_start);
    self->loop_end = std::max(self->loop_start, to_frames(self, state.loop_end));
    self->looping = state.looping;
    self->position = to_frames(self, state.position);
    set_state(self, frames_of(self) ? WOODSHED_STATE_PAUSED : WOODSHED_STATE_IDLE);
    update_tick(self);
    for (int prop : {PROP_FILE, PROP_DURATION, PROP_POSITION, PROP_TUNING, PROP_SPEED, PROP_TRANSPOSE,
                     PROP_FINE_TUNE, PROP_LOOP_START, PROP_LOOP_END, PROP_HAS_LOOP, PROP_LOOPING})
        notify(self, prop);
    remember_last(self, impl->file);
}

// GObject

static void woodshed_engine_dispose(GObject *object)
{
    WoodshedEngine *self = WOODSHED_ENGINE(object);
    engine_impl *impl = self->impl;
    if (impl->state_save)
        save_state_now(self);
    impl->capture.reset();
    impl->player.reset();
    impl->session.reset();
    g_idle_remove_by_data(self);
    g_clear_handle_id(&impl->tick, g_source_remove);
    g_clear_object(&self->source);
    for (auto &[id, source] : impl->by_id)
        g_object_unref(source);
    impl->by_id.clear();
    g_clear_object(&impl->sources);
    G_OBJECT_CLASS(woodshed_engine_parent_class)->dispose(object);
}

static void woodshed_engine_finalize(GObject *object)
{
    delete WOODSHED_ENGINE(object)->impl;
    G_OBJECT_CLASS(woodshed_engine_parent_class)->finalize(object);
}

static void woodshed_engine_get_property(GObject *object, guint prop_id, GValue *value,
                                         GParamSpec *pspec)
{
    WoodshedEngine *self = WOODSHED_ENGINE(object);
    switch (prop_id)
    {
    case PROP_STATE:
        g_value_set_enum(value, self->state);
        break;
    case PROP_SPEED:
        g_value_set_double(value, self->speed);
        break;
    case PROP_DURATION:
        g_value_set_double(value, woodshed_engine_get_duration(self));
        break;
    case PROP_POSITION:
        g_value_set_double(value, woodshed_engine_get_position(self));
        break;
    case PROP_LEVEL:
        g_value_set_double(value, self->level);
        break;
    case PROP_SOURCES:
        g_value_set_object(value, self->impl->sources);
        break;
    case PROP_SOURCE:
        g_value_set_object(value, self->source);
        break;
    case PROP_LOOP_START:
        g_value_set_double(value, woodshed_engine_get_loop_start(self));
        break;
    case PROP_LOOP_END:
        g_value_set_double(value, woodshed_engine_get_loop_end(self));
        break;
    case PROP_HAS_LOOP:
        g_value_set_boolean(value, woodshed_engine_get_has_loop(self));
        break;
    case PROP_LOOPING:
        g_value_set_boolean(value, self->looping);
        break;
    case PROP_TRANSPOSE:
        g_value_set_int(value, self->transpose);
        break;
    case PROP_FINE_TUNE:
        g_value_set_double(value, self->fine_tune);
        break;
    case PROP_TUNING:
        g_value_set_double(value, self->tuning);
        break;
    case PROP_FILE:
        g_value_set_string(value, woodshed_engine_get_file(self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void woodshed_engine_set_property(GObject *object, guint prop_id, const GValue *value,
                                         GParamSpec *pspec)
{
    WoodshedEngine *self = WOODSHED_ENGINE(object);
    switch (prop_id)
    {
    case PROP_SPEED:
        woodshed_engine_set_speed(self, g_value_get_double(value));
        break;
    case PROP_POSITION:
        woodshed_engine_seek(self, g_value_get_double(value));
        break;
    case PROP_SOURCE:
        woodshed_engine_set_source(self, WOODSHED_SOURCE(g_value_get_object(value)));
        break;
    case PROP_LOOPING:
        woodshed_engine_set_looping(self, g_value_get_boolean(value));
        break;
    case PROP_TRANSPOSE:
        woodshed_engine_set_transpose(self, g_value_get_int(value));
        break;
    case PROP_FINE_TUNE:
        woodshed_engine_set_fine_tune(self, g_value_get_double(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void woodshed_engine_class_init(WoodshedEngineClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = woodshed_engine_dispose;
    object_class->finalize = woodshed_engine_finalize;
    object_class->get_property = woodshed_engine_get_property;
    object_class->set_property = woodshed_engine_set_property;

    const auto ro = static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    const auto rw = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                             G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_STATE] = g_param_spec_enum("state", nullptr, nullptr, WOODSHED_TYPE_STATE,
                                               WOODSHED_STATE_IDLE, ro);
    properties[PROP_SPEED] = g_param_spec_double("speed", nullptr, nullptr, WOODSHED_ENGINE_MIN_SPEED,
                                                 WOODSHED_ENGINE_MAX_SPEED, 1.0, rw);
    properties[PROP_DURATION] =
        g_param_spec_double("duration", nullptr, nullptr, 0.0, G_MAXDOUBLE, 0.0, ro);
    properties[PROP_POSITION] =
        g_param_spec_double("position", nullptr, nullptr, 0.0, G_MAXDOUBLE, 0.0, rw);
    properties[PROP_LEVEL] = g_param_spec_double("level", nullptr, nullptr, 0.0, G_MAXDOUBLE, 0.0, ro);
    properties[PROP_SOURCES] =
        g_param_spec_object("sources", nullptr, nullptr, G_TYPE_LIST_MODEL, ro);
    properties[PROP_SOURCE] =
        g_param_spec_object("source", nullptr, nullptr, WOODSHED_TYPE_SOURCE, rw);
    properties[PROP_LOOP_START] =
        g_param_spec_double("loop-start", nullptr, nullptr, 0.0, G_MAXDOUBLE, 0.0, ro);
    properties[PROP_LOOP_END] =
        g_param_spec_double("loop-end", nullptr, nullptr, 0.0, G_MAXDOUBLE, 0.0, ro);
    properties[PROP_HAS_LOOP] = g_param_spec_boolean("has-loop", nullptr, nullptr, FALSE, ro);
    properties[PROP_LOOPING] = g_param_spec_boolean("looping", nullptr, nullptr, FALSE, rw);
    properties[PROP_TRANSPOSE] =
        g_param_spec_int("transpose", nullptr, nullptr, -WOODSHED_ENGINE_MAX_TRANSPOSE,
                         WOODSHED_ENGINE_MAX_TRANSPOSE, 0, rw);
    properties[PROP_FINE_TUNE] =
        g_param_spec_double("fine-tune", nullptr, nullptr, -WOODSHED_ENGINE_MAX_FINE_TUNE,
                            WOODSHED_ENGINE_MAX_FINE_TUNE, 0.0, rw);
    properties[PROP_TUNING] = g_param_spec_double("tuning", nullptr, nullptr, -50.0, 50.0, 0.0, ro);
    properties[PROP_FILE] = g_param_spec_string("file", nullptr, nullptr, nullptr, ro);
    g_object_class_install_properties(object_class, N_PROPS, properties);

    /**
     * WoodshedEngine::error:
     * @self: the engine
     * @message: short, for the user
     */
    signals[SIGNAL_ERROR] = g_signal_new("error", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
                                         nullptr, nullptr, nullptr, G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void woodshed_engine_init(WoodshedEngine *self)
{
    self->state = WOODSHED_STATE_IDLE;
    self->speed = 1.0;
    self->impl = new engine_impl{};
    self->impl->sources = g_list_store_new(WOODSHED_TYPE_SOURCE);
    try
    {
        self->impl->session = std::make_unique<woodshed::pw_session>();
        self->impl->session->set_on_nodes_changed([self]
        {
            if (!self->impl->nodes_pending.exchange(true))
                g_idle_add(on_nodes_idle, self);
        });
    }
    catch (const std::exception &error)
    {
        g_warning("%s", error.what());
    }
    refresh_sources(self);
}

// Public API

WoodshedEngine *woodshed_engine_new(void)
{
    return WOODSHED_ENGINE(g_object_new(WOODSHED_TYPE_ENGINE, nullptr));
}

const woodshed::audio_take *woodshed_engine_peek_take(WoodshedEngine *self)
{
    return self->impl->take.get();
}

WoodshedState woodshed_engine_get_state(WoodshedEngine *self)
{
    g_return_val_if_fail(WOODSHED_IS_ENGINE(self), WOODSHED_STATE_IDLE);
    return self->state;
}

double woodshed_engine_get_speed(WoodshedEngine *self)
{
    g_return_val_if_fail(WOODSHED_IS_ENGINE(self), 1.0);
    return self->speed;
}

void woodshed_engine_set_speed(WoodshedEngine *self, double speed)
{
    g_return_if_fail(WOODSHED_IS_ENGINE(self));
    speed = std::clamp(speed, WOODSHED_ENGINE_MIN_SPEED, WOODSHED_ENGINE_MAX_SPEED);
    if (std::abs(speed - 1.0) < 1e-3)
        speed = 1.0;
    if (speed == self->speed)
        return;
    self->speed = speed;
    if (self->impl->player)
        self->impl->player->set_speed(speed);
    schedule_state_save(self);
    notify(self, PROP_SPEED);
}

static double pitch_scale(WoodshedEngine *self)
{
    return std::exp2((self->transpose * 100.0 + self->fine_tune) / 1200.0);
}

static void push_pitch(WoodshedEngine *self)
{
    if (self->impl->player)
        self->impl->player->set_pitch(pitch_scale(self));
}

int woodshed_engine_get_transpose(WoodshedEngine *self)
{
    g_return_val_if_fail(WOODSHED_IS_ENGINE(self), 0);
    return self->transpose;
}

void woodshed_engine_set_transpose(WoodshedEngine *self, int semitones)
{
    g_return_if_fail(WOODSHED_IS_ENGINE(self));
    semitones = std::clamp(semitones, -WOODSHED_ENGINE_MAX_TRANSPOSE, WOODSHED_ENGINE_MAX_TRANSPOSE);
    if (semitones == self->transpose)
        return;
    self->transpose = semitones;
    push_pitch(self);
    schedule_state_save(self);
    notify(self, PROP_TRANSPOSE);
}

double woodshed_engine_get_fine_tune(WoodshedEngine *self)
{
    g_return_val_if_fail(WOODSHED_IS_ENGINE(self), 0.0);
    return self->fine_tune;
}

void woodshed_engine_set_fine_tune(WoodshedEngine *self, double cents)
{
    g_return_if_fail(WOODSHED_IS_ENGINE(self));
    cents = std::round(std::clamp(cents, -WOODSHED_ENGINE_MAX_FINE_TUNE, WOODSHED_ENGINE_MAX_FINE_TUNE) * 10.0) / 10.0;
    if (cents == self->fine_tune)
        return;
    self->fine_tune = cents;
    push_pitch(self);
    schedule_state_save(self);
    notify(self, PROP_FINE_TUNE);
}

double woodshed_engine_get_tuning(WoodshedEngine *self)
{
    g_return_val_if_fail(WOODSHED_IS_ENGINE(self), 0.0);
    return self->tuning;
}

void woodshed_engine_match_a440(WoodshedEngine *self)
{
    g_return_if_fail(WOODSHED_IS_ENGINE(self));
    woodshed_engine_set_fine_tune(self, -self->tuning);
}

double woodshed_engine_get_duration(WoodshedEngine *self)
{
    g_return_val_if_fail(WOODSHED_IS_ENGINE(self), 0.0);
    return to_seconds(frames_of(self));
}

double woodshed_engine_get_position(WoodshedEngine *self)
{
    g_return_val_if_fail(WOODSHED_IS_ENGINE(self), 0.0);
    return to_seconds(self->position);
}

void woodshed_engine_seek(WoodshedEngine *self, double seconds)
{
    g_return_if_fail(WOODSHED_IS_ENGINE(self));
    self->position = to_frames(self, seconds);
    if (self->impl->player)
        self->impl->player->seek(self->position);
    schedule_state_save(self);
    notify(self, PROP_POSITION);
}

double woodshed_engine_get_level(WoodshedEngine *self)
{
    g_return_val_if_fail(WOODSHED_IS_ENGINE(self), 0.0);
    return self->level;
}

GListModel *woodshed_engine_get_sources(WoodshedEngine *self)
{
    g_return_val_if_fail(WOODSHED_IS_ENGINE(self), nullptr);
    return G_LIST_MODEL(self->impl->sources);
}

WoodshedSource *woodshed_engine_get_source(WoodshedEngine *self)
{
    g_return_val_if_fail(WOODSHED_IS_ENGINE(self), nullptr);
    return self->source;
}

void woodshed_engine_set_source(WoodshedEngine *self, WoodshedSource *source)
{
    g_return_if_fail(WOODSHED_IS_ENGINE(self));
    if (g_set_object(&self->source, source))
        notify(self, PROP_SOURCE);
}

void woodshed_engine_record_start(WoodshedEngine *self)
{
    g_return_if_fail(WOODSHED_IS_ENGINE(self));
    engine_impl *impl = self->impl;
    if (self->state == WOODSHED_STATE_RECORDING)
        return;
    if (!impl->session)
    {
        fail(self, "No PipeWire");
        return;
    }
    if (impl->player)
        stop_player(self);
    if (impl->state_save)
        save_state_now(self);
    ++impl->load_generation;

    WoodshedSource *source = resolve_source(self);
    woodshed_engine_set_source(self, source);
    woodshed::capture_target target{};
    if (source)
    {
        target.serial = woodshed_source_get_serial(source);
        target.monitor = woodshed_source_get_kind(source) == WOODSHED_SOURCE_KIND_OUTPUT;
    }
    try
    {
        impl->capture.reset();
        impl->take = std::make_shared<woodshed::audio_take>();
        impl->capture = std::make_unique<woodshed::capture>(*impl->session, target);
    }
    catch (const std::exception &error)
    {
        fail(self, error.what());
        return;
    }
    self->position = 0;
    self->loop_start = self->loop_end = 0;
    self->tuning = 0.0;
    impl->file.clear();
    notify(self, PROP_FILE);
    notify(self, PROP_TUNING);
    set_state(self, WOODSHED_STATE_RECORDING);
    update_tick(self);
    for (int prop : {PROP_DURATION, PROP_POSITION, PROP_LOOP_START, PROP_LOOP_END, PROP_HAS_LOOP})
        notify(self, prop);
}

void woodshed_engine_record_stop(WoodshedEngine *self)
{
    g_return_if_fail(WOODSHED_IS_ENGINE(self));
    engine_impl *impl = self->impl;
    if (self->state != WOODSHED_STATE_RECORDING)
        return;
    impl->capture->drain(*impl->take);
    impl->capture.reset();
    self->level = 0.0;
    self->tuning = woodshed::detect_tuning(*impl->take);
    notify(self, PROP_TUNING);
    set_state(self, frames_of(self) ? WOODSHED_STATE_PAUSED : WOODSHED_STATE_IDLE);
    if (frames_of(self))
        save_take(self);
    update_tick(self);
    notify(self, PROP_DURATION);
    notify(self, PROP_LEVEL);
}

void woodshed_engine_toggle_record(WoodshedEngine *self)
{
    if (woodshed_engine_get_state(self) == WOODSHED_STATE_RECORDING)
        woodshed_engine_record_stop(self);
    else
        woodshed_engine_record_start(self);
}

void woodshed_engine_play(WoodshedEngine *self)
{
    g_return_if_fail(WOODSHED_IS_ENGINE(self));
    engine_impl *impl = self->impl;
    if (self->state != WOODSHED_STATE_PAUSED || !frames_of(self))
        return;
    const woodshed::loop_region loop = current_loop(self);
    std::size_t position = self->position >= frames_of(self) ? 0 : self->position;
    if (loop.enabled && (position < loop.start || position >= loop.end))
        position = loop.start;
    try
    {
        impl->player = std::make_unique<woodshed::player>(*impl->session, impl->take, position, loop,
                                                          self->speed, pitch_scale(self));
    }
    catch (const std::exception &error)
    {
        fail(self, error.what());
        return;
    }
    self->position = position;
    set_state(self, WOODSHED_STATE_PLAYING);
    update_tick(self);
    notify(self, PROP_POSITION);
}

void woodshed_engine_pause(WoodshedEngine *self)
{
    g_return_if_fail(WOODSHED_IS_ENGINE(self));
    if (self->impl->player)
        stop_player(self);
}

void woodshed_engine_toggle_play(WoodshedEngine *self)
{
    if (woodshed_engine_get_state(self) == WOODSHED_STATE_PLAYING)
        woodshed_engine_pause(self);
    else
        woodshed_engine_play(self);
}

double woodshed_engine_get_loop_start(WoodshedEngine *self)
{
    g_return_val_if_fail(WOODSHED_IS_ENGINE(self), 0.0);
    return to_seconds(self->loop_start);
}

double woodshed_engine_get_loop_end(WoodshedEngine *self)
{
    g_return_val_if_fail(WOODSHED_IS_ENGINE(self), 0.0);
    return to_seconds(self->loop_end);
}

gboolean woodshed_engine_get_has_loop(WoodshedEngine *self)
{
    g_return_val_if_fail(WOODSHED_IS_ENGINE(self), FALSE);
    return self->loop_end > self->loop_start;
}

static void set_loop_frames(WoodshedEngine *self, std::size_t start, std::size_t end)
{
    const bool had = woodshed_engine_get_has_loop(self);
    self->loop_start = std::min(start, end);
    self->loop_end = std::max(start, end);
    push_loop(self);
    schedule_state_save(self);
    notify(self, PROP_LOOP_START);
    notify(self, PROP_LOOP_END);
    if (had != static_cast<bool>(woodshed_engine_get_has_loop(self)))
        notify(self, PROP_HAS_LOOP);
}

void woodshed_engine_set_loop(WoodshedEngine *self, double start, double end)
{
    g_return_if_fail(WOODSHED_IS_ENGINE(self));
    set_loop_frames(self, to_frames(self, start), to_frames(self, end));
}

void woodshed_engine_clear_loop(WoodshedEngine *self)
{
    g_return_if_fail(WOODSHED_IS_ENGINE(self));
    set_loop_frames(self, 0, 0);
}

gboolean woodshed_engine_get_looping(WoodshedEngine *self)
{
    g_return_val_if_fail(WOODSHED_IS_ENGINE(self), FALSE);
    return self->looping;
}

void woodshed_engine_set_looping(WoodshedEngine *self, gboolean looping)
{
    g_return_if_fail(WOODSHED_IS_ENGINE(self));
    looping = !!looping;
    if (self->looping == looping)
        return;
    self->looping = looping;
    push_loop(self);
    schedule_state_save(self);
    notify(self, PROP_LOOPING);
}

const char *woodshed_engine_get_file(WoodshedEngine *self)
{
    g_return_val_if_fail(WOODSHED_IS_ENGINE(self), nullptr);
    return self->impl->file.empty() ? nullptr : self->impl->file.c_str();
}

void woodshed_engine_open(WoodshedEngine *self, const char *path)
{
    g_return_if_fail(WOODSHED_IS_ENGINE(self));
    g_return_if_fail(path != nullptr);
    if (self->state == WOODSHED_STATE_RECORDING)
        return;
    GTask *task = g_task_new(self, nullptr, on_loaded, nullptr);
    g_task_set_task_data(task, new load_job{path, ++self->impl->load_generation, nullptr, std::nullopt},
                         [](gpointer data) { delete static_cast<load_job *>(data); });
    g_task_run_in_thread(task, load_in_thread);
    g_object_unref(task);
}

gboolean woodshed_engine_open_last(WoodshedEngine *self)
{
    g_return_val_if_fail(WOODSHED_IS_ENGINE(self), FALSE);
    char *text = nullptr;
    if (!g_file_get_contents(last_take_pointer().c_str(), &text, nullptr, nullptr))
        return FALSE;
    const std::string path = g_strstrip(text);
    g_free(text);
    if (path.empty() || !g_file_test(path.c_str(), G_FILE_TEST_IS_REGULAR))
        return FALSE;
    woodshed_engine_open(self, path.c_str());
    return TRUE;
}

void woodshed_engine_flush(WoodshedEngine *self)
{
    g_return_if_fail(WOODSHED_IS_ENGINE(self));
    while (self->impl->saving > 0)
        g_main_context_iteration(nullptr, TRUE);
    save_state_now(self);
}
