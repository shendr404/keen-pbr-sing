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

struct SingboxConnection {
    std::string id;
    std::string name;
    std::string protocol;
    std::string server;
    int port{0};
    std::string link;
    std::string source{"manual"};
    // Set for nodes imported from a subscription. Empty for manual nodes.
    std::string subscription_id;
    // Set when the node came from JSON and has no share link to re-parse.
    nlohmann::json outbound = nullptr;
};

struct SingboxSubscription {
    std::string id;
    std::string url;
    int interval_hours{0};
    std::int64_t last_refresh{0};
    std::string last_error;
};

struct SingboxProfile {
    std::string id;
    std::string name;
    std::string interface_name{"sbtun"};
    std::string keenetic_tag;
    std::string mode{"selector"};
    std::string active;
    std::string urltest_url{"https://www.gstatic.com/generate_204"};
    std::string urltest_interval{"3m"};
    // sing-box TUN stack. gvisor is the default; system is the other accepted value.
    std::string tun_stack{"gvisor"};
    int tun_mtu{1400};
    // Empty means the default 172.19.x.1/30 address. Several CIDRs may be comma-separated.
    std::string tun_address;
    // Mirrors of subscriptions.front(), kept so older store files still load.
    std::string subscription_url;
    int interval_hours{0};
    std::int64_t last_refresh{0};
    std::string last_error;
    std::vector<SingboxSubscription> subscriptions;
    std::vector<SingboxConnection> connections;
};

struct SingboxProfileDraft {
    std::string id;
    std::string name;
    // Empty on create means "pick the next free name".
    std::string interface_name;
    std::string keenetic_tag;
    std::string mode{"selector"};
    std::string urltest_url{"https://www.gstatic.com/generate_204"};
    std::string urltest_interval{"3m"};
    std::string tun_stack{"gvisor"};
    int tun_mtu{1400};
    std::string tun_address;
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

using SingboxCheck = std::function<int(const std::string& config_path, std::string* error)>;
using KeeneticOutboundFn = std::function<void(const SingboxProfile& profile, bool present)>;
using SingboxReloadFn = std::function<bool()>;
using SubscriptionStopFn = std::function<bool()>;
using SubscriptionDownloadFn = std::function<std::string(const std::string&, const SubscriptionStopFn&)>;
using SubscriptionClock = std::function<std::int64_t()>;
// Returns the sing-box outbound tag currently selected for a urltest group, or empty.
using UrltestNowFn = std::function<std::string(const std::string& group_tag)>;

class SingboxBusy : public std::runtime_error {
public:
    SingboxBusy() : std::runtime_error("another sing-box operation is in progress") {}
};

class SingboxLinkService {
public:
    SingboxLinkService(SingboxPaths paths, SingboxCheck check, KeeneticOutboundFn keenetic,
                       SingboxReloadFn reload, SubscriptionDownloadFn download = {},
                       SubscriptionClock clock = {}, UrltestNowFn urltest_now = {});

    // Set once before starting the API or the refresh worker.
    void set_keenetic_callback(KeeneticOutboundFn keenetic) { keenetic_ = std::move(keenetic); }
    nlohmann::json preview(const std::string& link) const;
    nlohmann::json state() const;
    std::map<std::string, std::string> subscription_errors() const;
    SingboxApplyResult save_profile(const SingboxProfileDraft& draft);
    SingboxApplyResult remove_profile(const std::string& profile_id);
    // Adds every link to the profile identified by profile_id. Does not create or merge profiles.
    SingboxApplyResult add_connections(const std::string& profile_id,
                                       const std::vector<std::string>& links,
                                       const std::string& connection_name);
    SingboxApplyResult remove_connection(const std::string& profile_id, const std::string& connection_id);
    SingboxApplyResult set_mode(const std::string& profile_id, const std::string& mode,
                                const std::string& active);
    // Attaches or replaces one subscription. Does not change the profile mode.
    SingboxApplyResult save_subscription(const std::string& profile_id, const std::string& url,
                                         int interval_hours, const std::string& subscription_id);
    SingboxApplyResult remove_subscription(const std::string& profile_id,
                                           const std::string& subscription_id);
    // Empty profile id means all subscriptions. Forced refreshes ignore interval and backoff.
    nlohmann::json refresh_subscriptions(const std::string& profile_id, bool due_only,
                                         const SubscriptionStopFn& stopping = {},
                                         const std::string& subscription_id = {});

    static nlohmann::json render_singbox_config(const std::vector<SingboxProfile>& profiles);
    static std::vector<SingboxProfile> load_profiles(const std::string& path);
    static nlohmann::json public_state(const std::vector<SingboxProfile>& profiles);

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
    void clear_profile_status(const std::string& profile_id);
    RefreshStatus refresh_status(const SingboxProfile& profile, const SingboxSubscription& subscription) const;
    static std::string status_key(const SingboxProfile& profile, const SingboxSubscription& subscription);
    static std::int64_t next_refresh(const SingboxSubscription& subscription, const RefreshStatus& status);
    void refresh_one(std::vector<SingboxProfile>& profiles, std::size_t profile_index,
                     std::size_t subscription_index, const SubscriptionStopFn& stopping);
    ConfigWrite write_checked_config(const nlohmann::json& config);
    nlohmann::json read_config_object() const;
    SingboxApplyResult commit(std::vector<SingboxProfile> profiles, const std::string& changed_id,
                              const std::string& previous_tag);
    std::vector<SingboxProfile> read() const;
    static void write_store(const std::string& path, const std::vector<SingboxProfile>& profiles);

    SingboxPaths paths_;
    SingboxCheck check_;
    KeeneticOutboundFn keenetic_;
    SingboxReloadFn reload_;
    SubscriptionDownloadFn download_;
    SubscriptionClock clock_;
    UrltestNowFn urltest_now_;
    std::mutex mutation_mutex_;
    mutable std::mutex status_mutex_;
    std::map<std::string, RefreshStatus> refresh_status_;
};

SingboxLinkService make_singbox_link_service();
std::string sanitize_keenetic_tag(const std::string& name);
bool safe_interface_name(const std::string& name);
// User-facing sing-box check failure. Redacts uuids, passwords and keys.
std::string format_singbox_check_error(int exit_code, bool timed_out, const std::string& output);

}  // namespace keen_pbr3
