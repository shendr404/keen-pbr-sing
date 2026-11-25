#include "url_tester.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <thread>

namespace keen_pbr3 {
namespace {

bool ipv4_fallback_error(const std::string& error) {
    std::string lower = error;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower.find("timed out") != std::string::npos ||
           lower.find("timeout") != std::string::npos ||
           lower.find("connect") != std::string::npos ||
           lower.find("ssl") != std::string::npos ||
           lower.find("tls") != std::string::npos;
}

} // namespace

URLTester::URLTester() : URLTester(default_http_transport()) {}
URLTester::URLTester(std::shared_ptr<HttpTransport> transport) : transport_(std::move(transport)) {
    if (!transport_) throw std::invalid_argument("URLTester requires an HTTP transport");
}
URLTester::~URLTester() = default;

URLTestResult URLTester::test_once(const std::string& url, uint32_t fwmark, uint32_t timeout_ms,
                                   HttpIpResolve ip_resolve) {
    URLTestResult result;
    HttpTransportRequest request;
    request.url = url;
    request.timeout_ms = static_cast<long>(timeout_ms);
    request.ip_resolve = ip_resolve;
    request.user_agent = "keen-pbr-urltest";
    request.fwmark = fwmark;
    request.max_redirects = 3;
    request.discard_body = true;
    try {
        const auto response = transport_->perform(request);
        if (response.status_code >= 200 && response.status_code < 300) {
            result.success = true;
            const auto elapsed_us = static_cast<uint32_t>(
                std::min<int64_t>(response.elapsed.count(), UINT32_MAX));
            result.latency_us = elapsed_us;
            result.latency_ms = elapsed_us / 1000;
        } else {
            result.error = "HTTP " + std::to_string(response.status_code);
        }
    } catch (const HttpTransportError& error) {
        result.error = error.what();
    }
    return result;
}

URLTestResult URLTester::test(const std::string& url, uint32_t fwmark, uint32_t timeout_ms,
                              const RetryConfig& retry, bool ipv6_enabled) {
    URLTestResult best;
    best.error = "No attempts made";
    const auto attempts = static_cast<uint32_t>(retry.attempts.value_or(1));
    // One attempt has a single wall-clock budget of timeout_ms. With IPv6
    // enabled, the first try (either family) is limited to half of that so a
    // hung IPv6 handshake cannot consume it all. A timeout, connect, or TLS
    // failure is retried at once over IPv4 with the time still left. The
    // reported latency is only the successful try. retry.attempts repeats
    // this whole budgeted attempt.
    const uint32_t budget = timeout_ms == 0 ? 1U : timeout_ms;
    for (uint32_t attempt = 0; attempt < attempts; ++attempt) {
        if (attempt) std::this_thread::sleep_for(std::chrono::milliseconds(retry.interval_ms.value_or(1000)));
        if (!ipv6_enabled) {
            auto result = test_once(url, fwmark, budget, HttpIpResolve::v4);
            if (result.success) return result;
            best.error = result.error;
            continue;
        }
        const uint32_t first_budget = std::max<uint32_t>(1U, budget / 2U);
        const auto started = std::chrono::steady_clock::now();
        auto result = test_once(url, fwmark, first_budget, HttpIpResolve::whatever);
        if (result.success) return result;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
        const auto remaining = elapsed >= static_cast<int64_t>(budget)
                                   ? int64_t{0}
                                   : static_cast<int64_t>(budget) - elapsed;
        if (remaining > 0 && ipv4_fallback_error(result.error)) {
            auto ipv4 = test_once(url, fwmark, static_cast<uint32_t>(remaining), HttpIpResolve::v4);
            if (ipv4.success) return ipv4;
            result.error = ipv4.error;
        }
        best.error = result.error;
    }
    return best;
}
} // namespace keen_pbr3
