#include "woodshed-waveform.h"
#include "woodshed-enums.h"
#include "woodshed-private.hpp"

#include "woodshed/spectrogram.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

using woodshed::spectrogram;

static constexpr double MIN_FIT_SPAN = 8.0;
static constexpr double MIN_SPAN = 0.05;
static constexpr double HANDLE_REACH = 24.0;
static constexpr double TAP_SLOP = 10.0;
static constexpr float KNOB_RADIUS = 10.0f;
static constexpr double OVERVIEW_HEIGHT = 8.0;
static constexpr double OVERVIEW_MARGIN = 4.0;
static constexpr GdkRGBA ACCENT = {0.21f, 0.52f, 0.89f, 1.0f};
static constexpr GdkRGBA WHITE = {1.0f, 1.0f, 1.0f, 1.0f};

static constexpr double KEYS_WIDTH = 40.0;
static constexpr double BLACK_KEY_WIDTH = 24.0;
static constexpr double TOP_NOTE = spectrogram::HIGHEST_NOTE + 0.5;
static constexpr double BOTTOM_NOTE = spectrogram::LOWEST_NOTE - 0.5;
static constexpr int FIRST_KEY = spectrogram::LOWEST_NOTE - WOODSHED_ENGINE_MAX_TRANSPOSE - 1;
static constexpr int LAST_KEY = spectrogram::HIGHEST_NOTE + WOODSHED_ENGINE_MAX_TRANSPOSE + 1;
static constexpr std::size_t TILE_COLUMNS = 512;
static constexpr std::size_t STEP_COLUMNS = 32;
static constexpr int RANGE_STEPS = static_cast<int>(70 * spectrogram::STEPS_PER_DB);
static constexpr int GAIN_STEPS = static_cast<int>(6 * spectrogram::STEPS_PER_DB);

using rgb = std::array<std::uint8_t, 3>;

struct spectrum_cache
{
    explicit spectrum_cache(bool reassign) : data{reassign} {}

    spectrogram data;
    const woodshed::audio_take *take = nullptr;
    int top = -1;
    std::array<rgb, 256> palette{};
    std::vector<GdkTexture *> tiles;
    std::vector<std::size_t> tile_columns;

    void clear_tiles()
    {
        for (GdkTexture *tile : tiles)
            if (tile)
                g_object_unref(tile);
        tiles.clear();
        tile_columns.clear();
    }

    ~spectrum_cache() { clear_tiles(); }
};

struct _WoodshedWaveform
{
    GtkWidget parent_instance;
    WoodshedEngine *engine;
    WoodshedDisplay display;

    double view_start;
    double view_span;
    gboolean fit;

    GskRenderNode *wave;
    int wave_width;
    int wave_height;
    double wave_start;
    double wave_span;
    std::size_t wave_frames;
    GdkRGBA wave_color;

    GtkGesture *drag;
    double drag_x;
    double drag_anchor;
    gboolean drag_moved;
    gboolean drag_cancelled;
    double saved_start;
    double saved_end;

    double zoom_span;
    double zoom_anchor;

    double pointer_x;
    const char *cursor;
    int hover_note;

    gboolean focused;
    spectrum_cache *spectra[2];
    guint spectrum_idle;
};

G_DEFINE_FINAL_TYPE(WoodshedWaveform, woodshed_waveform, GTK_TYPE_WIDGET)

