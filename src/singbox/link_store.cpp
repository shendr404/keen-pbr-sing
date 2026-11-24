#include "link_store.hpp"
#include "link_parser.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace keen_pbr3 {
namespace {

nlohmann::json connection_json(const SingboxConnection& connection, bool with_secret) {
    nlohmann::json json{
        {"id", connection.id},
        {"name", connection.name},
        {"protocol", connection.protocol},
        {"server", connection.server},
        {"port", connection.port},
        {"source", connection.source},
    };
    if (with_secret) {
        if (!connection.link.empty()) json["link"] = connection.link;
        if (!connection.outbound.is_null()) json["outbound"] = connection.outbound;
    }
    return json;
}

SingboxConnection connection_from_json(const nlohmann::json& json) {
    SingboxConnection connection;
    connection.id = json.value("id", "");
    connection.name = json.value("name", "");
    connection.protocol = json.value("protocol", "");
    connection.server = json.value("server", "");
    connection.port = json.value("port", 0);
    connection.source = json.value("source", "manual");
    connection.link = json.value("link", "");
    if (json.contains("outbound")) connection.outbound = json["outbound"];
    return connection;
}

nlohmann::json profile_json(const SingboxProfile& profile, bool with_secret) {
    nlohmann::json connections = nlohmann::json::array();
    for (const auto& connection : profile.connections) {
        connections.push_back(connection_json(connection, with_secret));
    }
    nlohmann::json json{
        {"id", profile.id},
        {"name", profile.name},
        {"interface", profile.interface_name},
        {"keenetic_tag", profile.keenetic_tag},
        {"mode", profile.mode},
        {"active", profile.active},
        {"urltest_url", profile.urltest_url},
        {"interval_hours", profile.interval_hours},
        {"last_refresh", profile.last_refresh},
        {"last_error", profile.last_error},
        {"connections", std::move(connections)},
    };
    if (with_secret) json["subscription_url"] = profile.subscription_url;
    else json["has_subscription"] = !profile.subscription_url.empty();
    return json;
}

SingboxProfile profile_from_json(const nlohmann::json& json) {
    SingboxProfile profile;
    profile.id = json.value("id", "");
    profile.name = json.value("name", profile.id);
    profile.interface_name = json.value("interface", "sbtun");
    profile.keenetic_tag = json.value("keenetic_tag", profile.id);
    profile.mode = json.value("mode", "selector");
    profile.active = json.value("active", "");
    profile.urltest_url = json.value("urltest_url", "https://www.gstatic.com/generate_204");
    profile.subscription_url = json.value("subscription_url", "");
    profile.interval_hours = json.value("interval_hours", 0);
    profile.last_refresh = json.value("last_refresh", static_cast<std::int64_t>(0));
    profile.last_error = json.value("last_error", "");
    if (json.contains("connections") && json["connections"].is_array()) {
        for (const auto& item : json["connections"]) {
            profile.connections.push_back(connection_from_json(item));
        }
    }
    return profile;
}

std::string read_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) return {};
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void mkdir_parents(const std::string& dir) {
    std::string current;
    for (char c : dir) {
        current.push_back(c);
        if (c == '/' && current.size() > 1) ::mkdir(current.c_str(), 0750);
    }
    if (!dir.empty()) ::mkdir(dir.c_str(), 0750);
}

void write_private(const std::string& path, const std::string& body) {
    const auto slash = path.find_last_of('/');
    if (slash != std::string::npos) mkdir_parents(path.substr(0, slash));
    const auto temp = path + ".tmp";
    const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
    if (fd < 0) throw std::runtime_error("cannot write " + path);
    struct stat info{};
    bool ok = ::fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 &&
              ::fchmod(fd, 0600) == 0 && ::ftruncate(fd, 0) == 0;
    for (std::size_t offset = 0; ok && offset < body.size();) {
        const auto count = ::write(fd, body.data() + offset, body.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) ok = false;
        else offset += static_cast<std::size_t>(count);
    }
    if (::close(fd) != 0) ok = false;
    if (!ok) {
        ::unlink(temp.c_str());
        throw std::runtime_error("cannot write " + path);
    }
    if (::rename(temp.c_str(), path.c_str()) != 0) {
        ::unlink(temp.c_str());
        throw std::runtime_error("cannot replace " + path);
    }
}

