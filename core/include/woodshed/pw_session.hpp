#ifndef WOODSHED_PW_SESSION_HPP
#define WOODSHED_PW_SESSION_HPP

#include <pipewire/pipewire.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace woodshed
{

enum class node_kind
{
    app_output,
    sink,
    source
};

struct audio_node
{
    std::uint32_t id = 0;
    std::uint64_t serial = 0;
    node_kind kind = node_kind::app_output;
    std::string node_name;
    std::string description;
    std::string nick;
    std::string media_name;
    std::string app_name;
    std::string icon_name;
    std::string binary;
    std::string app_id; // Flatpak or portal id
    std::uint32_t pid = 0; // may be inside a sandbox
    bool running = false;
};

// PipeWire connection on its own thread, plus a live list of audio nodes.
class pw_session
{
public:
    pw_session();
    ~pw_session();
    pw_session(const pw_session &) = delete;
    pw_session &operator=(const pw_session &) = delete;

    std::vector<audio_node> nodes() const;
    // Called on the PipeWire thread.
    void set_on_nodes_changed(std::function<void()> callback);

    pw_core *core() const { return core_; }
    void lock() const { pw_thread_loop_lock(thread_loop_); }
    void unlock() const { pw_thread_loop_unlock(thread_loop_); }

private:
    struct node_entry;

    static void on_global(void *data, std::uint32_t id, std::uint32_t permissions,
                          const char *type, std::uint32_t version, const spa_dict *props);
    static void on_global_remove(void *data, std::uint32_t id);
    static void on_node_info(void *data, const pw_node_info *info);
    void changed();

    pw_thread_loop *thread_loop_ = nullptr;
    pw_context *context_ = nullptr;
    pw_core *core_ = nullptr;
    pw_registry *registry_ = nullptr;
    spa_hook registry_listener_{};
    std::map<std::uint32_t, std::unique_ptr<node_entry>> nodes_;
    std::function<void()> on_changed_;
};

}

#endif