enum
{
    PROP_0,
    PROP_ENGINE,
    PROP_DISPLAY,
    PROP_FOCUSED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static graphene_rect_t rect(double x, double y, double w, double h)
{
    graphene_rect_t r;
    graphene_rect_init(&r, static_cast<float>(x), static_cast<float>(y), static_cast<float>(w),
                       static_cast<float>(h));
    return r;
}

static GdkRGBA with_alpha(GdkRGBA color, float alpha)
{
    color.alpha *= alpha;
    return color;
}

static const woodshed::audio_take *take_of(WoodshedWaveform *self)
{
    const woodshed::audio_take *take =
        self->engine ? woodshed_engine_peek_take(self->engine) : nullptr;
    return take && take->frames() ? take : nullptr;
}

static double duration_of(WoodshedWaveform *self)
{
    const woodshed::audio_take *take = take_of(self);
    return take ? take->duration() : 0.0;
}

static double full_span(WoodshedWaveform *self)
{
    return std::max(duration_of(self), MIN_FIT_SPAN);
}

static void get_view(WoodshedWaveform *self, double *start, double *span)
{
    const double full = full_span(self);
    *span = self->fit ? full : std::clamp(self->view_span, MIN_SPAN, full);
    *start = self->fit ? 0.0 : std::clamp(self->view_start, 0.0, full - *span);
}

static void set_view(WoodshedWaveform *self, double start, double span)
{
    const double full = full_span(self);
    span = std::clamp(span, MIN_SPAN, full);
    self->fit = span >= full * 0.999;
    self->view_span = span;
    self->view_start = std::clamp(start, 0.0, full - span);
    gtk_widget_queue_draw(GTK_WIDGET(self));
}

static double area_left(WoodshedWaveform *self)
{
    return self->display == WOODSHED_DISPLAY_SPECTROGRAM ? KEYS_WIDTH : 0.0;
}

static double area_width(WoodshedWaveform *self)
{
    return std::max(1.0, gtk_widget_get_width(GTK_WIDGET(self)) - area_left(self));
}

static double time_at(WoodshedWaveform *self, double x)
{
    double start, span;
    get_view(self, &start, &span);
    return start + (x - area_left(self)) / area_width(self) * span;
}

static double x_at(WoodshedWaveform *self, double t)
{
    double start, span;
    get_view(self, &start, &span);
    return area_left(self) + (t - start) / span * area_width(self);
}

static double note_y(double note, int height)
{
    return (TOP_NOTE - note) / (TOP_NOTE - BOTTOM_NOTE) * height;
}

static spectrum_cache &active_spectrum(WoodshedWaveform *self)
{
    return *self->spectra[self->focused ? 1 : 0];
}

// Grid note where a key sits. Keys follow the take's tuning and name what is heard
// after transposing; the analysis grid stays on A440.
static double key_offset(WoodshedWaveform *self)
{
    return woodshed_engine_get_tuning(self->engine) / 100.0 -
           woodshed_engine_get_transpose(self->engine);
}

static double key_y(WoodshedWaveform *self, double note, int height)
{
    return note_y(note + key_offset(self), height);
}

static int key_at(WoodshedWaveform *self, double y, int height)
{
    const double note = TOP_NOTE - y / height * (TOP_NOTE - BOTTOM_NOTE) - key_offset(self);
    return std::clamp(static_cast<int>(std::lround(note)), FIRST_KEY, LAST_KEY);
}

static bool is_black(int note)
{
    const int pitch = note % 12;
    return pitch == 1 || pitch == 3 || pitch == 6 || pitch == 8 || pitch == 10;
}

static char *note_name(int note)
{
    static const char *const NAMES[] = {"C", "D♭", "D", "E♭", "E", "F",
                                        "F♯", "G", "A♭", "A", "B♭", "B"};
    return g_strdup_printf("%s%d", NAMES[note % 12], note / 12 - 1);
}

// Drawing

static woodshed::peak raw_range(const woodshed::audio_take &take, std::size_t begin, std::size_t end)
{
    end = std::min(end, take.frames());
    woodshed::peak p{0.0f, 0.0f};
    for (std::size_t i = begin; i < end; ++i)
    {
        p.min = std::min({p.min, take.left[i], take.right[i]});
        p.max = std::max({p.max, take.left[i], take.right[i]});
    }
    return p;
}

static GskRenderNode *build_wave(WoodshedWaveform *self, const woodshed::audio_take &take, int width,
                                 int height, double start, double span, const GdkRGBA &color)
{
    const int columns = std::min(width, static_cast<int>(std::ceil(x_at(self, take.duration()))));
    if (columns <= 0)
        return nullptr;
    // Scaled to the take's loudest peak, so quiet captures stay readable.
    const woodshed::peak loudest = take.peaks.range(0, take.frames());
    const double gain = 1.0 / std::max({-loudest.min, loudest.max, 0.01f});
    const double mid = height / 2.0;
    const double scale = mid * 0.9 * gain;
    const double frames_per_px = span * woodshed::SAMPLE_RATE / width;
    const auto step = std::max<std::size_t>(1, static_cast<std::size_t>(frames_per_px));

    std::vector<float> top(columns);
    std::vector<float> bottom(columns);
    for (int x = 0; x < columns; ++x)
    {
        const auto first =
            static_cast<std::size_t>((start + x * span / width) * woodshed::SAMPLE_RATE);
        const woodshed::peak p = frames_per_px < woodshed::peak_pyramid::BASE_BIN
                                     ? raw_range(take, first, first + step)
                                     : take.peaks.range(first, first + step);
        top[x] = static_cast<float>(mid - p.max * scale);
        bottom[x] = std::max(static_cast<float>(mid - p.min * scale), top[x] + 1.0f);
    }

    GskPathBuilder *builder = gsk_path_builder_new();
    gsk_path_builder_move_to(builder, 0.0f, top[0]);
    for (int x = 0; x < columns; ++x)
        gsk_path_builder_line_to(builder, x + 0.5f, top[x]);
    gsk_path_builder_line_to(builder, static_cast<float>(columns), top[columns - 1]);
    gsk_path_builder_line_to(builder, static_cast<float>(columns), bottom[columns - 1]);
    for (int x = columns - 1; x >= 0; --x)
        gsk_path_builder_line_to(builder, x + 0.5f, bottom[x]);
    gsk_path_builder_line_to(builder, 0.0f, bottom[0]);
    gsk_path_builder_close(builder);
    GskPath *path = gsk_path_builder_free_to_path(builder);

    GtkSnapshot *snapshot = gtk_snapshot_new();
    gtk_snapshot_append_fill(snapshot, path, GSK_FILL_RULE_WINDING, &color);
    gsk_path_unref(path);
    return gtk_snapshot_free_to_node(snapshot);
}

static void draw_text(GtkSnapshot *snapshot, GtkWidget *widget, const char *text, double x,
                      double y_centre, const GdkRGBA &color)
{
    PangoLayout *layout = gtk_widget_create_pango_layout(widget, text);
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_scale_new(PANGO_SCALE_X_SMALL));
    pango_layout_set_attributes(layout, attrs);
    pango_attr_list_unref(attrs);
    int width, height;
    pango_layout_get_pixel_size(layout, &width, &height);
    const double y = std::clamp(y_centre - height / 2.0, 0.0,
                                std::max(0.0, gtk_widget_get_height(widget) - height * 1.0));
    gtk_snapshot_save(snapshot);
    const graphene_point_t at = GRAPHENE_POINT_INIT(static_cast<float>(x), static_cast<float>(y));
    gtk_snapshot_translate(snapshot, &at);
    gtk_snapshot_append_layout(snapshot, layout, &color);
    gtk_snapshot_restore(snapshot);
    g_object_unref(layout);
}

