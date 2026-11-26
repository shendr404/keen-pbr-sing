#pragma once

#include "link_store.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace keen_pbr3 {

// First free name is "xrtun", then "xrtun2", "xrtun3", ... sing-box keeps "sbtun".
const char* tun_prefix_for_core(const std::string& core);
// Empty configured path searches the usual Entware locations, then "xray" on PATH.
std::string resolve_xray_binary(const std::string& configured);

// True when text contains a version >= major.minor.patch. "Xray 26.10.29 (...)" is accepted.
bool xray_version_at_least(const std::string& text, int major, int minor, int patch);

// MemTotal in KiB. 0 when the line is absent. Warning only, never a hard stop.
long ram_total_kib(const std::string& meminfo);
bool ram_below_xray_comfort(long kib);

// VLESS only. Other protocols are unsupported and name Xray in the reason.
// The outbound JSON is an Xray outbound without a tag.
struct XrayLinkView {
    bool supported{false};
    std::string reason;
    std::string protocol;
    std::string name;
    std::string server;
    int port{0};
    nlohmann::json outbound;
};

XrayLinkView classify_link_for_core(const std::string& link, const std::string& core);

// One TUN inbound per supported node. autoSystemRoutingTable stays off.
nlohmann::json render_xray_config(const std::vector<SingboxNode>& nodes);

}  // namespace keen_pbr3