std::string backup_of(const std::string& path) { return path + ".bak-keenpbr"; }

bool backup_file(const std::string& path) {
    const auto body = read_file(path);
    if (!body.empty() || ::access(path.c_str(), F_OK) == 0) {
        write_private(backup_of(path), body);
        return true;
    }
    return false;
}

void restore_backup(const std::string& path, bool had_config) {
    if (!had_config) {
        if (::unlink(path.c_str()) != 0 && errno != ENOENT) {
            throw std::runtime_error("cannot remove candidate sing-box config");
        }
        return;
    }
    const auto body = read_file(backup_of(path));
    write_private(path, body);
}

std::int64_t now_unix() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string redact_error(std::string text) {
    const auto scheme = text.find("://");
    if (scheme == std::string::npos) return text;
    const auto at = text.find('@', scheme + 3);
    const auto slash = text.find('/', scheme + 3);
    if (at != std::string::npos && (slash == std::string::npos || at < slash)) {
        text.replace(scheme + 3, at - (scheme + 3), "***");
    }
    if (text.size() > 300) text.resize(300);
    return text;
}

nlohmann::json outbound_for(const SingboxConnection& connection, const std::string& tag) {
    nlohmann::json outbound;
    if (!connection.link.empty()) {
        outbound = parse_proxy_link(connection.link).outbound;
    } else if (connection.outbound.is_object()) {
        outbound = connection.outbound;
    } else {
        throw std::runtime_error("saved connection has no proxy data");
    }
    outbound["tag"] = tag;
    return outbound;
}

std::string node_tag(const SingboxConnection& connection) {
    return "n_" + connection.id.substr(0, std::min<std::size_t>(12, connection.id.size()));
}

const SingboxConnection* find_active(const SingboxProfile& profile) {
    for (const auto& connection : profile.connections) {
        if (connection.id == profile.active) return &connection;
    }
    return profile.connections.empty() ? nullptr : &profile.connections.front();
}

void require_mode(const std::string& mode) {
    if (mode != "direct" && mode != "selector" && mode != "urltest") {
        throw std::runtime_error("mode must be direct, selector, or urltest");
    }
}

constexpr std::int64_t kSubscriptionRetrySeconds = std::int64_t{5} * 60;

std::vector<SingboxConnection> subscription_connections(const std::string& body, std::size_t& skipped) {
    auto nodes = parse_subscription(body, &skipped);
    std::vector<SingboxConnection> connections;
    connections.reserve(nodes.size());
    for (auto& node : nodes) {
        auto& link = node.link;
        connections.push_back({std::move(node.id), std::move(link.name), std::move(link.protocol),
                               std::move(link.server), link.port, {}, "subscription",
                               std::move(link.outbound)});
    }
    return connections;
}

bool same_subscription_nodes(const std::vector<SingboxConnection>& previous,
                             const std::vector<SingboxConnection>& candidate) {
    const auto sorted_nodes = [](const auto& connections) {
        std::vector<const SingboxConnection*> nodes;
        for (const auto& node : connections) {
            if (node.source == "subscription") nodes.push_back(&node);
        }
        std::stable_sort(nodes.begin(), nodes.end(), [](auto a, auto b) { return a->id < b->id; });
        return nodes;
    };
    const auto before = sorted_nodes(previous);
    const auto after = sorted_nodes(candidate);
    return before.size() == after.size() &&
           std::equal(before.begin(), before.end(), after.begin(), [](auto a, auto b) {
               return a->id == b->id && a->link == b->link && a->outbound == b->outbound;
           });
}

std::string skipped_warning(std::size_t skipped) {
    return skipped == 0 ? "" : std::to_string(skipped) + " subscription lines were skipped";
}

}  // namespace

bool safe_interface_name(const std::string& name) {
    if (name.empty() || name.size() > 15) return false;
    for (char c : name) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
        if (!ok) return false;
    }
    return true;
}