// Magma, sampled.
static rgb palette_color(double v)
{
    static constexpr std::array<std::array<double, 3>, 5> STOPS = {{
        {0, 0, 4}, {81, 18, 124}, {183, 55, 121}, {252, 137, 97}, {252, 253, 191}}};
    v = std::clamp(v, 0.0, 1.0) * (STOPS.size() - 1);
    const auto i = std::min(static_cast<std::size_t>(v), STOPS.size() - 2);
    const double f = v - i;
    rgb out;
    for (std::size_t c = 0; c < 3; ++c)
        out[c] = static_cast<std::uint8_t>(STOPS[i][c] + f * (STOPS[i + 1][c] - STOPS[i][c]));
    return out;
}

static void sync_spectrum(WoodshedWaveform *self, const woodshed::audio_take *take)
{
    spectrum_cache &cache = active_spectrum(self);
    if (cache.take != take)
    {
        cache.take = take;
        cache.data.clear();
        cache.clear_tiles();
    }
    // Gain follows the loudest bin, in 6 dB steps.
    const int top = std::min(255, (cache.data.loudest() + GAIN_STEPS - 1) / GAIN_STEPS * GAIN_STEPS);
    if (top != cache.top)
    {
        cache.top = top;
        for (int v = 0; v < 256; ++v)
            cache.palette[v] = palette_color(1.0 - static_cast<double>(top - v) / RANGE_STEPS);
        cache.clear_tiles();
    }
}

static GdkTexture *build_tile(const spectrum_cache &cache, std::size_t first, std::size_t count)
{
    const std::size_t stride = count * 3;
    auto *pixels = static_cast<std::uint8_t *>(g_malloc(stride * spectrogram::ROWS));
    for (std::size_t x = 0; x < count; ++x)
    {
        const std::uint8_t *column = cache.data.column(first + x);
        for (std::size_t y = 0; y < spectrogram::ROWS; ++y)
        {
            const rgb &color = cache.palette[column[spectrogram::ROWS - 1 - y]];
            std::copy(color.begin(), color.end(), pixels + y * stride + x * 3);
        }
    }
    GBytes *bytes = g_bytes_new_take(pixels, stride * spectrogram::ROWS);
    GdkTexture *texture = gdk_memory_texture_new(static_cast<int>(count), spectrogram::ROWS,
                                                 GDK_MEMORY_R8G8B8, bytes, stride);
    g_bytes_unref(bytes);
    return texture;
}

static void draw_spectrum(WoodshedWaveform *self, GtkSnapshot *snapshot,
                          const woodshed::audio_take &take, int width, int height)
{
    spectrum_cache &cache = active_spectrum(self);
    sync_spectrum(self, &take);

    const rgb floor = cache.palette[0];
    const GdkRGBA backdrop = {floor[0] / 255.0f, floor[1] / 255.0f, floor[2] / 255.0f, 1.0f};
    const double end_x = x_at(self, take.duration());
    const graphene_rect_t back = rect(KEYS_WIDTH, 0.0, std::max(0.0, end_x - KEYS_WIDTH), height);
    gtk_snapshot_append_color(snapshot, &backdrop, &back);

    const std::size_t columns = cache.data.columns();
    const std::size_t tile_count = (columns + TILE_COLUMNS - 1) / TILE_COLUMNS;
    cache.tiles.resize(tile_count, nullptr);
    cache.tile_columns.resize(tile_count, 0);
    const double half = spectrogram::column_time(1) / 2.0;
    for (std::size_t k = 0; k < tile_count; ++k)
    {
        const std::size_t first = k * TILE_COLUMNS;
        const std::size_t count = std::min(TILE_COLUMNS, columns - first);
        const double x0 = x_at(self, spectrogram::column_time(first) - half);
        const double x1 = x_at(self, spectrogram::column_time(first + count) - half);
        if (x1 < KEYS_WIDTH || x0 > width)
            continue;
        if (!cache.tiles[k] || cache.tile_columns[k] != count)
        {
            g_clear_object(&cache.tiles[k]);
            cache.tiles[k] = build_tile(cache, first, count);
            cache.tile_columns[k] = count;
        }
        const graphene_rect_t bounds = rect(x0, 0.0, x1 - x0, height);
        gtk_snapshot_append_scaled_texture(snapshot, cache.tiles[k], GSK_SCALING_FILTER_TRILINEAR,
                                           &bounds);
    }

    const GdkRGBA octave = with_alpha(WHITE, 0.15f);
    for (int note = 12; note <= LAST_KEY; note += 12)
    {
        const graphene_rect_t line = rect(KEYS_WIDTH, key_y(self, note - 0.5, height), width, 1.0);
        gtk_snapshot_append_color(snapshot, &octave, &line);
    }
    if (self->hover_note >= 0)
    {
        const double y0 = key_y(self, self->hover_note + 0.5, height);
        const GdkRGBA band = with_alpha(WHITE, 0.12f);
        const graphene_rect_t area =
            rect(KEYS_WIDTH, y0, width, key_y(self, self->hover_note - 0.5, height) - y0);
        gtk_snapshot_append_color(snapshot, &band, &area);
    }
}

