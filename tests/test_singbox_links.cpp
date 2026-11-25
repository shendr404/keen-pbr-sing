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
    SubscriptionParseInfo info;
    const auto nodes = parse_subscription(encoded, &info);
    REQUIRE(nodes.size() == 1);
    CHECK(nodes[0].link.protocol == "vless");
    CHECK(nodes[0].link.server == "server.example");

    const auto clash = R"({
      "proxies": [
        {"name":"a","type":"trojan","server":"t.example","port":443,"password":"pw","sni":"t.example"},
        {"name":"skip","type":"wireguard","server":"w.example","port":1}
      ]
    })";
    info = {};
    const auto parsed = parse_subscription(clash, &info);
    REQUIRE(parsed.size() == 1);
    CHECK(parsed[0].link.protocol == "trojan");
    CHECK(info.skipped == 1);
}

TEST_CASE("reality defaults utls, hysteria2 keeps obfs, and xhttp is skipped") {
    const auto reality = parse_proxy_link(
        "vless://11111111-1111-1111-1111-111111111111@example.com:443"
        "?security=reality&pbk=abc&sid=abcd&sni=www.example.com#R");
    CHECK(reality.outbound.at("tls").at("reality").at("enabled") == true);
    CHECK(reality.outbound.at("tls").at("utls").at("enabled") == true);
    CHECK(reality.outbound.at("tls").at("utls").at("fingerprint") == "chrome");
    CHECK_FALSE(reality.outbound.contains("transport"));

    const auto firefox = parse_proxy_link(
        "vless://11111111-1111-1111-1111-111111111111@example.com:443"
        "?security=reality&pbk=abc&fp=firefox#R");
    CHECK(firefox.outbound.at("tls").at("utls").at("fingerprint") == "firefox");

    const auto encrypted = parse_proxy_link(
        "vless://11111111-1111-1111-1111-111111111111@example.com:443"
        "?encryption=mlkem768x25519plus&security=tls&sni=example.com#E");
    CHECK_FALSE(encrypted.outbound.contains("encryption"));
    CHECK_FALSE(encrypted.warnings.empty());

    const auto hy2 = parse_proxy_link("hy2://secret@10.0.0.8:443?obfs=salamander&obfs-password=obfs#fi");
    CHECK(hy2.outbound.at("obfs").at("type") == "salamander");
    CHECK(hy2.outbound.at("obfs").at("password") == "obfs");

    CHECK_THROWS_AS(parse_proxy_link(
                        "vless://11111111-1111-1111-1111-111111111111@example.com:443?type=xhttp#X"),
                    UnsupportedProxyTransport);

    SubscriptionParseInfo info;
    const auto nodes = parse_subscription(
        "vless://11111111-1111-1111-1111-111111111111@good.example:443?security=reality&pbk=abc&sni=www.example.com#Good\n"
        "vless://11111111-1111-1111-1111-111111111111@xhttp.example:443?type=xhttp#Skip\n"
        "vless://11111111-1111-1111-1111-111111111111@split.example:443?type=splithttp#Skip2\n"
        "hysteria2://secret@hy.example:443?obfs=salamander&obfs-password=obfs#Hy\n",
        &info);
    REQUIRE(nodes.size() == 2);
    CHECK(info.skipped == 2);
    CHECK(info.unsupported_transports.at("xhttp") == 1);
    CHECK(info.unsupported_transports.at("splithttp") == 1);
    CHECK(nodes[0].link.outbound.at("tls").at("utls").at("fingerprint") == "chrome");
    CHECK(nodes[1].link.protocol == "hysteria2");
    CHECK(nodes[1].link.outbound.at("obfs").at("type") == "salamander");
}

