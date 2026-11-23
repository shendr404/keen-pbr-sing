#pragma once

#ifdef WITH_API

#include "server.hpp"

namespace keen_pbr3 {

struct ApiContext;

void register_singbox_handler(ApiServer& server, ApiContext& ctx);

}  // namespace keen_pbr3

#endif
