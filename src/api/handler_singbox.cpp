#ifdef WITH_API

#include "handler_singbox.hpp"
#include "handlers.hpp"
#include "status_stream.hpp"

#include "../config/config.hpp"
#include "../singbox/link_store.hpp"

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

}  // namespace

void register_singbox_handler(ApiServer& server, ApiContext& ctx, SingboxLinkService& service) {
    service.set_keenetic_callback([&ctx](const SingboxProfile& profile, bool present) {
        sync_keenetic_outbound(ctx, profile, present);
    });

    server.post("/api/sing-box/preview", [&service](const std::string& body) {
        try {
            const auto json = require_object(body);
            return service.preview(field(json, "link")).dump();
        } catch (const nlohmann::json::exception&) {
            throw ApiError("invalid JSON", 400);
        } catch (const std::runtime_error& error) {
            throw ApiError(error.what(), 400);
        }
    });
    server.get("/api/sing-box/profiles", [&service]() {
        return service.state().dump();
    });
    server.post("/api/sing-box/connections", [&service](const std::string& body) {
        try {
            const auto json = require_object(body);
            const auto result = service.save_connection(
                field(json, "profile"), field(json, "interface"), field(json, "tag"),
                field(json, "mode"), field(json, "name"), field(json, "link"), field(json, "id"));
            return result_json(result);
        } catch (const nlohmann::json::exception&) {
            throw ApiError("invalid JSON", 400);
        } catch (const SingboxBusy& error) {
            throw ApiError(error.what(), 409);
        } catch (const std::runtime_error& error) {
            throw ApiError(error.what(), 400);
        }
    });
    server.post("/api/sing-box/connections/delete", [&service](const std::string& body) {
        try {
            const auto json = require_object(body);
            return result_json(service.remove_connection(field(json, "profile_id"), field(json, "id")));
        } catch (const SingboxBusy& error) {
            throw ApiError(error.what(), 409);
        } catch (const std::runtime_error& error) {
            throw ApiError(error.what(), 400);
        }
    });
    server.post("/api/sing-box/profiles/select", [&service](const std::string& body) {
        try {
            const auto json = require_object(body);
            return result_json(service.set_mode(field(json, "profile_id"), field(json, "mode"),
                                                 field(json, "active")));
        } catch (const SingboxBusy& error) {
            throw ApiError(error.what(), 409);
        } catch (const std::runtime_error& error) {
            throw ApiError(error.what(), 400);
        }
    });
    server.post("/api/sing-box/subscriptions", [&service](const std::string& body) {
        try {
            const auto json = require_object(body);
            const auto url = field(json, "url");
            const int interval = json.value("interval_hours", 0);
            return result_json(service.save_subscription(
                field(json, "profile"), field(json, "interface"), field(json, "tag"),
                field(json, "mode"), url, interval));
        } catch (const nlohmann::json::exception&) {
            throw ApiError("invalid JSON", 400);
        } catch (const SingboxBusy& error) {
            throw ApiError(error.what(), 409);
        } catch (const std::runtime_error& error) {
            throw ApiError(error.what(), 400);
        }
    });
    server.post("/api/sing-box/subscriptions/refresh", [&service, &ctx](const std::string& body) {
        try {
            const auto json = require_object(body);
            const auto id = field(json, "profile_id");
            if (id.empty()) throw ApiError("subscription profile_id is required", 400);
            // cpp-httplib runs manual refreshes on an HTTP worker; the shared
            // service never waits for an in-flight background mutation.
            const auto outcome = service.refresh_subscriptions(id, false).at(0);
            if (ctx.status_stream) ctx.status_stream->reconcile(StatusUpdate::Issues);
            if (!outcome.at("ok").get<bool>()) {
                throw ApiError(outcome.at("message").get<std::string>(), 502);
            }
            return service.state().dump();
        } catch (const ApiError&) {
            throw;
        } catch (const nlohmann::json::exception&) {
            throw ApiError("invalid JSON", 400);
        } catch (const SingboxBusy& error) {
            throw ApiError(error.what(), 409);
        } catch (const std::runtime_error& error) {
            throw ApiError(error.what(), 400);
        }
    });
}

}  // namespace keen_pbr3

#endif
