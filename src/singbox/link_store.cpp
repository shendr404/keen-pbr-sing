#include "link_store.hpp"
#include "link_parser.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <fcntl.h>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace keen_pbr3 {
namespace {

std::string trim(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.erase(text.begin());
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
    return text;
}

bool blank(const std::string& text) {
    for (unsigned char c : text) {
        if (!std::isspace(c)) return false;
    }
    return true;
}

void mirror_subscription(SingboxProfile& profile) {
    if (profile.subscriptions.empty()) {
        profile.subscription_url.clear();
        profile.interval_hours = 0;
        return;
    }
    const auto& subscription = profile.subscriptions.front();
    profile.subscription_url = subscription.url;
    profile.interval_hours = subscription.interval_hours;
    profile.last_refresh = subscription.last_refresh;
    profile.last_error = subscription.last_error;
}

nlohmann::json connection_json(const SingboxConnection& connection, bool with_secret) {
    nlohmann::json json{
        {"id", connection.id},
        {"name", connection.name},
        {"protocol", connection.protocol},
        {"server", connection.server},
        {"port", connection.port},
        {"source", connection.source},
    };
    if (!connection.subscription_id.empty()) json["subscription_id"] = connection.subscription_id;
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
    connection.subscription_id = json.value("subscription_id", "");
    connection.link = json.value("link", "");
    if (json.contains("outbound")) connection.outbound = json["outbound"];
    return connection;
}

nlohmann::json subscription_json(const SingboxSubscription& subscription, bool with_secret) {
    nlohmann::json json{
        {"id", subscription.id},
        {"interval_hours", subscription.interval_hours},
        {"last_refresh", subscription.last_refresh},
        {"last_error", subscription.last_error},
    };
    if (with_secret) json["url"] = subscription.url;
    return json;
}

SingboxSubscription subscription_from_json(const nlohmann::json& json) {
    SingboxSubscription subscription;
    subscription.id = json.value("id", "");
    subscription.url = json.value("url", "");
    subscription.interval_hours = json.value("interval_hours", 0);
    subscription.last_refresh = json.value("last_refresh", static_cast<std::int64_t>(0));
    subscription.last_error = json.value("last_error", "");
    return subscription;
}

nlohmann::json profile_json(const SingboxProfile& profile, bool with_secret) {
    nlohmann::json connections = nlohmann::json::array();
    for (const auto& connection : profile.connections) connections.push_back(connection_json(connection, with_secret));
    nlohmann::json subscriptions = nlohmann::json::array();
    for (const auto& subscription : profile.subscriptions) {
        subscriptions.push_back(subscription_json(subscription, with_secret));
    }
    nlohmann::json json{
        {"id", profile.id},
        {"name", profile.name},
        {"interface", profile.interface_name},
        {"keenetic_tag", profile.keenetic_tag},
        {"mode", profile.mode},
        {"active", profile.active},
        {"urltest_url", profile.urltest_url},
        {"urltest_interval", profile.urltest_interval},
        {"tun_stack", profile.tun_stack},
        {"tun_mtu", profile.tun_mtu},
        {"tun_address", profile.tun_address},
        {"interval_hours", profile.interval_hours},
        {"last_refresh", profile.last_refresh},
        {"last_error", profile.last_error},
        {"connections", std::move(connections)},
        {"subscriptions", std::move(subscriptions)},
    };
    if (with_secret) json["subscription_url"] = profile.subscription_url;
    else json["has_subscription"] = !profile.subscriptions.empty() || !profile.subscription_url.empty();
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
    profile.urltest_interval = json.value("urltest_interval", "3m");
    profile.tun_stack = json.value("tun_stack", "gvisor");
    profile.tun_mtu = json.value("tun_mtu", 1400);
    profile.tun_address = json.value("tun_address", "");
    profile.subscription_url = json.value("subscription_url", "");
    profile.interval_hours = json.value("interval_hours", 0);
    profile.last_refresh = json.value("last_refresh", static_cast<std::int64_t>(0));
    profile.last_error = json.value("last_error", "");
    if (json.contains("subscriptions") && json["subscriptions"].is_array()) {
        for (const auto& item : json["subscriptions"]) profile.subscriptions.push_back(subscription_from_json(item));
    }
    if (profile.subscriptions.empty() && !profile.subscription_url.empty()) {
        SingboxSubscription subscription;
        subscription.id = "sub";
        subscription.url = profile.subscription_url;
        subscription.interval_hours = profile.interval_hours;
        subscription.last_refresh = profile.last_refresh;
        subscription.last_error = profile.last_error;
        profile.subscriptions.push_back(std::move(subscription));
    }
    if (json.contains("connections") && json["connections"].is_array()) {
        for (const auto& item : json["connections"]) profile.connections.push_back(connection_from_json(item));
    }
    if (profile.subscriptions.size() == 1) {
        for (auto& connection : profile.connections) {
            if (connection.source == "subscription" && connection.subscription_id.empty()) {
                connection.subscription_id = profile.subscriptions.front().id;
            }
        }
    }
    mirror_subscription(profile);
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

std::string original_backup_of(const std::string& path) { return path + ".bak-keenpbr-original"; }

bool backup_file(const std::string& path) {
    const auto body = read_file(path);
    if (!body.empty() || ::access(path.c_str(), F_OK) == 0) {
        write_private(backup_of(path), body);
        return true;
    }
    return false;
}

// Kept once, before the first keen-pbr edit of an existing sing-box config.
// Later saves overwrite only the rolling .bak-keenpbr copy.
void preserve_original_config(const std::string& path) {
    const auto original = original_backup_of(path);
    if (::access(original.c_str(), F_OK) == 0) return;
    if (::access(path.c_str(), F_OK) != 0) return;
    const auto body = read_file(path);
    if (body.empty()) return;
    write_private(original, body);
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

bool is_hex(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }

bool is_uuid_at(const std::string& text, std::size_t index) {
    if (index + 36 > text.size()) return false;
    const int groups[] = {8, 4, 4, 4, 12};
    std::size_t cursor = index;
    for (int group = 0; group < 5; ++group) {
        if (group != 0) {
            if (text[cursor] != '-') return false;
            ++cursor;
        }
        for (int digit = 0; digit < groups[group]; ++digit) {
            if (!is_hex(text[cursor])) return false;
            ++cursor;
        }
    }
    return true;
}

void redact_uuids(std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        if (is_uuid_at(text, i)) {
            out += "***";
            i += 36;
        } else {
            out.push_back(text[i]);
            ++i;
        }
    }
    text.swap(out);
}

void redact_json_field(std::string& text, const std::string& key) {
    const std::string pattern = "\"" + key + "\"";
    std::size_t pos = 0;
    while ((pos = text.find(pattern, pos)) != std::string::npos) {
        const auto colon = text.find(':', pos + pattern.size());
        if (colon == std::string::npos || colon > pos + pattern.size() + 8) {
            pos += pattern.size();
            continue;
        }
        const auto open = text.find('"', colon + 1);
        if (open == std::string::npos) return;
        const auto close = text.find('"', open + 1);
        if (close == std::string::npos) return;
        text.replace(open + 1, close - open - 1, "***");
        pos = open + 4;
    }
}

void redact_query_param(std::string& text, const std::string& key) {
    const std::string pattern = key + "=";
    std::size_t pos = 0;
    while ((pos = text.find(pattern, pos)) != std::string::npos) {
        if (pos > 0) {
            const unsigned char previous = static_cast<unsigned char>(text[pos - 1]);
            if (std::isalnum(previous) || previous == '_' || previous == '-') {
                pos += pattern.size();
                continue;
            }
        }
        const auto start = pos + pattern.size();
        auto end = start;
        while (end < text.size() && text[end] != '&' && !std::isspace(static_cast<unsigned char>(text[end])) &&
               text[end] != '"' && text[end] != '\'') {
            ++end;
        }
        text.replace(start, end - start, "***");
        pos = start + 3;
    }
}

void redact_urls(std::string& text) {
    for (const char* scheme : {"https://", "http://"}) {
        const auto length = std::char_traits<char>::length(scheme);
        std::size_t pos = 0;
        while ((pos = text.find(scheme, pos)) != std::string::npos) {
            auto end = pos + length;
            while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])) && text[end] != '"' &&
                   text[end] != '\'') {
                ++end;
            }
            text.replace(pos + length, end - (pos + length), "***");
            pos += length + 3;
        }
    }
}

