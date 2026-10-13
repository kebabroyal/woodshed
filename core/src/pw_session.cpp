#include "woodshed/pw_session.hpp"

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>

namespace woodshed
{

struct pw_session::node_entry
{
    pw_session *session;
    audio_node info;
    pw_proxy *proxy;
    spa_hook listener;
};

static std::string lookup(const spa_dict *props, const char *key)
{
    const char *value = spa_dict_lookup(props, key);
    return value ? value : "";
}

static void read_props(audio_node &node, const spa_dict *props)
{
    node.serial = std::strtoull(lookup(props, PW_KEY_OBJECT_SERIAL).c_str(), nullptr, 10);
    node.node_name = lookup(props, PW_KEY_NODE_NAME);
    node.description = lookup(props, PW_KEY_NODE_DESCRIPTION);
    node.nick = lookup(props, PW_KEY_NODE_NICK);
    node.media_name = lookup(props, PW_KEY_MEDIA_NAME);
    node.app_name = lookup(props, PW_KEY_APP_NAME);
    node.icon_name = lookup(props, PW_KEY_APP_ICON_NAME);
    node.binary = lookup(props, PW_KEY_APP_PROCESS_BINARY);
    node.app_id = lookup(props, "pipewire.access.portal.app_id");
    if (node.app_id.empty())
        node.app_id = lookup(props, PW_KEY_APP_ID);
    node.pid = static_cast<std::uint32_t>(
        std::strtoul(lookup(props, PW_KEY_APP_PROCESS_ID).c_str(), nullptr, 10));
}

static bool kind_of(const char *media_class, node_kind &kind)
{
    if (std::strcmp(media_class, "Stream/Output/Audio") == 0)
        kind = node_kind::app_output;
    else if (std::strcmp(media_class, "Audio/Sink") == 0)
        kind = node_kind::sink;
    else if (std::strcmp(media_class, "Audio/Source") == 0)
        kind = node_kind::source;
    else
        return false;
    return true;
}

pw_session::pw_session()
{
    static std::once_flag init;
    std::call_once(init, [] { pw_init(nullptr, nullptr); });

    thread_loop_ = pw_thread_loop_new("woodshed", nullptr);
    if (!thread_loop_)
        throw std::runtime_error{"PipeWire: no thread loop"};
    context_ = pw_context_new(pw_thread_loop_get_loop(thread_loop_),
                              pw_properties_new(PW_KEY_APP_NAME, "Woodshed", nullptr), 0);
    core_ = context_ ? pw_context_connect(context_, nullptr, 0) : nullptr;
    if (!core_)
    {
        if (context_)
            pw_context_destroy(context_);
        pw_thread_loop_destroy(thread_loop_);
        throw std::runtime_error{"PipeWire: cannot connect"};
    }

    static const pw_registry_events registry_events = {
        .version = PW_VERSION_REGISTRY_EVENTS,
        .global = on_global,
        .global_remove = on_global_remove,
    };
    registry_ = pw_core_get_registry(core_, PW_VERSION_REGISTRY, 0);
    pw_registry_add_listener(registry_, &registry_listener_, &registry_events, this);
    pw_thread_loop_start(thread_loop_);
}

pw_session::~pw_session()
{
    pw_thread_loop_stop(thread_loop_);
    for (auto &[id, entry] : nodes_)
    {
        spa_hook_remove(&entry->listener);
        pw_proxy_destroy(entry->proxy);
    }
    nodes_.clear();
    spa_hook_remove(&registry_listener_);
    pw_proxy_destroy(reinterpret_cast<pw_proxy *>(registry_));
    pw_core_disconnect(core_);
    pw_context_destroy(context_);
    pw_thread_loop_destroy(thread_loop_);
}

std::vector<audio_node> pw_session::nodes() const
{
    std::lock_guard guard{*this};
    std::vector<audio_node> result;
    result.reserve(nodes_.size());
    for (const auto &[id, entry] : nodes_)
        result.push_back(entry->info);
    return result;
}

void pw_session::set_on_nodes_changed(std::function<void()> callback)
{
    std::lock_guard guard{*this};
    on_changed_ = std::move(callback);
}

void pw_session::changed()
{
    if (on_changed_)
        on_changed_();
}

void pw_session::on_global(void *data, std::uint32_t id, std::uint32_t, const char *type,
                           std::uint32_t, const spa_dict *props)
{
    auto *self = static_cast<pw_session *>(data);
    if (std::strcmp(type, PW_TYPE_INTERFACE_Node) != 0 || !props)
        return;
    const char *media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
    node_kind kind;
    if (!media_class || !kind_of(media_class, kind))
        return;
    // Skip our own streams.
    if (lookup(props, PW_KEY_NODE_NAME).starts_with("woodshed"))
        return;

    auto entry = std::make_unique<node_entry>();
    entry->session = self;
    entry->info.id = id;
    entry->info.kind = kind;
    read_props(entry->info, props);
    entry->proxy =
        static_cast<pw_proxy *>(pw_registry_bind(self->registry_, id, type, PW_VERSION_NODE, 0));
    if (!entry->proxy)
        return;
    static const pw_node_events node_events = {
        .version = PW_VERSION_NODE_EVENTS,
        .info = on_node_info,
    };
    pw_node_add_listener(reinterpret_cast<pw_node *>(entry->proxy), &entry->listener, &node_events,
                         entry.get());
    self->nodes_[id] = std::move(entry);
    self->changed();
}

void pw_session::on_global_remove(void *data, std::uint32_t id)
{
    auto *self = static_cast<pw_session *>(data);
    const auto it = self->nodes_.find(id);
    if (it == self->nodes_.end())
        return;
    spa_hook_remove(&it->second->listener);
    pw_proxy_destroy(it->second->proxy);
    self->nodes_.erase(it);
    self->changed();
}

void pw_session::on_node_info(void *data, const pw_node_info *info)
{
    auto *entry = static_cast<node_entry *>(data);
    entry->info.running = info->state == PW_NODE_STATE_RUNNING;
    if ((info->change_mask & PW_NODE_CHANGE_MASK_PROPS) && info->props)
        read_props(entry->info, info->props);
    entry->session->changed();
}

}
