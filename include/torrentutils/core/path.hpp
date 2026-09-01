#pragma once

#include <string>
#include <torrentutils/core/result.hpp>
#include <vector>

namespace torrentutils::core {

namespace detail {
struct MetadataEngineAccess;
} // namespace detail

/** Portable torrent path represented as validated UTF-8 relative path segments. */
class LogicalPath
{
  public:
    [[nodiscard]] static Result<LogicalPath> from_segments(std::vector<std::string> segments);

    [[nodiscard]] const std::vector<std::string>& segments() const noexcept;
    [[nodiscard]] std::string to_string() const;

    friend bool operator==(const LogicalPath& lhs, const LogicalPath& rhs) noexcept;
    friend bool operator!=(const LogicalPath& lhs, const LogicalPath& rhs) noexcept;

  private:
    friend struct detail::MetadataEngineAccess;

    explicit LogicalPath(std::vector<std::string> segments);

    std::vector<std::string> segments_;
};

} // namespace torrentutils::core
