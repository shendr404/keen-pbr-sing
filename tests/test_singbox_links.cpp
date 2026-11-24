#include <doctest/doctest.h>

#include "../src/singbox/link_parser.hpp"
#include "../src/singbox/link_store.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <utility>

using namespace keen_pbr3;

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

}  // namespace

TEST_CASE("proxy links parse without putting secrets in the public preview") {
    const auto vless = parse_proxy_link(
        "vless://11111111-1111-1111-1111-111111111111@example.com:443"
        "?type=ws&security=tls&sni=example.com&path=%2Fvpn#Home");
    CHECK(vless.protocol == "vless");
    CHECK(vless.server == "example.com");
    CHECK(vless.port == 443);
    CHECK(vless.name == "Home");
    CHECK(vless.outbound.at("uuid") == "11111111-1111-1111-1111-111111111111");
    CHECK(vless.outbound.at("tls").at("server_name") == "example.com");
    CHECK(vless.outbound.at("transport").at("type") == "ws");
    const auto preview = public_proxy_fields(vless).dump();
    CHECK(preview.find("11111111") == std::string::npos);

    const auto hy2 = parse_proxy_link("hy2://secret@10.0.0.8:443?obfs=salamander&obfs-password=obfs#fi");
    CHECK(hy2.protocol == "hysteria2");
    CHECK(hy2.outbound.at("obfs").at("type") == "salamander");

    const auto shadowsocks = parse_proxy_link("ss://YWVzLTI1Ni1nY206c2VjcmV0@1.2.3.4:8388#box");
    CHECK(shadowsocks.protocol == "shadowsocks");
    CHECK(shadowsocks.outbound.at("method") == "aes-256-gcm");
    CHECK(shadowsocks.server == "1.2.3.4");

    CHECK_THROWS_WITH(parse_proxy_link("http://example"), "unsupported proxy scheme");
}

TEST_CASE("subscriptions accept a base64 list and clash JSON") {
    const auto encoded = "dmxlc3M6Ly8xMTExMTExMS0xMTExLTExMTEtMTExMS0xMTExMTExMTExMTFAc2VydmVyLmV4YW1wbGU6NDQzI0EK";
    std::size_t skipped = 0;
    const auto nodes = parse_subscription(encoded, &skipped);
    REQUIRE(nodes.size() == 1);
    CHECK(nodes[0].link.protocol == "vless");
    CHECK(nodes[0].link.server == "server.example");

    const auto clash = R"({
      "proxies": [
        {"name":"a","type":"trojan","server":"t.example","port":443,"password":"pw","sni":"t.example"},
        {"name":"skip","type":"wireguard","server":"w.example","port":1}
      ]
    })";
    skipped = 0;
    const auto parsed = parse_subscription(clash, &skipped);
    REQUIRE(parsed.size() == 1);
    CHECK(parsed[0].link.protocol == "trojan");
    CHECK(skipped == 1);
}

TEST_CASE("sing-box check failure restores the previous config and store") {
    const auto root = std::filesystem::temp_directory_path() / "keen-pbr-singbox-links";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    SingboxPaths paths;
    paths.store = (root / "links.json").string();
    paths.singbox_config = (root / "config.json").string();
    std::ofstream(paths.singbox_config) << "{\"old\":true}";
    int checks = 0;
    SingboxLinkService service(
        paths,
        [&checks](const std::string&, std::string* error) {
            ++checks;
            if (error != nullptr) *error = "bad outbound";
            return checks == 1 ? 1 : 0;
        },
        [](const SingboxProfile&, bool) {},
        [] { return true; });

    CHECK_THROWS_WITH(service.save_connection("Proxy", "sbtun", "proxy", "selector", "Home",
                                              "vless://11111111-1111-1111-1111-111111111111@example.com:443#Home",
                                              ""),
                      "sing-box check failed: bad outbound");
    CHECK(read_file(paths.singbox_config).find("\"old\":true") != std::string::npos);
    CHECK(service.state()["profiles"].empty());

    const auto saved = service.save_connection(
        "Proxy", "sbtun", "proxy", "direct", "Home",
        "trojan://pw@example.com:443?sni=example.com#Home", "");
    CHECK(saved.singbox_checked);
    CHECK(saved.singbox_reloaded);
    CHECK(saved.keenetic_applied);
    const auto config = nlohmann::json::parse(read_file(paths.singbox_config));
    CHECK(config["inbounds"][0]["interface_name"] == "sbtun");
    CHECK(config["inbounds"][0]["auto_route"] == false);
    CHECK(config["outbounds"][0]["type"] == "trojan");
    CHECK(config["route"]["final"] == config["outbounds"][0]["tag"]);
    const auto state = saved.state["profiles"][0];
    CHECK(state["keenetic_tag"] == "proxy");
    CHECK(state["interface"] == "sbtun");
    CHECK(state["connections"][0]["server"] == "example.com");
    CHECK(state["connections"][0].dump().find("pw") == std::string::npos);
    std::filesystem::remove_all(root);
}

