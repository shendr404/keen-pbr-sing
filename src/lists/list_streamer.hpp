#pragma once

#include "../cache/cache_manager.hpp"
#include "../config/config.hpp"
#include "list_entry_visitor.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace keen_pbr3 {

// Empty when the body can be stored as a domain/IP list. Otherwise a sentence
// saying it is an HTML page or a line longer than 4096 bytes. Callers keep
// the previous cache when this is set.
std::optional<std::string> downloaded_list_problem(std::string_view body);

class ListStreamer {
public:
    static constexpr std::size_t kMaxLineBytes = 4096;
    explicit ListStreamer(const CacheManager& cache);

    // Stream all sources for a named list (cache file, local file, inline entries)
    // through the visitor. Calls visitor.on_list_complete(name) when done.
    void stream_list(const std::string& name, const ListConfig& config, ListEntryVisitor& visitor);

    // Stream all sources for a named list, the same as stream_list(), but use
    // the cached file whenever it exists — even if the list no longer declares
    // a URL source. Calls visitor.on_list_complete(name) when done.
    void stream_list_preferring_cache(const std::string& name,
                                     const ListConfig& config,
                                     ListEntryVisitor& visitor);

    // Whether a downloaded copy of the list's URL source is cached.
    bool has_cached(const std::string& name) const { return cache_.has_cache(name); }

    // Stream only the cached file for a named list through the visitor.
    void stream_cache(const std::string& name, ListEntryVisitor& visitor);

private:
    // Stream a list's local file and inline entries through the visitor,
    // optionally preceded by the cached file. Calls on_list_complete(name).
    void stream_all_sources(const std::string& name,
                            const ListConfig& config,
                            ListEntryVisitor& visitor,
                            bool include_cache);

    // Open a file and stream its entries through the visitor.
    void stream_file(const std::filesystem::path& path,
                     ListEntryVisitor& visitor,
                     bool log_invalid_entries);

    const CacheManager& cache_;
    std::size_t max_file_size_bytes_;
};

} // namespace keen_pbr3