std::string sanitize_keenetic_tag(const std::string& name) {
    std::string tag;
    for (unsigned char c : name) {
        if (c >= 'A' && c <= 'Z') tag.push_back(static_cast<char>(c - 'A' + 'a'));
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) tag.push_back(static_cast<char>(c));
        else if (c == '-' || c == ' ' || c == '.') tag.push_back('_');
    }
    while (!tag.empty() && tag.front() == '_') tag.erase(tag.begin());
    if (tag.empty() || (tag.front() < 'a' || tag.front() > 'z')) tag.insert(tag.begin(), 'p');
    if (tag.size() > 24) tag.resize(24);
    while (!tag.empty() && tag.back() == '_') tag.pop_back();
    return tag;
}

nlohmann::json render_profile(const SingboxProfile& profile, int index, nlohmann::json& outbounds,
                             nlohmann::json& rules) {
    std::vector<std::string> tags;
    for (const auto& connection : profile.connections) {
        const auto tag = node_tag(connection);
        outbounds.push_back(outbound_for(connection, tag));
        tags.push_back(tag);
    }
    const std::string group = "p_" + profile.id;
    std::string final_tag = group;
    if (profile.mode == "direct") {
        const auto* active = find_active(profile);
        final_tag = active == nullptr ? tags.front() : node_tag(*active);
    } else if (profile.mode == "urltest") {
        outbounds.push_back({
            {"type", "urltest"},
            {"tag", group},
            {"outbounds", tags},
            {"url", profile.urltest_url},
            {"interval", "3m"},
        });
    } else {
        nlohmann::json selector{{"type", "selector"}, {"tag", group}, {"outbounds", tags}};
        const auto* active = find_active(profile);
        if (active != nullptr) selector["default"] = node_tag(*active);
        outbounds.push_back(std::move(selector));
    }
    const std::string inbound = "tun_" + profile.id;
    rules.push_back({{"inbound", inbound}, {"outbound", final_tag}});
    const int octet = 1 + (index % 200);
    return {
        {"type", "tun"},
        {"tag", inbound},
        {"interface_name", profile.interface_name},
        {"address", nlohmann::json::array({"172.19." + std::to_string(octet) + ".1/30"})},
        {"auto_route", false},
        {"strict_route", false},
        {"stack", "system"},
    };
}

nlohmann::json SingboxLinkService::render_singbox_config(const std::vector<SingboxProfile>& profiles) {
    nlohmann::json inbounds = nlohmann::json::array();
    nlohmann::json outbounds = nlohmann::json::array();
    nlohmann::json rules = nlohmann::json::array();
    int index = 0;
    for (const auto& profile : profiles) {
        if (profile.connections.empty()) continue;
        inbounds.push_back(render_profile(profile, index, outbounds, rules));
        ++index;
    }
    nlohmann::json route{{"auto_detect_interface", true}, {"rules", std::move(rules)}};
    if (inbounds.size() == 1 && route["rules"].size() == 1) {
        route["final"] = route["rules"][0].at("outbound");
    }
    return {
        {"log", {{"level", "warn"}, {"timestamp", true}}},
        {"inbounds", std::move(inbounds)},
        {"outbounds", std::move(outbounds)},
        {"route", std::move(route)},
    };
}

std::vector<SingboxProfile> SingboxLinkService::load_profiles(const std::string& path) {
    const auto body = read_file(path);
    if (body.empty()) return {};
    const auto json = nlohmann::json::parse(body);
    std::vector<SingboxProfile> profiles;
    if (json.contains("profiles") && json["profiles"].is_array()) {
        for (const auto& item : json["profiles"]) profiles.push_back(profile_from_json(item));
    }
    return profiles;
}

nlohmann::json SingboxLinkService::public_state(const std::vector<SingboxProfile>& profiles) {
    nlohmann::json array = nlohmann::json::array();
    for (const auto& profile : profiles) array.push_back(profile_json(profile, false));
    return {{"profiles", std::move(array)}};
}

