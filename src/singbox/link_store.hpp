#pragma once

#include "link_parser.hpp"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
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

class SingboxLinkService {
public:
    SingboxLinkService(SingboxPaths paths, SingboxCheck check, KeeneticOutboundFn keenetic,
                       SingboxReloadFn reload);

    nlohmann::json preview(const std::string& link) const;
    nlohmann::json state(bool refresh_due);
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
                                         int interval_hours,
                                         const std::string& body);
    SingboxApplyResult refresh_subscription(const std::string& profile_id, const std::string& body);

    static nlohmann::json render_singbox_config(const std::vector<SingboxProfile>& profiles);
    static std::vector<SingboxProfile> load_profiles(const std::string& path);
    static nlohmann::json public_state(const std::vector<SingboxProfile>& profiles);

private:
    SingboxApplyResult commit(std::vector<SingboxProfile> profiles, const std::string& changed_id);
    std::vector<SingboxProfile> read() const;
    static void write_store(const std::string& path, const std::vector<SingboxProfile>& profiles);

    SingboxPaths paths_;
    SingboxCheck check_;
    KeeneticOutboundFn keenetic_;
    SingboxReloadFn reload_;
};

std::string sanitize_keenetic_tag(const std::string& name);
bool safe_interface_name(const std::string& name);

}  // namespace keen_pbr3