namespace {

struct SubscriptionFixture {
    const std::filesystem::path dir;
    SingboxPaths paths;
    std::int64_t now{1000};
    std::string url{"https://example.test/secret-token"};
    std::string body{"trojan://pw@a.example:443#A\ntrojan://pw@b.example:443#B"};
    int downloads{0};
    int checks{0};
    int restarts{0};
    bool download_fails{false};
    bool check_fails{false};
    bool restart_fails_once{false};
    std::function<void()> on_download;
    SingboxLinkService service;

    static std::filesystem::path temp_dir() {
        auto pattern = (std::filesystem::temp_directory_path() / "keen-pbr-subscriptions-XXXXXX").string();
        if (!::mkdtemp(pattern.data())) throw std::runtime_error("mkdtemp failed");
        return pattern;
    }

    SubscriptionFixture()
        : dir(temp_dir()), paths{(dir / "links.json").string(), (dir / "config.json").string()},
          service(paths,
                  [this](const std::string&, std::string* output) {
                      ++checks;
                      *output = url;
                      return check_fails ? 1 : 0;
                  },
                  {},
                  [this] {
                      ++restarts;
                      return !std::exchange(restart_fails_once, false);
                  },
                  [this](const std::string& requested, const SubscriptionStopFn&) {
                      ++downloads;
                      if (on_download) on_download();
                      if (download_fails) throw std::runtime_error(requested);
                      return body;
                  },
                  [this] { return now; }) {}

    ~SubscriptionFixture() { std::filesystem::remove_all(dir); }

    void save(int interval = 1) {
        REQUIRE(service.save_subscription("Proxy", "sbtun", "proxy", "direct", url, interval).singbox_reloaded);
    }

    nlohmann::json profile() { return service.state()["profiles"].at(0); }
    nlohmann::json tick() { return service.refresh_subscriptions("", true); }
};

}  // namespace

TEST_CASE("subscription scheduler honors due time without writing or restarting for identical nodes") {
    SubscriptionFixture f;
    f.save();
    CHECK(f.profile()["has_subscription"] == true);
    CHECK(f.profile().dump().find("secret-token") == std::string::npos);
    const auto config = read_file(f.paths.singbox_config);
    const auto store = read_file(f.paths.store);
    const auto store_mtime = std::filesystem::last_write_time(f.paths.store);
    const auto config_mtime = std::filesystem::last_write_time(f.paths.singbox_config);
    CHECK(f.profile()["next_refresh"] == 4600);
    f.now = 4599;
    CHECK(f.tick().empty());
    CHECK(f.downloads == 1);

    f.body = "trojan://pw@b.example:443#B\ntrojan://pw@a.example:443#A";
    f.now = 4600;
    REQUIRE(f.tick().at(0).at("ok") == true);
    CHECK(f.downloads == 2);
    CHECK(f.checks == 1);
    CHECK(f.restarts == 1);
    CHECK(f.profile()["last_refresh"] == 4600);
    CHECK(f.profile()["next_refresh"] == 8200);
    CHECK(std::filesystem::last_write_time(f.paths.store) == store_mtime);
    CHECK(std::filesystem::last_write_time(f.paths.singbox_config) == config_mtime);
    CHECK(read_file(f.paths.store) == store);
    CHECK(read_file(f.paths.singbox_config) == config);
    CHECK(f.tick().empty());
}

