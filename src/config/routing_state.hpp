#pragma once

#include "../config/config.hpp"
#include "../routing/firewall_state.hpp"
#include "../routing/policy_rule.hpp"
#include "../routing/route_table.hpp"
#include "../routing/routing_reconciler.hpp"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace keen_pbr3 {

// Routing table IDs that must never be used for keen-pbr-sx outbound tables.
static constexpr uint32_t RESERVED_TABLE_UNSPEC         = 0;
static constexpr uint32_t RESERVED_TABLE_PRELOCAL       = 128;
static constexpr uint32_t RESERVED_TABLE_BLOCK_LOW      = 250;
static constexpr uint32_t RESERVED_TABLE_BLOCK_HIGH     = 260; // covers 253=default, 254=main, 255=local
static constexpr uint32_t RESERVED_TABLE_SYSTEM_START   = 32000;

inline bool is_reserved_table(uint32_t id) {
    return id == RESERVED_TABLE_UNSPEC ||
           id == RESERVED_TABLE_PRELOCAL ||
           (id >= RESERVED_TABLE_BLOCK_LOW && id <= RESERVED_TABLE_BLOCK_HIGH) ||
           id >= RESERVED_TABLE_SYSTEM_START;
}

using OutboundReachabilityFn = std::function<bool(const Outbound&)>;
using OutboundFamilyAvailabilityFn = std::function<bool(const Outbound&, int)>;

// Resolve an interface outbound's literal "auto" gateway from the main table.
// A missing, ambiguous, or multipath default route is unavailable.
struct InterfaceGatewayDiscovery {
    bool available{false};           // true also for a selected gatewayless route
    std::optional<std::string> gateway;
};

InterfaceGatewayDiscovery discover_interface_gateway(
    const Outbound& outbound,
    int family,
    const std::vector<DumpedRoute>& main_routes);

bool interface_outbound_uses_auto_gateway(const Outbound& outbound);

// tun_flags means a TUN (sing-box sbtun). point_to_point is IFF_POINTOPOINT
// (L2TP and many tunnels). A TUN is routed as `dev <iface>` with no gateway.
// A point-to-point interface is routed that way only when no gateway route
// can be found, so an L2TP peer that is present is still used.
struct InterfaceLinkFlags {
    bool tun{false};
    bool point_to_point{false};
};

InterfaceLinkFlags interface_link_flags(const std::string& iface);
// Tests substitute a probe so a missing /sys/class/net entry can stand in
// for sbtun. Pass an empty function to restore the real check.
void set_interface_link_probe_for_tests(
    const std::function<InterfaceLinkFlags(const std::string&)>& probe);

// True when this family should be installed as `default dev <iface>` and a
// configured or "auto" gateway must not make the outbound unavailable.
bool interface_uses_device_route(const Outbound& outbound,
                                 int family,
                                 const std::vector<DumpedRoute>* main_routes);

// Set when this outbound is routed as `dev <iface>` and a configured gateway
// (including "auto") is not used. Empty for an ordinary interface.
std::optional<std::string> point_to_point_gateway_note(
    const Outbound& outbound, const std::vector<DumpedRoute>& main_routes);

// Resolve every routable outbound to the table used by its own fwmark. Test
// groups map to their generated kill-switch table; their live lookup may point
// at a selected child's table instead.
std::map<std::string, uint32_t> build_outbound_table_map(const Config& cfg);

// The policy-rule namespace any configuration with the same fwmark and
// iproute settings can allocate from; throws ConfigError on an invalid fwmark.
RuleNamespace policy_rule_namespace(const Config& cfg);

// Populate route tables and policy rules from config. Works for real or dry-run instances.
void populate_routing_state(const Config& cfg,
                            const OutboundMarkMap& marks,
                            RouteTable& routes,
                            PolicyRuleManager& rules,
                            OutboundReachabilityFn reachability_check = {},
                            const std::map<std::string, std::string>* urltest_selections = nullptr,
                            bool ipv6_enabled = true,
                            OutboundFamilyAvailabilityFn family_available = {},
                            const std::vector<DumpedRoute>* main_routes = nullptr);

bool is_interface_outbound_reachable(const Outbound& outbound, NetlinkManager& netlink);
bool is_interface_outbound_reachable(const Outbound& outbound,
                                     const std::vector<DumpedRoute>& routes);
bool is_interface_outbound_family_reachable(const Outbound& outbound,
                                            int family,
                                            const std::vector<DumpedRoute>& routes);

// A link-local address only proves that IPv6 is enabled on the link; it does
// not make a gatewayless tunnel capable of carrying arbitrary IPv6 traffic.
bool interface_has_routed_ipv6(const DumpedInterface& interface);

// Build the realized firewall selector criteria for a route rule.
FirewallRuleCriteria build_firewall_rule_criteria(
    const RouteRule& rule,
    const std::vector<DumpedRoute>& main_routes = {},
    const std::vector<DumpedInterface>& interfaces = {});

// Infer the selected child of a URLTEST outbound from its live metric-zero
// default routes. Equivalent IPv4 and IPv6 routes for one child are accepted.
std::optional<std::string> infer_urltest_selection_from_routes(
    const std::vector<Outbound>& outbounds,
    const Outbound& urltest,
    const std::vector<DumpedRoute>& routes);

// Build firewall rule state (set names, actions, selectors) from config without
// touching firewall. Test-group outbounds keep their own stable mark; selection
// changes are realized exclusively in their routing tables.
std::vector<RuleState> build_fw_rule_states(
    const Config& cfg,
    const OutboundMarkMap& marks);

} // namespace keen_pbr3
