#include <doctest/doctest.h>

#include "../src/health/outbound_traffic.hpp"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace keen_pbr3 {
namespace {

Outbound interface_outbound(std::string tag, std::string iface) {
  Outbound outbound;
  outbound.tag = std::move(tag);
  outbound.type = OutboundType::INTERFACE;
  outbound.interface = std::move(iface);
  return outbound;
}

Outbound typed_outbound(std::string tag, OutboundType type) {
  Outbound outbound;
  outbound.tag = std::move(tag);
  outbound.type = type;
  return outbound;
}

const OutboundTrafficEntry* find_entry(const OutboundTrafficReport& report,
                                       const std::string& tag) {
  for (const auto& entry : report.entries) {
    if (entry.tag == tag) return &entry;
  }
  return nullptr;
}

const char* kDev =
    "Inter-|   Receive                                                |  Transmit\n"
    " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
    "    lo: 10 1 0 0 0 0 0 0 20 2 0 0 0 0 0 0\n"
    " sbtun: 1000 8 0 0 0 0 0 0 4000 16 0 0 0 0 0 0\n"
    "  eth0: 50 5 0 0 0 0 0 0 70 7 0 0 0 0 0 0\n";

} // namespace

TEST_CASE("proc net dev parser reads rx and tx and ignores the header") {
  const auto dev = parse_proc_net_dev(kDev);
  REQUIRE(dev.count("sbtun") == 1);
  CHECK(dev.at("sbtun").rx_bytes == 1000);
  CHECK(dev.at("sbtun").rx_packets == 8);
  CHECK(dev.at("sbtun").tx_bytes == 4000);
  CHECK(dev.at("sbtun").tx_packets == 16);
  CHECK(dev.at("lo").rx_bytes == 10);
  CHECK(dev.count("Inter-") == 0);
  CHECK(parse_proc_net_dev("no colon here\n").empty());
}

TEST_CASE("conntrack mark parser ignores secmark and counts both families") {
  const auto counts = parse_conntrack_marks(
      "ipv4     2 tcp 6 10 ESTABLISHED src=10.0.0.2 dst=1.1.1.1 sport=1 dport=443 "
      "src=1.1.1.1 dst=10.0.0.2 sport=443 dport=1 [ASSURED] mark=256 secmark=0 use=2\n"
      "ipv4     2 udp 17 10 src=10.0.0.3 dst=8.8.8.8 sport=2 dport=53 "
      "src=8.8.8.8 dst=10.0.0.3 sport=53 dport=2 mark=256 use=1\n"
      "ipv6     10 tcp 6 10 ESTABLISHED src=fe80::1 dst=2001:db8::1 sport=9 dport=443 "
      "src=2001:db8::1 dst=fe80::1 sport=443 dport=9 mark=512 use=1\n"
      "ipv4     2 tcp 6 10 CLOSE src=1.1.1.1 dst=2.2.2.2 sport=1 dport=2 "
      "src=2.2.2.2 dst=1.1.1.1 sport=2 dport=1 mark=0 use=1\n");
  CHECK(counts.saw_mark);
  CHECK(counts.by_mark.at(256) == 2);
  CHECK(counts.by_mark.at(512) == 1);
  CHECK(counts.by_mark.at(0) == 1);
  const auto unmarked = parse_conntrack_marks(
      "ipv4 2 tcp 6 10 ESTABLISHED src=1.1.1.1 dst=2.2.2.2 sport=1 dport=2 "
      "src=2.2.2.2 dst=1.1.1.1 sport=2 dport=1 use=1\n");
  CHECK_FALSE(unmarked.saw_mark);
  CHECK(unmarked.any_line);
}

TEST_CASE("interface outbounds use dev counters and other outbounds use fwmark") {
  Config config;
  config.outbounds = std::vector<Outbound>{
      interface_outbound("proxy", "sbtun"),
      interface_outbound("missing", "no-such"),
      typed_outbound("wan", OutboundType::TABLE),
      typed_outbound("drop", OutboundType::BLACKHOLE),
      typed_outbound("auto", OutboundType::URLTEST),
  };
  const auto dev = parse_proc_net_dev(kDev);
  OutboundMarkMap marks{{"proxy", 0x100}, {"wan", 0x200}, {"auto", 0x300}};
  std::map<std::string, MarkByteCounters> fwmark{
      {"wan", MarkByteCounters{12, 3400}},
      {"auto", MarkByteCounters{0, 0}},
  };
  ConntrackMarkCounts conntrack;
  conntrack.saw_mark = true;
  conntrack.by_mark[0x100] = 4;
  conntrack.by_mark[0x200] = 1;

  OutboundTrafficInput input;
  input.config = &config;
  input.marks = &marks;
  input.fwmark = &fwmark;
  input.dev = &dev;
  input.conntrack = &conntrack;
  const auto report = build_outbound_traffic_report(input);

  const auto* proxy = find_entry(report, "proxy");
  REQUIRE(proxy != nullptr);
  CHECK(proxy->source == "interface");
  CHECK(proxy->interface_name == "sbtun");
  CHECK(proxy->interface_present == true);
  CHECK(proxy->rx_bytes == 1000);
  CHECK(proxy->tx_bytes == 4000);
  CHECK(proxy->rx_packets == 8);
  CHECK(proxy->tx_packets == 16);
  CHECK(proxy->connections == 4);

  const auto* missing = find_entry(report, "missing");
  REQUIRE(missing != nullptr);
  CHECK(missing->source == "interface");
  CHECK(missing->interface_present == false);
  CHECK_FALSE(missing->rx_bytes.has_value());
  CHECK_FALSE(missing->tx_bytes.has_value());

  const auto* wan = find_entry(report, "wan");
  REQUIRE(wan != nullptr);
  CHECK(wan->source == "fwmark");
  CHECK(wan->tx_bytes == 3400);
  CHECK(wan->tx_packets == 12);
  CHECK_FALSE(wan->rx_bytes.has_value());
  CHECK(wan->connections == 1);

  const auto* drop = find_entry(report, "drop");
  REQUIRE(drop != nullptr);
  CHECK(drop->source == "none");
  CHECK_FALSE(drop->connections.has_value());

  const auto* group = find_entry(report, "auto");
  REQUIRE(group != nullptr);
  CHECK(group->source == "fwmark");
  CHECK(group->tx_bytes == 0);
  CHECK(group->connections == 0);
}