TEST_CASE("subscription failures back off and recover without exposing the URL") {
    SubscriptionFixture f;
    f.save();
    f.now = 4600;
    f.download_fails = true;
    REQUIRE(f.tick().at(0).at("ok") == false);
    CHECK(f.profile()["last_refresh"] == 1000);
    CHECK(f.profile()["next_refresh"] == 4900);
    CHECK(f.service.subscription_errors().at("proxy") == "subscription download failed");
    f.now = 4899;
    CHECK(f.tick().empty());
    CHECK(f.downloads == 2);

    f.download_fails = false;
    f.now = 4900;
    // A change in credentials (same node ID) must still update sing-box.
    f.body = "trojan://new@a.example:443#A";
    REQUIRE(f.tick().at(0).at("ok") == true);
    CHECK(f.service.subscription_errors().empty());
    CHECK(f.profile()["next_refresh"] == 8500);
    CHECK(f.checks == 2);
    CHECK(f.restarts == 2);
    CHECK(read_file(f.paths.singbox_config).find("\"password\": \"new\"") != std::string::npos);
    CHECK(SingboxLinkService::load_profiles(f.paths.store).at(0).last_refresh == 4900);
}

TEST_CASE("forced refresh works with automatic refresh disabled and protects the selected node") {
    SubscriptionFixture f;
    f.save(0);
    f.now = 4600;
    CHECK(f.tick().empty());
    CHECK(f.profile()["next_refresh"].is_null());
    f.body = "trojan://pw@b.example:443#B";
    const auto config = read_file(f.paths.singbox_config);
    const auto store = read_file(f.paths.store);
    REQUIRE(f.service.refresh_subscriptions("proxy", false).at(0).at("ok") == false);
    CHECK(f.service.subscription_errors().at("proxy").find("selected node disappeared") != std::string::npos);
    CHECK(f.checks == 1);
    CHECK(f.restarts == 1);
    CHECK(read_file(f.paths.singbox_config) == config);
    CHECK(read_file(f.paths.store) == store);
    CHECK(f.profile()["last_refresh"] == 1000);
    CHECK_THROWS(f.service.refresh_subscriptions("unknown", false));
}

TEST_CASE("subscription check and restart failures roll back without propagating child output") {
    SubscriptionFixture f;
    f.save();
    f.body = "trojan://new@a.example:443#A";
    const auto config = read_file(f.paths.singbox_config);
    const auto store = read_file(f.paths.store);
    f.now = 4600;
    f.check_fails = true;
    REQUIRE(f.tick().at(0).at("ok") == false);
    CHECK(f.restarts == 1);
    CHECK(f.profile()["last_error"].dump().find("secret-token") == std::string::npos);
    CHECK(read_file(f.paths.singbox_config) == config);
    CHECK(read_file(f.paths.store) == store);

    f.check_fails = false;
    f.restart_fails_once = true;
    // Manual requests bypass the retry delay.
    REQUIRE(f.service.refresh_subscriptions("proxy", false).at(0).at("ok") == false);
    CHECK(f.restarts == 3);
    CHECK(read_file(f.paths.singbox_config) == config);
    CHECK(read_file(f.paths.store) == store);
}

TEST_CASE("subscription download does not block reads and excludes concurrent updates") {
    SubscriptionFixture f;
    f.save();
    f.now = 4600;
    std::promise<void> downloading, release;
    auto released = release.get_future();
    f.on_download = [&] {
        downloading.set_value();
        released.wait_for(std::chrono::seconds{5});
    };
    auto background = std::async(std::launch::async, [&] {
        return f.service.refresh_subscriptions("proxy", false);
    });
    downloading.get_future().wait();
    CHECK(f.profile()["last_refresh"] == 1000);
    CHECK_THROWS_AS(f.service.refresh_subscriptions("proxy", false), SingboxBusy);
    CHECK_THROWS_AS(f.service.set_mode("proxy", "selector", ""), SingboxBusy);
    release.set_value();
    CHECK(background.get().at(0).at("ok") == true);
}
