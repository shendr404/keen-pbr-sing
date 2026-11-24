#pragma once

#ifdef WITH_API

#include "server.hpp"

namespace keen_pbr3 {

struct ApiContext;

class SingboxLinkService;
void register_singbox_handler(ApiServer& server, ApiContext& ctx, SingboxLinkService& service);

}  // namespace keen_pbr3

#endif
