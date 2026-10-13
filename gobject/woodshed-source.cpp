#include "woodshed-source.h"
#include "woodshed-enums.h"
#include "woodshed-private.hpp"

struct _WoodshedSource
{
    GObject parent_instance;
    std::uint32_t id;
    std::uint64_t serial;
    WoodshedSourceKind kind;
    char *name;
    char *detail;
    char *icon_name;
    char *binary;
    char *app_id;
    guint pid;
    gboolean playing;
};

G_DEFINE_FINAL_TYPE(WoodshedSource, woodshed_source, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_NAME,
    PROP_DETAIL,
    PROP_ICON_NAME,
    PROP_KIND,
    PROP_PLAYING,
    PROP_BINARY,
    PROP_APP_ID,
    PROP_PROCESS_ID,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void woodshed_source_finalize(GObject *object)
{
    WoodshedSource *self = WOODSHED_SOURCE(object);
    g_free(self->name);
    g_free(self->detail);
    g_free(self->icon_name);
    g_free(self->binary);
    g_free(self->app_id);
    G_OBJECT_CLASS(woodshed_source_parent_class)->finalize(object);
}

static void woodshed_source_get_property(GObject *object, guint prop_id, GValue *value,
                                         GParamSpec *pspec)
{
    WoodshedSource *self = WOODSHED_SOURCE(object);
    switch (prop_id)
    {
    case PROP_NAME:
        g_value_set_string(value, self->name);
        break;
    case PROP_DETAIL:
        g_value_set_string(value, self->detail);
        break;
    case PROP_ICON_NAME:
        g_value_set_string(value, self->icon_name);
        break;
    case PROP_KIND:
        g_value_set_enum(value, self->kind);
        break;
    case PROP_PLAYING:
        g_value_set_boolean(value, self->playing);
        break;
    case PROP_BINARY:
        g_value_set_string(value, self->binary);
        break;
    case PROP_APP_ID:
        g_value_set_string(value, self->app_id);
        break;
    case PROP_PROCESS_ID:
        g_value_set_uint(value, self->pid);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void woodshed_source_class_init(WoodshedSourceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = woodshed_source_finalize;
    object_class->get_property = woodshed_source_get_property;

    const auto flags = static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    properties[PROP_NAME] = g_param_spec_string("name", nullptr, nullptr, nullptr, flags);
    properties[PROP_DETAIL] = g_param_spec_string("detail", nullptr, nullptr, nullptr, flags);
    properties[PROP_ICON_NAME] = g_param_spec_string("icon-name", nullptr, nullptr, nullptr, flags);
    properties[PROP_KIND] = g_param_spec_enum("kind", nullptr, nullptr, WOODSHED_TYPE_SOURCE_KIND,
                                              WOODSHED_SOURCE_KIND_APP, flags);
    properties[PROP_PLAYING] = g_param_spec_boolean("playing", nullptr, nullptr, FALSE, flags);
    properties[PROP_BINARY] = g_param_spec_string("binary", nullptr, nullptr, nullptr, flags);
    properties[PROP_APP_ID] = g_param_spec_string("app-id", nullptr, nullptr, nullptr, flags);
    properties[PROP_PROCESS_ID] =
        g_param_spec_uint("process-id", nullptr, nullptr, 0, G_MAXUINT, 0, flags);
    g_object_class_install_properties(object_class, N_PROPS, properties);
}

static void woodshed_source_init(WoodshedSource *)
{
}

static void set_string(WoodshedSource *self, char **field, const char *value, int prop)
{
    if (g_strcmp0(*field, value) == 0)
        return;
    g_free(*field);
    *field = g_strdup(value);
    g_object_notify_by_pspec(G_OBJECT(self), properties[prop]);
}

WoodshedSource *woodshed_source_new(const source_info &info)
{
    WoodshedSource *self = WOODSHED_SOURCE(g_object_new(WOODSHED_TYPE_SOURCE, nullptr));
    self->id = info.id;
    self->serial = info.serial;
    self->kind = info.kind;
    self->name = g_strdup(info.name);
    self->detail = g_strdup(info.detail);
    self->icon_name = g_strdup(info.icon_name);
    self->binary = g_strdup(info.binary);
    self->app_id = g_strdup(info.app_id);
    self->pid = info.pid;
    self->playing = info.playing;
    return self;
}

void woodshed_source_update(WoodshedSource *self, const source_info &info)
{
    self->serial = info.serial;
    set_string(self, &self->name, info.name, PROP_NAME);
    set_string(self, &self->detail, info.detail, PROP_DETAIL);
    set_string(self, &self->icon_name, info.icon_name, PROP_ICON_NAME);
    set_string(self, &self->binary, info.binary, PROP_BINARY);
    set_string(self, &self->app_id, info.app_id, PROP_APP_ID);
    if (self->pid != info.pid)
    {
        self->pid = info.pid;
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_PROCESS_ID]);
    }
    if (self->playing != info.playing)
    {
        self->playing = info.playing;
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_PLAYING]);
    }
}

std::uint32_t woodshed_source_get_id(WoodshedSource *self)
{
    return self->id;
}

std::uint64_t woodshed_source_get_serial(WoodshedSource *self)
{
    return self->serial;
}

const char *woodshed_source_get_name(WoodshedSource *self)
{
    g_return_val_if_fail(WOODSHED_IS_SOURCE(self), nullptr);
    return self->name;
}

const char *woodshed_source_get_detail(WoodshedSource *self)
{
    g_return_val_if_fail(WOODSHED_IS_SOURCE(self), nullptr);
    return self->detail;
}

const char *woodshed_source_get_icon_name(WoodshedSource *self)
{
    g_return_val_if_fail(WOODSHED_IS_SOURCE(self), nullptr);
    return self->icon_name;
}

WoodshedSourceKind woodshed_source_get_kind(WoodshedSource *self)
{
    g_return_val_if_fail(WOODSHED_IS_SOURCE(self), WOODSHED_SOURCE_KIND_APP);
    return self->kind;
}

gboolean woodshed_source_get_playing(WoodshedSource *self)
{
    g_return_val_if_fail(WOODSHED_IS_SOURCE(self), FALSE);
    return self->playing;
}

const char *woodshed_source_get_binary(WoodshedSource *self)
{
    g_return_val_if_fail(WOODSHED_IS_SOURCE(self), nullptr);
    return self->binary;
}

const char *woodshed_source_get_app_id(WoodshedSource *self)
{
    g_return_val_if_fail(WOODSHED_IS_SOURCE(self), nullptr);
    return self->app_id;
}

guint woodshed_source_get_process_id(WoodshedSource *self)
{
    g_return_val_if_fail(WOODSHED_IS_SOURCE(self), 0);
    return self->pid;
}