static void draw_keys(WoodshedWaveform *self, GtkSnapshot *snapshot, int height)
{
    GtkWidget *widget = GTK_WIDGET(self);
    const GdkRGBA ivory = {0.96f, 0.96f, 0.95f, 1.0f};
    const GdkRGBA ebony = {0.13f, 0.13f, 0.14f, 1.0f};
    const GdkRGBA seam = {0.7f, 0.7f, 0.7f, 1.0f};
    const GdkRGBA label = {0.35f, 0.35f, 0.35f, 1.0f};
    const graphene_rect_t strip = rect(0.0, 0.0, KEYS_WIDTH, height);
    gtk_snapshot_append_color(snapshot, &ivory, &strip);

    for (int note = FIRST_KEY; note <= LAST_KEY; ++note)
    {
        const double y0 = key_y(self, note + 0.5, height);
        const double y1 = key_y(self, note - 0.5, height);
        const bool black = is_black(note);
        const bool hovered = note == self->hover_note;
        if (black || hovered)
        {
            const graphene_rect_t key = rect(0.0, y0, black ? BLACK_KEY_WIDTH : KEYS_WIDTH, y1 - y0);
            gtk_snapshot_append_color(snapshot, hovered ? &ACCENT : &ebony, &key);
        }
        const int pitch = note % 12;
        if (black)
        {
            const graphene_rect_t line =
                rect(BLACK_KEY_WIDTH, (y0 + y1) / 2.0, KEYS_WIDTH - BLACK_KEY_WIDTH, 1.0);
            gtk_snapshot_append_color(snapshot, &seam, &line);
        }
        else if (pitch == 0 || pitch == 5)
        {
            const graphene_rect_t line = rect(0.0, y1 - 0.5, KEYS_WIDTH, 1.0);
            gtk_snapshot_append_color(snapshot, &seam, &line);
        }
        if (pitch == 0 && !hovered)
        {
            char *name = note_name(note);
            draw_text(snapshot, widget, name, BLACK_KEY_WIDTH + 1.0, (y0 + y1) / 2.0, label);
            g_free(name);
        }
    }
}

static void draw_tuning(WoodshedWaveform *self, GtkSnapshot *snapshot, int height)
{
    const double hz = 440.0 * std::exp2(woodshed_engine_get_tuning(self->engine) / 1200.0);
    char *text = g_strdup_printf("A4 = %.1f Hz", hz);
    const GdkRGBA backing = {0.0f, 0.0f, 0.0f, 0.6f};
    GskRoundedRect pill;
    const graphene_rect_t bounds = rect(KEYS_WIDTH + 4.0, height - 24.0, 84.0, 20.0);
    gsk_rounded_rect_init_from_rect(&pill, &bounds, 10.0f);
    gtk_snapshot_push_rounded_clip(snapshot, &pill);
    gtk_snapshot_append_color(snapshot, &backing, &bounds);
    gtk_snapshot_pop(snapshot);
    draw_text(snapshot, GTK_WIDGET(self), text, KEYS_WIDTH + 12.0, height - 14.0, with_alpha(WHITE, 0.8f));
    g_free(text);
}

static void draw_hover_label(WoodshedWaveform *self, GtkSnapshot *snapshot, int height)
{
    if (self->hover_note < 0)
        return;
    char *name = note_name(self->hover_note);
    draw_text(snapshot, GTK_WIDGET(self), name, KEYS_WIDTH + 6.0, key_y(self, self->hover_note, height) - 12.0,
              WHITE);
    g_free(name);
}

static GskRoundedRect pill(double x0, double x1, double y)
{
    const graphene_rect_t bounds = rect(x0, y, std::max(x1 - x0, OVERVIEW_HEIGHT), OVERVIEW_HEIGHT);
    GskRoundedRect out;
    gsk_rounded_rect_init_from_rect(&out, &bounds, static_cast<float>(OVERVIEW_HEIGHT / 2.0));
    return out;
}