void SingboxLinkService::write_store(const std::string& path, const std::vector<SingboxProfile>& profiles) {
    nlohmann::json array = nlohmann::json::array();
    for (const auto& profile : profiles) array.push_back(profile_json(profile, true));
    write_private(path, nlohmann::json{{"profiles", std::move(array)}}.dump(2));
}

SingboxLinkService::SingboxLinkService(SingboxPaths paths, SingboxCheck check,
                                       KeeneticOutboundFn keenetic, SingboxReloadFn reload,
                                       SubscriptionDownloadFn download, SubscriptionClock clock)
    : paths_(std::move(paths)), check_(std::move(check)), keenetic_(std::move(keenetic)),
      reload_(std::move(reload)), download_(std::move(download)),
      clock_(clock ? std::move(clock) : now_unix) {}

std::vector<SingboxProfile> SingboxLinkService::read() const {
    try {
        return load_profiles(paths_.store);
    } catch (const std::exception&) {
        throw std::runtime_error("could not read the sing-box subscription store");
    }
}

nlohmann::json SingboxLinkService::preview(const std::string& link) const {
    return public_proxy_fields(parse_proxy_link(link));
}

std::unique_lock<std::mutex> SingboxLinkService::begin_mutation() {
    std::unique_lock<std::mutex> lock(mutation_mutex_, std::try_to_lock);
    if (!lock) throw SingboxBusy();
    return lock;
}

void SingboxLinkService::update_refresh_status(const std::string& id, RefreshStatus status) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    if (status.last_refresh == 0 && status.retry_at == 0 && status.error.empty()) {
        refresh_status_.erase(id);
    } else {
        refresh_status_[id] = std::move(status);
    }
}

SingboxLinkService::RefreshStatus SingboxLinkService::refresh_status(const SingboxProfile& profile) const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    const auto it = refresh_status_.find(profile.id);
    return it == refresh_status_.end() ? RefreshStatus{profile.last_refresh, 0, profile.last_error}
                                      : it->second;
}

std::int64_t SingboxLinkService::next_refresh(const SingboxProfile& profile, const RefreshStatus& status) {
    if (profile.subscription_url.empty() || profile.interval_hours <= 0) return 0;
    if (status.retry_at != 0) return status.retry_at;
    if (status.last_refresh <= 0) return 0;
    return status.last_refresh + static_cast<std::int64_t>(profile.interval_hours) * 3600;
}

nlohmann::json SingboxLinkService::state() const {
    nlohmann::json array = nlohmann::json::array();
    for (const auto& profile : read()) {
        auto json = profile_json(profile, false);
        if (!profile.subscription_url.empty()) {
            const auto status = refresh_status(profile);
            const auto next = next_refresh(profile, status);
            json["last_refresh"] = status.last_refresh;
            json["last_error"] = status.error;
            json["next_refresh"] = profile.interval_hours <= 0 ? nlohmann::json(nullptr)
                                                               : nlohmann::json(next == 0 ? clock_() : next);
        }
        array.push_back(std::move(json));
    }
    return {{"profiles", std::move(array)}};
}

std::map<std::string, std::string> SingboxLinkService::subscription_errors() const {
    std::map<std::string, std::string> errors;
    std::lock_guard<std::mutex> lock(status_mutex_);
    for (const auto& entry : refresh_status_) {
        if (!entry.second.error.empty()) errors.emplace(entry.first, entry.second.error);
    }
    return errors;
}

bool SingboxLinkService::write_checked_config(const nlohmann::json& config) {
    const bool had_config = backup_file(paths_.singbox_config);
    write_private(paths_.singbox_config, config.dump(2));
    try {
        std::string check_error;
        if (check_ && check_(paths_.singbox_config, &check_error) != 0) {
            throw std::runtime_error("sing-box check failed: " + redact_error(check_error));
        }
    } catch (...) {
        restore_backup(paths_.singbox_config, had_config);
        throw;
    }
    return had_config;
}

