#pragma once

#include <string>
#include <vector>

namespace keen_pbr3 {

// Read-only iptables listings take the xtables lock. Keenetic's iptables
// 1.4.21 accepts a bare -w and waits; a numeric timeout is a later option
// and would be a stray argument there. NDM reloads the firewall while
// keen-pbr checks its hooks, and a lock miss used to look like a missing chain.
inline std::vector<std::string> xtables_list_args(std::string command,
                                                  std::string table,
                                                  const char* chain = nullptr) {
    std::vector<std::string> args{std::move(command), "-w", "-t", std::move(table), "-S"};
    if (chain != nullptr && chain[0] != '\0') {
        args.emplace_back(chain);
    }
    return args;
}

}  // namespace keen_pbr3