std::string redact_secrets(std::string text) {
    redact_urls(text);
    redact_uuids(text);
    for (const char* key : {"password", "uuid", "public_key", "private_key", "short_id", "token", "auth"}) {
        redact_json_field(text, key);
    }
    for (const char* key : {"password", "obfs-password", "pbk", "sid", "encryption"}) {
        redact_query_param(text, key);
    }
    if (text.size() > 500) text.resize(500);
    return text;
}

std::string prominent_line(const std::string& output) {
    std::string first;
    std::size_t pos = 0;
    while (pos < output.size()) {
        const auto end = output.find('\n', pos);
        auto line = trim(output.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
        if (line.find("FATAL") != std::string::npos || line.find("ERROR") != std::string::npos) return line;
        if (first.empty() && !line.empty()) first = std::move(line);
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return first.empty() ? "invalid configuration" : first;
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
    return "kpbr-n-" + connection.id.substr(0, std::min<std::size_t>(12, connection.id.size()));
}

std::string legacy_node_tag(const SingboxConnection& connection) {
    return "n_" + connection.id.substr(0, std::min<std::size_t>(12, connection.id.size()));
}

std::string group_tag(const SingboxProfile& profile) { return "kpbr-g-" + profile.id; }

std::string inbound_tag(const SingboxProfile& profile) { return "kpbr-tun-" + profile.id; }

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

void require_stack(const std::string& stack) {
    if (stack != "gvisor" && stack != "system") {
        throw std::runtime_error("TUN stack must be gvisor or system");
    }
}

void require_mtu(int mtu) {
    if (mtu < 576 || mtu > 65535) throw std::runtime_error("TUN MTU must be between 576 and 65535");
}

void require_duration(const std::string& value) {
    if (value.empty()) return;
    std::size_t index = 0;
    if (value[index] < '1' || value[index] > '9') throw std::runtime_error("urltest interval is invalid");
    while (index < value.size() && std::isdigit(static_cast<unsigned char>(value[index]))) ++index;
    const auto unit = value.substr(index);
    if (unit != "ms" && unit != "s" && unit != "m" && unit != "h") {
        throw std::runtime_error("urltest interval is invalid");
    }
}

bool safe_cidr(const std::string& cidr) {
    if (cidr.size() < 3 || cidr.size() > 64) return false;
    int slashes = 0;
    for (unsigned char c : cidr) {
        const bool ok = std::isxdigit(c) || c == '.' || c == ':' || c == '/';
        if (!ok) return false;
        if (c == '/') ++slashes;
    }
    return slashes == 1;
}

int tun_subnet_octet(const std::string& cidr) {
    const std::string prefix = "172.19.";
    if (cidr.rfind(prefix, 0) != 0) return 0;
    int octet = 0;
    std::size_t index = prefix.size();
    if (index >= cidr.size() || !std::isdigit(static_cast<unsigned char>(cidr[index]))) return 0;
    while (index < cidr.size() && std::isdigit(static_cast<unsigned char>(cidr[index]))) {
        octet = octet * 10 + (cidr[index] - '0');
        if (octet > 255) return 0;
        ++index;
    }
    return octet >= 1 && octet <= 200 ? octet : 0;
}

void note_tun_address(std::set<int>& used, const nlohmann::json& address) {
    if (address.is_string()) {
        const int octet = tun_subnet_octet(address.get<std::string>());
        if (octet > 0) used.insert(octet);
    } else if (address.is_array()) {
        for (const auto& item : address) {
            if (!item.is_string()) continue;
            const int octet = tun_subnet_octet(item.get<std::string>());
            if (octet > 0) used.insert(octet);
        }
    }
}

std::string next_free_interface(const nlohmann::json& existing, const std::vector<SingboxProfile>& profiles,
                                const std::string& self_id) {
    std::set<std::string> used;
    for (const auto& profile : profiles) {
        if (profile.id == self_id || profile.interface_name.empty()) continue;
        used.insert(profile.interface_name);
    }
    if (existing.contains("inbounds") && existing["inbounds"].is_array()) {
        for (const auto& inbound : existing["inbounds"]) {
            if (!inbound.is_object()) continue;
            const auto tag = inbound.value("tag", "");
            if (!self_id.empty() && (tag == "kpbr-tun-" + self_id || tag == "tun_" + self_id)) continue;
            const auto iface = inbound.value("interface_name", "");
            if (!iface.empty()) used.insert(iface);
        }
    }
    if (!used.count("sbtun")) return "sbtun";
    for (int number = 2; number < 1000; ++number) {
        const auto name = "sbtun" + std::to_string(number);
        if (name.size() > 15) break;
        if (!used.count(name)) return name;
    }
    throw std::runtime_error("no free TUN interface name");
}

int next_free_tun_octet(std::set<int>& used) {
    for (int octet = 1; octet <= 200; ++octet) {
        if (!used.count(octet)) {
            used.insert(octet);
            return octet;
        }
    }
    throw std::runtime_error("no free TUN address in 172.19.0.0/16");
}

std::vector<std::string> split_addresses(const std::string& text) {
    std::vector<std::string> out;
    std::string token;
    for (char c : text) {
        if (c == ',' || std::isspace(static_cast<unsigned char>(c))) {
            if (!token.empty()) {
                out.push_back(token);
                token.clear();
            }
        } else {
            token.push_back(c);
        }
    }
    if (!token.empty()) out.push_back(token);
    return out;
}

void require_addresses(const std::string& text) {
    for (const auto& cidr : split_addresses(text)) {
        if (!safe_cidr(cidr)) throw std::runtime_error("TUN address is invalid");
    }
}

void validate_unique(const std::vector<SingboxProfile>& profiles) {
    for (std::size_t i = 0; i < profiles.size(); ++i) {
        for (std::size_t j = i + 1; j < profiles.size(); ++j) {
            if (profiles[i].id == profiles[j].id || profiles[i].keenetic_tag == profiles[j].keenetic_tag) {
                throw std::runtime_error("keen-pbr tag is already used");
            }
            if (profiles[i].interface_name == profiles[j].interface_name) {
                throw std::runtime_error("TUN interface is already used");
            }
        }
    }
}

constexpr std::int64_t kSubscriptionRetrySeconds = std::int64_t{5} * 60;

std::string subscription_warning(const SubscriptionParseInfo& info) {
    std::size_t transport_count = 0;
    std::string names;
    for (const auto& item : info.unsupported_transports) {
        transport_count += item.second;
        if (!names.empty()) names += ", ";
        names += item.first;
    }
    std::string text;
    if (transport_count > 0) {
        text = std::to_string(transport_count) + " nodes skipped: transport " + names +
               " is not supported by sing-box";
    }
    const auto other = info.skipped > transport_count ? info.skipped - transport_count : 0;
    if (other > 0) {
        if (!text.empty()) text += ". ";
        text += std::to_string(other) + " subscription lines were skipped";
    }
    if (info.encryption_ignored > 0) {
        if (!text.empty()) text += ". ";
        text += "vless encryption is ignored; sing-box does not support it";
    }
    return text;
}

void attach_skips(SingboxApplyResult& result, const SubscriptionParseInfo& info) {
    std::size_t transport_count = 0;
    for (const auto& item : info.unsupported_transports) {
        transport_count += item.second;
        result.skipped_transports.push_back(item.first);
    }
    result.skipped = transport_count > 0 ? transport_count : info.skipped;
    result.encryption_ignored = info.encryption_ignored > 0;
    const auto other = info.skipped > transport_count ? info.skipped - transport_count : 0;
    if (other > 0) {
        if (!result.warning.empty()) result.warning += " ";
        result.warning += std::to_string(other) + " subscription lines were skipped";
    }
}

std::vector<SingboxConnection> subscription_connections(const std::string& body, SubscriptionParseInfo& info) {
    std::vector<SubscriptionNode> nodes;
    try {
        nodes = parse_subscription(body, &info);
    } catch (const std::runtime_error&) {
        auto message = subscription_warning(info);
        if (message.empty()) message = "subscription did not contain usable proxy data";
        throw std::runtime_error(message);
    }
    std::vector<SingboxConnection> connections;
    connections.reserve(nodes.size());
    for (auto& node : nodes) {
        auto& link = node.link;
        connections.push_back(SingboxConnection{std::move(node.id), std::move(link.name), std::move(link.protocol),
                                                 std::move(link.server), link.port, {}, "subscription", "",
                                                 std::move(link.outbound)});
    }
    return connections;
}

bool same_nodes(const std::vector<SingboxConnection>& previous, const std::vector<SingboxConnection>& candidate,
                const std::string& subscription_id) {
    const auto sorted_nodes = [&](const auto& connections) {
        std::vector<const SingboxConnection*> nodes;
        for (const auto& node : connections) {
            if (node.subscription_id == subscription_id) nodes.push_back(&node);
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

struct DropTags {
    std::set<std::string> inbounds;
    std::set<std::string> outbounds;
};

void collect_tags(DropTags& tags, const SingboxProfile& profile) {
    tags.inbounds.insert("tun_" + profile.id);
    tags.inbounds.insert(inbound_tag(profile));
    tags.outbounds.insert("p_" + profile.id);
    tags.outbounds.insert(group_tag(profile));
    for (const auto& connection : profile.connections) {
        tags.outbounds.insert(legacy_node_tag(connection));
        tags.outbounds.insert(node_tag(connection));
    }
}

bool is_dropped_inbound(const std::string& tag, const DropTags& tags) {
    return tags.inbounds.count(tag) > 0 || tag.rfind("kpbr-", 0) == 0;
}

bool is_dropped_outbound(const std::string& tag, const DropTags& tags) {
    return tags.outbounds.count(tag) > 0 || tag.rfind("kpbr-", 0) == 0;
}

bool unmanaged_remains(const nlohmann::json& original, const DropTags& tags) {
    for (const char* key : {"dns", "experimental", "endpoints", "services"}) {
        if (original.contains(key) && !original.at(key).is_null()) return true;
    }
    if (original.contains("inbounds") && original["inbounds"].is_array()) {
        for (const auto& inbound : original["inbounds"]) {
            if (!inbound.is_object() || !is_dropped_inbound(inbound.value("tag", ""), tags)) return true;
        }
    }
    if (original.contains("outbounds") && original["outbounds"].is_array()) {
        for (const auto& outbound : original["outbounds"]) {
            if (!outbound.is_object() || !is_dropped_outbound(outbound.value("tag", ""), tags)) return true;
        }
    }
    if (original.contains("route") && original["route"].is_object() && original["route"].contains("rules") &&
        original["route"]["rules"].is_array()) {
        for (const auto& rule : original["route"]["rules"]) {
            if (!rule.is_object()) return true;
            const auto inbound = rule.value("inbound", "");
            const auto outbound = rule.value("outbound", "");
            if (!is_dropped_inbound(inbound, tags) && !is_dropped_outbound(outbound, tags)) return true;
        }
    }
    for (auto it = original.begin(); it != original.end(); ++it) {
        const std::string key = it.key();
        if (key != "log" && key != "inbounds" && key != "outbounds" && key != "route") return true;
    }
    return false;
}

std::string interface_taken_message(const std::string& iface) {
    return "TUN interface " + iface +
           " already exists in the sing-box config and is not managed by keen-pbr. "
           "The existing config was not changed. Choose another interface name.";
}

// Keenetic owns sing-box objects whose tags start with kpbr- (and the legacy
// tun_/n_/p_ tags of profiles still stored). dns, log, experimental and every
// other inbound, outbound and route rule are copied through unchanged.
// A TUN interface_name that already belongs to an unmanaged inbound is refused
// so a hand-written config, including one that already uses sbtun, is not overwritten.
nlohmann::json merge_singbox_config(const nlohmann::json& existing, const std::vector<SingboxProfile>& live,
                                    const std::vector<SingboxProfile>& drop_sources, bool* remove_file) {
    DropTags tags;
    for (const auto& profile : drop_sources) collect_tags(tags, profile);
    for (const auto& profile : live) collect_tags(tags, profile);

    nlohmann::json kept_inbounds = nlohmann::json::array();
    if (existing.contains("inbounds") && existing["inbounds"].is_array()) {
        for (const auto& inbound : existing["inbounds"]) {
            if (!inbound.is_object() || !is_dropped_inbound(inbound.value("tag", ""), tags)) {
                kept_inbounds.push_back(inbound);
            }
        }
    }
    for (const auto& profile : live) {
        if (profile.connections.empty()) continue;
        for (const auto& inbound : kept_inbounds) {
            if (!inbound.is_object()) continue;
            if (inbound.value("interface_name", "") == profile.interface_name) {
                throw std::runtime_error(interface_taken_message(profile.interface_name));
            }
        }
    }

    nlohmann::json kept_outbounds = nlohmann::json::array();
    if (existing.contains("outbounds") && existing["outbounds"].is_array()) {
        for (const auto& outbound : existing["outbounds"]) {
            if (!outbound.is_object() || !is_dropped_outbound(outbound.value("tag", ""), tags)) {
                kept_outbounds.push_back(outbound);
            }
        }
    }
    nlohmann::json kept_rules = nlohmann::json::array();
    nlohmann::json route = existing.contains("route") && existing["route"].is_object() ? existing["route"]
                                                                                        : nlohmann::json::object();
    if (route.contains("rules") && route["rules"].is_array()) {
        for (const auto& rule : route["rules"]) {
            if (!rule.is_object()) {
                kept_rules.push_back(rule);
                continue;
            }
            const auto inbound = rule.value("inbound", "");
            const auto outbound = rule.value("outbound", "");
            if (is_dropped_inbound(inbound, tags) || is_dropped_outbound(outbound, tags)) continue;
            kept_rules.push_back(rule);
        }
    }

    nlohmann::json managed_inbounds = nlohmann::json::array();
    nlohmann::json managed_outbounds = nlohmann::json::array();
    std::set<int> used_octets;
    for (const auto& inbound : kept_inbounds) {
        if (inbound.is_object() && inbound.contains("address")) note_tun_address(used_octets, inbound["address"]);
    }
    for (const auto& profile : live) {
        for (const auto& cidr : split_addresses(profile.tun_address)) {
            const int octet = tun_subnet_octet(cidr);
            if (octet > 0) used_octets.insert(octet);
        }
    }
    bool live_nodes = false;
    for (const auto& profile : live) {
        if (profile.connections.empty()) continue;
        live_nodes = true;
        std::vector<std::string> node_tags;
        for (const auto& connection : profile.connections) {
            const auto tag = node_tag(connection);
            managed_outbounds.push_back(outbound_for(connection, tag));
            node_tags.push_back(tag);
        }
        const auto group = group_tag(profile);
        std::string selected = group;
        if (profile.mode == "direct") {
            const auto* active = find_active(profile);
            selected = active == nullptr ? node_tags.front() : node_tag(*active);
        } else if (profile.mode == "urltest") {
            const auto interval = profile.urltest_interval.empty() ? "3m" : profile.urltest_interval;
            const auto url = profile.urltest_url.empty() ? "https://www.gstatic.com/generate_204" : profile.urltest_url;
            managed_outbounds.push_back({
                {"type", "urltest"},
                {"tag", group},
                {"outbounds", node_tags},
                {"url", url},
                {"interval", interval},
            });
        } else {
            nlohmann::json selector{{"type", "selector"}, {"tag", group}, {"outbounds", node_tags}};
            const auto* active = find_active(profile);
            if (active != nullptr) selector["default"] = node_tag(*active);
            managed_outbounds.push_back(std::move(selector));
        }
        const auto inbound = inbound_tag(profile);
        kept_rules.push_back({{"inbound", inbound}, {"outbound", selected}});
        auto addresses = split_addresses(profile.tun_address);
        nlohmann::json address_json = nlohmann::json::array();
        if (addresses.empty()) {
            const int octet = next_free_tun_octet(used_octets);
            address_json.push_back("172.19." + std::to_string(octet) + ".1/30");
        } else {
            for (const auto& cidr : addresses) address_json.push_back(cidr);
        }
        const int mtu = profile.tun_mtu > 0 ? profile.tun_mtu : 1400;
        managed_inbounds.push_back({
            {"type", "tun"},
            {"tag", inbound},
            {"interface_name", profile.interface_name},
            {"address", std::move(address_json)},
            {"mtu", mtu},
            {"auto_route", false},
            {"strict_route", false},
            {"stack", profile.tun_stack == "system" ? "system" : "gvisor"},
        });
    }

    if (remove_file != nullptr) *remove_file = !live_nodes && !unmanaged_remains(existing, tags);

    nlohmann::json result = existing.is_object() ? existing : nlohmann::json::object();
    nlohmann::json inbounds = kept_inbounds;
    for (auto& inbound : managed_inbounds) inbounds.push_back(std::move(inbound));
    nlohmann::json outbounds = kept_outbounds;
    for (auto& outbound : managed_outbounds) outbounds.push_back(std::move(outbound));
    if (route.contains("final") && route["final"].is_string() &&
        is_dropped_outbound(route["final"].get<std::string>(), tags)) {
        route.erase("final");
    }
    if (live_nodes && kept_outbounds.empty() && !route.contains("final")) {
        // kpbr- prefix so this fallback is removed together with the managed TUN.
        outbounds.push_back({{"type", "direct"}, {"tag", "kpbr-direct"}});
        route["final"] = "kpbr-direct";
    }
    if (live_nodes && !route.contains("auto_detect_interface")) route["auto_detect_interface"] = true;
    route["rules"] = std::move(kept_rules);
    if (!inbounds.empty()) result["inbounds"] = std::move(inbounds);
    else result.erase("inbounds");
    if (!outbounds.empty()) result["outbounds"] = std::move(outbounds);
    else result.erase("outbounds");
    if ((route.contains("rules") && route["rules"].is_array() && !route["rules"].empty()) || route.contains("final") ||
        route.contains("auto_detect_interface")) {
        result["route"] = std::move(route);
    } else {
        result.erase("route");
    }
    if (live_nodes && !result.contains("log")) result["log"] = {{"level", "warn"}, {"timestamp", true}};
    return result;
}

std::vector<std::string> split_links(const std::vector<std::string>& links) {
    std::vector<std::string> lines;
    for (const auto& chunk : links) {
        std::size_t pos = 0;
        while (pos < chunk.size()) {
            const auto end = chunk.find('\n', pos);
            auto line = trim(chunk.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
            if (!line.empty()) lines.push_back(std::move(line));
            if (end == std::string::npos) break;
            pos = end + 1;
        }
    }
    return lines;
}

std::string make_subscription_id(const SingboxProfile& profile, const std::string& url) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : url) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    std::ostringstream out;
    out << std::hex << hash;
    std::string id = "s" + out.str().substr(0, 8);
    for (int suffix = 0; suffix < 100; ++suffix) {
        const auto candidate = suffix == 0 ? id : id + std::to_string(suffix);
        const bool used = std::any_of(profile.subscriptions.begin(), profile.subscriptions.end(),
                                      [&](const auto& subscription) { return subscription.id == candidate; });
        if (!used) return candidate;
    }
    throw std::runtime_error("could not allocate a subscription id");
}

void apply_draft(SingboxProfile& profile, const SingboxProfileDraft& draft, bool create) {
    const auto mode = draft.mode.empty() ? (create ? "selector" : profile.mode) : draft.mode;
    require_mode(mode);
    const auto tag_source = !draft.keenetic_tag.empty() ? draft.keenetic_tag : draft.name;
    if (create && tag_source.empty()) throw std::runtime_error("keen-pbr tag is required");
    const auto iface = draft.interface_name.empty() ? (create ? std::string{} : profile.interface_name)
                                                     : draft.interface_name;
    if (!iface.empty() && !safe_interface_name(iface)) throw std::runtime_error("interface name is invalid");
    const auto stack = draft.tun_stack.empty() ? (create ? "gvisor" : profile.tun_stack) : draft.tun_stack;
    require_stack(stack);
    const int mtu = draft.tun_mtu == 0 ? (create ? 1400 : profile.tun_mtu) : draft.tun_mtu;
    require_mtu(mtu);
    const auto interval = draft.urltest_interval.empty() ? (create ? "3m" : profile.urltest_interval)
                                                          : draft.urltest_interval;
    require_duration(interval);
    require_addresses(draft.tun_address);
    if (!tag_source.empty()) profile.keenetic_tag = sanitize_keenetic_tag(tag_source);
    if (create) profile.id = profile.keenetic_tag;
    if (!draft.name.empty()) profile.name = draft.name;
    else if (create) profile.name = profile.keenetic_tag;
    profile.interface_name = iface;
    profile.mode = mode;
    profile.tun_stack = stack;
    profile.tun_mtu = mtu;
    profile.tun_address = draft.tun_address;
    profile.urltest_interval = interval.empty() ? "3m" : interval;
    if (!draft.urltest_url.empty()) profile.urltest_url = draft.urltest_url;
    else if (create) profile.urltest_url = "https://www.gstatic.com/generate_204";
}

void assert_interfaces_available(const nlohmann::json& existing, const std::vector<SingboxProfile>& profiles) {
    if (!existing.contains("inbounds") || !existing["inbounds"].is_array()) return;
    for (const auto& inbound : existing["inbounds"]) {
        if (!inbound.is_object()) continue;
        const auto iface = inbound.value("interface_name", "");
        if (iface.empty()) continue;
        const auto tag = inbound.value("tag", "");
        for (const auto& profile : profiles) {
            if (profile.interface_name != iface) continue;
            if (tag == inbound_tag(profile) || tag == "tun_" + profile.id) continue;
            throw std::runtime_error(interface_taken_message(iface));
        }
    }
}

SingboxProfile* find_profile(std::vector<SingboxProfile>& profiles, const std::string& id) {
    for (auto& profile : profiles) {
        if (profile.id == id) return &profile;
    }
    return nullptr;
}

}  // namespace

std::string format_singbox_check_error(int exit_code, bool timed_out, const std::string& output) {
    if (timed_out) return "sing-box check failed: timed out";
    if (exit_code == 127 || exit_code == -1) return "sing-box binary is not available";
    return "sing-box check failed: " + redact_secrets(prominent_line(output));
}

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

nlohmann::json SingboxLinkService::render_singbox_config(const std::vector<SingboxProfile>& profiles) {
    bool remove_file = false;
    return merge_singbox_config(nlohmann::json::object(), profiles, profiles, &remove_file);
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

SingboxLinkService::SingboxLinkService(SingboxPaths paths, SingboxCheck check, KeeneticOutboundFn keenetic,
                                       SingboxReloadFn reload, SubscriptionDownloadFn download,
                                       SubscriptionClock clock, UrltestNowFn urltest_now)
    : paths_(std::move(paths)), check_(std::move(check)), keenetic_(std::move(keenetic)),
      reload_(std::move(reload)), download_(std::move(download)),
      clock_(clock ? std::move(clock) : now_unix), urltest_now_(std::move(urltest_now)) {}

std::vector<SingboxProfile> SingboxLinkService::read() const {
    try {
        return load_profiles(paths_.store);
    } catch (const std::exception&) {
        throw std::runtime_error("could not read the sing-box subscription store");
    }
}

nlohmann::json SingboxLinkService::read_config_object() const {
    const auto body = read_file(paths_.singbox_config);
    if (blank(body)) return nlohmann::json::object();
    try {
        auto json = nlohmann::json::parse(body);
        if (!json.is_object()) {
            throw std::runtime_error("existing sing-box config is not a JSON object; it was not modified");
        }
        return json;
    } catch (const std::runtime_error&) {
        throw;
    } catch (const std::exception&) {
        throw std::runtime_error("existing sing-box config is not valid JSON; it was not modified");
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
    if (status.last_refresh == 0 && status.retry_at == 0 && status.error.empty()) refresh_status_.erase(id);
    else refresh_status_[id] = std::move(status);
}

void SingboxLinkService::clear_profile_status(const std::string& profile_id) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    for (auto it = refresh_status_.begin(); it != refresh_status_.end();) {
        const auto& key = it->first;
        if (key == profile_id || key.rfind(profile_id + ":", 0) == 0) it = refresh_status_.erase(it);
        else ++it;
    }
}

std::string SingboxLinkService::status_key(const SingboxProfile& profile, const SingboxSubscription& subscription) {
    if (profile.subscriptions.size() <= 1) return profile.id;
    return profile.id + ":" + subscription.id;
}

SingboxLinkService::RefreshStatus SingboxLinkService::refresh_status(const SingboxProfile& profile,
                                                                     const SingboxSubscription& subscription) const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    const auto it = refresh_status_.find(status_key(profile, subscription));
    if (it == refresh_status_.end()) return RefreshStatus{subscription.last_refresh, 0, subscription.last_error};
    return it->second;
}

std::int64_t SingboxLinkService::next_refresh(const SingboxSubscription& subscription, const RefreshStatus& status) {
    if (subscription.url.empty() || subscription.interval_hours <= 0) return 0;
    if (status.retry_at != 0) return status.retry_at;
    if (status.last_refresh <= 0) return 0;
    return status.last_refresh + static_cast<std::int64_t>(subscription.interval_hours) * 3600;
}

nlohmann::json SingboxLinkService::state() const {
    const auto profiles = read();
    nlohmann::json existing = nlohmann::json::object();
    try {
        existing = read_config_object();
    } catch (const std::exception&) {
        existing = nlohmann::json::object();
    }
    nlohmann::json array = nlohmann::json::array();
    for (const auto& profile : profiles) {
        auto json = profile_json(profile, false);
        if (json.contains("subscriptions") && json["subscriptions"].is_array()) {
            for (std::size_t i = 0; i < profile.subscriptions.size() && i < json["subscriptions"].size(); ++i) {
                const auto& subscription = profile.subscriptions[i];
                const auto status = refresh_status(profile, subscription);
                const auto next = next_refresh(subscription, status);
                json["subscriptions"][i]["last_refresh"] = status.last_refresh;
                json["subscriptions"][i]["last_error"] = status.error;
                json["subscriptions"][i]["next_refresh"] =
                    subscription.interval_hours <= 0 ? nlohmann::json(nullptr)
                                                     : nlohmann::json(next == 0 ? clock_() : next);
                if (i == 0) {
                    json["last_refresh"] = status.last_refresh;
                    json["last_error"] = status.error;
                    json["interval_hours"] = subscription.interval_hours;
                    json["next_refresh"] = json["subscriptions"][i]["next_refresh"];
                }
            }
        }
        if (profile.mode == "urltest" && urltest_now_) {
            try {
                const auto selected = urltest_now_(group_tag(profile));
                for (const auto& connection : profile.connections) {
                    if (!selected.empty() && node_tag(connection) == selected) json["urltest_now"] = connection.id;
                }
            } catch (const std::exception&) {
                json.erase("urltest_now");
            }
        }
        array.push_back(std::move(json));
    }
    return {{"profiles", std::move(array)},
            {"suggested_interface", next_free_interface(existing, profiles, "")}};
}

std::map<std::string, std::string> SingboxLinkService::subscription_errors() const {
    std::map<std::string, std::string> errors;
    std::lock_guard<std::mutex> lock(status_mutex_);
    for (const auto& entry : refresh_status_) {
        if (!entry.second.error.empty()) errors.emplace(entry.first, entry.second.error);
    }
    return errors;
}

SingboxLinkService::ConfigWrite SingboxLinkService::write_checked_config(const nlohmann::json& config) {
    preserve_original_config(paths_.singbox_config);
    const bool had_config = backup_file(paths_.singbox_config);
    write_private(paths_.singbox_config, config.dump(2));
    try {
        std::string check_error;
        const int code = check_ ? check_(paths_.singbox_config, &check_error) : 0;
        if (code != 0) {
            const bool timed_out = check_error == "timed out";
            throw std::runtime_error(format_singbox_check_error(code, timed_out, check_error));
        }
    } catch (...) {
        restore_backup(paths_.singbox_config, had_config);
        throw;
    }
    return ConfigWrite{had_config, false};
}

SingboxApplyResult SingboxLinkService::commit(std::vector<SingboxProfile> profiles, const std::string& changed_id,
                                              const std::string& previous_tag) {
    const SingboxProfile* changed = nullptr;
    for (const auto& profile : profiles) {
        if (profile.id == changed_id) changed = &profile;
    }
    if (changed == nullptr) throw std::runtime_error("sing-box profile disappeared");
    for (auto& profile : profiles) mirror_subscription(profile);
    const auto previous_profiles = read();
    const auto existing = read_config_object();
    auto known = previous_profiles;
    for (const auto& profile : profiles) known.push_back(profile);
    bool remove_file = false;
    const auto rendered = merge_singbox_config(existing, profiles, known, &remove_file);
    backup_file(paths_.store);
    ConfigWrite written;
    if (remove_file) {
        preserve_original_config(paths_.singbox_config);
        written.had_config = backup_file(paths_.singbox_config);
        if (::unlink(paths_.singbox_config.c_str()) != 0 && errno != ENOENT) {
            throw std::runtime_error("cannot remove sing-box config");
        }
        written.removed = true;
    } else {
        written = write_checked_config(rendered);
    }
    try {
        write_store(paths_.store, profiles);
        if (keenetic_ && !previous_tag.empty() && previous_tag != changed->keenetic_tag) {
            SingboxProfile ghost = *changed;
            ghost.keenetic_tag = previous_tag;
            keenetic_(ghost, false);
        }
        if (keenetic_) keenetic_(*changed, true);
    } catch (const std::exception&) {
        write_store(paths_.store, previous_profiles);
        restore_backup(paths_.singbox_config, written.had_config);
        throw;
    }
    SingboxApplyResult result;
    result.keenetic_applied = true;
    if (written.removed) {
        result.warning = "removed the generated sing-box config because no nodes remain";
    } else {
        result.singbox_checked = true;
        try {
            result.singbox_reloaded = reload_ && reload_();
            if (!result.singbox_reloaded) {
                result.warning = "sing-box config passed check; restart sing-box on the router to load it";
            }
        } catch (const std::exception& error) {
            result.warning = std::string("sing-box config is valid but was not restarted: ") + error.what();
        }
    }
    result.state = public_state(profiles);
    return result;
}

SingboxApplyResult SingboxLinkService::save_profile(const SingboxProfileDraft& draft) {
    const auto lock = begin_mutation();
    auto profiles = read();
    std::string previous_tag;
    SingboxProfile* profile = nullptr;
    if (draft.id.empty()) {
        SingboxProfile created;
        apply_draft(created, draft, true);
        if (find_profile(profiles, created.id) != nullptr) throw std::runtime_error("keen-pbr tag is already used");
        profiles.push_back(std::move(created));
        profile = &profiles.back();
    } else {
        profile = find_profile(profiles, draft.id);
        if (profile == nullptr) throw std::runtime_error("sing-box profile was not found");
        previous_tag = profile->keenetic_tag;
        apply_draft(*profile, draft, false);
    }
    const auto existing = read_config_object();
    if (profile->interface_name.empty()) {
        profile->interface_name = next_free_interface(existing, profiles, profile->id);
    }
    if (!safe_interface_name(profile->interface_name)) throw std::runtime_error("interface name is invalid");
    validate_unique(profiles);
    assert_interfaces_available(existing, profiles);
    const auto id = profile->id;
    const bool has_nodes = !profile->connections.empty();
    if (has_nodes) return commit(std::move(profiles), id, previous_tag);

    const auto previous = read();
    for (auto& item : profiles) mirror_subscription(item);
    write_store(paths_.store, profiles);
    try {
        if (keenetic_ && !previous_tag.empty() && previous_tag != profile->keenetic_tag) {
            SingboxProfile ghost = *profile;
            ghost.keenetic_tag = previous_tag;
            keenetic_(ghost, false);
        }
        if (keenetic_) keenetic_(*profile, true);
    } catch (const std::exception&) {
        write_store(paths_.store, previous);
        throw;
    }
    SingboxApplyResult result;
    result.keenetic_applied = true;
    result.state = public_state(profiles);
    return result;
}

SingboxApplyResult SingboxLinkService::remove_profile(const std::string& profile_id) {
    const auto lock = begin_mutation();
    auto profiles = read();
    SingboxProfile removed;
    std::vector<SingboxProfile> remaining;
    bool found = false;
    for (auto& profile : profiles) {
        if (profile.id == profile_id) {
            removed = profile;
            found = true;
        } else {
            remaining.push_back(std::move(profile));
        }
    }
    if (!found) throw std::runtime_error("sing-box profile was not found");
    clear_profile_status(profile_id);
    if (removed.connections.empty()) {
        write_store(paths_.store, remaining);
        if (keenetic_) keenetic_(removed, false);
        SingboxApplyResult result;
        result.keenetic_applied = true;
        result.state = public_state(remaining);
        return result;
    }
    for (auto& profile : remaining) mirror_subscription(profile);
    const auto existing = read_config_object();
    bool remove_file = false;
    const auto rendered = merge_singbox_config(existing, remaining, profiles, &remove_file);
    backup_file(paths_.store);
    ConfigWrite written;
    if (remove_file) {
        preserve_original_config(paths_.singbox_config);
        written.had_config = backup_file(paths_.singbox_config);
        if (::unlink(paths_.singbox_config.c_str()) != 0 && errno != ENOENT) {
            throw std::runtime_error("cannot remove sing-box config");
        }
        written.removed = true;
    } else {
        written = write_checked_config(rendered);
    }
    try {
        write_store(paths_.store, remaining);
        if (keenetic_) keenetic_(removed, false);
    } catch (const std::exception&) {
        write_store(paths_.store, profiles);
        restore_backup(paths_.singbox_config, written.had_config);
        throw;
    }
    SingboxApplyResult result;
    result.keenetic_applied = true;
    result.singbox_checked = !written.removed;
    if (!written.removed) {
        try {
            result.singbox_reloaded = reload_ && reload_();
        } catch (const std::exception& error) {
            result.warning = std::string("sing-box config is valid but was not restarted: ") + error.what();
        }
    }
    result.state = public_state(remaining);
    return result;
}

SingboxApplyResult SingboxLinkService::add_connections(const std::string& profile_id,
                                                       const std::vector<std::string>& links,
                                                       const std::string& connection_name) {
    const auto lock = begin_mutation();
    if (profile_id.empty()) throw std::runtime_error("profile id is required");
    auto profiles = read();
    auto* profile = find_profile(profiles, profile_id);
    if (profile == nullptr) throw std::runtime_error("sing-box profile was not found");
    SubscriptionParseInfo info;
    std::size_t added = 0;
    std::string last_id;
    const bool name_one = !connection_name.empty();
    std::size_t accepted = 0;
    for (const auto& line : split_links(links)) {
        try {
            auto parsed = parse_proxy_link(line);
            for (const auto& warning : parsed.warnings) {
                if (warning.find("encryption") != std::string::npos) ++info.encryption_ignored;
            }
            SingboxConnection connection;
            connection.id = proxy_node_id(parsed);
            connection.name = (name_one && accepted == 0) ? connection_name : parsed.name;
            connection.protocol = parsed.protocol;
            connection.server = parsed.server;
            connection.port = parsed.port;
            connection.link = line;
            connection.source = "manual";
            bool replaced = false;
            for (auto& existing : profile->connections) {
                if (existing.id == connection.id) {
                    connection.source = existing.source;
                    connection.subscription_id = existing.subscription_id;
                    existing = connection;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) profile->connections.push_back(connection);
            last_id = connection.id;
            ++added;
            ++accepted;
        } catch (const UnsupportedProxyTransport& error) {
            ++info.skipped;
            ++info.unsupported_transports[error.transport()];
        } catch (const std::runtime_error&) {
            ++info.skipped;
        }
    }
    if (added == 0) {
        auto message = subscription_warning(info);
        if (message.empty()) message = "proxy link could not be parsed";
        throw std::runtime_error(message);
    }
    if (profile->active.empty() || profile->mode == "direct") profile->active = last_id;
    auto result = commit(std::move(profiles), profile_id, {});
    attach_skips(result, info);
    return result;
}

SingboxApplyResult SingboxLinkService::remove_connection(const std::string& profile_id,
                                                         const std::string& connection_id) {
    const auto lock = begin_mutation();
    auto profiles = read();
    auto* profile = find_profile(profiles, profile_id);
    if (profile == nullptr) throw std::runtime_error("sing-box profile was not found");
    std::vector<SingboxConnection> kept;
    bool found = false;
    for (auto& connection : profile->connections) {
        if (connection.id == connection_id) found = true;
        else kept.push_back(std::move(connection));
    }
    if (!found) throw std::runtime_error("sing-box connection was not found");
    profile->connections = std::move(kept);
    if (profile->active == connection_id) {
        profile->active = profile->connections.empty() ? "" : profile->connections.front().id;
    }
    return commit(std::move(profiles), profile_id, {});
}

SingboxApplyResult SingboxLinkService::set_mode(const std::string& profile_id, const std::string& mode,
                                                const std::string& active) {
    const auto lock = begin_mutation();
    require_mode(mode);
    auto profiles = read();
    auto* profile = find_profile(profiles, profile_id);
    if (profile == nullptr) throw std::runtime_error("sing-box profile was not found");
    profile->mode = mode;
    if (!active.empty()) {
        if (std::none_of(profile->connections.begin(), profile->connections.end(),
                         [&](const auto& node) { return node.id == active; })) {
            throw std::runtime_error("sing-box connection was not found");
        }
        profile->active = active;
    }
    if (profile->connections.empty()) {
        for (auto& item : profiles) mirror_subscription(item);
        write_store(paths_.store, profiles);
        SingboxApplyResult result;
        result.keenetic_applied = true;
        result.state = public_state(profiles);
        return result;
    }
    return commit(std::move(profiles), profile_id, {});
}

SingboxApplyResult SingboxLinkService::save_subscription(const std::string& profile_id, const std::string& url,
                                                         int interval_hours, const std::string& subscription_id) {
    const auto lock = begin_mutation();
    if (profile_id.empty()) throw std::runtime_error("profile id is required");
    if (url.find("://") == std::string::npos) throw std::runtime_error("subscription URL is invalid");
    std::string body;
    try {
        body = download_(url, {});
    } catch (const std::exception&) {
        throw std::runtime_error("subscription download failed");
    }
    SubscriptionParseInfo info;
    std::vector<SingboxConnection> nodes;
    try {
        nodes = subscription_connections(body, info);
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(error.what());
    }
    auto profiles = read();
    auto* profile = find_profile(profiles, profile_id);
    if (profile == nullptr) throw std::runtime_error("sing-box profile was not found");
    SingboxSubscription* subscription = nullptr;
    if (!subscription_id.empty()) {
        for (auto& item : profile->subscriptions) {
            if (item.id == subscription_id) subscription = &item;
        }
        if (subscription == nullptr) throw std::runtime_error("subscription was not found");
    } else {
        profile->subscriptions.push_back(SingboxSubscription{});
        subscription = &profile->subscriptions.back();
        subscription->id = make_subscription_id(*profile, url);
    }
    const auto id = subscription->id;
    subscription->url = url;
    subscription->interval_hours = interval_hours < 0 ? 0 : interval_hours;
    subscription->last_refresh = clock_();
    subscription->last_error = subscription_warning(info);
    for (auto& node : nodes) {
        node.subscription_id = id;
        node.source = "subscription";
    }
    std::vector<SingboxConnection> kept;
    for (auto& connection : profile->connections) {
        if (connection.subscription_id != id) kept.push_back(std::move(connection));
    }
    for (auto& node : nodes) kept.push_back(std::move(node));
    profile->connections = std::move(kept);
    bool active_ok = false;
    for (const auto& connection : profile->connections) {
        if (connection.id == profile->active) active_ok = true;
    }
    if (!active_ok && !profile->connections.empty()) profile->active = profile->connections.front().id;
    mirror_subscription(*profile);
    auto result = commit(std::move(profiles), profile_id, {});
    clear_profile_status(profile_id);
    attach_skips(result, info);
    return result;
}

SingboxApplyResult SingboxLinkService::remove_subscription(const std::string& profile_id,
                                                           const std::string& subscription_id) {
    const auto lock = begin_mutation();
    auto profiles = read();
    auto* profile = find_profile(profiles, profile_id);
    if (profile == nullptr) throw std::runtime_error("sing-box profile was not found");
    std::vector<SingboxSubscription> subscriptions;
    bool found = false;
    for (auto& subscription : profile->subscriptions) {
        if (subscription.id == subscription_id) found = true;
        else subscriptions.push_back(std::move(subscription));
    }
    if (!found) throw std::runtime_error("subscription was not found");
    profile->subscriptions = std::move(subscriptions);
    std::vector<SingboxConnection> kept;
    bool removed_nodes = false;
    for (auto& connection : profile->connections) {
        if (connection.subscription_id == subscription_id) removed_nodes = true;
        else kept.push_back(std::move(connection));
    }
    profile->connections = std::move(kept);
    if (std::none_of(profile->connections.begin(), profile->connections.end(),
                     [&](const auto& connection) { return connection.id == profile->active; })) {
        profile->active = profile->connections.empty() ? "" : profile->connections.front().id;
    }
    mirror_subscription(*profile);
    clear_profile_status(profile_id);
    if (!removed_nodes) {
        for (auto& item : profiles) mirror_subscription(item);
        write_store(paths_.store, profiles);
        SingboxApplyResult result;
        result.keenetic_applied = true;
        result.state = public_state(profiles);
        return result;
    }
    return commit(std::move(profiles), profile_id, {});
}

void SingboxLinkService::refresh_one(std::vector<SingboxProfile>& profiles, std::size_t profile_index,
                                     std::size_t subscription_index, const SubscriptionStopFn& stopping) {
    auto& profile = profiles[profile_index];
    auto& subscription = profile.subscriptions[subscription_index];
    auto status = refresh_status(profile, subscription);
    const auto key = status_key(profile, subscription);
    const char* failure = "subscription download failed";
    try {
        SubscriptionParseInfo info;
        auto fresh = [&] {
            const auto body = download_(subscription.url, stopping);
            if (stopping && stopping()) return std::vector<SingboxConnection>{};
            failure = "subscription did not contain usable proxy data; keeping the previous configuration";
            return subscription_connections(body, info);
        }();
        if (stopping && stopping()) return;
        for (auto& node : fresh) {
            node.subscription_id = subscription.id;
            node.source = "subscription";
        }
        std::vector<SingboxConnection> merged;
        for (const auto& node : profile.connections) {
            if (node.subscription_id != subscription.id) merged.push_back(node);
        }
        for (auto& node : fresh) merged.push_back(std::move(node));
        const auto active = std::find_if(merged.begin(), merged.end(), [&](const auto& node) {
            return node.id == profile.active;
        });
        if (!profile.active.empty() && active == merged.end()) {
            failure = "selected node disappeared from subscription; keeping the previous configuration";
            throw std::runtime_error(failure);
        }
        const auto refreshed = clock_();
        auto warning = subscription_warning(info);
        if (!same_nodes(profile.connections, merged, subscription.id)) {
            const auto previous_active = profile.active;
            const auto previous_refresh = subscription.last_refresh;
            auto previous_error = subscription.last_error;
            auto previous_connections = profile.connections;
            if (profile.active.empty() && !merged.empty()) profile.active = merged.front().id;
            profile.connections.swap(merged);
            subscription.last_refresh = refreshed;
            subscription.last_error = warning;
            mirror_subscription(profile);
            auto known = profiles;
            known[profile_index].connections = previous_connections;
            try {
                failure = "could not apply subscription: sing-box config write or check failed";
                const auto existing = read_config_object();
                bool remove_file = false;
                const auto rendered = merge_singbox_config(existing, profiles, known, &remove_file);
                const bool had_config = write_checked_config(rendered).had_config;
                try {
                    failure = "could not restart sing-box; restoring the previous configuration";
                    if (!reload_ || !reload_()) throw std::runtime_error(failure);
                    failure = "could not save subscription; restoring the previous configuration";
                    for (auto& item : profiles) mirror_subscription(item);
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
                profile.connections.swap(merged);
                profile.connections = std::move(previous_connections);
                profile.active = previous_active;
                subscription.last_refresh = previous_refresh;
                subscription.last_error = std::move(previous_error);
                mirror_subscription(profile);
                throw;
            }
        }
        update_refresh_status(key, {refreshed, 0, std::move(warning)});
    } catch (const std::exception&) {
        if (stopping && stopping()) return;
        update_refresh_status(key, {status.last_refresh, clock_() + kSubscriptionRetrySeconds, failure});
    }
}

nlohmann::json SingboxLinkService::refresh_subscriptions(const std::string& profile_id, bool due_only,
                                                        const SubscriptionStopFn& stopping,
                                                        const std::string& subscription_id) {
    const auto lock = begin_mutation();
    auto profiles = read();
    nlohmann::json results = nlohmann::json::array();
    bool matched_profile = profile_id.empty();
    for (std::size_t i = 0; i < profiles.size(); ++i) {
        if (stopping && stopping()) break;
        auto& profile = profiles[i];
        if (!profile_id.empty() && profile.id != profile_id) continue;
        if (!profile_id.empty()) matched_profile = true;
        bool matched_subscription = subscription_id.empty();
        for (std::size_t s = 0; s < profile.subscriptions.size(); ++s) {
            if (stopping && stopping()) break;
            const auto& subscription = profile.subscriptions[s];
            if (!subscription_id.empty() && subscription.id != subscription_id) continue;
            matched_subscription = true;
            if (subscription.url.empty()) continue;
            const auto status = refresh_status(profile, subscription);
            if (due_only && (subscription.interval_hours <= 0 || next_refresh(subscription, status) > clock_())) {
                continue;
            }
            refresh_one(profiles, i, s, stopping);
            if (stopping && stopping()) break;
            const auto updated = refresh_status(profile, subscription);
            results.push_back({{"id", profile.id},
                               {"subscription_id", subscription.id},
                               {"ok", updated.retry_at == 0},
                               {"message", updated.error}});
        }
        if (!subscription_id.empty() && !matched_subscription && !(stopping && stopping())) {
            throw std::runtime_error("subscription was not found");
        }
    }
    if (!profile_id.empty() && !matched_profile && !(stopping && stopping())) {
        throw std::runtime_error("subscription was not found");
    }
    if (!profile_id.empty() && results.empty() && subscription_id.empty() && !(stopping && stopping())) {
        bool has_url = false;
        for (const auto& profile : profiles) {
            if (profile.id != profile_id) continue;
            for (const auto& subscription : profile.subscriptions) has_url = has_url || !subscription.url.empty();
        }
        if (!has_url) throw std::runtime_error("subscription was not found");
    }
    return results;
}

}  // namespace keen_pbr3