TEST_CASE("sing-box check errors name a missing binary and redact secrets") {
    CHECK(format_singbox_check_error(127, false, "") == "sing-box binary is not available");
    CHECK(format_singbox_check_error(-1, false, "exec failed") == "sing-box binary is not available");
    CHECK(format_singbox_check_error(1, true, "FATAL ignored") == "sing-box check failed: timed out");
    const auto message = format_singbox_check_error(
        1, false,
        "FATAL[0000] decode config: password=supersecret uuid 11111111-1111-1111-1111-111111111111\n");
    CHECK(message.find("FATAL") != std::string::npos);
    CHECK(message.find("supersecret") == std::string::npos);
    CHECK(message.find("11111111") == std::string::npos);
    CHECK(message.find("https://example.test/secret-token") == std::string::npos);
    const auto url = format_singbox_check_error(1, false, "https://example.test/secret-token\n");
    CHECK(url.find("secret-token") == std::string::npos);
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

    SingboxProfileDraft draft;
    draft.name = "Proxy";
    draft.interface_name = "sbtun";
    draft.keenetic_tag = "proxy";
    draft.mode = "selector";
    REQUIRE(service.save_profile(draft).keenetic_applied);
    CHECK_THROWS_WITH(service.add_connections(
                          "proxy",
                          {"vless://11111111-1111-1111-1111-111111111111@example.com:443#Home"}, "Home"),
                      "sing-box check failed: bad outbound");
    CHECK(read_file(paths.singbox_config).find("\"old\":true") != std::string::npos);
    CHECK(service.state()["profiles"][0]["connections"].empty());

    draft.id = "proxy";
    draft.mode = "direct";
    REQUIRE(service.save_profile(draft).keenetic_applied);
    const auto saved = service.add_connections(
        "proxy", {"trojan://pw@example.com:443?sni=example.com#Home"}, "Home");
    CHECK(saved.singbox_checked);
    CHECK(saved.singbox_reloaded);
    CHECK(saved.keenetic_applied);
    const auto config = nlohmann::json::parse(read_file(paths.singbox_config));
    CHECK(config["inbounds"][0]["interface_name"] == "sbtun");
    CHECK(config["inbounds"][0]["auto_route"] == false);
    CHECK(config["inbounds"][0]["stack"] == "gvisor");
    CHECK(config["inbounds"][0]["mtu"] == 1400);
    CHECK(config["outbounds"][0]["type"] == "trojan");
    CHECK(config["route"]["rules"][0]["outbound"] == config["outbounds"][0]["tag"]);
    CHECK(config["route"]["final"] == "kpbr-direct");
    CHECK(config.contains("old"));
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
        SingboxProfileDraft draft;
        draft.name = "Proxy";
        draft.interface_name = "sbtun";
        draft.keenetic_tag = "proxy";
        draft.mode = "direct";
        REQUIRE(service.save_profile(draft).keenetic_applied);
        REQUIRE(service.save_subscription("proxy", url, interval, "").singbox_reloaded);
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

TEST_CASE("sing-box merge keeps unmanaged config and refuses a taken TUN name") {
    const auto root = std::filesystem::temp_directory_path() / "keen-pbr-singbox-merge";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    SingboxPaths paths;
    paths.store = (root / "links.json").string();
    paths.singbox_config = (root / "config.json").string();
    const char* manual = R"({
      "log": {"level": "info"},
      "dns": {"servers": [{"tag": "local", "address": "local"}]},
      "inbounds": [
        {"type": "tun", "tag": "tun-in", "interface_name": "sbtun",
         "address": ["172.19.0.1/30", "fdfe:dcba:9876::1/126"], "mtu": 1400, "stack": "gvisor"},
        {"type": "mixed", "tag": "mixed-in", "listen": "192.168.1.1", "listen_port": 2080}
      ],
      "outbounds": [
        {"type": "hysteria2", "tag": "proxy", "server": "hy.example", "server_port": 443, "password": "keep"},
        {"type": "direct", "tag": "direct"}
      ],
      "route": {"final": "direct", "rules": [{"inbound": "tun-in", "outbound": "proxy"}]}
    })";
    std::ofstream(paths.singbox_config) << manual;
    const auto before = read_file(paths.singbox_config);
    SingboxLinkService service(
        paths, [](const std::string&, std::string*) { return 0; }, [](const SingboxProfile&, bool) {}, [] { return true; });

    SingboxProfileDraft taken;
    taken.name = "Taken";
    taken.interface_name = "sbtun";
    taken.keenetic_tag = "taken";
    taken.mode = "selector";
    CHECK_THROWS_WITH(service.save_profile(taken),
                      "TUN interface sbtun already exists in the sing-box config and is not managed by keen-pbr. "
                      "The existing config was not changed. Choose another interface name.");
    CHECK(read_file(paths.singbox_config) == before);
    CHECK_FALSE(std::filesystem::exists(paths.singbox_config + ".bak-keenpbr-original"));

    SingboxProfileDraft draft;
    draft.name = "Second";
    draft.interface_name = "sbtun2";
    draft.keenetic_tag = "proxy2";
    draft.mode = "urltest";
    draft.urltest_url = "https://www.gstatic.com/generate_204";
    draft.urltest_interval = "1m";
    draft.tun_stack = "system";
    draft.tun_mtu = 1280;
    draft.tun_address = "172.19.8.1/30, fdfe:dcba:9876::5/126";
    REQUIRE(service.save_profile(draft).keenetic_applied);
    REQUIRE(service.add_connections("proxy2", {"trojan://pw@a.example:443#A\ntrojan://pw@b.example:443#B"}, "")
                .singbox_checked);
    const auto config = nlohmann::json::parse(read_file(paths.singbox_config));
    CHECK(config["dns"]["servers"][0]["tag"] == "local");
    CHECK(config["log"]["level"] == "info");
    CHECK(config["inbounds"][0]["tag"] == "tun-in");
    CHECK(config["inbounds"][0]["address"][1] == "fdfe:dcba:9876::1/126");
    CHECK(config["inbounds"][0]["mtu"] == 1400);
    CHECK(config["inbounds"][0]["stack"] == "gvisor");
    CHECK(config["inbounds"][1]["listen"] == "192.168.1.1");
    CHECK(config["inbounds"][2]["interface_name"] == "sbtun2");
    CHECK(config["inbounds"][2]["stack"] == "system");
    CHECK(config["inbounds"][2]["mtu"] == 1280);
    CHECK(config["inbounds"][2]["address"][1] == "fdfe:dcba:9876::5/126");
    CHECK(config["outbounds"][0]["type"] == "hysteria2");
    CHECK(config["outbounds"][0]["password"] == "keep");
    CHECK(config["outbounds"][1]["tag"] == "direct");
    CHECK(config["route"]["final"] == "direct");
    CHECK(config["route"]["rules"][0]["outbound"] == "proxy");
    bool saw_group = false;
    for (const auto& outbound : config["outbounds"]) {
        if (outbound.value("type", "") == "urltest") {
            saw_group = true;
            CHECK(outbound["interval"] == "1m");
            CHECK(outbound["outbounds"].size() == 2);
        }
    }
    CHECK(saw_group);
    CHECK(std::filesystem::exists(paths.singbox_config + ".bak-keenpbr-original"));
    CHECK(read_file(paths.singbox_config + ".bak-keenpbr-original") == before);

    SingboxProfileDraft other;
    other.name = "Other";
    other.interface_name = "sbtun2";
    other.keenetic_tag = "other";
    CHECK_THROWS_WITH(service.save_profile(other), "TUN interface is already used");
    other.interface_name = "sbtun3";
    other.keenetic_tag = "proxy2";
    CHECK_THROWS_WITH(service.save_profile(other), "keen-pbr tag is already used");

    const auto removed_id = service.state()["profiles"][0]["connections"][0]["id"].get<std::string>();
    REQUIRE(service.remove_connection("proxy2", removed_id).singbox_checked);
    const auto still = nlohmann::json::parse(read_file(paths.singbox_config));
    CHECK(still["outbounds"][0]["password"] == "keep");
    CHECK(still["inbounds"][0]["interface_name"] == "sbtun");
    CHECK(service.state()["suggested_interface"] == "sbtun3");
    std::filesystem::remove_all(root);
}

