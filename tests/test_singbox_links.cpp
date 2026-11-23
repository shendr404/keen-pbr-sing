#include <doctest/doctest.h>

#include "../src/singbox/link_parser.hpp"
#include "../src/singbox/link_store.hpp"

#include <filesystem>
#include <fstream>

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
    CHECK(service.state(false)["profiles"].empty());

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