// Where the view sits in the take. Shown when zoomed in.
static void draw_overview(WoodshedWaveform *self, GtkSnapshot *snapshot, const GdkRGBA &ink)
{
    double start, span;
    get_view(self, &start, &span);
    const double full = full_span(self);
    if (span >= full * 0.999)
        return;
    const double left = area_left(self) + OVERVIEW_MARGIN;
    const double width = area_width(self) - 2.0 * OVERVIEW_MARGIN;
    const auto x = [&](double t) { return left + t / full * width; };
    const double y = OVERVIEW_MARGIN;

    const GskRoundedRect track = pill(left, left + width, y);
    const GdkRGBA track_color = with_alpha(ink, 0.15f);
    gtk_snapshot_push_rounded_clip(snapshot, &track);
    gtk_snapshot_append_color(snapshot, &track_color, &track.bounds);
    if (woodshed_engine_get_has_loop(self->engine))
    {
        const GdkRGBA color = woodshed_engine_get_looping(self->engine) ? ACCENT : with_alpha(ink, 0.4f);
        const graphene_rect_t loop = rect(x(woodshed_engine_get_loop_start(self->engine)), y,
                                          std::max(2.0, x(woodshed_engine_get_loop_end(self->engine)) -
                                                            x(woodshed_engine_get_loop_start(self->engine))),
                                          OVERVIEW_HEIGHT);
        gtk_snapshot_append_color(snapshot, &color, &loop);
    }
    const graphene_rect_t playhead =
        rect(x(woodshed_engine_get_position(self->engine)) - 1.0, y, 2.0, OVERVIEW_HEIGHT);
    gtk_snapshot_append_color(snapshot, &ink, &playhead);
    gtk_snapshot_pop(snapshot);

    const GskRoundedRect thumb = pill(x(start), x(start + span), y);
    const float border[4] = {1.5f, 1.5f, 1.5f, 1.5f};
    const GdkRGBA thumb_color = with_alpha(ink, 0.8f);
    const GdkRGBA colors[4] = {thumb_color, thumb_color, thumb_color, thumb_color};
    gtk_snapshot_append_border(snapshot, &thumb, border, colors);
}

static void draw_handle(GtkSnapshot *snapshot, double x, int height, const GdkRGBA &color)
{
    const graphene_rect_t line = rect(x - 1.0, 0.0, 2.0, height);
    gtk_snapshot_append_color(snapshot, &color, &line);
    GskRoundedRect knob;
    const graphene_rect_t bounds = rect(x - KNOB_RADIUS, height - 2.0 * KNOB_RADIUS - 6.0,
                                        2.0 * KNOB_RADIUS, 2.0 * KNOB_RADIUS);
    gsk_rounded_rect_init_from_rect(&knob, &bounds, KNOB_RADIUS);
    gtk_snapshot_push_rounded_clip(snapshot, &knob);
    gtk_snapshot_append_color(snapshot, &color, &bounds);
    gtk_snapshot_pop(snapshot);
}

static void woodshed_waveform_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
    WoodshedWaveform *self = WOODSHED_WAVEFORM(widget);
    const woodshed::audio_take *take = take_of(self);
    if (!take)
        return;
    const int width = gtk_widget_get_width(widget);
    const int height = gtk_widget_get_height(widget);
    const bool spectrum = self->display == WOODSHED_DISPLAY_SPECTROGRAM;
    double start, span;
    get_view(self, &start, &span);
    GdkRGBA fg;
    gtk_widget_get_color(widget, &fg);
    const GdkRGBA ink = spectrum ? WHITE : fg;

    const bool has_loop = woodshed_engine_get_has_loop(self->engine);
    const bool looping = woodshed_engine_get_looping(self->engine);
    const double x0 = x_at(self, woodshed_engine_get_loop_start(self->engine));
    const double x1 = x_at(self, woodshed_engine_get_loop_end(self->engine));
    const GdkRGBA selection = looping ? with_alpha(ACCENT, spectrum ? 0.25f : 0.3f)
                                      : with_alpha(ink, spectrum ? 0.15f : 0.12f);
    const graphene_rect_t selected = rect(x0, 0.0, x1 - x0, height);

    if (spectrum)
    {
        draw_keys(self, snapshot, height);
        const graphene_rect_t area = rect(KEYS_WIDTH, 0.0, width - KEYS_WIDTH, height);
        gtk_snapshot_push_clip(snapshot, &area);
        draw_spectrum(self, snapshot, *take, width, height);
        if (has_loop)
            gtk_snapshot_append_color(snapshot, &selection, &selected);
    }
    else
    {
        if (has_loop)
            gtk_snapshot_append_color(snapshot, &selection, &selected);
        const GdkRGBA wave_color = with_alpha(fg, 0.8f);
        if (!self->wave || self->wave_width != width || self->wave_height != height ||
            self->wave_start != start || self->wave_span != span ||
            self->wave_frames != take->frames() || !gdk_rgba_equal(&self->wave_color, &wave_color))
        {
            g_clear_pointer(&self->wave, gsk_render_node_unref);
            self->wave = build_wave(self, *take, width, height, start, span, wave_color);
            self->wave_width = width;
            self->wave_height = height;
            self->wave_start = start;
            self->wave_span = span;
            self->wave_frames = take->frames();
            self->wave_color = wave_color;
        }
        if (self->wave)
            gtk_snapshot_append_node(snapshot, self->wave);
    }

    if (has_loop)
    {
        const GdkRGBA color = looping ? ACCENT : with_alpha(ink, 0.6f);
        draw_handle(snapshot, x0, height, color);
        draw_handle(snapshot, x1, height, color);
    }

    if (woodshed_engine_get_state(self->engine) != WOODSHED_STATE_RECORDING)
    {
        const graphene_rect_t playhead =
            rect(x_at(self, woodshed_engine_get_position(self->engine)) - 1.0, 0.0, 2.0, height);
        gtk_snapshot_append_color(snapshot, &ink, &playhead);
    }

    if (spectrum)
    {
        draw_hover_label(self, snapshot, height);
        draw_tuning(self, snapshot, height);
        gtk_snapshot_pop(snapshot);
    }
    draw_overview(self, snapshot, ink);
}

