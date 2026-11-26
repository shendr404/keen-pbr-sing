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

    CHECK_THROWS_WITH(parse_proxy_link(
                          "vless://11111111-1111-1111-1111-111111111111@example.com:443"
                          "?encryption=mlkem768x25519plus.native.600s&security=tls&sni=example.com#E"),
                      "VLESS Encryption (mlkem768x25519plus) не поддерживается sing-box");
    const auto plain = parse_proxy_link(
        "vless://11111111-1111-1111-1111-111111111111@example.com:443?encryption=none&security=tls&sni=example.com#E");
    CHECK(plain.protocol == "vless");
    CHECK_FALSE(plain.outbound.contains("encryption"));

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
    REQUIRE(info.skipped_nodes.size() == 2);
    CHECK(info.skipped_nodes[0].name == "Skip");
    CHECK(info.skipped_nodes[0].reason.find("xhttp") != std::string::npos);
    CHECK(nodes[0].link.outbound.at("tls").at("utls").at("fingerprint") == "chrome");
    CHECK(nodes[1].link.protocol == "hysteria2");
    CHECK(nodes[1].link.outbound.at("obfs").at("type") == "salamander");

    info = {};
    const auto mixed = parse_subscription(
        "vless://11111111-1111-1111-1111-111111111111@good.example:443?security=tls&sni=good.example#Good\n"
        "vless://11111111-1111-1111-1111-111111111111@enc.example:443?encryption=mlkem768x25519plus&security=reality&pbk=abc#Enc\n",
        &info);
    REQUIRE(mixed.size() == 1);
    CHECK(mixed[0].link.server == "good.example");
    REQUIRE(info.skipped_nodes.size() == 1);
    CHECK(info.skipped_nodes[0].name == "Enc");
    CHECK(info.skipped_nodes[0].protocol == "vless");
    CHECK(info.skipped_nodes[0].server == "enc.example");
    CHECK(info.skipped_nodes[0].reason == "VLESS Encryption (mlkem768x25519plus) не поддерживается sing-box");
}

TEST_CASE("node names become keen-pbr-sx tags") {
    CHECK(sanitize_keenetic_tag("es1-3") == "es1_3");
    CHECK(sanitize_keenetic_tag("HU 1.3 Венгрия") == "hu_1_3_vengriya");
    CHECK(sanitize_keenetic_tag("1abc") == "n_1abc");
    CHECK(sanitize_keenetic_tag("3") == "n_3");
    CHECK(sanitize_keenetic_tag("___") == "n_");
    CHECK(sanitize_keenetic_tag("") == "n_");
    const auto first = sanitize_keenetic_tag("es1-3");
    CHECK(first == "es1_3");
    const auto second = first + "_2";
    CHECK(second == "es1_3_2");
    CHECK(sanitize_keenetic_tag(second) == "es1_3_2");
}

