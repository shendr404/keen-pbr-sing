#include "link_store.hpp"

#include <algorithm>
#include <chrono>
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
    return {
        {"id", profile.id},
        {"name", profile.name},
        {"interface", profile.interface_name},
        {"keenetic_tag", profile.keenetic_tag},
        {"mode", profile.mode},
        {"active", profile.active},
        {"urltest_url", profile.urltest_url},
        {"subscription_url", profile.subscription_url},
        {"interval_hours", profile.interval_hours},
        {"last_refresh", profile.last_refresh},
        {"last_error", profile.last_error},
        {"connections", std::move(connections)},
    };
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
    {
        std::ofstream output(temp, std::ios::trunc);
        if (!output) throw std::runtime_error("cannot write " + path);
        output << body;
        if (!output) throw std::runtime_error("cannot write " + path);
    }
    ::chmod(temp.c_str(), 0600);
    if (::rename(temp.c_str(), path.c_str()) != 0) {
        ::unlink(temp.c_str());
        throw std::runtime_error("cannot replace " + path);
    }
}

std::string backup_of(const std::string& path) { return path + ".bak-keenpbr"; }

void backup_file(const std::string& path) {
    const auto body = read_file(path);
    if (!body.empty() || ::access(path.c_str(), F_OK) == 0) {
        write_private(backup_of(path), body);
    }
}

void restore_backup(const std::string& path) {
    const auto backup = backup_of(path);
    if (::access(backup.c_str(), F_OK) != 0) {
        ::unlink(path.c_str());
        return;
    }
    const auto body = read_file(backup);
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
                                       KeeneticOutboundFn keenetic, SingboxReloadFn reload)
    : paths_(std::move(paths)), check_(std::move(check)), keenetic_(std::move(keenetic)),
      reload_(std::move(reload)) {}

std::vector<SingboxProfile> SingboxLinkService::read() const {
    return load_profiles(paths_.store);
}

nlohmann::json SingboxLinkService::preview(const std::string& link) const {
    return public_proxy_fields(parse_proxy_link(link));
}

nlohmann::json SingboxLinkService::state(bool refresh_due) {
    auto profiles = read();
    if (!refresh_due) return public_state(profiles);
    for (auto& profile : profiles) {
        if (profile.subscription_url.empty() || profile.interval_hours <= 0) continue;
        if (profile.last_refresh != 0 &&
            now_unix() - profile.last_refresh < static_cast<std::int64_t>(profile.interval_hours) * 3600) {
            continue;
        }
        profile.last_error = "subscription is due; open the page and refresh it";
    }
    return public_state(profiles);
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
    backup_file(paths_.singbox_config);
    backup_file(paths_.store);
    const auto previous_profiles = read();
    write_private(paths_.singbox_config, rendered.dump(2));
    std::string check_error;
    const int status = check_ ? check_(paths_.singbox_config, &check_error) : 0;
    if (status != 0) {
        restore_backup(paths_.singbox_config);
        throw std::runtime_error(std::string("sing-box check failed: ") + redact_error(check_error));
    }
    try {
        write_store(paths_.store, profiles);
        if (keenetic_) keenetic_(*changed, !changed->connections.empty());
    } catch (const std::exception&) {
        write_store(paths_.store, previous_profiles);
        restore_backup(paths_.singbox_config);
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
        result.state = public_state(remaining);
        return result;
    }
    return commit(std::move(profiles), profile->id);
}

SingboxApplyResult SingboxLinkService::set_mode(const std::string& profile_id, const std::string& mode,
                                                const std::string& active) {
    require_mode(mode);
    auto profiles = read();
    SingboxProfile* profile = nullptr;
    for (auto& item : profiles) {
        if (item.id == profile_id) profile = &item;
    }
    if (profile == nullptr) throw std::runtime_error("sing-box profile was not found");
    profile->mode = mode;
    if (!active.empty()) profile->active = active;
    return commit(std::move(profiles), profile->id);
}

SingboxApplyResult SingboxLinkService::save_subscription(const std::string& profile_name,
                                                         const std::string& interface_name,
                                                         const std::string& tag, const std::string& mode,
                                                         const std::string& url, int interval_hours,
                                                         const std::string& body) {
    if (url.find("://") == std::string::npos) throw std::runtime_error("subscription URL is invalid");
    require_mode(mode.empty() ? "urltest" : mode);
    std::size_t skipped = 0;
    const auto nodes = parse_subscription(body, &skipped);
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
    profile->last_refresh = now_unix();
    profile->last_error = skipped == 0 ? "" : std::to_string(skipped) + " subscription lines were skipped";
    std::vector<SingboxConnection> manual;
    for (auto& connection : profile->connections) {
        if (connection.source == "manual") manual.push_back(std::move(connection));
    }
    profile->connections = std::move(manual);
    for (const auto& node : nodes) {
        SingboxConnection connection;
        connection.id = node.id;
        connection.name = node.link.name;
        connection.protocol = node.link.protocol;
        connection.server = node.link.server;
        connection.port = node.link.port;
        connection.source = "subscription";
        if (node.link.outbound.contains("uuid") || node.link.outbound.contains("password")) {
            connection.outbound = node.link.outbound;
        }
        profile->connections.push_back(std::move(connection));
    }
    if (find_active(*profile) == nullptr && !profile->connections.empty()) {
        profile->active = profile->connections.front().id;
    }
    bool active_ok = false;
    for (const auto& connection : profile->connections) {
        if (connection.id == profile->active) active_ok = true;
    }
    if (!active_ok && !profile->connections.empty()) profile->active = profile->connections.front().id;
    const auto id = profile->id;
    return commit(std::move(profiles), id);
}

SingboxApplyResult SingboxLinkService::refresh_subscription(const std::string& profile_id,
                                                           const std::string& body) {
    auto profiles = read();
    SingboxProfile* profile = nullptr;
    for (auto& item : profiles) {
        if (item.id == profile_id) profile = &item;
    }
    if (profile == nullptr) throw std::runtime_error("sing-box profile was not found");
    return save_subscription(profile->name, profile->interface_name, profile->keenetic_tag, profile->mode,
                             profile->subscription_url, profile->interval_hours, body);
}

}  // namespace keen_pbr3