static void woodshed_waveform_measure(GtkWidget *, GtkOrientation orientation, int, int *minimum,
                                      int *natural, int *, int *)
{
    *minimum = orientation == GTK_ORIENTATION_VERTICAL ? 120 : 200;
    *natural = orientation == GTK_ORIENTATION_VERTICAL ? 240 : 600;
}

// Input

static bool busy_or_empty(WoodshedWaveform *self)
{
    return !take_of(self) ||
           woodshed_engine_get_state(self->engine) == WOODSHED_STATE_RECORDING;
}

// Grabbing a handle: the other edge becomes the anchor.
static double anchor_for(WoodshedWaveform *self, double x)
{
    if (woodshed_engine_get_has_loop(self->engine))
    {
        const double start = woodshed_engine_get_loop_start(self->engine);
        const double end = woodshed_engine_get_loop_end(self->engine);
        const double d0 = std::abs(x - x_at(self, start));
        const double d1 = std::abs(x - x_at(self, end));
        if (std::min(d0, d1) <= HANDLE_REACH)
            return d0 <= d1 ? end : start;
    }
    return time_at(self, x);
}

static void on_drag_begin(GtkGestureDrag *gesture, double x, double, WoodshedWaveform *self)
{
    if (busy_or_empty(self))
    {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
        return;
    }
    self->drag_x = x;
    self->drag_moved = FALSE;
    self->drag_cancelled = FALSE;
    self->saved_start = woodshed_engine_get_loop_start(self->engine);
    self->saved_end = woodshed_engine_get_loop_end(self->engine);
    self->drag_anchor = anchor_for(self, x);
}

static void on_drag_update(GtkGestureDrag *, double dx, double dy, WoodshedWaveform *self)
{
    if (self->drag_cancelled || busy_or_empty(self))
        return;
    if (!self->drag_moved && std::hypot(dx, dy) < TAP_SLOP)
        return;
    self->drag_moved = TRUE;
    const double t = std::clamp(time_at(self, self->drag_x + dx), 0.0, duration_of(self));
    woodshed_engine_set_loop(self->engine, self->drag_anchor, t);
}

static void on_drag_end(GtkGestureDrag *, double, double, WoodshedWaveform *self)
{
    if (self->drag_cancelled || self->drag_moved || busy_or_empty(self))
        return;
    woodshed_engine_seek(self->engine, time_at(self, self->drag_x));
}

static void on_double_tap(GtkGestureClick *, int n_press, double, double, WoodshedWaveform *self)
{
    if (n_press == 2 && !busy_or_empty(self))
        woodshed_engine_clear_loop(self->engine);
}

static void on_zoom_begin(GtkGesture *gesture, GdkEventSequence *, WoodshedWaveform *self)
{
    if (self->drag_moved && !self->drag_cancelled)
        woodshed_engine_set_loop(self->engine, self->saved_start, self->saved_end);
    self->drag_cancelled = TRUE;
    gtk_gesture_set_state(self->drag, GTK_EVENT_SEQUENCE_DENIED);
    double start, cx, cy;
    get_view(self, &start, &self->zoom_span);
    gtk_gesture_get_bounding_box_center(gesture, &cx, &cy);
    self->zoom_anchor = time_at(self, cx);
}

static void on_zoom_changed(GtkGestureZoom *gesture, double scale, WoodshedWaveform *self)
{
    double cx, cy;
    if (!gtk_gesture_get_bounding_box_center(GTK_GESTURE(gesture), &cx, &cy))
        return;
    const double span = self->zoom_span / std::max(scale, 1e-3);
    set_view(self, self->zoom_anchor - (cx - area_left(self)) / area_width(self) * span, span);
}

static gboolean on_scroll(GtkEventControllerScroll *scroll, double dx, double dy,
                          WoodshedWaveform *self)
{
    double start, span;
    get_view(self, &start, &span);
    const bool pixels = gtk_event_controller_scroll_get_unit(scroll) == GDK_SCROLL_UNIT_SURFACE;
    const double width = area_width(self);
    const GdkModifierType state =
        gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(scroll));
    if (state & GDK_CONTROL_MASK)
    {
        const double anchor = time_at(self, self->pointer_x);
        const double new_span = span * std::exp(pixels ? dy / 100.0 : dy * 0.2);
        set_view(self, anchor - (self->pointer_x - area_left(self)) / width * new_span, new_span);
    }
    else
    {
        const double delta = dx != 0.0 ? dx : dy;
        set_view(self, start + (pixels ? delta * span / width : delta * span * 0.1), span);
    }
    return TRUE;
}

static void set_hover_note(WoodshedWaveform *self, int note)
{
    if (note == self->hover_note)
        return;
    self->hover_note = note;
    gtk_widget_queue_draw(GTK_WIDGET(self));
}

static void on_leave(GtkEventControllerMotion *, WoodshedWaveform *self)
{
    set_hover_note(self, -1);
}