TEST_CASE("duplicate node names get an underscore suffix") {
    const auto root = std::filesystem::temp_directory_path() / "keen-pbr-singbox-tags";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    SingboxPaths paths;
    paths.store = (root / "links.json").string();
    paths.singbox_config = (root / "config.json").string();
    SingboxLinkService service(
        paths, [](const std::string&, std::string*) { return 0; }, [](const KeeneticSyncRequest&) {},
        [] { return true; });
    const auto saved = service.add_links(
        {"trojan://pw@a.example:443#es1-3\ntrojan://pw@b.example:443#es1-3"}, {});
    REQUIRE(saved.state["nodes"].size() == 2);
    CHECK(saved.state["nodes"][0]["tag"] == "es1_3");
    CHECK(saved.state["nodes"][1]["tag"] == "es1_3_2");
    const auto config = nlohmann::json::parse(read_file(paths.singbox_config));
    CHECK(config["inbounds"][0]["tag"].get<std::string>().rfind("kpbr-tun-", 0) == 0);
    CHECK(config["outbounds"][0]["tag"].get<std::string>().find('-') != std::string::npos);
    for (const auto& outbound : config["outbounds"]) {
        const auto tag = outbound.value("tag", "");
        if (tag.rfind("kpbr-", 0) != 0) continue;
        const auto token = tag.substr(tag.rfind('-') + 1);
        CHECK(token.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") == std::string::npos);
    }
    std::filesystem::remove_all(root);
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


TEST_CASE("a keen-pbr-sx outbound referenced by a rule or group cannot be removed") {
    const auto config = nlohmann::json::parse(R"({
      "route": {"rules": [{"outbound": "hu-1"}, {"outbound": "other"}]},
      "outbounds": [
        {"type": "urltest", "tag": "proxy", "outbound_groups": [{"members": [{"outbound": "hu-1"}]}]},
        {"type": "icmptest", "tag": "probe", "outbound_groups": [{"outbounds": ["other"]}]}
      ]
    })");
    const auto message = outbound_reference_message(config, "hu-1");
    CHECK(message.find("route rule 1") != std::string::npos);
    CHECK(message.find("urltest proxy") != std::string::npos);
    CHECK(outbound_reference_message(config, "other").find("icmptest probe") != std::string::npos);
    CHECK(outbound_reference_message(config, "free").empty());
}

TEST_CASE("pasted links become separate interfaces and roll back together") {
    const auto root = std::filesystem::temp_directory_path() / "keen-pbr-singbox-nodes";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    SingboxPaths paths;
    paths.store = (root / "links.json").string();
    paths.singbox_config = (root / "config.json").string();
    const char* manual = R"({"inbounds":[{"type":"tun","tag":"tun-in","interface_name":"sbtun","address":["172.19.1.1/30"]}]})";
    std::ofstream(paths.singbox_config) << manual;
    const auto before = read_file(paths.singbox_config);
    int calls = 0;
    SingboxLinkService failing(
        paths, [](const std::string&, std::string*) { return 0; },
        [&](const KeeneticSyncRequest&) {
            ++calls;
            throw std::runtime_error("invalid runtime transition: running -> running");
        },
        [] { return true; });
    CHECK_THROWS_WITH(failing.add_links({"trojan://pw@a.example:443#hu-1"}, {}),
                      "invalid runtime transition: running -> running");
    CHECK(calls == 1);
    CHECK(read_file(paths.singbox_config) == before);
    CHECK(failing.state()["nodes"].empty());

    SingboxLinkService service(
        paths, [](const std::string&, std::string*) { return 0; },
        [](const KeeneticSyncRequest& request) {
            if (!request.removed_tags.empty()) {
                throw std::runtime_error("outbound hu_1 is used by route rule 1");
            }
            REQUIRE(request.nodes.size() == 2);
            CHECK(request.nodes[0].tag == "hu_1");
            CHECK(request.nodes[0].interface_name == "sbtun2");
            CHECK(request.nodes[1].tag == "hu_1_2");
            CHECK(request.nodes[1].interface_name == "sbtun3");
        },
        [] { return true; });
    const auto saved = service.add_links(
        {"trojan://pw@a.example:443#hu-1\ntrojan://pw@b.example:443#hu-1"}, {});
    CHECK(saved.singbox_checked);
    CHECK(saved.singbox_reloaded);
    const auto config = nlohmann::json::parse(read_file(paths.singbox_config));
    CHECK(config["inbounds"][0]["interface_name"] == "sbtun");
    CHECK(config["inbounds"][1]["interface_name"] == "sbtun2");
    CHECK(config["inbounds"][1]["address"][0] == "172.19.2.1/30");
    CHECK(config["inbounds"][2]["interface_name"] == "sbtun3");
    CHECK(config["inbounds"][2]["address"][0] == "172.19.3.1/30");
    CHECK(config["route"]["rules"][0]["inbound"].get<std::string>().rfind("kpbr-tun-", 0) == 0);
    CHECK(config["route"]["rules"][0]["outbound"] == config["outbounds"][0]["tag"]);
    int trojans = 0;
    for (const auto& outbound : config["outbounds"]) {
        if (outbound.value("type", "") == "trojan") ++trojans;
        CHECK(outbound.value("type", "") != "urltest");
        CHECK(outbound.value("type", "") != "selector");
    }
    CHECK(trojans == 2);
    CHECK(saved.state["nodes"].size() == 2);
    CHECK(saved.state.dump().find("pw") == std::string::npos);

    const auto id = saved.state["nodes"][0]["id"].get<std::string>();
    CHECK_THROWS_WITH(service.remove_node(id), "outbound hu_1 is used by route rule 1");
    CHECK(read_file(paths.singbox_config).find("sbtun2") != std::string::npos);
    CHECK(service.state()["nodes"].size() == 2);
    std::filesystem::remove_all(root);
}

TEST_CASE("subscription selection creates only ticked nodes and refresh keeps the rest") {
    const auto root = std::filesystem::temp_directory_path() / "keen-pbr-singbox-sub-nodes";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    SingboxPaths paths;
    paths.store = (root / "links.json").string();
    paths.singbox_config = (root / "config.json").string();
    std::int64_t now = 1000;
    std::string body = "trojan://pw@a.example:443#hu-1\n"
                       "trojan://pw@b.example:443#hu-2\n"
                       "vless://11111111-1111-1111-1111-111111111111@x.example:443?type=xhttp#skip";
    int downloads = 0;
    SingboxLinkService service(
        paths, [](const std::string&, std::string*) { return 0; }, [](const KeeneticSyncRequest&) {},
        [] { return true; },
        [&](const std::string&, const SubscriptionStopFn&) {
            ++downloads;
            return body;
        },
        [&] { return now; });
    const auto preview = service.preview_subscription("https://example.test/sub");
    REQUIRE(preview["nodes"].size() == 3);
    CHECK(preview["nodes"][2]["skipped"] == true);
    CHECK(preview["nodes"][2]["reason"].get<std::string>().find("xhttp") != std::string::npos);
    std::vector<std::string> selected{preview["nodes"][0]["identity"].get<std::string>()};
    const auto saved = service.save_subscription("https://example.test/sub", 1, selected, {});
    REQUIRE(saved.state["nodes"].size() == 1);
    CHECK(saved.state["nodes"][0]["tag"] == "hu_1");
    CHECK(saved.state["nodes"][0]["source"] == "subscription");
    CHECK(saved.state["subscriptions"][0]["new_nodes"] == 1);
    CHECK(saved.state.dump().find("https://") == std::string::npos);
    CHECK(saved.state.dump().find("/sub") == std::string::npos);
    const auto config_after_select = read_file(paths.singbox_config);
    CHECK(config_after_select.find("b.example") == std::string::npos);
    CHECK(config_after_select.find("a.example") != std::string::npos);

    body = "trojan://new@a.example:8443#hu-1\ntrojan://pw@c.example:443#hu-3";
    now = 4600;
    REQUIRE(service.refresh_subscriptions("", true).at(0).at("ok") == true);
    CHECK(downloads == 3);
    const auto state = service.state();
    CHECK(state["nodes"].size() == 1);
    CHECK(state["nodes"][0]["server"] == "a.example");
    CHECK(state["nodes"][0]["port"] == 8443);
    CHECK(state["nodes"][0]["missing"] == false);
    CHECK(state["subscriptions"][0]["new_nodes"] == 1);
    CHECK(read_file(paths.singbox_config).find("\"password\": \"new\"") != std::string::npos);
    CHECK(read_file(paths.singbox_config).find("c.example") == std::string::npos);

    const auto store_before = read_file(paths.store);
    const auto config_before = read_file(paths.singbox_config);
    body = "trojan://pw@c.example:443#hu-3";
    now = 8200;
    REQUIRE(service.refresh_subscriptions(state["subscriptions"][0]["id"].get<std::string>(), false).at(0).at("ok") == true);
    const auto missing = service.state();
    CHECK(missing["nodes"].size() == 1);
    CHECK(missing["nodes"][0]["missing"] == true);
    CHECK(missing["nodes"][0]["server"] == "a.example");
    CHECK(read_file(paths.singbox_config) == config_before);
    CHECK(missing["subscriptions"][0]["new_nodes"] == 1);
    CHECK(read_file(paths.store) != store_before);

    body = "trojan://pw@c.example:443#hu-3";
    const auto unchanged_config = read_file(paths.singbox_config);
    const auto unchanged_store = read_file(paths.store);
    const auto store_mtime = std::filesystem::last_write_time(paths.store);
    REQUIRE(service.refresh_subscriptions("", false).at(0).at("ok") == true);
    CHECK(std::filesystem::last_write_time(paths.store) == store_mtime);
    CHECK(read_file(paths.singbox_config) == unchanged_config);
    CHECK(read_file(paths.store) == unchanged_store);
    std::filesystem::remove_all(root);
}

TEST_CASE("subscription refresh backs off and rolls back a failed check") {
    const auto root = std::filesystem::temp_directory_path() / "keen-pbr-singbox-sub-rollback";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    SingboxPaths paths;
    paths.store = (root / "links.json").string();
    paths.singbox_config = (root / "config.json").string();
    std::int64_t now = 1000;
    std::string body = "trojan://pw@a.example:443#hu-1";
    bool download_fails = false;
    bool check_fails = false;
    int checks = 0;
    SingboxLinkService service(
        paths,
        [&](const std::string&, std::string* error) {
            ++checks;
            if (error) *error = "https://example.test/secret-token";
            return check_fails ? 1 : 0;
        },
        [](const KeeneticSyncRequest&) {}, [] { return true; },
        [&](const std::string&, const SubscriptionStopFn&) {
            if (download_fails) throw std::runtime_error("https://example.test/secret-token");
            return body;
        },
        [&] { return now; });
    const auto saved = service.save_subscription("https://example.test/sub", 1, {}, {});
    const auto identity = saved.state["subscriptions"][0]["catalog"][0]["identity"].get<std::string>();
    REQUIRE(service.select_subscription(saved.state["subscriptions"][0]["id"], {identity}, {}).state["nodes"].size() == 1);
    const auto id = service.state()["subscriptions"][0]["id"].get<std::string>();
    now = 4600;
    download_fails = true;
    REQUIRE(service.refresh_subscriptions("", true).at(0).at("ok") == false);
    CHECK(service.subscription_errors().at(id) == "subscription download failed");
    CHECK(service.state()["subscriptions"][0]["next_refresh"] == 4900);
    CHECK(service.state().dump().find("secret-token") == std::string::npos);
    now = 4899;
    CHECK(service.refresh_subscriptions("", true).empty());

    download_fails = false;
    check_fails = true;
    body = "trojan://new@a.example:443#hu-1";
    const auto config = read_file(paths.singbox_config);
    const auto store = read_file(paths.store);
    now = 4900;
    REQUIRE(service.refresh_subscriptions(id, false).at(0).at("ok") == false);
    CHECK(read_file(paths.singbox_config) == config);
    CHECK(read_file(paths.store) == store);
    CHECK(service.state()["nodes"][0]["port"] == 443);
    CHECK(checks >= 2);
    std::filesystem::remove_all(root);
}

TEST_CASE("sing-box merge keeps unmanaged config and an old profile store migrates") {
    const auto root = std::filesystem::temp_directory_path() / "keen-pbr-singbox-merge-nodes";
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
    std::ofstream(paths.store) << R"({
      "profiles": [{
        "id": "proxy", "name": "Proxy", "interface": "sbtun9", "keenetic_tag": "legacy",
        "tun_stack": "system", "tun_mtu": 1280, "tun_address": "172.19.8.1/30",
        "connections": [{
          "id": "old", "name": "Home", "protocol": "trojan", "server": "a.example", "port": 443,
          "link": "trojan://pw@a.example:443#Home"
        }]
      }]
    })";
    const auto before = read_file(paths.singbox_config);
    SingboxLinkService service(
        paths, [](const std::string&, std::string*) { return 0; }, [](const KeeneticSyncRequest&) {}, [] { return true; });
    const auto migrated = service.state();
    REQUIRE(migrated["nodes"].size() == 1);
    CHECK(migrated["nodes"][0]["tag"] == "legacy");
    CHECK(migrated["nodes"][0]["interface"] == "sbtun9");
    SingboxNodePatch patch;
    patch.id = service.state()["nodes"][0]["id"].get<std::string>();
    patch.interface_name = "sbtun";
    patch.tun_stack = "system";
    patch.tun_mtu = 1280;
    CHECK_THROWS_WITH(service.update_node(patch),
                      "TUN interface sbtun already exists in the sing-box config and is not managed by keen-pbr-sx. "
                      "The existing config was not changed. Choose another interface name.");
    CHECK(read_file(paths.singbox_config) == before);
    const auto added = service.add_links({"trojan://pw@b.example:443#Other"}, SingboxTunOptions{"system", 1280, "172.19.8.1/30"});
    const auto config = nlohmann::json::parse(read_file(paths.singbox_config));
    CHECK(config["dns"]["servers"][0]["tag"] == "local");
    CHECK(config["inbounds"][0]["interface_name"] == "sbtun");
    CHECK(config["inbounds"][1]["listen"] == "192.168.1.1");
    CHECK(config["outbounds"][0]["password"] == "keep");
    CHECK(config["route"]["final"] == "direct");
    CHECK(config["route"]["rules"][0]["outbound"] == "proxy");
    CHECK(std::filesystem::exists(paths.singbox_config + ".bak-keenpbr-original"));
    CHECK(read_file(paths.singbox_config + ".bak-keenpbr-original") == before);
    CHECK(added.state["nodes"].size() == 2);
    CHECK(service.state()["suggested_interface"] == "sbtun3");
    std::filesystem::remove_all(root);
}

