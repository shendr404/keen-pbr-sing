#pragma once

#include <nlohmann/json.hpp>

#include <map>
#include <stdexcept>
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
    // Non-fatal notes, such as a vless encryption value that was ignored.
    std::vector<std::string> warnings;
};

// sing-box 1.14 accepts tcp, ws, grpc, http, httpupgrade and quic.
// xhttp, splithttp and anything else are rejected by `sing-box check`.
class UnsupportedProxyTransport : public std::runtime_error {
public:
    explicit UnsupportedProxyTransport(std::string transport)
        : std::runtime_error("transport " + transport + " is not supported by sing-box"),
          transport_(std::move(transport)) {}

    const std::string& transport() const noexcept { return transport_; }

private:
    std::string transport_;
};

struct SubscriptionParseInfo {
    std::size_t skipped{0};
    // Transport name -> number of nodes skipped because sing-box cannot use it.
    std::map<std::string, std::size_t> unsupported_transports;
    std::size_t encryption_ignored{0};
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
                                                 SubscriptionParseInfo* info);

std::string proxy_node_id(const ParsedProxyLink& link);

// Public fields only: protocol, name, server, port.
nlohmann::json public_proxy_fields(const ParsedProxyLink& link);

}  // namespace keen_pbr3
