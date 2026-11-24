#pragma once

#include "link_parser.hpp"

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
    // Set when the node came from JSON and has no share link to re-parse.
    nlohmann::json outbound = nullptr;
};

struct SingboxProfile {
    std::string id;
    std::string name;
    std::string interface_name{"sbtun"};
    std::string keenetic_tag;
    std::string mode{"selector"};
    std::string active;
    std::string urltest_url{"https://www.gstatic.com/generate_204"};
    std::string subscription_url;
    int interval_hours{0};
    std::int64_t last_refresh{0};
    std::string last_error;
    std::vector<SingboxConnection> connections;
};

struct SingboxApplyResult {
    bool singbox_checked{false};
    bool singbox_reloaded{false};
    bool keenetic_applied{false};
    std::string warning;
    nlohmann::json state;
};

using SingboxCheck = std::function<int(const std::string& config_path, std::string* error)>;
using KeeneticOutboundFn = std::function<void(const SingboxProfile& profile, bool present)>;
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
    SingboxLinkService(SingboxPaths paths, SingboxCheck check, KeeneticOutboundFn keenetic,
                       SingboxReloadFn reload, SubscriptionDownloadFn download = {},
                       SubscriptionClock clock = {});

    // Set once before starting the API or the refresh worker.
    void set_keenetic_callback(KeeneticOutboundFn keenetic) { keenetic_ = std::move(keenetic); }
    nlohmann::json preview(const std::string& link) const;
    nlohmann::json state() const;
    std::map<std::string, std::string> subscription_errors() const;
    SingboxApplyResult save_connection(const std::string& profile_name,
                                       const std::string& interface_name,
                                       const std::string& tag,
                                       const std::string& mode,
                                       const std::string& connection_name,
                                       const std::string& link,
                                       const std::string& replace_id);
    SingboxApplyResult remove_connection(const std::string& profile_id, const std::string& connection_id);
    SingboxApplyResult set_mode(const std::string& profile_id, const std::string& mode,
                                const std::string& active);
    SingboxApplyResult save_subscription(const std::string& profile_name,
                                         const std::string& interface_name,
                                         const std::string& tag,
                                          const std::string& mode,
                                          const std::string& url,
                                          int interval_hours);
    // Empty ID means all subscriptions. Forced refreshes ignore interval and backoff.
    nlohmann::json refresh_subscriptions(const std::string& profile_id, bool due_only,
                                         const SubscriptionStopFn& stopping = {});

    static nlohmann::json render_singbox_config(const std::vector<SingboxProfile>& profiles);
    static std::vector<SingboxProfile> load_profiles(const std::string& path);
    static nlohmann::json public_state(const std::vector<SingboxProfile>& profiles);

private:
    struct RefreshStatus {
        std::int64_t last_refresh{0};
        std::int64_t retry_at{0};
        std::string error;
    };

    std::unique_lock<std::mutex> begin_mutation();
    void update_refresh_status(const std::string& id, RefreshStatus status);
    RefreshStatus refresh_status(const SingboxProfile& profile) const;
    static std::int64_t next_refresh(const SingboxProfile& profile, const RefreshStatus& status);
    void refresh_one(std::vector<SingboxProfile>& profiles, std::size_t index,
                     const SubscriptionStopFn& stopping);
    bool write_checked_config(const nlohmann::json& config);
    SingboxApplyResult commit(std::vector<SingboxProfile> profiles, const std::string& changed_id);
    std::vector<SingboxProfile> read() const;
    static void write_store(const std::string& path, const std::vector<SingboxProfile>& profiles);

    SingboxPaths paths_;
    SingboxCheck check_;
    KeeneticOutboundFn keenetic_;
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

}  // namespace keen_pbr3