TEST_CASE("VLESS Encryption is not imported and an existing interface is only warned") {
    const auto root = std::filesystem::temp_directory_path() / "keen-pbr-singbox-vless-enc";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    SingboxPaths paths;
    paths.store = (root / "links.json").string();
    paths.singbox_config = (root / "config.json").string();
    std::string body =
        "vless://11111111-1111-1111-1111-111111111111@a.example:443?security=tls&sni=a.example#hu-1";
    SingboxLinkService service(
        paths, [](const std::string&, std::string*) { return 0; }, [](const KeeneticSyncRequest&) {},
        [] { return true; }, [&](const std::string&, const SubscriptionStopFn&) { return body; },
        [] { return static_cast<std::int64_t>(1000); });
    CHECK_THROWS_WITH(service.add_links(
                          {"vless://11111111-1111-1111-1111-111111111111@a.example:443?encryption=mlkem768x25519plus&security=reality&pbk=abc#hu-1"},
                          {}),
                      "VLESS Encryption (mlkem768x25519plus) не поддерживается sing-box");
    CHECK(service.state()["nodes"].empty());

    const auto saved = service.save_subscription("https://example.test/sub", 0, {"name:hu-1"}, {});
    REQUIRE(saved.state["nodes"].size() == 1);
    const auto config = read_file(paths.singbox_config);
    body = "vless://11111111-1111-1111-1111-111111111111@a.example:443?encryption=mlkem768x25519plus.native&security=reality&pbk=abc#hu-1";
    REQUIRE(service.refresh_subscriptions(saved.state["subscriptions"][0]["id"].get<std::string>(), false).at(0).at("ok") == true);
    const auto after = service.state();
    REQUIRE(after["nodes"].size() == 1);
    CHECK(after["nodes"][0]["unsupported"] == true);
    CHECK(after["nodes"][0]["missing"] == false);
    CHECK(after["nodes"][0]["unsupported_reason"] == "VLESS Encryption (mlkem768x25519plus) не поддерживается sing-box");
    CHECK(read_file(paths.singbox_config) == config);
    const auto preview = service.preview_subscription("https://example.test/sub");
    bool saw_disabled = false;
    for (const auto& node : preview["nodes"]) {
        if (node.value("skipped", false)) {
            saw_disabled = true;
            CHECK(node["reason"] == "VLESS Encryption (mlkem768x25519plus) не поддерживается sing-box");
        }
    }
    CHECK(saw_disabled);
    std::filesystem::remove_all(root);
}