TEST_CASE("empty TUN name and address pick the next free values") {
    const auto root = std::filesystem::temp_directory_path() / "keen-pbr-singbox-auto-tun";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    SingboxPaths paths;
    paths.store = (root / "links.json").string();
    paths.singbox_config = (root / "config.json").string();
    std::ofstream(paths.singbox_config) << R"({
      "inbounds": [{
        "type": "tun", "tag": "tun-in", "interface_name": "sbtun",
        "address": ["172.19.1.1/30", "fdfe:dcba:9876::1/126"], "mtu": 1400, "stack": "gvisor"
      }],
      "outbounds": [{"type": "direct", "tag": "direct"}]
    })";
    SingboxLinkService service(
        paths, [](const std::string&, std::string*) { return 0; }, [](const SingboxProfile&, bool) {}, [] { return true; });
    CHECK(service.state()["suggested_interface"] == "sbtun2");

    SingboxProfileDraft draft;
    draft.name = "Auto";
    draft.keenetic_tag = "auto";
    draft.mode = "direct";
    REQUIRE(service.save_profile(draft).state["profiles"][0]["interface"] == "sbtun2");
    REQUIRE(service.add_connections("auto", {"trojan://pw@a.example:443#A"}, "A").singbox_checked);
    const auto config = nlohmann::json::parse(read_file(paths.singbox_config));
    CHECK(config["inbounds"][0]["interface_name"] == "sbtun");
    CHECK(config["inbounds"][0]["address"][0] == "172.19.1.1/30");
    CHECK(config["inbounds"][1]["interface_name"] == "sbtun2");
    CHECK(config["inbounds"][1]["address"][0] == "172.19.2.1/30");
    CHECK(service.state()["profiles"][0]["tun_address"] == "");
    std::filesystem::remove_all(root);
}

