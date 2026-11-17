#ifdef WITH_API

#include "handler_outbound_traffic.hpp"

namespace keen_pbr3 {

void register_outbound_traffic_handler(ApiServer& server, ApiContext& ctx) {
    server.get("/api/runtime/outbound-traffic", [&ctx]() -> std::string {
        if (!ctx.get_outbound_traffic_json_fn) {
            throw ApiError("Outbound traffic counters are unavailable", 503);
        }
        return ctx.get_outbound_traffic_json_fn();
    });
}

} // namespace keen_pbr3

#endif // WITH_API
