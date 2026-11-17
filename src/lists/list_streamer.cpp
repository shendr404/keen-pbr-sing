#include "list_streamer.hpp"
#include "../config/list_parser.hpp"
#include "../log/logger.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace keen_pbr3 {

namespace {

char ascii_lower(char c) {
    if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
    return c;
}

bool starts_with_ignore_case(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (ascii_lower(text[i]) != ascii_lower(prefix[i])) return false;
    }
    return true;
}

} // namespace

std::optional<std::string> downloaded_list_problem(std::string_view body) {
    std::size_t start = 0;
    if (body.size() >= 3 &&
        static_cast<unsigned char>(body[0]) == 0xEF &&
        static_cast<unsigned char>(body[1]) == 0xBB &&
        static_cast<unsigned char>(body[2]) == 0xBF) {
        start = 3;
    }
    while (start < body.size() &&
           (body[start] == ' ' || body[start] == '\t' ||
            body[start] == '\r' || body[start] == '\n')) {
        ++start;
    }
    const std::string_view head = body.substr(start);
    if (starts_with_ignore_case(head, "<!doctype") ||
        starts_with_ignore_case(head, "<html") ||
        starts_with_ignore_case(head, "<head") ||
        starts_with_ignore_case(head, "<body")) {
        return std::string("downloaded list looks like an HTML page, not a domain or IP list");
    }
    std::size_t column = 0;
    std::size_t line_number = 1;
    for (std::size_t i = 0; i < body.size(); ++i) {
        if (body[i] == '\n') {
            column = 0;
            ++line_number;
            continue;
        }
        if (body[i] == '\r') continue;
        ++column;
        if (column > ListStreamer::kMaxLineBytes) {
            return "downloaded list line " + std::to_string(line_number) +
                   " exceeds 4096 bytes; the previous list was kept";
        }
    }
    return std::nullopt;
}

ListStreamer::ListStreamer(const CacheManager& cache)
    : cache_(cache)
    , max_file_size_bytes_(cache.max_file_size()) {}

void ListStreamer::stream_list(const std::string& name, const ListConfig& config, ListEntryVisitor& visitor) {
    // The cached URL file is used only while the list still declares a URL
    // source; a stale cache for a removed URL is ignored here.
    const bool include_cache = config.url.has_value() && cache_.has_cache(name);
    stream_all_sources(name, config, visitor, include_cache);
}

void ListStreamer::stream_list_preferring_cache(const std::string& name,
                                                const ListConfig& config,
                                                ListEntryVisitor& visitor) {
    // Use the cached file whenever it exists, even if the URL source was
    // removed from the config. The local file and inline entries are always
    // streamed too — they must not be dropped just because a cache exists.
    stream_all_sources(name, config, visitor, cache_.has_cache(name));
}

void ListStreamer::stream_all_sources(const std::string& name,
                                      const ListConfig& config,
                                      ListEntryVisitor& visitor,
                                      bool include_cache) {
    // 1. Cached URL file
    if (include_cache) {
        stream_file(cache_.cache_path(name), visitor, /*log_invalid_entries=*/false);
    }

    // 2. Local file (if configured)
    if (config.file.has_value()) {
        stream_file(config.file.value(), visitor, /*log_invalid_entries=*/true);
    }

    // 3. Inline entries use parse_line() too, so comments, blank entries, and
    // malformed entries are handled consistently with file and URL sources.
    const std::string inline_source = "inline list '" + name + "'";
    std::size_t inline_line_number = 1;
    ListParser::ParseContext inline_context;
    for (const auto& entry : config.ip_cidrs.value_or(std::vector<std::string>{})) {
        ListParser::parse_line(entry, visitor, inline_source, inline_line_number++, &inline_context);
    }

    // 4. Inline domains use the same parser and normalization as every other source.
    for (const auto& domain : config.domains.value_or(std::vector<std::string>{})) {
        ListParser::parse_line(domain, visitor, inline_source, inline_line_number++, &inline_context);
    }

    // Signal that all sources for this list have been processed
    visitor.on_list_complete(name);
}

void ListStreamer::stream_cache(const std::string& name, ListEntryVisitor& visitor) {
    if (cache_.has_cache(name)) {
        stream_file(cache_.cache_path(name), visitor, /*log_invalid_entries=*/false);
    }
}

void ListStreamer::stream_file(const std::filesystem::path& path,
                               ListEntryVisitor& visitor,
                               bool log_invalid_entries) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        throw std::runtime_error("Failed to open list file " + path.string() + ": " +
                                 std::strerror(errno));
    }
    const auto close_fd = [&]() { ::close(fd); };

    struct stat st {};
    if (::fstat(fd, &st) != 0) {
        const std::string error = std::strerror(errno);
        close_fd();
        throw std::runtime_error("Failed to inspect list file " + path.string() + ": " + error);
    }
    if (!S_ISREG(st.st_mode)) {
        close_fd();
        throw std::runtime_error("List source is not a regular file: " + path.string());
    }
    if (st.st_size < 0 || static_cast<std::uintmax_t>(st.st_size) > max_file_size_bytes_) {
        close_fd();
        throw std::runtime_error("List file exceeds configured size limit: " + path.string());
    }

    std::array<char, 4096> buffer {};
    std::string line;
    line.reserve(256);
    std::string prefix;
    bool prefix_checked = log_invalid_entries;
    std::size_t total_bytes = 0;
    std::size_t line_number = 1;
    ListParser::ParseContext context{log_invalid_entries};
    while (true) {
        const ssize_t count = ::read(fd, buffer.data(), buffer.size());
        if (count < 0) {
            if (errno == EINTR) continue;
            const std::string error = std::strerror(errno);
            close_fd();
            throw std::runtime_error("Failed to read list file " + path.string() + ": " + error);
        }
        if (count == 0) break;
        total_bytes += static_cast<std::size_t>(count);
        if (total_bytes > max_file_size_bytes_) {
            close_fd();
            throw std::runtime_error("List file exceeds configured size limit: " + path.string());
        }
        for (ssize_t i = 0; i < count; ++i) {
            if (!prefix_checked) {
                const char ch = buffer[static_cast<std::size_t>(i)];
                if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') prefix.push_back(ch);
                if (prefix.size() >= 12) {
                    prefix_checked = true;
                    if (const auto problem = downloaded_list_problem(prefix)) {
                        close_fd();
                        Logger::instance().error(
                            "List cache {} is not a domain list ({}). The cache is left unused.",
                            path.string(), *problem);
                        return;
                    }
                }
            }
            if (buffer[static_cast<std::size_t>(i)] == '\n') {
                ListParser::parse_line(line, visitor, path.string(), line_number++, &context);
                line.clear();
                continue;
            }
            if (line.size() >= kMaxLineBytes) {
                close_fd();
                if (!log_invalid_entries) {
                    Logger::instance().error(
                        "List cache {} line {} exceeds 4096 bytes or is not a domain list "
                        "(often an HTML error page). The cache is left unused.",
                        path.string(), line_number);
                    return;
                }
                throw std::runtime_error("List line exceeds 4096-byte limit in " +
                                         path.string() + " at line " +
                                         std::to_string(line_number));
            }
            line.push_back(buffer[static_cast<std::size_t>(i)]);
        }
    }
    close_fd();
    if (!line.empty()) {
        ListParser::parse_line(line, visitor, path.string(), line_number, &context);
    }
}

} // namespace keen_pbr3