TEST_CASE("a profile created with a link gets a TUN and rolls back when keenetic apply fails") {
    const auto root = std::filesystem::temp_directory_path() / "keen-pbr-singbox-one-step";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    SingboxPaths paths;
    paths.store = (root / "links.json").string();
    paths.singbox_config = (root / "config.json").string();
    const char* manual = R"({"inbounds":[{"type":"tun","tag":"tun-in","interface_name":"sbtun","address":["172.19.1.1/30"]}]})";
    std::ofstream(paths.singbox_config) << manual;
    const auto before = read_file(paths.singbox_config);
    int keenetic_calls = 0;
    SingboxLinkService failing(
        paths,
        [](const std::string&, std::string*) { return 0; },
        [&](const SingboxProfile&, bool) {
            ++keenetic_calls;
            throw std::runtime_error("invalid runtime transition: running -> running");
        },
        [] { return true; });
    SingboxProfileDraft draft;
    draft.name = "Proxy";
    draft.keenetic_tag = "proxy";
    draft.mode = "direct";
    CHECK_THROWS_WITH(failing.save_profile(draft, {"trojan://pw@a.example:443#A"}),
                      "invalid runtime transition: running -> running");
    CHECK(keenetic_calls == 1);
    CHECK(read_file(paths.singbox_config) == before);
    CHECK(failing.state()["profiles"].empty());

    SingboxLinkService service(
        paths, [](const std::string&, std::string*) { return 0; },
        [](const SingboxProfile& profile, bool present) {
            CHECK(present);
            CHECK(profile.interface_name == "sbtun2");
            CHECK(profile.keenetic_tag == "proxy");
        },
        [] { return true; });
    const auto saved = service.save_profile(draft, {"trojan://pw@a.example:443#A"});
    CHECK(saved.singbox_checked);
    CHECK(saved.singbox_reloaded);
    const auto config = nlohmann::json::parse(read_file(paths.singbox_config));
    CHECK(config["inbounds"][0]["interface_name"] == "sbtun");
    CHECK(config["inbounds"][1]["interface_name"] == "sbtun2");
    CHECK(config["inbounds"][1]["tag"].get<std::string>().rfind("kpbr-tun-", 0) == 0);
    bool saw_trojan = false;
    for (const auto& outbound : config["outbounds"]) {
        if (outbound.value("type", "") == "trojan") saw_trojan = true;
    }
    CHECK(saw_trojan);
    REQUIRE(saved.state["profiles"].size() == 1);
    CHECK(saved.state["profiles"][0]["connections"].size() == 1);
    std::filesystem::remove_all(root);
}
