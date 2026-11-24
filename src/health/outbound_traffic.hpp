#pragma once

#include "../config/config.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace keen_pbr3 {

// One row of /proc/net/dev.  For a TUN such as sing-box's sbtun, rx is what
// the kernel receives from the interface and tx is what it sends into it.
struct InterfaceCounters {
  uint64_t rx_bytes{0};
  uint64_t rx_packets{0};
  uint64_t tx_bytes{0};
  uint64_t tx_packets{0};
};

// iptables MARK rule totals for one outbound (see FirewallCounters).
struct MarkByteCounters {
  uint64_t packets{0};
  uint64_t bytes{0};
};

// mark -> number of conntrack entries carrying that mark.
struct ConntrackMarkCounts {
  std::map<uint32_t, uint64_t> by_mark;
  bool saw_mark{false};
  bool any_line{false};
};

std::map<std::string, InterfaceCounters> parse_proc_net_dev(std::string_view text);
ConntrackMarkCounts parse_conntrack_marks(std::string_view text);

// One configured outbound.  Absent counters mean that direction is not known
// from the chosen source, which is different from a real zero.
struct OutboundTrafficEntry {
  std::string tag;
  std::string type;
  // "interface" (/proc/net/dev), "fwmark" (iptables MARK counters), "none".
  std::string source{"none"};
  std::optional<std::string> interface_name;
  // Set only for source "interface": false when the name is not in the table.
  std::optional<bool> interface_present;
  std::optional<uint64_t> rx_bytes;
  std::optional<uint64_t> rx_packets;
  std::optional<uint64_t> tx_bytes;
  std::optional<uint64_t> tx_packets;
  std::optional<double> rx_bytes_per_sec;
  std::optional<double> tx_bytes_per_sec;
  std::optional<double> window_seconds;
  std::optional<uint64_t> connections;
};

struct OutboundTrafficReport {
  std::vector<OutboundTrafficEntry> entries;
  int64_t collected_at_unix_ms{0};
};

struct OutboundTrafficInput {
  const Config* config{nullptr};
  const OutboundMarkMap* marks{nullptr};
  // Non-null only when iptables counters were actually read.  An empty map
  // still means "read, and nothing was marked yet".
  const std::map<std::string, MarkByteCounters>* fwmark{nullptr};
  const std::map<std::string, InterfaceCounters>* dev{nullptr};
  // Non-null only when the conntrack table was readable AND exposed marks.
  const ConntrackMarkCounts* conntrack{nullptr};
};

OutboundTrafficReport build_outbound_traffic_report(const OutboundTrafficInput& input);

// Fills per-second rates from the delta against `baseline`, then stores the
// current counters as the next baseline.  A counter that went backwards (an
// apply rebuilt the iptables rules, or the interface was recreated) yields no
// rate for that direction on this sample.
struct TrafficRateBaseline {
  using Clock = std::chrono::steady_clock;
  bool valid{false};
  Clock::time_point at{};
  struct Point {
    uint64_t rx_bytes{0};
    uint64_t tx_bytes{0};
    bool have_rx{false};
    bool have_tx{false};
  };
  std::map<std::string, Point> points;
};

void apply_traffic_rates(OutboundTrafficReport& report, TrafficRateBaseline& baseline,
                         TrafficRateBaseline::Clock::time_point now);

std::string outbound_traffic_json(const OutboundTrafficReport& report);

// Reads /proc/net/dev and /proc/net/nf_conntrack, keeps the previous sample so
// the next call can report bytes/s, and caches the report for a couple of
// seconds so the UI poll and /metrics do not scan conntrack twice.
class OutboundTrafficCollector {
public:
  using Clock = std::chrono::steady_clock;
  explicit OutboundTrafficCollector(
      std::chrono::seconds cache_for = std::chrono::seconds{2});

  struct Input {
    const Config* config{nullptr};
    const OutboundMarkMap* marks{nullptr};
    const std::map<std::string, MarkByteCounters>* fwmark{nullptr};
  };

  OutboundTrafficReport collect(const Input& input, Clock::time_point now);

  void set_proc_paths_for_tests(std::string net_dev, std::string conntrack);

private:
  std::chrono::seconds cache_for_;
  std::string net_dev_path_{"/proc/net/dev"};
  std::string conntrack_path_{"/proc/net/nf_conntrack"};
  std::mutex mutex_;
  bool cache_valid_{false};
  Clock::time_point cached_at_{};
  OutboundTrafficReport cached_;
  TrafficRateBaseline baseline_;
};

} // namespace keen_pbr3
