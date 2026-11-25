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

int number_field(const nlohmann::json& json, const char* key, int fallback) {
    if (!json.contains(key) || json.at(key).is_null()) return fallback;
    if (json.at(key).is_number_integer()) return json.at(key).get<int>();
    if (json.at(key).is_number()) return static_cast<int>(json.at(key).get<double>());
    if (json.at(key).is_string()) {
        const auto text = json.at(key).get<std::string>();
        if (text.empty()) return fallback;
        int value = 0;
        for (unsigned char c : text) {
            if (c < '0' || c > '9') throw ApiError(std::string(key) + " must be a number", 400);
            value = value * 10 + static_cast<int>(c - '0');
        }
        return value;
    }
    throw ApiError(std::string(key) + " must be a number", 400);
}

std::vector<std::string> link_lines(const nlohmann::json& json) {
    std::vector<std::string> lines;
    if (json.contains("links") && json.at("links").is_array()) {
        for (const auto& item : json.at("links")) {
            if (!item.is_string()) throw ApiError("links must be strings", 400);
            lines.push_back(item.get<std::string>());
        }
    } else if (json.contains("links") && json.at("links").is_string()) {
        lines.push_back(json.at("links").get<std::string>());
    }
    if (json.contains("link") && json.at("link").is_string()) lines.push_back(json.at("link").get<std::string>());
    return lines;
}

SingboxProfileDraft profile_draft(const nlohmann::json& json) {
    SingboxProfileDraft draft;
    draft.id = field(json, "id");
    if (draft.id.empty()) draft.id = field(json, "profile_id");
    draft.name = field(json, "name");
    if (draft.name.empty()) draft.name = field(json, "profile");
    draft.interface_name = field(json, "interface");
    draft.keenetic_tag = field(json, "tag");
    draft.mode = field(json, "mode");
    draft.urltest_url = field(json, "urltest_url");
    draft.urltest_interval = field(json, "urltest_interval");
    draft.tun_stack = field(json, "tun_stack");
    draft.tun_mtu = number_field(json, "tun_mtu", 0);
    draft.tun_address = field(json, "tun_address");
    return draft;
}

std::string result_json(const SingboxApplyResult& result) {
    nlohmann::json json = result.state;
    json["singbox_checked"] = result.singbox_checked;
    json["singbox_reloaded"] = result.singbox_reloaded;
    json["keenetic_applied"] = result.keenetic_applied;
    if (!result.warning.empty()) json["warning"] = result.warning;
    if (result.skipped > 0) json["skipped"] = result.skipped;
    if (!result.skipped_transports.empty()) json["skipped_transports"] = result.skipped_transports;
    if (result.encryption_ignored) json["encryption_ignored"] = true;
    return json.dump();
}

void sync_keenetic_outbound(ApiContext& ctx, const SingboxProfile& profile, bool present) {
    if (!ctx.get_visible_config_fn || !ctx.validate_candidate_config_fn ||
        !ctx.enqueue_apply_validated_config_fn) {
        throw ApiError("config apply is unavailable", 503);
    }
    if (ctx.begin_save_operation_fn) ctx.begin_save_operation_fn();
    struct FinishSave {
        ApiContext* context;
        bool active;
        ~FinishSave() {
            if (active && context->finish_config_operation_fn) context->finish_config_operation_fn();
        }
    } finish{&ctx, static_cast<bool>(ctx.begin_save_operation_fn)};
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
        // TUN and point-to-point interfaces are device routes. Writing
        // gateway "auto" makes keen-pbr report that the gateway is ignored.
        kept.push_back({
            {"type", "interface"},
            {"tag", profile.keenetic_tag},
            {"interface", profile.interface_name},
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
    server.post("/api/sing-box/profiles", [&service](const std::string& body) {
        try {
            const auto json = require_object(body);
            return result_json(service.save_profile(profile_draft(json), link_lines(json)));
        } catch (const nlohmann::json::exception&) {
            throw ApiError("invalid JSON", 400);
        } catch (const SingboxBusy& error) {
            throw ApiError(error.what(), 409);
        } catch (const std::runtime_error& error) {
            throw ApiError(error.what(), 400);
        }
    });
    server.post("/api/sing-box/profiles/delete", [&service](const std::string& body) {
        try {
            const auto json = require_object(body);
            return result_json(service.remove_profile(field(json, "profile_id")));
        } catch (const SingboxBusy& error) {
            throw ApiError(error.what(), 409);
        } catch (const std::runtime_error& error) {
            throw ApiError(error.what(), 400);
        }
    });
    server.post("/api/sing-box/connections", [&service](const std::string& body) {
        try {
            const auto json = require_object(body);
            const auto result = service.add_connections(field(json, "profile_id"), link_lines(json), field(json, "name"));
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
            const int interval = number_field(json, "interval_hours", 0);
            const auto profile_id = field(json, "profile_id");
            return result_json(service.save_subscription(profile_id.empty() ? field(json, "profile") : profile_id,
                                                         url, interval, field(json, "subscription_id")));
        } catch (const nlohmann::json::exception&) {
            throw ApiError("invalid JSON", 400);
        } catch (const SingboxBusy& error) {
            throw ApiError(error.what(), 409);
        } catch (const std::runtime_error& error) {
            throw ApiError(error.what(), 400);
        }
    });
    server.post("/api/sing-box/subscriptions/delete", [&service](const std::string& body) {
        try {
            const auto json = require_object(body);
            return result_json(service.remove_subscription(field(json, "profile_id"), field(json, "subscription_id")));
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
            const auto outcomes = service.refresh_subscriptions(id, false, {}, field(json, "subscription_id"));
            if (outcomes.empty()) throw ApiError("subscription was not found", 404);
            std::string errors;
            for (const auto& outcome : outcomes) {
                if (outcome.at("ok").get<bool>()) continue;
                if (!errors.empty()) errors += "; ";
                errors += outcome.at("message").get<std::string>();
            }
            if (ctx.status_stream) ctx.status_stream->reconcile(StatusUpdate::Issues);
            if (!errors.empty()) throw ApiError(errors, 502);
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