TEST_CASE("subscription download does not block reads and excludes concurrent updates") {
    const auto root = std::filesystem::temp_directory_path() / "keen-pbr-singbox-sub-busy";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    SingboxPaths paths;
    paths.store = (root / "links.json").string();
    paths.singbox_config = (root / "config.json").string();
    std::promise<void> downloading, release;
    auto released = release.get_future();
    int downloads = 0;
    SingboxLinkService service(
        paths, [](const std::string&, std::string*) { return 0; }, [](const KeeneticSyncRequest&) {},
        [] { return true; },
        [&](const std::string&, const SubscriptionStopFn&) {
            if (++downloads > 1) {
                downloading.set_value();
                released.wait_for(std::chrono::seconds{5});
            }
            return std::string("trojan://pw@a.example:443#hu-1");
        },
        [] { return static_cast<std::int64_t>(1000); });
    const auto saved = service.save_subscription("https://example.test/sub", 0, {"name:hu-1"}, {});
    const auto id = saved.state["subscriptions"][0]["id"].get<std::string>();
    auto background = std::async(std::launch::async, [&] { return service.refresh_subscriptions(id, false); });
    downloading.get_future().wait();
    CHECK(service.state()["nodes"].size() == 1);
    CHECK_THROWS_AS(service.refresh_subscriptions(id, false), SingboxBusy);
    CHECK_THROWS_AS(service.remove_node(saved.state["nodes"][0]["id"].get<std::string>()), SingboxBusy);
    release.set_value();
    CHECK(background.get().at(0).at("ok") == true);
    std::filesystem::remove_all(root);
}
