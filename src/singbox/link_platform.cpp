#include "link_store.hpp"

#include "../http/http_client.hpp"
#include "../util/safe_exec.hpp"

#include <arpa/inet.h>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace keen_pbr3 {
namespace {

struct OwnedFd {
    int fd{-1};
    ~OwnedFd() {
        if (fd >= 0) ::close(fd);
    }
    OwnedFd() = default;
    OwnedFd(const OwnedFd&) = delete;
    OwnedFd& operator=(const OwnedFd&) = delete;
};

bool loopback_controller(const std::string& controller, int& port) {
    const auto colon = controller.rfind(':');
    if (colon == std::string::npos || colon == 0) return false;
    auto host = controller.substr(0, colon);
    if (!host.empty() && host.front() == '[') {
        if (host.back() != ']') return false;
        host = host.substr(1, host.size() - 2);
    }
    if (host != "127.0.0.1" && host != "localhost" && host != "0.0.0.0" && host != "::" && host != "::1") {
        return false;
    }
    int parsed = 0;
    for (char c : controller.substr(colon + 1)) {
        if (c < '0' || c > '9') return false;
        parsed = parsed * 10 + (c - '0');
        if (parsed > 65535) return false;
    }
    if (parsed <= 0) return false;
    port = parsed;
    return true;
}

bool safe_group_tag(const std::string& tag) {
    if (tag.empty() || tag.size() > 64) return false;
    for (unsigned char c : tag) {
        if (!std::isalnum(c) && c != '-' && c != '_') return false;
    }
    return true;
}

std::string http_get_loopback(int port, const std::string& path, const std::string& secret) {
    OwnedFd socket;
    socket.fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (socket.fd < 0) return {};
    const int flags = ::fcntl(socket.fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(socket.fd, F_SETFL, flags | O_NONBLOCK) != 0) return {};
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int connected = ::connect(socket.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    if (connected != 0 && errno != EINPROGRESS) return {};
    pollfd ready{socket.fd, POLLOUT, 0};
    if (::poll(&ready, 1, 300) <= 0) return {};
    int so_error = 0;
    socklen_t length = sizeof(so_error);
    if (::getsockopt(socket.fd, SOL_SOCKET, SO_ERROR, &so_error, &length) != 0 || so_error != 0) return {};
    if (::fcntl(socket.fd, F_SETFL, flags) != 0) return {};
    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 300000;
    if (::setsockopt(socket.fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) return {};

    std::string request = "GET " + path + " HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: close\r\n";
    if (!secret.empty()) request += "Authorization: Bearer " + secret + "\r\n";
    request += "\r\n";
    std::size_t sent = 0;
    while (sent < request.size()) {
        const auto count = ::send(socket.fd, request.data() + sent, request.size() - sent, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return {};
        sent += static_cast<std::size_t>(count);
    }
    std::string response;
    char buffer[1024];
    while (response.size() < static_cast<std::size_t>(8) * 1024U) {
        const auto count = ::recv(socket.fd, buffer, sizeof(buffer), 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        response.append(buffer, static_cast<std::size_t>(count));
    }
    const auto split = response.find("\r\n\r\n");
    if (split == std::string::npos || response.find(" 200 ") == std::string::npos) return {};
    return response.substr(split + 4);
}

std::string probe_urltest_now(const std::string& config_path, const std::string& group_tag) {
    if (!safe_group_tag(group_tag)) return {};
    struct Cache {
        std::mutex mutex;
        std::map<std::string, std::pair<std::int64_t, std::string>> values;
    };
    static Cache cache;
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
    {
        std::lock_guard<std::mutex> lock(cache.mutex);
        const auto it = cache.values.find(group_tag);
        if (it != cache.values.end() && now - it->second.first < 15) return it->second.second;
    }
    std::string selected;
    try {
        std::ifstream input(config_path);
        if (!input) return {};
        const auto json = nlohmann::json::parse(std::string(std::istreambuf_iterator<char>(input), {}));
        if (!json.contains("experimental") || !json["experimental"].is_object()) return {};
        const auto& experimental = json["experimental"];
        if (!experimental.contains("clash_api") || !experimental["clash_api"].is_object()) return {};
        const auto& api = experimental["clash_api"];
        int port = 0;
        if (!loopback_controller(api.value("external_controller", ""), port)) return {};
        const auto body = http_get_loopback(port, "/proxies/" + group_tag, api.value("secret", ""));
        if (body.empty()) return {};
        const auto parsed = nlohmann::json::parse(body);
        selected = parsed.value("now", "");
    } catch (const std::exception&) {
        selected.clear();
    }
    std::lock_guard<std::mutex> lock(cache.mutex);
    cache.values[group_tag] = {now, selected};
    return selected;
}

}  // namespace

SingboxLinkService make_singbox_link_service() {
    SingboxPaths paths;
    if (const char* value = std::getenv("KEEN_PBR_SINGBOX_STORE")) paths.store = value;
    if (const char* value = std::getenv("KEEN_PBR_SINGBOX_CONFIG")) paths.singbox_config = value;
    if (const char* value = std::getenv("KEEN_PBR_SINGBOX_BIN")) paths.singbox_bin = value;

    return SingboxLinkService(
        paths,
        [bin = paths.singbox_bin](const std::string& config, std::string* error) {
            // Keep a bounded merged stderr so the UI can show the FATAL/ERROR line.
            // format_singbox_check_error redacts uuids, passwords and keys.
            const auto result = safe_exec_capture_with_timeouts(
                {bin, "check", "-c", config}, safe_exec_timeouts(), false, std::size_t{2} * 1024U, true, true);
            if (error) *error = result.timed_out ? "timed out" : result.stdout_output;
            return result.exit_code;
        },
        {},
        [] {
            for (const char* script : {"/opt/etc/init.d/S99sing-box", "/opt/etc/init.d/S98sing-box"}) {
                std::ifstream probe(script);
                if (!probe) continue;
                const auto result = safe_exec_capture_with_timeouts(
                    {script, "restart"}, safe_exec_timeouts(), true, 0, true, false);
                if (result.exit_code == 0) return true;
                throw std::runtime_error("sing-box restart failed");
            }
            return false;
        },
        [](const std::string& url, const SubscriptionStopFn& stopping) {
            HttpClient client;
            client.set_timeout(std::chrono::seconds{20});
            client.set_max_response_size(std::size_t{2} * 1024U * 1024U);
            // Do not wait behind unrelated transfers outside curl's timeout.
            return client.download(url, {0, true, stopping});
        },
        {},
        [config = paths.singbox_config](const std::string& group) {
            return probe_urltest_now(config, group);
        });
}

}  // namespace keen_pbr3