SingboxApplyResult SingboxLinkService::commit(std::vector<SingboxProfile> profiles,
                                              const std::string& changed_id) {
    const SingboxProfile* changed = nullptr;
    for (const auto& profile : profiles) {
        if (profile.id == changed_id) changed = &profile;
    }
    if (changed == nullptr) throw std::runtime_error("sing-box profile disappeared");
    bool any = false;
    for (const auto& profile : profiles) any = any || !profile.connections.empty();
    if (!any) {
        backup_file(paths_.singbox_config);
        ::unlink(paths_.singbox_config.c_str());
        write_store(paths_.store, profiles);
        if (keenetic_) keenetic_(*changed, false);
        SingboxApplyResult result;
        result.keenetic_applied = true;
        result.state = public_state(profiles);
        result.warning = "removed the sing-box config because no connections remain";
        return result;
    }
    const auto rendered = render_singbox_config(profiles);
    backup_file(paths_.store);
    const auto previous_profiles = read();
    const bool had_config = write_checked_config(rendered);
    try {
        write_store(paths_.store, profiles);
        if (keenetic_) keenetic_(*changed, !changed->connections.empty());
    } catch (const std::exception&) {
        write_store(paths_.store, previous_profiles);
        restore_backup(paths_.singbox_config, had_config);
        throw;
    }
    SingboxApplyResult result;
    result.singbox_checked = true;
    result.keenetic_applied = true;
    try {
        result.singbox_reloaded = reload_ && reload_();
        if (!result.singbox_reloaded) {
            result.warning = "sing-box config passed check; restart sing-box on the router to load it";
        }
    } catch (const std::exception& error) {
        result.warning = std::string("sing-box config is valid but was not restarted: ") + error.what();
    }
    result.state = public_state(profiles);
    return result;
}

SingboxApplyResult SingboxLinkService::save_connection(const std::string& profile_name,
                                                       const std::string& interface_name,
                                                       const std::string& tag,
                                                       const std::string& mode,
                                                       const std::string& connection_name,
                                                        const std::string& link,
                                                        const std::string& replace_id) {
    const auto lock = begin_mutation();
    require_mode(mode.empty() ? "selector" : mode);
    const auto parsed = parse_proxy_link(link);
    auto profiles = read();
    const auto wanted_tag = sanitize_keenetic_tag(tag.empty() ? profile_name : tag);
    const auto wanted_iface = interface_name.empty() ? "sbtun" : interface_name;
    if (!safe_interface_name(wanted_iface)) throw std::runtime_error("interface name is invalid");
    SingboxProfile* profile = nullptr;
    for (auto& item : profiles) {
        if (item.keenetic_tag == wanted_tag || item.interface_name == wanted_iface ||
            item.name == profile_name) {
            profile = &item;
            break;
        }
    }
    if (profile == nullptr) {
        SingboxProfile created;
        created.id = wanted_tag;
        created.name = profile_name.empty() ? wanted_tag : profile_name;
        created.interface_name = wanted_iface;
        created.keenetic_tag = wanted_tag;
        created.mode = mode.empty() ? "selector" : mode;
        profiles.push_back(std::move(created));
        profile = &profiles.back();
    }
    if (!mode.empty()) profile->mode = mode;
    SingboxConnection connection;
    connection.id = proxy_node_id(parsed);
    connection.name = connection_name.empty() ? parsed.name : connection_name;
    connection.protocol = parsed.protocol;
    connection.server = parsed.server;
    connection.port = parsed.port;
    connection.link = link;
    connection.source = "manual";
    bool replaced = false;
    for (auto& existing : profile->connections) {
        if ((!replace_id.empty() && existing.id == replace_id) || existing.id == connection.id) {
            connection.source = existing.source;
            existing = connection;
            replaced = true;
            break;
        }
    }
    if (!replaced) profile->connections.push_back(connection);
    if (profile->active.empty() || profile->mode == "direct") profile->active = connection.id;
    const auto id = profile->id;
    return commit(std::move(profiles), id);
}

