#pragma once

#include "../config/config.hpp"
#include "../http/http_transport.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace keen_pbr3 {

// Keep urltest probe timeouts independent from circuit-breaker cooldowns.
constexpr uint32_t kDefaultUrltestProbeTimeoutMs = 5000;

struct URLTestResult {
    bool success{false};
    // Millisecond latency drives selection (thresholds are in ms).
    uint32_t latency_ms{0};
    // Sub-millisecond latency for metrics; when unset, latency_ms is used.
    // min/max are only filled by ICMP probes (fastest/slowest reply).
    std::optional<uint32_t> latency_us;
    std::optional<uint32_t> latency_min_us;
    std::optional<uint32_t> latency_max_us;
    std::string error;
    std::optional<std::string> probe_target;
    std::optional<uint32_t> packets_attempted;
    std::optional<uint32_t> packets_sent;
    std::optional<uint32_t> packets_received;
    std::optional<uint32_t> packets_failed;
};

class URLTester {
public:
    URLTester();
    explicit URLTester(std::shared_ptr<HttpTransport> transport);
    ~URLTester();

    URLTester(const URLTester&) = delete;
    URLTester& operator=(const URLTester&) = delete;

    // Test a URL through an outbound identified by its fwmark.
    // Uses CURLOPT_MARK to route test traffic via the correct routing table.
    // Retries up to retry.attempts times with retry.interval_ms delay between attempts.
    // Returns the result with latency_ms from the fastest successful attempt.
    // When ipv6_enabled is false, every attempt is IPv4. When it is true, an
    // attempt starts on either family and, after a timeout, connect, or TLS
    // failure, retries once on IPv4 inside the same attempt.
    URLTestResult test(const std::string& url, uint32_t fwmark,
                       uint32_t timeout_ms, const RetryConfig& retry,
                       bool ipv6_enabled = true);

private:
    URLTestResult test_once(const std::string& url, uint32_t fwmark,
                            uint32_t timeout_ms, HttpIpResolve ip_resolve);
    std::shared_ptr<HttpTransport> transport_;
};

} // namespace keen_pbr3
