#include "cli_common.hpp"

#include "progress.hpp"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <torrentutils/core/error.hpp>
#include <torrentutils/core/torrent_engine.hpp>
#include <vector>

namespace torrentcraft::cli::detail {

[[nodiscard]] std::filesystem::path path_from_utf8(const std::string& value)
{
    return std::filesystem::u8path(value);
}

[[nodiscard]] bool help_requested(const std::vector<std::string>& args) noexcept
{
    for (const auto& argument : args)
    {
        if (argument == "-h" || argument == "--help")
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::string format_name(const torrentutils::core::TorrentFormat format)
{
    switch (format)
    {
    case torrentutils::core::TorrentFormat::V1:
        return "v1";
    case torrentutils::core::TorrentFormat::V2:
        return "v2";
    case torrentutils::core::TorrentFormat::Hybrid:
        return "hybrid";
    }
    return "hybrid";
}

[[nodiscard]] std::string file_order_name(const torrentutils::core::FileOrderPolicy policy)
{
    switch (policy)
    {
    case torrentutils::core::FileOrderPolicy::Lexicographical:
        return "lexicographical";
    case torrentutils::core::FileOrderPolicy::CanonicalAlignment:
        return "canonical_alignment";
    case torrentutils::core::FileOrderPolicy::Natural:
        return "natural";
    case torrentutils::core::FileOrderPolicy::BreadthFirst:
        return "breadth_first";
    }
    return "lexicographical";
}

[[nodiscard]] std::optional<std::uint64_t> parse_unsigned(std::string_view value)
{
    if (value.empty())
    {
        return std::nullopt;
    }
    std::uint64_t parsed{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
    {
        return std::nullopt;
    }
    return parsed;
}

[[nodiscard]] int error_exit_code(const ErrorCode code)
{
    switch (code)
    {
    case ErrorCode::ValidationFailed:
    case ErrorCode::InvalidTorrent:
    case ErrorCode::InvalidBencode:
        return 3;
    case ErrorCode::FileNotFound:
        return 4;
    case ErrorCode::AccessDenied:
        return 5;
    case ErrorCode::Conflict:
        return 6;
    case ErrorCode::Cancelled:
        return 7;
    case ErrorCode::IoFailure:
        return 8;
    case ErrorCode::ResourceLimitExceeded:
        return 9;
    case ErrorCode::UnsupportedFeature:
    case ErrorCode::Internal:
        return 10;
    }
    return 10;
}

[[nodiscard]] std::string error_code_name(const ErrorCode code)
{
    switch (code)
    {
    case ErrorCode::FileNotFound:
        return "file_not_found";
    case ErrorCode::AccessDenied:
        return "access_denied";
    case ErrorCode::InvalidBencode:
        return "invalid_bencode";
    case ErrorCode::InvalidTorrent:
        return "invalid_torrent";
    case ErrorCode::UnsupportedFeature:
        return "unsupported_feature";
    case ErrorCode::ValidationFailed:
        return "validation_failed";
    case ErrorCode::IoFailure:
        return "io_failure";
    case ErrorCode::Cancelled:
        return "cancelled";
    case ErrorCode::Conflict:
        return "conflict";
    case ErrorCode::ResourceLimitExceeded:
        return "resource_limit_exceeded";
    case ErrorCode::Internal:
        return "internal";
    }
    return "internal";
}

void print_error(const Error& error, const bool json, std::ostream& diagnostics)
{
    if (json)
    {
        Json result;
        result["ok"] = false;
        result["error"]["code"] = error_code_name(error.code);
        result["error"]["message"] = error.message;
        result["error"]["issues"] = Json::array();
        for (const auto& issue : error.issues)
        {
            result["error"]["issues"].push_back(
                {{"field", issue.field}, {"message", issue.message}});
        }
        diagnostics << dump_json(result) << '\n';
        return;
    }
    diagnostics << "error: ";
    write_human_text(diagnostics, error.message);
    diagnostics << "\n";
    for (const auto& issue : error.issues)
    {
        diagnostics << "  " << issue.field << ": ";
        write_human_text(diagnostics, issue.message);
        diagnostics << "\n";
    }
}

} // namespace torrentcraft::cli::detail