static void on_motion(GtkEventControllerMotion *, double x, double y, WoodshedWaveform *self)
{
    self->pointer_x = x;
    const bool spectrum = self->display == WOODSHED_DISPLAY_SPECTROGRAM && take_of(self);
    set_hover_note(self, spectrum ? key_at(self, y, gtk_widget_get_height(GTK_WIDGET(self))) : -1);
    const char *cursor = nullptr;
    if (!busy_or_empty(self) && woodshed_engine_get_has_loop(self->engine))
    {
        const double d0 = std::abs(x - x_at(self, woodshed_engine_get_loop_start(self->engine)));
        const double d1 = std::abs(x - x_at(self, woodshed_engine_get_loop_end(self->engine)));
        if (std::min(d0, d1) <= HANDLE_REACH)
            cursor = "col-resize";
    }
    if (cursor != self->cursor)
    {
        self->cursor = cursor;
        gtk_widget_set_cursor_from_name(GTK_WIDGET(self), cursor);
    }
}

// Spectrum runs in idle chunks, only while shown.

static gboolean spectrum_step(gpointer data)
{
    WoodshedWaveform *self = WOODSHED_WAVEFORM(data);
    const woodshed::audio_take *take = take_of(self);
    std::size_t added = 0;
    if (take && self->display == WOODSHED_DISPLAY_SPECTROGRAM)
    {
        sync_spectrum(self, take);
        const bool final = woodshed_engine_get_state(self->engine) != WOODSHED_STATE_RECORDING;
        added = active_spectrum(self).data.update(*take, final, STEP_COLUMNS);
    }
    if (added)
        gtk_widget_queue_draw(GTK_WIDGET(self));
    if (added == STEP_COLUMNS)
        return G_SOURCE_CONTINUE;
    self->spectrum_idle = 0;
    return G_SOURCE_REMOVE;
}

static void schedule_spectrum(WoodshedWaveform *self)
{
    const woodshed::audio_take *take = take_of(self);
    if (self->spectrum_idle || !take || self->display != WOODSHED_DISPLAY_SPECTROGRAM)
        return;
    const spectrum_cache &cache = active_spectrum(self);
    if (cache.take == take && cache.data.columns() * spectrogram::HOP >= take->frames())
        return;
    self->spectrum_idle = g_idle_add(spectrum_step, self);
}

// Engine

static void on_engine_changed(WoodshedEngine *, GParamSpec *, WoodshedWaveform *self)
{
    gtk_widget_queue_draw(GTK_WIDGET(self));
}

static void on_duration_changed(WoodshedEngine *, GParamSpec *, WoodshedWaveform *self)
{
    schedule_spectrum(self);
    gtk_widget_queue_draw(GTK_WIDGET(self));
}

static void on_state_changed(WoodshedEngine *engine, GParamSpec *, WoodshedWaveform *self)
{
    if (woodshed_engine_get_state(engine) == WOODSHED_STATE_RECORDING)
        self->fit = TRUE;
    schedule_spectrum(self);
    gtk_widget_queue_draw(GTK_WIDGET(self));
}

void woodshed_waveform_set_engine(WoodshedWaveform *self, WoodshedEngine *engine)
{
    g_return_if_fail(WOODSHED_IS_WAVEFORM(self));
    if (self->engine == engine)
        return;
    if (self->engine)
        g_signal_handlers_disconnect_by_data(self->engine, self);
    g_set_object(&self->engine, engine);
    if (engine)
    {
        for (const char *signal :
             {"notify::position", "notify::loop-start", "notify::loop-end", "notify::looping",
              "notify::tuning", "notify::transpose"})
            g_signal_connect(engine, signal, G_CALLBACK(on_engine_changed), self);
        g_signal_connect(engine, "notify::duration", G_CALLBACK(on_duration_changed), self);
        g_signal_connect(engine, "notify::state", G_CALLBACK(on_state_changed), self);
    }
    self->fit = TRUE;
    schedule_spectrum(self);
    gtk_widget_queue_draw(GTK_WIDGET(self));
    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_ENGINE]);
}

WoodshedEngine *woodshed_waveform_get_engine(WoodshedWaveform *self)
{
    g_return_val_if_fail(WOODSHED_IS_WAVEFORM(self), nullptr);
    return self->engine;
}

WoodshedDisplay woodshed_waveform_get_display(WoodshedWaveform *self)
{
    g_return_val_if_fail(WOODSHED_IS_WAVEFORM(self), WOODSHED_DISPLAY_WAVEFORM);
    return self->display;
}

void woodshed_waveform_set_display(WoodshedWaveform *self, WoodshedDisplay display)
{
    g_return_if_fail(WOODSHED_IS_WAVEFORM(self));
    if (self->display == display)
        return;
    self->display = display;
    self->hover_note = -1;
    schedule_spectrum(self);
    gtk_widget_queue_draw(GTK_WIDGET(self));
    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_DISPLAY]);
}

gboolean woodshed_waveform_get_focused(WoodshedWaveform *self)
{
    g_return_val_if_fail(WOODSHED_IS_WAVEFORM(self), FALSE);
    return self->focused;
}

void woodshed_waveform_set_focused(WoodshedWaveform *self, gboolean focused)
{
    g_return_if_fail(WOODSHED_IS_WAVEFORM(self));
    focused = !!focused;
    if (self->focused == focused)
        return;
    self->focused = focused;
    schedule_spectrum(self);
    gtk_widget_queue_draw(GTK_WIDGET(self));
    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_FOCUSED]);
}

