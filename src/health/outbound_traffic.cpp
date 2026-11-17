#include "outbound_traffic.hpp"

#include <fstream>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

namespace keen_pbr3 {
namespace {

std::optional<uint64_t> parse_u64(std::string_view text) {
  if (text.empty()) return std::nullopt;
  uint64_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') return std::nullopt;
    const uint64_t digit = static_cast<uint64_t>(c - '0');
    if (value > (UINT64_MAX - digit) / 10ULL) return std::nullopt;
    value = value * 10ULL + digit;
  }
  return value;
}

std::string_view trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
    text.remove_prefix(1);
  }
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' ||
                           text.back() == '\r')) {
    text.remove_suffix(1);
  }
  return text;
}

const char* outbound_type_name(OutboundType type) {
  switch (type) {
  case OutboundType::INTERFACE:
    return "interface";
  case OutboundType::TABLE:
    return "table";
  case OutboundType::BLACKHOLE:
    return "blackhole";
  case OutboundType::IGNORE:
    return "ignore";
  case OutboundType::URLTEST:
    return "urltest";
  case OutboundType::ICMPTEST:
    return "icmptest";
  }
  return "unknown";
}

void note_conntrack_line(std::string_view line, ConntrackMarkCounts& into) {
  if (trim(line).empty()) return;
  into.any_line = true;
  std::size_t at = 0;
  while (at < line.size()) {
    const auto found = line.find("mark=", at);
    if (found == std::string_view::npos) return;
    // "secmark=" contains the same suffix.  Only a field of its own counts.
    if (found > 0 && line[found - 1] != ' ' && line[found - 1] != '\t') {
      at = found + 5;
      continue;
    }
    into.saw_mark = true;
    std::size_t end = found + 5;
    while (end < line.size() && line[end] >= '0' && line[end] <= '9') ++end;
    const auto value = parse_u64(line.substr(found + 5, end - (found + 5)));
    if (value.has_value() && *value <= 0xFFFFFFFFULL) {
      into.by_mark[static_cast<uint32_t>(*value)] += 1;
    }
    return;
  }
}

void put_u64(nlohmann::json& object, const char* key, const std::optional<uint64_t>& value) {
  if (value.has_value()) object[key] = *value;
}

void put_f64(nlohmann::json& object, const char* key, const std::optional<double>& value) {
  if (value.has_value()) object[key] = *value;
}

std::optional<std::map<std::string, InterfaceCounters>> read_proc_net_dev(
    const std::string& path) {
  std::ifstream in(path);
  if (!in) return std::nullopt;
  std::ostringstream buffer;
  buffer << in.rdbuf();
  if (!in && !in.eof()) return std::nullopt;
  return parse_proc_net_dev(buffer.str());
}

std::optional<ConntrackMarkCounts> read_conntrack_marks(const std::string& path) {
  std::ifstream in(path);
  if (!in) return std::nullopt;
  ConntrackMarkCounts counts;
  std::string line;
  while (std::getline(in, line)) note_conntrack_line(line, counts);
  if (!in && !in.eof()) return std::nullopt;
  return counts;
}

} // namespace

std::map<std::string, InterfaceCounters> parse_proc_net_dev(std::string_view text) {
  std::map<std::string, InterfaceCounters> out;
  int line_no = 0;
  std::size_t pos = 0;
  while (pos < text.size()) {
    std::size_t end = text.find('\n', pos);
    if (end == std::string_view::npos) end = text.size();
    std::string_view line = text.substr(pos, end - pos);
    pos = end + (end < text.size() ? 1 : 0);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    ++line_no;
    if (line_no <= 2) continue; // the two header rows
    const auto colon = line.find(':');
    if (colon == std::string_view::npos) continue;
    const std::string name(trim(line.substr(0, colon)));
    if (name.empty()) continue;
    std::vector<uint64_t> fields;
    fields.reserve(16);
    std::size_t i = colon + 1;
    while (i < line.size()) {
      while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
      if (i >= line.size()) break;
      std::size_t j = i;
      while (j < line.size() && line[j] != ' ' && line[j] != '\t') ++j;
      const auto value = parse_u64(line.substr(i, j - i));
      if (!value.has_value()) {
        fields.clear();
        break;
      }
      fields.push_back(*value);
      i = j;
    }
    // rx bytes, rx packets, 6 more rx fields, tx bytes, tx packets.
    if (fields.size() < 10 || name.empty()) continue;
    InterfaceCounters counters;
    counters.rx_bytes = fields[0];
    counters.rx_packets = fields[1];
    counters.tx_bytes = fields[8];
    counters.tx_packets = fields[9];
    out.emplace(name, counters);
  }
  return out;
}

