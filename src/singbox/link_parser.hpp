#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace keen_pbr3 {

struct ParsedProxyLink {
    std::string protocol;
    std::string name;
    std::string server;
    int port{0};
    // Full sing-box 1.14 outbound, including credentials. Never log this.
    nlohmann::json outbound;
};

struct SubscriptionNode {
    ParsedProxyLink link;
    std::string id;
};

// Throws std::runtime_error with a message that does not contain the secret.
ParsedProxyLink parse_proxy_link(const std::string& link);

// base64 list, plain list, sing-box JSON, or Clash JSON. Unsupported lines are
// counted and skipped. Throws if nothing usable is found.
std::vector<SubscriptionNode> parse_subscription(const std::string& body,
                                                 std::size_t* skipped);

std::string proxy_node_id(const ParsedProxyLink& link);

// Public fields only: protocol, name, server, port.
nlohmann::json public_proxy_fields(const ParsedProxyLink& link);

}  // namespace keen_pbr3