void woodshed_waveform_zoom_to_fit(WoodshedWaveform *self)
{
    g_return_if_fail(WOODSHED_IS_WAVEFORM(self));
    self->fit = TRUE;
    gtk_widget_queue_draw(GTK_WIDGET(self));
}

GtkWidget *woodshed_waveform_new(WoodshedEngine *engine)
{
    return GTK_WIDGET(g_object_new(WOODSHED_TYPE_WAVEFORM, "engine", engine, nullptr));
}

// GObject

static void woodshed_waveform_dispose(GObject *object)
{
    WoodshedWaveform *self = WOODSHED_WAVEFORM(object);
    woodshed_waveform_set_engine(self, nullptr);
    g_clear_handle_id(&self->spectrum_idle, g_source_remove);
    g_clear_pointer(&self->wave, gsk_render_node_unref);
    for (spectrum_cache *cache : self->spectra)
        if (cache)
            cache->clear_tiles();
    G_OBJECT_CLASS(woodshed_waveform_parent_class)->dispose(object);
}

static void woodshed_waveform_finalize(GObject *object)
{
    for (spectrum_cache *cache : WOODSHED_WAVEFORM(object)->spectra)
        delete cache;
    G_OBJECT_CLASS(woodshed_waveform_parent_class)->finalize(object);
}

static void woodshed_waveform_get_property(GObject *object, guint prop_id, GValue *value,
                                           GParamSpec *pspec)
{
    if (prop_id == PROP_ENGINE)
        g_value_set_object(value, WOODSHED_WAVEFORM(object)->engine);
    else if (prop_id == PROP_DISPLAY)
        g_value_set_enum(value, WOODSHED_WAVEFORM(object)->display);
    else if (prop_id == PROP_FOCUSED)
        g_value_set_boolean(value, WOODSHED_WAVEFORM(object)->focused);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
}

static void woodshed_waveform_set_property(GObject *object, guint prop_id, const GValue *value,
                                           GParamSpec *pspec)
{
    if (prop_id == PROP_ENGINE)
        woodshed_waveform_set_engine(WOODSHED_WAVEFORM(object),
                                     WOODSHED_ENGINE(g_value_get_object(value)));
    else if (prop_id == PROP_DISPLAY)
        woodshed_waveform_set_display(WOODSHED_WAVEFORM(object),
                                      static_cast<WoodshedDisplay>(g_value_get_enum(value)));
    else if (prop_id == PROP_FOCUSED)
        woodshed_waveform_set_focused(WOODSHED_WAVEFORM(object), g_value_get_boolean(value));
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
}

static void woodshed_waveform_class_init(WoodshedWaveformClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = woodshed_waveform_dispose;
    object_class->finalize = woodshed_waveform_finalize;
    object_class->get_property = woodshed_waveform_get_property;
    object_class->set_property = woodshed_waveform_set_property;

    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
    widget_class->snapshot = woodshed_waveform_snapshot;
    widget_class->measure = woodshed_waveform_measure;
    gtk_widget_class_set_css_name(widget_class, "waveform");

    properties[PROP_ENGINE] = g_param_spec_object(
        "engine", nullptr, nullptr, WOODSHED_TYPE_ENGINE,
        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                 G_PARAM_EXPLICIT_NOTIFY));
    properties[PROP_DISPLAY] = g_param_spec_enum(
        "display", nullptr, nullptr, WOODSHED_TYPE_DISPLAY, WOODSHED_DISPLAY_WAVEFORM,
        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                 G_PARAM_EXPLICIT_NOTIFY));
    properties[PROP_FOCUSED] = g_param_spec_boolean(
        "focused", nullptr, nullptr, FALSE,
        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                 G_PARAM_EXPLICIT_NOTIFY));
    g_object_class_install_properties(object_class, N_PROPS, properties);
}

static void woodshed_waveform_init(WoodshedWaveform *self)
{
    GtkWidget *widget = GTK_WIDGET(self);
    self->fit = TRUE;
    self->hover_note = -1;
    self->spectra[0] = new spectrum_cache{false};
    self->spectra[1] = new spectrum_cache{true};

    self->drag = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(self->drag), GDK_BUTTON_PRIMARY);
    g_signal_connect(self->drag, "drag-begin", G_CALLBACK(on_drag_begin), self);
    g_signal_connect(self->drag, "drag-update", G_CALLBACK(on_drag_update), self);
    g_signal_connect(self->drag, "drag-end", G_CALLBACK(on_drag_end), self);
    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(self->drag));

    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_double_tap), self);
    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(click));

    GtkGesture *zoom = gtk_gesture_zoom_new();
    g_signal_connect(zoom, "begin", G_CALLBACK(on_zoom_begin), self);
    g_signal_connect(zoom, "scale-changed", G_CALLBACK(on_zoom_changed), self);
    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(zoom));

    GtkEventController *scroll = gtk_event_controller_scroll_new(
        static_cast<GtkEventControllerScrollFlags>(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES));
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), self);
    gtk_widget_add_controller(widget, scroll);

    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), self);
    g_signal_connect(motion, "leave", G_CALLBACK(on_leave), self);
    gtk_widget_add_controller(widget, motion);
}
