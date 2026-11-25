#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace keen_pbr3 {

struct SingboxPaths {
    std::string store{"/opt/etc/keen-pbr/sing-box-links.json"};
    std::string singbox_config{"/opt/etc/sing-box/config.json"};
    std::string singbox_bin{"sing-box"};
};

// One proxy node is one TUN interface and one keen-pbr interface outbound.
// Grouping belongs to the Outbounds page, not to this store.
struct SingboxNode {
    std::string id;
    std::string name;
    std::string tag;
    std::string protocol;
    std::string server;
    int port{0};
    std::string link;
    nlohmann::json outbound = nullptr;
    std::string interface_name;
    std::string tun_stack{"gvisor"};
    int tun_mtu{1400};
    std::string tun_address;
    std::string subscription_id;
    std::string identity;
    bool missing{false};
};

struct SingboxCatalogEntry {
    std::string identity;
    std::string name;
    std::string protocol;
    std::string server;
    int port{0};
    bool skipped{false};
    std::string reason;
};

struct SingboxSubscription {
    std::string id;
    std::string url;
    std::string name;
    int interval_hours{0};
    std::int64_t last_refresh{0};
    std::string last_error;
    std::vector<std::string> selected;
    std::vector<SingboxCatalogEntry> catalog;
    int new_nodes{0};
};

struct SingboxStore {
    std::vector<SingboxNode> nodes;
    std::vector<SingboxSubscription> subscriptions;
};

struct SingboxTunOptions {
    std::string tun_stack{"gvisor"};
    int tun_mtu{1400};
    std::string tun_address;
};

struct SingboxNodePatch {
    std::string id;
    std::string tag;
    std::string interface_name;
    std::string tun_stack;
    int tun_mtu{0};
    std::string tun_address;
    bool tun_address_set{false};
};

struct SingboxApplyResult {
    bool singbox_checked{false};
    bool singbox_reloaded{false};
    bool keenetic_applied{false};
    std::string warning;
    std::size_t skipped{0};
    std::vector<std::string> skipped_transports;
    bool encryption_ignored{false};
    nlohmann::json state;
};

// nodes is the full set that should exist. removed_tags are keen-pbr tags that
// this change drops. owned_tags are the tags this store already managed.
struct KeeneticSyncRequest {
    std::vector<SingboxNode> nodes;
    std::vector<std::string> removed_tags;
    std::vector<std::string> owned_tags;
};

using SingboxCheck = std::function<int(const std::string& config_path, std::string* error)>;
using KeeneticSyncFn = std::function<void(const KeeneticSyncRequest& request)>;
using SingboxReloadFn = std::function<bool()>;
using SubscriptionStopFn = std::function<bool()>;
using SubscriptionDownloadFn = std::function<std::string(const std::string&, const SubscriptionStopFn&)>;
using SubscriptionClock = std::function<std::int64_t()>;

class SingboxBusy : public std::runtime_error {
public:
    SingboxBusy() : std::runtime_error("another sing-box operation is in progress") {}
};

class SingboxLinkService {
public:
    SingboxLinkService(SingboxPaths paths, SingboxCheck check, KeeneticSyncFn keenetic,
                       SingboxReloadFn reload, SubscriptionDownloadFn download = {},
                       SubscriptionClock clock = {});

    void set_keenetic_callback(KeeneticSyncFn keenetic) { keenetic_ = std::move(keenetic); }
    nlohmann::json preview(const std::string& link) const;
    // Downloads a subscription and returns nodes plus skipped lines. Does not save.
    nlohmann::json preview_subscription(const std::string& url_or_id) const;
    nlohmann::json state() const;
    std::map<std::string, std::string> subscription_errors() const;

    SingboxApplyResult add_links(const std::vector<std::string>& links, const SingboxTunOptions& tun);
    SingboxApplyResult update_node(const SingboxNodePatch& patch);
    SingboxApplyResult remove_node(const std::string& id);
    // Creates or updates a subscription and adds only the ticked identities.
    SingboxApplyResult save_subscription(const std::string& url, int interval_hours,
                                         const std::vector<std::string>& selected,
                                         const SingboxTunOptions& tun,
                                         const std::string& subscription_id = {});
    SingboxApplyResult select_subscription(const std::string& subscription_id,
                                           const std::vector<std::string>& selected,
                                           const SingboxTunOptions& tun);
    // Removes the subscription and its interfaces. Refuses when a keen-pbr
    // outbound is still referenced; nothing is deleted in that case.
    SingboxApplyResult remove_subscription(const std::string& subscription_id);
    // Empty id refreshes every subscription that is due (or all, when due_only is false).
    nlohmann::json refresh_subscriptions(const std::string& subscription_id, bool due_only,
                                         const SubscriptionStopFn& stopping = {});

    static nlohmann::json render_singbox_config(const std::vector<SingboxNode>& nodes);
    static SingboxStore load_store(const std::string& path);

private:
    struct RefreshStatus {
        std::int64_t last_refresh{0};
        std::int64_t retry_at{0};
        std::string error;
    };
    struct ConfigWrite {
        bool had_config{false};
        bool removed{false};
    };

    std::unique_lock<std::mutex> begin_mutation();
    void update_refresh_status(const std::string& id, RefreshStatus status);
    RefreshStatus refresh_status(const SingboxSubscription& subscription) const;
    static std::int64_t next_refresh(const SingboxSubscription& subscription, const RefreshStatus& status);
    bool refresh_one(SingboxStore& store, std::size_t index, const SubscriptionStopFn& stopping);
    ConfigWrite write_checked_config(const nlohmann::json& config);
    nlohmann::json read_config_object() const;
    SingboxApplyResult persist(const SingboxStore& previous, SingboxStore next, bool rewrite_singbox,
                               bool sync_keenetic, bool reload_must_succeed = false);
    SingboxStore read() const;
    static void write_store(const std::string& path, const SingboxStore& store);
    nlohmann::json public_state(const SingboxStore& store) const;

    SingboxPaths paths_;
    SingboxCheck check_;
    KeeneticSyncFn keenetic_;
    SingboxReloadFn reload_;
    SubscriptionDownloadFn download_;
    SubscriptionClock clock_;
    std::mutex mutation_mutex_;
    mutable std::mutex status_mutex_;
    std::map<std::string, RefreshStatus> refresh_status_;
};

SingboxLinkService make_singbox_link_service();
std::string sanitize_keenetic_tag(const std::string& name);
bool safe_interface_name(const std::string& name);
std::string format_singbox_check_error(int exit_code, bool timed_out, const std::string& output);
// Empty when tag is unused. Otherwise names the route rules and urltest/icmptest groups.
std::string outbound_reference_message(const nlohmann::json& config, const std::string& tag);

}  // namespace keen_pbr3