ConntrackMarkCounts parse_conntrack_marks(std::string_view text) {
  ConntrackMarkCounts counts;
  std::size_t pos = 0;
  while (pos < text.size()) {
    std::size_t end = text.find('\n', pos);
    if (end == std::string_view::npos) end = text.size();
    note_conntrack_line(text.substr(pos, end - pos), counts);
    pos = end + (end < text.size() ? 1 : 0);
  }
  return counts;
}

OutboundTrafficReport build_outbound_traffic_report(const OutboundTrafficInput& input) {
  OutboundTrafficReport report;
  if (input.config == nullptr) return report;
  const auto& outbounds = input.config->outbounds.value_or(std::vector<Outbound>{});
  report.entries.reserve(outbounds.size());
  for (const auto& outbound : outbounds) {
    OutboundTrafficEntry entry;
    entry.tag = outbound.tag;
    entry.type = outbound_type_name(outbound.type);

    uint32_t mark = 0;
    if (input.marks != nullptr) {
      const auto it = input.marks->find(outbound.tag);
      if (it != input.marks->end()) mark = it->second;
    }

    const bool interface_outbound = outbound.type == OutboundType::INTERFACE &&
                                    outbound.interface.has_value() &&
                                    !outbound.interface->empty();
    if (interface_outbound && input.dev != nullptr) {
      entry.source = "interface";
      entry.interface_name = *outbound.interface;
      const auto found = input.dev->find(*outbound.interface);
      entry.interface_present = found != input.dev->end();
      if (found != input.dev->end()) {
        entry.rx_bytes = found->second.rx_bytes;
        entry.rx_packets = found->second.rx_packets;
        entry.tx_bytes = found->second.tx_bytes;
        entry.tx_packets = found->second.tx_packets;
      }
    } else if (mark != 0 && input.fwmark != nullptr) {
      entry.source = "fwmark";
      if (interface_outbound) entry.interface_name = *outbound.interface;
      const auto found = input.fwmark->find(outbound.tag);
      entry.tx_bytes = found == input.fwmark->end() ? 0 : found->second.bytes;
      entry.tx_packets = found == input.fwmark->end() ? 0 : found->second.packets;
    } else if (interface_outbound) {
      entry.interface_name = *outbound.interface;
    }

    if (input.conntrack != nullptr && input.conntrack->saw_mark && mark != 0) {
      const auto found = input.conntrack->by_mark.find(mark);
      entry.connections = found == input.conntrack->by_mark.end() ? 0 : found->second;
    }
    report.entries.push_back(std::move(entry));
  }
  return report;
}