SingboxApplyResult SingboxLinkService::remove_connection(const std::string& profile_id,
                                                         const std::string& connection_id) {
    const auto lock = begin_mutation();
    auto profiles = read();
    SingboxProfile* profile = nullptr;
    for (auto& item : profiles) {
        if (item.id == profile_id) profile = &item;
    }
    if (profile == nullptr) throw std::runtime_error("sing-box profile was not found");
    std::vector<SingboxConnection> kept;
    for (auto& connection : profile->connections) {
        if (connection.id != connection_id) kept.push_back(std::move(connection));
    }
    profile->connections = std::move(kept);
    if (profile->active == connection_id) {
        profile->active = profile->connections.empty() ? "" : profile->connections.front().id;
    }
    if (profile->connections.empty()) {
        const auto id = profile->id;
        auto result = commit(profiles, id);
        std::vector<SingboxProfile> remaining;
        for (auto& item : profiles) {
            if (item.id != id) remaining.push_back(std::move(item));
        }
        write_store(paths_.store, remaining);
        update_refresh_status(id, {});
        result.state = public_state(remaining);
        return result;
    }
    return commit(std::move(profiles), profile_id);
}

SingboxApplyResult SingboxLinkService::set_mode(const std::string& profile_id, const std::string& mode,
                                                const std::string& active) {
    const auto lock = begin_mutation();
    require_mode(mode);
    auto profiles = read();
    SingboxProfile* profile = nullptr;
    for (auto& item : profiles) {
        if (item.id == profile_id) profile = &item;
    }
    if (profile == nullptr) throw std::runtime_error("sing-box profile was not found");
    profile->mode = mode;
    if (!active.empty()) {
        if (std::none_of(profile->connections.begin(), profile->connections.end(),
                         [&](const auto& node) { return node.id == active; })) {
            throw std::runtime_error("sing-box connection was not found");
        }
        profile->active = active;
    }
    return commit(std::move(profiles), profile_id);
}

SingboxApplyResult SingboxLinkService::save_subscription(const std::string& profile_name,
                                                         const std::string& interface_name,
                                                         const std::string& tag, const std::string& mode,
                                                          const std::string& url, int interval_hours) {
    const auto lock = begin_mutation();
    if (url.find("://") == std::string::npos) throw std::runtime_error("subscription URL is invalid");
    require_mode(mode.empty() ? "urltest" : mode);
    std::string body;
    try {
        body = download_(url, {});
    } catch (const std::exception&) {
        throw std::runtime_error("subscription download failed");
    }
    std::size_t skipped = 0;
    std::vector<SingboxConnection> nodes;
    try {
        nodes = subscription_connections(body, skipped);
    } catch (const std::exception&) {
        throw std::runtime_error("subscription did not contain usable proxy data");
    }
    auto profiles = read();
    const auto wanted_tag = sanitize_keenetic_tag(tag.empty() ? profile_name : tag);
    const auto wanted_iface = interface_name.empty() ? "sbtun" : interface_name;
    if (!safe_interface_name(wanted_iface)) throw std::runtime_error("interface name is invalid");
    SingboxProfile* profile = nullptr;
    for (auto& item : profiles) {
        if (item.id == wanted_tag) profile = &item;
    }
    if (profile == nullptr) {
        profiles.push_back(SingboxProfile{});
        profile = &profiles.back();
        profile->id = wanted_tag;
    }
    profile->name = profile_name.empty() ? wanted_tag : profile_name;
    profile->interface_name = wanted_iface;
    profile->keenetic_tag = wanted_tag;
    profile->mode = mode.empty() ? "urltest" : mode;
    profile->subscription_url = url;
    profile->interval_hours = interval_hours < 0 ? 0 : interval_hours;
    profile->last_refresh = clock_();
    profile->last_error = skipped_warning(skipped);
    std::vector<SingboxConnection> manual;
    for (auto& connection : profile->connections) {
        if (connection.source == "manual") manual.push_back(std::move(connection));
    }
    profile->connections = std::move(manual);
    for (auto& node : nodes) profile->connections.push_back(std::move(node));
    bool active_ok = false;
    for (const auto& connection : profile->connections) {
        if (connection.id == profile->active) active_ok = true;
    }
    if (!active_ok && !profile->connections.empty()) profile->active = profile->connections.front().id;
    const auto id = profile->id;
    auto result = commit(std::move(profiles), id);
    update_refresh_status(id, {});
    return result;
}

