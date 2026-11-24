#include "link_store.hpp"

#include "../http/http_client.hpp"
#include "../util/safe_exec.hpp"

#include <cstdlib>
#include <fstream>

namespace keen_pbr3 {

SingboxLinkService make_singbox_link_service() {
    SingboxPaths paths;
    if (const char* value = std::getenv("KEEN_PBR_SINGBOX_STORE")) paths.store = value;
    if (const char* value = std::getenv("KEEN_PBR_SINGBOX_CONFIG")) paths.singbox_config = value;
    if (const char* value = std::getenv("KEEN_PBR_SINGBOX_BIN")) paths.singbox_bin = value;

    return SingboxLinkService(
        paths,
        [bin = paths.singbox_bin](const std::string& config, std::string* error) {
            // sing-box may print config values on error. Do not keep child output,
            // including in the command-failure diagnostic journal.
            const auto result = safe_exec_capture_with_timeouts(
                {bin, "check", "-c", config}, safe_exec_timeouts(), true, 0, true, false);
            if (error) *error = result.timed_out ? "timed out" : "invalid configuration or binary unavailable";
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
        });
}

}  // namespace keen_pbr3