void apply_traffic_rates(OutboundTrafficReport& report, TrafficRateBaseline& baseline,
                         TrafficRateBaseline::Clock::time_point now) {
  for (auto& entry : report.entries) {
    entry.rx_bytes_per_sec.reset();
    entry.tx_bytes_per_sec.reset();
    entry.window_seconds.reset();
  }
  if (baseline.valid) {
    const double dt = std::chrono::duration<double>(now - baseline.at).count();
    if (dt >= 0.2 && dt <= 120.0) {
      for (auto& entry : report.entries) {
        const auto prev = baseline.points.find(entry.tag);
        if (prev == baseline.points.end()) continue;
        bool any = false;
        if (entry.rx_bytes.has_value() && prev->second.have_rx &&
            *entry.rx_bytes >= prev->second.rx_bytes) {
          entry.rx_bytes_per_sec =
              static_cast<double>(*entry.rx_bytes - prev->second.rx_bytes) / dt;
          any = true;
        }
        if (entry.tx_bytes.has_value() && prev->second.have_tx &&
            *entry.tx_bytes >= prev->second.tx_bytes) {
          entry.tx_bytes_per_sec =
              static_cast<double>(*entry.tx_bytes - prev->second.tx_bytes) / dt;
          any = true;
        }
        if (any) entry.window_seconds = dt;
      }
    }
  }

  TrafficRateBaseline next;
  next.valid = true;
  next.at = now;
  for (const auto& entry : report.entries) {
    TrafficRateBaseline::Point point;
    if (entry.rx_bytes.has_value()) {
      point.rx_bytes = *entry.rx_bytes;
      point.have_rx = true;
    }
    if (entry.tx_bytes.has_value()) {
      point.tx_bytes = *entry.tx_bytes;
      point.have_tx = true;
    }
    next.points.emplace(entry.tag, point);
  }
  baseline = std::move(next);
}

std::string outbound_traffic_json(const OutboundTrafficReport& report) {
  nlohmann::json root;
  root["collected_at_unix_ms"] = report.collected_at_unix_ms;
  nlohmann::json entries = nlohmann::json::array();
  for (const auto& entry : report.entries) {
    nlohmann::json item;
    item["tag"] = entry.tag;
    item["type"] = entry.type;
    item["source"] = entry.source;
    if (entry.interface_name.has_value()) item["interface_name"] = *entry.interface_name;
    if (entry.interface_present.has_value()) {
      item["interface_present"] = *entry.interface_present;
    }
    put_u64(item, "rx_bytes", entry.rx_bytes);
    put_u64(item, "rx_packets", entry.rx_packets);
    put_u64(item, "tx_bytes", entry.tx_bytes);
    put_u64(item, "tx_packets", entry.tx_packets);
    put_f64(item, "rx_bytes_per_sec", entry.rx_bytes_per_sec);
    put_f64(item, "tx_bytes_per_sec", entry.tx_bytes_per_sec);
    put_f64(item, "window_seconds", entry.window_seconds);
    put_u64(item, "connections", entry.connections);
    entries.push_back(std::move(item));
  }
  root["outbounds"] = std::move(entries);
  return root.dump();
}

OutboundTrafficCollector::OutboundTrafficCollector(std::chrono::seconds cache_for)
    : cache_for_(cache_for) {}

void OutboundTrafficCollector::set_proc_paths_for_tests(std::string net_dev,
                                                       std::string conntrack) {
  std::lock_guard<std::mutex> lock(mutex_);
  net_dev_path_ = std::move(net_dev);
  conntrack_path_ = std::move(conntrack);
  cache_valid_ = false;
}

OutboundTrafficReport OutboundTrafficCollector::collect(const Input& input,
                                                       Clock::time_point now) {
  std::string net_dev_path;
  std::string conntrack_path;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cache_valid_ && now - cached_at_ < cache_for_) return cached_;
    net_dev_path = net_dev_path_;
    conntrack_path = conntrack_path_;
  }

  const auto dev = read_proc_net_dev(net_dev_path);
  const auto conntrack = read_conntrack_marks(conntrack_path);
  const ConntrackMarkCounts* marks_view = nullptr;
  if (conntrack.has_value() && conntrack->saw_mark) marks_view = &*conntrack;

  OutboundTrafficInput built;
  built.config = input.config;
  built.marks = input.marks;
  built.fwmark = input.fwmark;
  built.dev = dev.has_value() ? &*dev : nullptr;
  built.conntrack = marks_view;
  auto report = build_outbound_traffic_report(built);

  std::lock_guard<std::mutex> lock(mutex_);
  if (cache_valid_ && now - cached_at_ < cache_for_) return cached_;
  apply_traffic_rates(report, baseline_, now);
  report.collected_at_unix_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
  cached_ = report;
  cached_at_ = now;
  cache_valid_ = true;
  return cached_;
}

} // namespace keen_pbr3