void SingboxLinkService::refresh_one(std::vector<SingboxProfile>& profiles, std::size_t index,
                                    const SubscriptionStopFn& stopping) {
    auto& profile = profiles[index];
    auto status = refresh_status(profile);
    const char* failure = "subscription download failed";
    try {
        std::size_t skipped = 0;
        auto nodes = [&] {
            const auto body = download_(profile.subscription_url, stopping);
            if (stopping && stopping()) return std::vector<SingboxConnection>{};
            failure = "subscription did not contain usable proxy data; keeping the previous configuration";
            return subscription_connections(body, skipped);
        }();
        if (stopping && stopping()) return;
        for (const auto& node : profile.connections) {
            if (node.source != "subscription") nodes.push_back(node);
        }
        const auto active = std::find_if(nodes.begin(), nodes.end(), [&](const auto& node) {
            return node.id == profile.active;
        });
        if (!profile.active.empty() && active == nodes.end()) {
            failure = "selected node disappeared from subscription; keeping the previous configuration";
            throw std::runtime_error(failure);
        }
        const auto refreshed = clock_();
        auto warning = skipped_warning(skipped);
        if (!same_subscription_nodes(profile.connections, nodes)) {
            // The Keenetic tag and interface have not changed; do not reapply routing.
            const auto previous_active = profile.active;
            const auto previous_refresh = profile.last_refresh;
            auto previous_error = profile.last_error;
            std::stable_partition(nodes.begin(), nodes.end(),
                                  [](const auto& node) { return node.source != "subscription"; });
            if (profile.active.empty()) profile.active = nodes.front().id;
            profile.connections.swap(nodes);
            profile.last_refresh = refreshed;
            profile.last_error = warning;
            try {
                failure = "could not apply subscription: sing-box config write or check failed";
                const bool had_config = write_checked_config(render_singbox_config(profiles));
                try {
                    failure = "could not restart sing-box; restoring the previous configuration";
                    if (!reload_ || !reload_()) throw std::runtime_error(failure);
                    failure = "could not save subscription; restoring the previous configuration";
                    write_store(paths_.store, profiles);
                } catch (...) {
                    try {
                        restore_backup(paths_.singbox_config, had_config);
                        if (had_config && reload_ && !reload_()) throw std::runtime_error("restart failed");
                    } catch (...) {
                        failure = "subscription rollback failed; check sing-box configuration and service";
                    }
                    throw;
                }
            } catch (...) {
                profile.connections.swap(nodes);
                profile.active = previous_active;
                profile.last_refresh = previous_refresh;
                profile.last_error = std::move(previous_error);
                throw;
            }
        }
        update_refresh_status(profile.id, {refreshed, 0, std::move(warning)});
    } catch (const std::exception&) {
        if (stopping && stopping()) return;
        update_refresh_status(profile.id, {status.last_refresh, clock_() + kSubscriptionRetrySeconds, failure});
    }
}

nlohmann::json SingboxLinkService::refresh_subscriptions(const std::string& profile_id, bool due_only,
                                                        const SubscriptionStopFn& stopping) {
    const auto lock = begin_mutation();
    auto profiles = read();
    nlohmann::json results = nlohmann::json::array();
    for (std::size_t i = 0; i < profiles.size(); ++i) {
        if (stopping && stopping()) break;
        const auto& profile = profiles[i];
        if (profile.subscription_url.empty() || (!profile_id.empty() && profile.id != profile_id)) continue;
        const auto status = refresh_status(profile);
        if (due_only && (profile.interval_hours <= 0 || next_refresh(profile, status) > clock_())) continue;
        refresh_one(profiles, i, stopping);
        if (stopping && stopping()) break;
        const auto updated = refresh_status(profile);
        results.push_back({{"id", profile.id}, {"ok", updated.retry_at == 0}, {"message", updated.error}});
    }
    if (!profile_id.empty() && results.empty() && !(stopping && stopping())) {
        throw std::runtime_error("subscription was not found");
    }
    return results;
}

}  // namespace keen_pbr3
