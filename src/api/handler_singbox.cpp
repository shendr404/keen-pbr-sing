#ifdef WITH_API

#include "handler_singbox.hpp"
#include "handlers.hpp"

#include "../config/config.hpp"
#include "../http/http_client.hpp"
#include "../singbox/link_store.hpp"
#include "../util/safe_exec.hpp"

#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>

namespace keen_pbr3 {
namespace {

nlohmann::json require_object(const std::string& body) {
    const auto json = nlohmann::json::parse(body);
    if (!json.is_object()) throw ApiError("JSON object required", 400);
    return json;
}

std::string field(const nlohmann::json& json, const char* key) {
    if (!json.contains(key) || json.at(key).is_null()) return {};
    if (!json.at(key).is_string()) throw ApiError(std::string(key) + " must be a string", 400);
    return json.at(key).get<std::string>();
}

SingboxPaths paths_from_env() {
    SingboxPaths paths;
    if (const char* value = std::getenv("KEEN_PBR_SINGBOX_STORE")) paths.store = value;
    if (const char* value = std::getenv("KEEN_PBR_SINGBOX_CONFIG")) paths.singbox_config = value;
    if (const char* value = std::getenv("KEEN_PBR_SINGBOX_BIN")) paths.singbox_bin = value;
    return paths;
}

std::string result_json(const SingboxApplyResult& result) {
    nlohmann::json json = result.state;
    json["singbox_checked"] = result.singbox_checked;
    json["singbox_reloaded"] = result.singbox_reloaded;
    json["keenetic_applied"] = result.keenetic_applied;
    if (!result.warning.empty()) json["warning"] = result.warning;
    return json.dump();
}

void sync_keenetic_outbound(ApiContext& ctx, const SingboxProfile& profile, bool present) {
    if (!ctx.get_visible_config_fn || !ctx.validate_candidate_config_fn ||
        !ctx.enqueue_apply_validated_config_fn) {
        throw ApiError("config apply is unavailable", 503);
    }
    nlohmann::json doc;
    api::to_json(doc, ctx.get_visible_config_fn());
    if (!doc.contains("outbounds") || !doc["outbounds"].is_array()) {
        doc["outbounds"] = nlohmann::json::array();
    }
    nlohmann::json kept = nlohmann::json::array();
    for (const auto& outbound : doc["outbounds"]) {
        if (outbound.value("tag", "") == profile.keenetic_tag) continue;
        kept.push_back(outbound);
    }
    if (present) {
        kept.push_back({
            {"type", "interface"},
            {"tag", profile.keenetic_tag},
            {"interface", profile.interface_name},
            {"gateway", "auto"},
        });
    }
    doc["outbounds"] = std::move(kept);
    const auto dumped = doc.dump(2);
    Config updated = parse_config(dumped);
    ctx.validate_candidate_config_fn(updated);
    const auto applied = ctx.enqueue_apply_validated_config_fn(std::move(updated), dumped);
    if (!applied.error.empty() || !applied.saved) {
        throw std::runtime_error(applied.error.empty() ? "keen-pbr config was not saved" : applied.error);
    }
}

bool reload_singbox() {
    const char* scripts[] = {"/opt/etc/init.d/S99sing-box", "/opt/etc/init.d/S98sing-box"};
    for (const char* script : scripts) {
        std::ifstream probe(script);
        if (!probe) continue;
        const int status = safe_exec({script, "restart"}, true);
        if (status == 0) return true;
        throw std::runtime_error(std::string(script) + " restart failed");
    }
    return false;
}

std::string download_subscription(const std::string& url) {
    HttpClient client;
    client.set_timeout(std::chrono::seconds{20});
    client.set_max_response_size(std::size_t{2} * 1024U * 1024U);
    return client.download(url);
}

}  // namespace

void register_singbox_handler(ApiServer& server, ApiContext& ctx) {
    const auto paths = paths_from_env();
    auto service = std::make_shared<SingboxLinkService>(
        paths,
        [bin = paths.singbox_bin](const std::string& config_path, std::string* error) {
            const auto result = safe_exec_capture({bin, "check", "-c", config_path}, true, 4096, true);
            if (error != nullptr) *error = result.stdout_output;
            return result.exit_code;
        },
        [&ctx](const SingboxProfile& profile, bool present) {
            sync_keenetic_outbound(ctx, profile, present);
        },
        [] { return reload_singbox(); });

    server.post("/api/sing-box/preview", [service](const std::string& body) {
        try {
            const auto json = require_object(body);
            return service->preview(field(json, "link")).dump();
        } catch (const nlohmann::json::exception&) {
            throw ApiError("invalid JSON", 400);
        } catch (const std::runtime_error& error) {
            throw ApiError(error.what(), 400);
        }
    });
    server.get("/api/sing-box/profiles", [service]() {
        return service->state(true).dump();
    });
    server.post("/api/sing-box/connections", [service](const std::string& body) {
        try {
            const auto json = require_object(body);
            const auto result = service->save_connection(
                field(json, "profile"), field(json, "interface"), field(json, "tag"),
                field(json, "mode"), field(json, "name"), field(json, "link"), field(json, "id"));
            return result_json(result);
        } catch (const nlohmann::json::exception&) {
            throw ApiError("invalid JSON", 400);
        } catch (const std::runtime_error& error) {
            throw ApiError(error.what(), 400);
        }
    });
    server.post("/api/sing-box/connections/delete", [service](const std::string& body) {
        try {
            const auto json = require_object(body);
            return result_json(service->remove_connection(field(json, "profile_id"), field(json, "id")));
        } catch (const std::runtime_error& error) {
            throw ApiError(error.what(), 400);
        }
    });
    server.post("/api/sing-box/profiles/select", [service](const std::string& body) {
        try {
            const auto json = require_object(body);
            return result_json(service->set_mode(field(json, "profile_id"), field(json, "mode"),
                                                 field(json, "active")));
        } catch (const std::runtime_error& error) {
            throw ApiError(error.what(), 400);
        }
    });
    server.post("/api/sing-box/subscriptions", [service](const std::string& body) {
        try {
            const auto json = require_object(body);
            const auto url = field(json, "url");
            const int interval = json.value("interval_hours", 0);
            const auto fetched = download_subscription(url);
            return result_json(service->save_subscription(
                field(json, "profile"), field(json, "interface"), field(json, "tag"),
                field(json, "mode"), url, interval, fetched));
        } catch (const HttpError&) {
            throw ApiError("subscription download failed", 502);
        } catch (const std::runtime_error& error) {
            throw ApiError(error.what(), 400);
        }
    });
    server.post("/api/sing-box/subscriptions/refresh", [service, paths](const std::string& body) {
        try {
            const auto json = require_object(body);
            const auto profiles = SingboxLinkService::load_profiles(paths.store);
            const auto id = field(json, "profile_id");
            std::string url;
            for (const auto& profile : profiles) {
                if (profile.id == id) url = profile.subscription_url;
            }
            if (url.empty()) throw ApiError("subscription URL is missing", 400);
            return result_json(service->refresh_subscription(id, download_subscription(url)));
        } catch (const ApiError&) {
            throw;
        } catch (const HttpError&) {
            throw ApiError("subscription download failed", 502);
        } catch (const std::runtime_error& error) {
            throw ApiError(error.what(), 400);
        }
    });
}

}  // namespace keen_pbr3

#endif