TEST_CASE("rates use the delta over the sample window and ignore a reset") {
  OutboundTrafficReport first;
  OutboundTrafficEntry entry;
  entry.tag = "proxy";
  entry.rx_bytes = 1000;
  entry.tx_bytes = 4000;
  first.entries.push_back(entry);
  TrafficRateBaseline baseline;
  const auto t0 = TrafficRateBaseline::Clock::time_point{};
  apply_traffic_rates(first, baseline, t0);
  CHECK_FALSE(first.entries[0].rx_bytes_per_sec.has_value());

  OutboundTrafficReport second = first;
  second.entries[0].rx_bytes = 1000 + 200;
  second.entries[0].tx_bytes = 4000 + 500;
  apply_traffic_rates(second, baseline, t0 + std::chrono::seconds{2});
  REQUIRE(second.entries[0].rx_bytes_per_sec.has_value());
  CHECK(*second.entries[0].rx_bytes_per_sec == doctest::Approx(100.0));
  CHECK(*second.entries[0].tx_bytes_per_sec == doctest::Approx(250.0));
  CHECK(*second.entries[0].window_seconds == doctest::Approx(2.0));

  OutboundTrafficReport reset = second;
  reset.entries[0].rx_bytes = 10;
  reset.entries[0].tx_bytes = 4000 + 500 + 100;
  apply_traffic_rates(reset, baseline, t0 + std::chrono::seconds{4});
  CHECK_FALSE(reset.entries[0].rx_bytes_per_sec.has_value());
  CHECK(*reset.entries[0].tx_bytes_per_sec == doctest::Approx(50.0));
}

TEST_CASE("json omits unknown directions and keeps a real zero") {
  OutboundTrafficReport report;
  report.collected_at_unix_ms = 42;
  OutboundTrafficEntry entry;
  entry.tag = "wan";
  entry.type = "table";
  entry.source = "fwmark";
  entry.tx_bytes = 0;
  entry.tx_packets = 0;
  entry.connections = 0;
  report.entries.push_back(entry);
  const auto parsed = nlohmann::json::parse(outbound_traffic_json(report));
  CHECK(parsed["collected_at_unix_ms"] == 42);
  const auto& item = parsed["outbounds"][0];
  CHECK(item["tag"] == "wan");
  CHECK(item["source"] == "fwmark");
  CHECK(item["tx_bytes"] == 0);
  CHECK(item["connections"] == 0);
  CHECK(item.find("rx_bytes") == item.end());
  CHECK(item.find("rx_bytes_per_sec") == item.end());
}

TEST_CASE("collector reads proc files and rates the second sample") {
  const auto dir = std::filesystem::temp_directory_path() / "keen-pbr-traffic-test";
  std::filesystem::create_directories(dir);
  const auto dev_path = dir / "dev";
  const auto ct_path = dir / "nf_conntrack";
  {
    std::ofstream dev(dev_path);
    dev << kDev;
    std::ofstream ct(ct_path);
    ct << "ipv4 2 tcp 6 10 ESTABLISHED src=1.1.1.1 dst=2.2.2.2 sport=1 dport=2 "
          "src=2.2.2.2 dst=1.1.1.1 sport=2 dport=1 mark=256 use=1\n";
  }
  Config config;
  config.outbounds = std::vector<Outbound>{interface_outbound("proxy", "sbtun")};
  OutboundMarkMap marks{{"proxy", 256}};
  OutboundTrafficCollector collector(std::chrono::seconds{2});
  collector.set_proc_paths_for_tests(dev_path.string(), ct_path.string());
  OutboundTrafficCollector::Input input;
  input.config = &config;
  input.marks = &marks;
  const auto t0 = OutboundTrafficCollector::Clock::time_point{};
  const auto first = collector.collect(input, t0);
  const auto* before = find_entry(first, "proxy");
  REQUIRE(before != nullptr);
  CHECK(before->source == "interface");
  CHECK(before->tx_bytes == 4000);
  CHECK(before->connections == 1);
  CHECK_FALSE(before->tx_bytes_per_sec.has_value());

  {
    std::ofstream dev(dev_path);
    dev << "Inter-|   Receive                                                |  Transmit\n"
           " face |bytes    packets errs drop fifo frame compressed multicast|bytes    "
           "packets errs drop fifo colls carrier compressed\n"
           " sbtun: 1000 8 0 0 0 0 0 0 5000 20 0 0 0 0 0 0\n";
  }
  const auto second = collector.collect(input, t0 + std::chrono::seconds{2});
  const auto* after = find_entry(second, "proxy");
  REQUIRE(after != nullptr);
  CHECK(after->tx_bytes == 5000);
  CHECK(*after->tx_bytes_per_sec == doctest::Approx(500.0));
  CHECK(*after->rx_bytes_per_sec == doctest::Approx(0.0));

  std::filesystem::remove_all(dir);
}

} // namespace keen_pbr3
