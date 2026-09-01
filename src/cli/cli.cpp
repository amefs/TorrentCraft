#include "cli.hpp"

#include "progress.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <torrentutils/core/application.hpp>
#include <torrentutils/core/error.hpp>
#include <torrentutils/core/torrent_engine.hpp>
#include <torrentutils/core/version.hpp>
#include <torrentutils/frontend/config.hpp>
#include <torrentutils/frontend/settings.hpp>
#include <utility>
#include <vector>

namespace torrentcraft::cli {
namespace {

using Json = nlohmann::json;
using torrentutils::core::Error;
using torrentutils::core::ErrorCode;
using torrentutils::core::FieldIssue;
using torrentutils::core::Result;
using torrentutils::frontend::ConfigFile;
using torrentutils::frontend::ConfigSearchPaths;
using torrentutils::frontend::CreationSettingsPatch;
using torrentutils::frontend::ParsedPreset;
using torrentutils::frontend::ResolvedCreationSettings;
using torrentutils::frontend::VerifyResourceSettings;

[[nodiscard]] std::filesystem::path path_from_utf8(const std::string& value)
{
    return std::filesystem::u8path(value);
}

struct CreateArguments
{
    std::filesystem::path content;
    std::filesystem::path target;
    std::optional<std::filesystem::path> config_path;
    std::optional<std::string> preset_name;
    std::optional<std::filesystem::path> preset_file;
    CreationSettingsPatch cli_settings;
    std::vector<std::pair<std::size_t, std::string>> trackers;
    bool tracker_specified{};
    bool overwrite{};
    bool dry_run{};
    bool quiet{};
    std::optional<std::uint64_t> memory_working_set_limit_bytes;
    bool json{};
    std::optional<ProgressMode> progress;
    std::optional<std::int64_t> creation_date;
};

struct ParsedArguments
{
    CreateArguments create;
};

constexpr std::string_view kUsage = R"(Usage:
  torrentcraft <command> [options]
  torrentcraft <path...>       Infer a command from the provided paths
  torrentcraft --help
  torrentcraft --version

Commands:
  create      Create a torrent from a file or directory
  inspect     Show a summary of a torrent file
  tree        Print the torrent's file tree
  verify      Verify content against a torrent file
  validate    Validate a torrent file
  tracker     Manage tracker tiers
  metadata    Show or edit top-level metadata
  config      Manage the canonical torrentcraft.json config
  preset      Manage named creation presets
  completion  Print shell completion scripts

Run 'torrentcraft <command> --help' for command-specific options.
)";

constexpr std::string_view kCreateHelp = R"(Usage:
  torrentcraft create <content> -o <target.torrent> [options]

Create a torrent from a file or directory.

Options:
  --config PATH             Read torrentcraft.json from PATH
  --preset NAME             Select an inline config preset
  --preset-file PATH        Read a legacy flat preset file
  --format v1|v2|hybrid     Select torrent format
  --piece-size KIB|auto     Select fixed or automatic piece size
  --file-order POLICY       Select file ordering policy (lexicographical,
                            canonical_alignment, natural, breadth_first)
  --private | --no-private | --public  Set or clear the private flag
  --tracker URL             Replace lower-layer trackers; repeatable
  --tier N                  Assign the preceding tracker to tier N
  --web-seed URL            Add a web seed; repeatable
  --comment TEXT            Set top-level comment
  --creation-date N         Set top-level creation date (unix seconds; default now)
  --created-by TEXT         Set top-level creator
  --source TEXT             Set info-dictionary source
  --memory-working-set-limit SIZE
                            Limit the Windows process working set (default 512 MiB)
  --overwrite               Allow replacing an existing target
  --dry-run                 Validate without creating a file
  --quiet                   Suppress successful human output
  --json                    Emit stable JSON output
  --progress json|plain|tty Emit progress (json/plain/tty)
  -h, --help                Show this help
)";

constexpr std::string_view kInspectHelp = R"(Usage:
  torrentcraft inspect <torrent> [options]

Show a summary of a torrent file.

Options:
  --json                    Emit stable JSON output
  --quiet                   Suppress successful human output
  -h, --help                Show this help
)";

constexpr std::string_view kTreeHelp = R"(Usage:
  torrentcraft tree <torrent> [--depth N] [options]

Print the torrent's file tree.

Options:
  --depth N                 Limit the printed tree depth
  --json                    Emit stable JSON output
  --quiet                   Suppress successful human output
  -h, --help                Show this help
)";

constexpr std::string_view kVerifyHelp = R"(Usage:
  torrentcraft verify <torrent> <content> [options]

Verify content against a torrent file and report per-file results.

Options:
  --config PATH             Read torrentcraft.json from PATH
  --verify-workers N        Limit hashing worker threads (default 1)
  --verify-memory SIZE      Limit checking memory in bytes or KiB/MiB/GiB
                            (default 32 MiB)
  --memory-working-set-limit SIZE
                            Limit the Windows process working set (default 512 MiB)
  --progress json|plain|tty Emit progress (json/plain/tty)
  --json                    Emit stable JSON output
  --quiet                   Suppress successful human output
  -h, --help                Show this help
)";

constexpr std::string_view kValidateHelp = R"(Usage:
  torrentcraft validate <torrent> [options]

Validate a torrent file.

Options:
  --strict                  Reject leniently loaded torrents
  --json                    Emit stable JSON output
  --quiet                   Suppress successful human output
  -h, --help                Show this help
)";

constexpr std::string_view kTrackerHelp = R"(Usage:
  torrentcraft tracker list|add|remove|replace <torrent> ... [options]

Manage tracker tiers.

Options:
  --tracker URL             Replace the tier (tracker replace)
  --tier N                  Assign the preceding tracker to tier N
  --dry-run                 Validate without writing
  --backup                  Back up the torrent before writing
  --json                    Emit stable JSON output
  --quiet                   Suppress successful human output
  -h, --help                Show this help
)";

constexpr std::string_view kMetadataHelp = R"(Usage:
  torrentcraft metadata show|set|clear <torrent> ... [options]

Show or edit top-level metadata.

Options:
  --comment TEXT            Set top-level comment (metadata set)
  --created-by TEXT         Set top-level creator (metadata set)
    --info-source TEXT        Set explicit info-dictionary source extension (metadata set)
    --name TEXT               Set info-dictionary name (metadata set)
  --creation-time N|now     Set top-level creation time (metadata set)
  --web-seed URL            Add a web seed (metadata set)
  --dht-node NODE           Add a DHT node (metadata set)
  --private | --no-private | --public  Set or clear the private flag (metadata set)
  --dry-run                 Validate without writing
  --backup                  Back up the torrent before writing
  --json                    Emit stable JSON output
  --quiet                   Suppress successful human output
  -h, --help                Show this help
)";

constexpr std::string_view kConfigHelp = R"(Usage:
  torrentcraft config path|show|init|get <key>|set <key> <value> [options]

Manage the canonical torrentcraft.json config.

Options:
  --config PATH             Read torrentcraft.json from PATH
  --dry-run                 Validate without writing
  --backup                  Back up the config before writing
  --force                   Overwrite an existing config file
  --json                    Emit stable JSON output
  --quiet                   Suppress successful human output
  -h, --help                Show this help
)";

constexpr std::string_view kPresetHelp = R"(Usage:
  torrentcraft preset list|show <name>|add <file>|remove <name> [options]

Manage named creation presets.

Options:
  --config PATH             Read torrentcraft.json from PATH
  --dry-run                 Validate without writing
  --backup                  Back up the config before writing
  --force                   Replace an existing preset
  --json                    Emit stable JSON output
  --quiet                   Suppress successful human output
  -h, --help                Show this help
)";

constexpr std::string_view kCompletionHelp = R"(Usage:
  torrentcraft completion bash|zsh|fish

Print shell completion scripts.

Options:
  -h, --help                Show this help
)";

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

// Human verify output switches to a summary when the logical file count
// exceeds this value, so large torrents do not flood the terminal.
constexpr std::size_t kVerifyFileSummaryThreshold = 50U;

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

[[nodiscard]] bool parse_piece_size(std::string_view value, CreationSettingsPatch& settings,
                                    std::string& error)
{
    if (value == "auto")
    {
        settings.piece_size = torrentutils::frontend::PieceSizeSetting{};
        return true;
    }
    const auto parsed = parse_unsigned(value);
    if (!parsed || *parsed < 16U || *parsed > 16384U || (*parsed & (*parsed - 1U)) != 0U)
    {
        error = "--piece-size must be auto or a power of two between 16 and 16384 KiB";
        return false;
    }
    settings.piece_size =
        torrentutils::frontend::PieceSizeSetting{static_cast<std::uint32_t>(*parsed)};
    return true;
}

[[nodiscard]] bool parse_format(std::string_view value, CreationSettingsPatch& settings,
                                std::string& error)
{
    if (value == "v1")
    {
        settings.format = torrentutils::core::TorrentFormat::V1;
    }
    else if (value == "v2")
    {
        settings.format = torrentutils::core::TorrentFormat::V2;
    }
    else if (value == "hybrid")
    {
        settings.format = torrentutils::core::TorrentFormat::Hybrid;
    }
    else
    {
        error = "--format must be v1, v2, or hybrid";
        return false;
    }
    return true;
}

[[nodiscard]] bool parse_file_order(std::string_view value, CreationSettingsPatch& settings,
                                    std::string& error)
{
    if (value == "lexicographical")
    {
        settings.file_order = torrentutils::core::FileOrderPolicy::Lexicographical;
    }
    else if (value == "canonical_alignment")
    {
        settings.file_order = torrentutils::core::FileOrderPolicy::CanonicalAlignment;
    }
    else if (value == "natural")
    {
        settings.file_order = torrentutils::core::FileOrderPolicy::Natural;
    }
    else if (value == "breadth_first")
    {
        settings.file_order = torrentutils::core::FileOrderPolicy::BreadthFirst;
    }
    else
    {
        error = "--file-order must be lexicographical, canonical_alignment, natural, or "
                "breadth_first";
        return false;
    }
    return true;
}

[[nodiscard]] bool require_value(int& index, int argc, const char* const argv[],
                                 std::string_view option, std::string& value, std::string& error)
{
    if (index + 1 >= argc)
    {
        error = std::string(option) + " requires a value";
        return false;
    }
    value = argv[++index];
    if (value.empty())
    {
        error = std::string(option) + " requires a non-empty value";
        return false;
    }
    return true;
}

[[nodiscard]] bool parse_create(int argc, const char* const argv[], ParsedArguments& result,
                                std::string& error)
{
    auto& arguments = result.create;
    std::vector<std::string> positional;
    for (int index = 2; index < argc; ++index)
    {
        const std::string_view argument = argv[index];
        if (argument == "-o" || argument == "--output")
        {
            std::string value;
            if (!require_value(index, argc, argv, argument, value, error))
            {
                return false;
            }
            if (!arguments.target.empty())
            {
                error = "output target was specified more than once";
                return false;
            }
            arguments.target = path_from_utf8(value);
            continue;
        }
        if (argument == "--memory-working-set-limit")
        {
            std::string value;
            if (!require_value(index, argc, argv, argument, value, error))
            {
                return false;
            }
            const auto parsed = torrentutils::frontend::parse_memory_size(value);
            if (!parsed)
            {
                error = "--memory-working-set-limit must be a positive byte count or a size with "
                        "KiB/MiB/GiB suffix";
                return false;
            }
            if (arguments.memory_working_set_limit_bytes)
            {
                error = "--memory-working-set-limit was specified more than once";
                return false;
            }
            arguments.memory_working_set_limit_bytes = parsed;
            continue;
        }
        if (argument == "--progress")
        {
            std::string value;
            if (!require_value(index, argc, argv, argument, value, error))
            {
                return false;
            }
            ProgressMode mode;
            if (!parse_progress_mode(value, mode, error))
            {
                return false;
            }
            if (arguments.progress)
            {
                error = "--progress was specified more than once";
                return false;
            }
            arguments.progress = mode;
            continue;
        }
        if (argument.rfind("--progress=", 0) == 0)
        {
            ProgressMode mode;
            if (!parse_progress_mode(argument.substr(11), mode, error))
            {
                return false;
            }
            if (arguments.progress)
            {
                error = "--progress was specified more than once";
                return false;
            }
            arguments.progress = mode;
            continue;
        }
        if (argument.rfind("--memory-working-set-limit=", 0) == 0)
        {
            const auto value =
                argument.substr(std::string_view("--memory-working-set-limit=").size());
            const auto parsed = torrentutils::frontend::parse_memory_size(value);
            if (!parsed)
            {
                error = "--memory-working-set-limit must be a positive byte count or a size with "
                        "KiB/MiB/GiB suffix";
                return false;
            }
            if (arguments.memory_working_set_limit_bytes)
            {
                error = "--memory-working-set-limit was specified more than once";
                return false;
            }
            arguments.memory_working_set_limit_bytes = parsed;
            continue;
        }
        if (argument == "--config" || argument == "--preset" || argument == "--preset-file" ||
            argument == "--format" || argument == "--piece-size" || argument == "--file-order" ||
            argument == "--tracker" || argument == "--tier" || argument == "--web-seed" ||
            argument == "--comment" || argument == "--created-by" || argument == "--source" ||
            argument == "--creation-date")
        {
            std::string value;
            if (!require_value(index, argc, argv, argument, value, error))
            {
                return false;
            }
            if (argument == "--config")
            {
                if (arguments.config_path)
                {
                    error = "--config was specified more than once";
                    return false;
                }
                arguments.config_path = path_from_utf8(value);
            }
            else if (argument == "--preset" || argument == "--preset-file")
            {
                if (arguments.preset_name || arguments.preset_file)
                {
                    error = "--preset and --preset-file are mutually exclusive";
                    return false;
                }
                if (argument == "--preset")
                {
                    arguments.preset_name = value;
                }
                else
                {
                    arguments.preset_file = path_from_utf8(value);
                }
            }
            else if (argument == "--format")
            {
                if (!parse_format(value, arguments.cli_settings, error))
                {
                    return false;
                }
            }
            else if (argument == "--piece-size")
            {
                if (!parse_piece_size(value, arguments.cli_settings, error))
                {
                    return false;
                }
            }
            else if (argument == "--file-order")
            {
                if (!parse_file_order(value, arguments.cli_settings, error))
                {
                    return false;
                }
            }
            else if (argument == "--tracker")
            {
                arguments.tracker_specified = true;
                std::size_t tier = 0;
                if (index + 2 < argc && std::string_view(argv[index + 1]) == "--tier")
                {
                    const auto parsed_tier = parse_unsigned(argv[index + 2]);
                    if (!parsed_tier || *parsed_tier > 64U)
                    {
                        error = "--tier must be an integer between 0 and 64";
                        return false;
                    }
                    tier = static_cast<std::size_t>(*parsed_tier);
                    index += 2;
                }
                arguments.trackers.emplace_back(tier, std::move(value));
            }
            else if (argument == "--tier")
            {
                error = "--tier must follow a --tracker option";
                return false;
            }
            else if (argument == "--web-seed")
            {
                if (!arguments.cli_settings.web_seeds)
                {
                    arguments.cli_settings.web_seeds = std::vector<std::string>{};
                }
                arguments.cli_settings.web_seeds->push_back(std::move(value));
            }
            else if (argument == "--creation-date")
            {
                const auto parsed = parse_unsigned(value);
                if (!parsed || *parsed > static_cast<std::uint64_t>(
                                             (std::numeric_limits<std::int64_t>::max)()))
                {
                    error = "--creation-date must be a non-negative unix timestamp";
                    return false;
                }
                if (arguments.creation_date)
                {
                    error = "--creation-date was specified more than once";
                    return false;
                }
                arguments.creation_date = static_cast<std::int64_t>(*parsed);
            }
            else if (argument == "--comment")
            {
                arguments.cli_settings.comment = std::move(value);
            }
            else if (argument == "--created-by")
            {
                arguments.cli_settings.created_by = std::move(value);
            }
            else if (argument == "--source")
            {
                arguments.cli_settings.info_source = std::move(value);
            }
            continue;
        }
        if (argument == "--private")
        {
            arguments.cli_settings.is_private = true;
            continue;
        }
        if (argument == "--no-private" || argument == "--public")
        {
            arguments.cli_settings.is_private = false;
            continue;
        }
        if (argument == "--overwrite")
        {
            arguments.overwrite = true;
            continue;
        }
        if (argument == "--dry-run")
        {
            arguments.dry_run = true;
            continue;
        }
        if (argument == "--quiet")
        {
            arguments.quiet = true;
            continue;
        }
        if (argument == "--json")
        {
            arguments.json = true;
            continue;
        }
        if (!argument.empty() && argument.front() == '-')
        {
            error = "unknown option: " + std::string(argument);
            return false;
        }
        positional.emplace_back(argument);
    }

    if (positional.size() != 1U)
    {
        error = "create requires exactly one content path";
        return false;
    }
    arguments.content = path_from_utf8(positional.front());
    if (arguments.target.empty())
    {
        error = "create requires -o or --output";
        return false;
    }
    if (arguments.tracker_specified)
    {
        std::size_t maximum_tier = 0;
        for (const auto& tracker : arguments.trackers)
        {
            maximum_tier = std::max(maximum_tier, tracker.first);
        }
        std::vector<std::vector<std::string>> tiers(maximum_tier + 1U);
        for (auto& tracker : arguments.trackers)
        {
            tiers[tracker.first].push_back(std::move(tracker.second));
        }
        arguments.cli_settings.tracker_tiers = std::move(tiers);
    }
    return true;
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
        diagnostics << result.dump() << '\n';
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

void print_success(const CreateArguments& arguments, const ResolvedCreationSettings& settings,
                   const std::optional<torrentutils::core::CreateResult>& result,
                   std::ostream& output)
{
    if (arguments.json)
    {
        Json value;
        value["ok"] = true;
        value["data"]["dry_run"] = arguments.dry_run;
        value["data"]["target"] = arguments.target.u8string();
        value["data"]["format"] = format_name(settings.options.format());
        value["data"]["file_order"] = file_order_name(settings.options.file_order_policy());
        value["data"]["private"] = settings.options.is_private();
        if (settings.creation_metadata.creation_time_unix_seconds)
        {
            value["data"]["creation_date"] = *settings.creation_metadata.creation_time_unix_seconds;
        }
        if (result)
        {
            value["data"]["payload_bytes"] = result->payload_bytes;
            value["data"]["piece_length"] = result->piece_length;
            const auto& v1 = result->info_hashes.v1();
            if (v1)
            {
                value["data"]["info_hash_v1"] = v1->to_hex();
            }
            const auto& v2 = result->info_hashes.v2();
            if (v2)
            {
                value["data"]["info_hash_v2"] = v2->to_hex();
            }
        }
        output << value.dump() << '\n';
        return;
    }
    if (!arguments.quiet)
    {
        output << (arguments.dry_run ? "create dry-run valid: " : "created: ");
        write_human_text(output, arguments.target.u8string());
        output << "\n";
    }
}

[[nodiscard]] Result<ResolvedCreationSettings>
resolve_create_settings(const CreateArguments& arguments,
                        std::optional<std::uint64_t>& memory_working_set_limit_bytes)
{
    memory_working_set_limit_bytes.reset();
    CreationSettingsPatch effective;
    std::optional<ConfigFile> config;
    {
        std::error_code error;
        const auto working_directory = std::filesystem::current_path(error);
        if (error)
        {
            return Result<ResolvedCreationSettings>::failure(
                {ErrorCode::IoFailure, "could not determine current directory", {}});
        }
        const auto paths = torrentutils::frontend::default_config_search_paths(
            arguments.config_path, working_directory);
        auto discovered = torrentutils::frontend::discover_config(paths);
        if (!discovered)
        {
            return Result<ResolvedCreationSettings>::failure(std::move(discovered).error());
        }
        const auto discovered_path = discovered.value();
        if (!discovered_path)
        {
            if (arguments.preset_name)
            {
                return Result<ResolvedCreationSettings>::failure(
                    {ErrorCode::ValidationFailed, "named preset requires a config file", {}});
            }
        }
        else
        {
            auto loaded = ConfigFile::load(*discovered_path);
            if (!loaded)
            {
                return Result<ResolvedCreationSettings>::failure(std::move(loaded).error());
            }
            config.emplace(std::move(loaded).value());
            effective = config->parsed().defaults;
            memory_working_set_limit_bytes = config->parsed().memory_working_set_limit_bytes;
        }
    }
    if (arguments.preset_name)
    {
        if (!config)
        {
            return Result<ResolvedCreationSettings>::failure(
                {ErrorCode::ValidationFailed, "named preset requires a config file", {}});
        }
        const auto preset = config->parsed().presets.find(*arguments.preset_name);
        if (preset == config->parsed().presets.end())
        {
            return Result<ResolvedCreationSettings>::failure(
                {ErrorCode::ValidationFailed,
                 "named preset was not found",
                 {{"preset", *arguments.preset_name}}});
        }
        effective = torrentutils::frontend::overlay_settings(effective, preset->second);
    }
    if (arguments.preset_file)
    {
        auto preset = torrentutils::frontend::load_preset_file(*arguments.preset_file);
        if (!preset)
        {
            return Result<ResolvedCreationSettings>::failure(std::move(preset).error());
        }
        effective = torrentutils::frontend::overlay_settings(effective, preset.value().settings);
    }
    effective = torrentutils::frontend::overlay_settings(effective, arguments.cli_settings);
    auto resolved = torrentutils::frontend::resolve_settings(effective);
    if (resolved)
    {
        resolved.value().disk_io = config ? config->parsed().disk_io : std::nullopt;
    }
    return resolved;
}

struct ConfigInvocation
{
    std::optional<std::filesystem::path> config_path;
    bool json{};
    bool quiet{};
    bool dry_run{};
    bool backup{};
    bool force{};
    std::vector<std::string> positional;
};

[[nodiscard]] bool parse_config_invocation(const std::vector<std::string>& args,
                                           ConfigInvocation& result, std::string& error)
{
    for (std::size_t index = 0; index < args.size(); ++index)
    {
        const auto& argument = args[index];
        if (argument == "--config")
        {
            if (index + 1 >= args.size())
            {
                error = "--config requires a value";
                return false;
            }
            if (result.config_path)
            {
                error = "--config was specified more than once";
                return false;
            }
            result.config_path = path_from_utf8(args[++index]);
        }
        else if (argument == "--json")
        {
            result.json = true;
        }
        else if (argument == "--quiet")
        {
            result.quiet = true;
        }
        else if (argument == "--dry-run")
        {
            result.dry_run = true;
        }
        else if (argument == "--backup")
        {
            result.backup = true;
        }
        else if (argument == "--force")
        {
            result.force = true;
        }
        else if (!argument.empty() && argument.front() == '-')
        {
            error = "unknown option: " + argument;
            return false;
        }
        else
        {
            result.positional.push_back(argument);
        }
    }
    return true;
}

[[nodiscard]] Result<ConfigFile> load_management_config(const ConfigInvocation& invocation)
{
    std::error_code error;
    const auto working_directory = std::filesystem::current_path(error);
    if (error)
    {
        return Result<ConfigFile>::failure(
            {ErrorCode::IoFailure, "could not determine current directory", {}});
    }
    const auto paths = torrentutils::frontend::default_config_search_paths(invocation.config_path,
                                                                           working_directory);
    auto discovered = torrentutils::frontend::discover_config(paths);
    if (!discovered)
    {
        return Result<ConfigFile>::failure(std::move(discovered).error());
    }
    const auto discovered_path = std::move(discovered).value();
    if (!discovered_path)
    {
        return Result<ConfigFile>::failure({ErrorCode::FileNotFound, "no config file found", {}});
    }
    return ConfigFile::load(*discovered_path);
}

[[nodiscard]] Result<void> make_backup(const std::filesystem::path& path)
{
    std::error_code error;
    if (!std::filesystem::exists(path, error))
    {
        return Result<void>::success();
    }
    const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
    auto backup = path;
    backup += (".bak-" + std::to_string(stamp));
    std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing,
                               error);
    if (error)
    {
        return Result<void>::failure({ErrorCode::IoFailure, "could not create backup file", {}});
    }
    return Result<void>::success();
}

[[nodiscard]] std::string pretty_json(const std::string& compact)
{
    auto document = Json::parse(compact, nullptr, false);
    if (document.is_discarded())
    {
        return compact;
    }
    return document.dump(2);
}

[[nodiscard]] int run_config_command(const std::vector<std::string>& args, std::ostream& output,
                                     std::ostream& diagnostics)
{
    if (help_requested(args))
    {
        output << kConfigHelp;
        return 0;
    }
    ConfigInvocation invocation;
    std::string parse_error;
    if (!parse_config_invocation(args, invocation, parse_error))
    {
        diagnostics << "error: " << parse_error << '\n';
        return 2;
    }
    if (invocation.positional.empty())
    {
        diagnostics << "error: config requires a subcommand\n";
        return 2;
    }
    const auto& subcommand = invocation.positional[0];

    if (subcommand == "path")
    {
        std::error_code error;
        const auto working_directory = std::filesystem::current_path(error);
        if (error)
        {
            print_error({ErrorCode::IoFailure, "could not determine current directory", {}},
                        invocation.json, diagnostics);
            return 8;
        }
        const auto paths = torrentutils::frontend::default_config_search_paths(
            invocation.config_path, working_directory);
        auto discovered = torrentutils::frontend::discover_config(paths);
        if (!discovered)
        {
            print_error(discovered.error(), invocation.json, diagnostics);
            return error_exit_code(discovered.error().code);
        }
        const auto discovered_path = std::move(discovered).value();
        if (!discovered_path)
        {
            print_error({ErrorCode::FileNotFound, "no config file found", {}}, invocation.json,
                        diagnostics);
            return 4;
        }
        if (invocation.json)
        {
            output << Json{{"ok", true}, {"data", {{"path", discovered_path->u8string()}}}}.dump()
                   << '\n';
        }
        else if (!invocation.quiet)
        {
            write_human_text(output, discovered_path->u8string());
            output << "\n";
        }
        return 0;
    }

    if (subcommand == "init")
    {
        std::error_code error;
        const auto working_directory = std::filesystem::current_path(error);
        if (error)
        {
            print_error({ErrorCode::IoFailure, "could not determine current directory", {}},
                        invocation.json, diagnostics);
            return 8;
        }
        std::filesystem::path target;
        if (invocation.config_path)
        {
            target = *invocation.config_path;
        }
        else
        {
            const auto paths = torrentutils::frontend::default_config_search_paths(
                std::nullopt, working_directory);
            std::vector<std::filesystem::path> candidates;
            candidates.push_back(working_directory / "torrentcraft.json");
            if (paths.user_config_path)
            {
                candidates.push_back(*paths.user_config_path);
            }
            bool chosen = false;
            for (const auto& candidate : candidates)
            {
                std::error_code candidate_error;
                if (!std::filesystem::exists(candidate, candidate_error))
                {
                    target = candidate;
                    chosen = true;
                    break;
                }
            }
            if (!chosen)
            {
                target = candidates.front();
            }
        }
        auto created = ConfigFile::create(target, invocation.force);
        if (!created)
        {
            print_error(created.error(), invocation.json, diagnostics);
            return error_exit_code(created.error().code);
        }
        if (invocation.json)
        {
            output << Json{{"ok", true}, {"data", {{"path", target.u8string()}}}}.dump() << '\n';
        }
        else if (!invocation.quiet)
        {
            output << "config initialized: ";
            write_human_text(output, target.u8string());
            output << "\n";
        }
        return 0;
    }

    if (subcommand == "show")
    {
        auto loaded = load_management_config(invocation);
        if (!loaded)
        {
            print_error(loaded.error(), invocation.json, diagnostics);
            return error_exit_code(loaded.error().code);
        }
        auto document = loaded.value().document_json();
        if (!document)
        {
            print_error(document.error(), invocation.json, diagnostics);
            return error_exit_code(document.error().code);
        }
        if (invocation.json)
        {
            output << document.value() << '\n';
        }
        else
        {
            write_human_text(output, pretty_json(document.value()));
            output << "\n";
        }
        return 0;
    }

    if (subcommand == "get")
    {
        if (invocation.positional.size() != 2U)
        {
            diagnostics << "error: config get requires exactly one key\n";
            return 2;
        }
        auto key = torrentutils::frontend::parse_config_key(invocation.positional[1]);
        if (!key)
        {
            print_error(key.error(), invocation.json, diagnostics);
            return error_exit_code(key.error().code);
        }
        auto loaded = load_management_config(invocation);
        if (!loaded)
        {
            print_error(loaded.error(), invocation.json, diagnostics);
            return error_exit_code(loaded.error().code);
        }
        auto value = loaded.value().get_key(key.value());
        if (!value)
        {
            print_error(value.error(), invocation.json, diagnostics);
            return error_exit_code(value.error().code);
        }
        if (invocation.json)
        {
            output << value.value() << '\n';
        }
        else
        {
            write_human_text(output, pretty_json(value.value()));
            output << "\n";
        }
        return 0;
    }

    if (subcommand == "set")
    {
        if (invocation.positional.size() != 3U)
        {
            diagnostics << "error: config set requires a key and a JSON value\n";
            return 2;
        }
        auto key = torrentutils::frontend::parse_config_key(invocation.positional[1]);
        if (!key)
        {
            print_error(key.error(), invocation.json, diagnostics);
            return error_exit_code(key.error().code);
        }
        auto loaded = load_management_config(invocation);
        if (!loaded)
        {
            print_error(loaded.error(), invocation.json, diagnostics);
            return error_exit_code(loaded.error().code);
        }
        std::optional<std::string> value;
        if (invocation.positional[2] != "null")
        {
            value = invocation.positional[2];
        }
        auto applied = loaded.value().set_key(key.value(), value);
        if (!applied)
        {
            print_error(applied.error(), invocation.json, diagnostics);
            return error_exit_code(applied.error().code);
        }
        if (!invocation.dry_run)
        {
            if (invocation.backup)
            {
                auto backup = make_backup(loaded.value().path());
                if (!backup)
                {
                    print_error(backup.error(), invocation.json, diagnostics);
                    return error_exit_code(backup.error().code);
                }
            }
            auto saved = loaded.value().save();
            if (!saved)
            {
                print_error(saved.error(), invocation.json, diagnostics);
                return error_exit_code(saved.error().code);
            }
        }
        if (invocation.json)
        {
            output << Json{{"ok", true},
                           {"data",
                            {{"path", loaded.value().path().u8string()},
                             {"key", invocation.positional[1]},
                             {"dry_run", invocation.dry_run}}}}
                          .dump()
                   << '\n';
        }
        else if (!invocation.quiet)
        {
            output << (invocation.dry_run ? "config dry-run valid: " : "config updated: ")
                   << invocation.positional[1] << '\n';
        }
        return 0;
    }

    diagnostics << "error: unknown config subcommand: " << subcommand << '\n';
    return 2;
}

[[nodiscard]] int run_preset_command(const std::vector<std::string>& args, std::ostream& output,
                                     std::ostream& diagnostics)
{
    if (help_requested(args))
    {
        output << kPresetHelp;
        return 0;
    }
    ConfigInvocation invocation;
    std::string parse_error;
    if (!parse_config_invocation(args, invocation, parse_error))
    {
        diagnostics << "error: " << parse_error << '\n';
        return 2;
    }
    if (invocation.positional.empty())
    {
        diagnostics << "error: preset requires a subcommand\n";
        return 2;
    }
    const auto& subcommand = invocation.positional[0];

    if (subcommand == "list")
    {
        std::optional<ConfigFile> config;
        if (invocation.config_path)
        {
            auto loaded = ConfigFile::load(*invocation.config_path);
            if (!loaded)
            {
                print_error(loaded.error(), invocation.json, diagnostics);
                return error_exit_code(loaded.error().code);
            }
            config.emplace(std::move(loaded).value());
        }
        else
        {
            auto loaded = load_management_config(invocation);
            if (loaded)
            {
                config.emplace(std::move(loaded).value());
            }
            else if (loaded.error().code != ErrorCode::FileNotFound)
            {
                print_error(loaded.error(), invocation.json, diagnostics);
                return error_exit_code(loaded.error().code);
            }
        }
        std::vector<std::string> names;
        if (config)
        {
            for (const auto& entry : config->parsed().presets)
            {
                names.push_back(entry.first);
            }
        }
        if (invocation.json)
        {
            output << Json{{"ok", true}, {"data", {{"presets", names}}}}.dump() << '\n';
        }
        else if (!invocation.quiet)
        {
            for (const auto& name : names)
            {
                write_human_text(output, name);
                output << "\n";
            }
        }
        return 0;
    }

    if (subcommand == "show")
    {
        if (invocation.positional.size() != 2U)
        {
            diagnostics << "error: preset show requires exactly one name\n";
            return 2;
        }
        auto loaded = load_management_config(invocation);
        if (!loaded)
        {
            print_error(loaded.error(), invocation.json, diagnostics);
            return error_exit_code(loaded.error().code);
        }
        auto document_result = loaded.value().document_json();
        if (!document_result)
        {
            print_error(document_result.error(), invocation.json, diagnostics);
            return error_exit_code(document_result.error().code);
        }
        auto document = Json::parse(document_result.value(), nullptr, false);
        const auto& name = invocation.positional[1];
        if (document.is_discarded() || !document.is_object() ||
            document.find("presets") == document.end() ||
            document["presets"].find(name) == document["presets"].end())
        {
            print_error({ErrorCode::FileNotFound, "preset was not found", {{"preset", name}}},
                        invocation.json, diagnostics);
            return 4;
        }
        const auto& preset = document["presets"][name];
        if (invocation.json)
        {
            output << preset.dump() << '\n';
        }
        else
        {
            write_human_text(output, preset.dump(2));
            output << "\n";
        }
        return 0;
    }

    if (subcommand == "add")
    {
        if (invocation.positional.size() != 2U)
        {
            diagnostics << "error: preset add requires exactly one file\n";
            return 2;
        }
        auto loaded = load_management_config(invocation);
        if (!loaded)
        {
            print_error(loaded.error(), invocation.json, diagnostics);
            return error_exit_code(loaded.error().code);
        }
        const auto& file = invocation.positional[1];
        auto parsed = torrentutils::frontend::load_preset_file(path_from_utf8(file));
        if (!parsed)
        {
            print_error(parsed.error(), invocation.json, diagnostics);
            return error_exit_code(parsed.error().code);
        }
        std::string name = path_from_utf8(file).stem().u8string();
        if (name.rfind("preset_", 0) == 0)
        {
            name = name.substr(7);
        }
        if (name.empty())
        {
            print_error({ErrorCode::ValidationFailed, "preset name must not be empty", {}},
                        invocation.json, diagnostics);
            return 3;
        }
        auto added = loaded.value().add_preset(name, parsed.value().settings, invocation.force);
        if (!added)
        {
            print_error(added.error(), invocation.json, diagnostics);
            return error_exit_code(added.error().code);
        }
        if (!invocation.dry_run)
        {
            if (invocation.backup)
            {
                auto backup = make_backup(loaded.value().path());
                if (!backup)
                {
                    print_error(backup.error(), invocation.json, diagnostics);
                    return error_exit_code(backup.error().code);
                }
            }
            auto saved = loaded.value().save();
            if (!saved)
            {
                print_error(saved.error(), invocation.json, diagnostics);
                return error_exit_code(saved.error().code);
            }
        }
        if (invocation.json)
        {
            output << Json{{"ok", true},
                           {"data",
                            {{"path", loaded.value().path().u8string()},
                             {"preset", name},
                             {"dry_run", invocation.dry_run}}}}
                          .dump()
                   << '\n';
        }
        else if (!invocation.quiet)
        {
            output << (invocation.dry_run ? "preset dry-run valid: " : "preset added: ");
            write_human_text(output, name);
            output << "\n";
        }
        return 0;
    }

    if (subcommand == "remove")
    {
        if (invocation.positional.size() != 2U)
        {
            diagnostics << "error: preset remove requires exactly one name\n";
            return 2;
        }
        auto loaded = load_management_config(invocation);
        if (!loaded)
        {
            print_error(loaded.error(), invocation.json, diagnostics);
            return error_exit_code(loaded.error().code);
        }
        const auto& name = invocation.positional[1];
        auto removed = loaded.value().remove_preset(name);
        if (!removed)
        {
            print_error(removed.error(), invocation.json, diagnostics);
            return error_exit_code(removed.error().code);
        }
        if (!invocation.dry_run)
        {
            if (invocation.backup)
            {
                auto backup = make_backup(loaded.value().path());
                if (!backup)
                {
                    print_error(backup.error(), invocation.json, diagnostics);
                    return error_exit_code(backup.error().code);
                }
            }
            auto saved = loaded.value().save();
            if (!saved)
            {
                print_error(saved.error(), invocation.json, diagnostics);
                return error_exit_code(saved.error().code);
            }
        }
        if (invocation.json)
        {
            output << Json{{"ok", true},
                           {"data",
                            {{"path", loaded.value().path().u8string()},
                             {"preset", name},
                             {"dry_run", invocation.dry_run}}}}
                          .dump()
                   << '\n';
        }
        else if (!invocation.quiet)
        {
            output << (invocation.dry_run ? "preset dry-run valid: " : "preset removed: ");
            write_human_text(output, name);
            output << "\n";
        }
        return 0;
    }

    diagnostics << "error: unknown preset subcommand: " << subcommand << '\n';
    return 2;
}

[[nodiscard]] std::string
capability_code_name(const torrentutils::core::VerificationCapabilityDiagnosticCode code)
{
    switch (code)
    {
    case torrentutils::core::VerificationCapabilityDiagnosticCode::UnsupportedTorrentFormat:
        return "unsupported_torrent_format";
    case torrentutils::core::VerificationCapabilityDiagnosticCode::UnsupportedPieceHashScheme:
        return "unsupported_piece_hash_scheme";
    case torrentutils::core::VerificationCapabilityDiagnosticCode::UnsupportedFileLayout:
        return "unsupported_file_layout";
    case torrentutils::core::VerificationCapabilityDiagnosticCode::UnsupportedFileAttribute:
        return "unsupported_file_attribute";
    case torrentutils::core::VerificationCapabilityDiagnosticCode::UnsupportedSymlinkSemantics:
        return "unsupported_symlink_semantics";
    case torrentutils::core::VerificationCapabilityDiagnosticCode::BackendFeatureUnavailable:
        return "backend_feature_unavailable";
    }
    return "backend_feature_unavailable";
}

[[nodiscard]] std::string finding_name(const torrentutils::core::FileVerificationFinding finding)
{
    switch (finding)
    {
    case torrentutils::core::FileVerificationFinding::Missing:
        return "missing";
    case torrentutils::core::FileVerificationFinding::NotRegularFile:
        return "not_regular_file";
    case torrentutils::core::FileVerificationFinding::LengthMismatch:
        return "length_mismatch";
    case torrentutils::core::FileVerificationFinding::HashMismatch:
        return "hash_mismatch";
    case torrentutils::core::FileVerificationFinding::SharedPieceMismatch:
        return "shared_piece_mismatch";
    case torrentutils::core::FileVerificationFinding::SymlinkMissing:
        return "symlink_missing";
    case torrentutils::core::FileVerificationFinding::SymlinkTargetMismatch:
        return "symlink_target_mismatch";
    case torrentutils::core::FileVerificationFinding::None:
        return "none";
    }
    return "none";
}

[[nodiscard]] Json file_attributes_json(const torrentutils::core::FileAttributes& attributes)
{
    return {{"padding", attributes.padding},
            {"executable", attributes.executable},
            {"hidden", attributes.hidden},
            {"symlink", attributes.symlink}};
}

[[nodiscard]] Json make_file_tree_node(const std::string& name,
                                       const torrentutils::core::FileEntry& file)
{
    Json node;
    node["name"] = name;
    node["type"] = "file";
    node["length"] = file.length();
    node["attributes"] = file_attributes_json(file.attributes());
    if (const auto& target = file.symlink_target())
    {
        node["symlink_target"] = target->to_string();
    }
    return node;
}

void insert_tree_node(Json& children, const std::vector<std::string>& segments,
                      const std::size_t index, const torrentutils::core::FileEntry& file,
                      const std::optional<std::size_t>& max_depth, const std::size_t level)
{
    const auto& name = segments[index];
    if (index + 1 == segments.size())
    {
        children.push_back(make_file_tree_node(name, file));
        return;
    }

    Json* directory = nullptr;
    for (auto& child : children)
    {
        if (child.value("type", std::string{}) == "directory" &&
            child.value("name", std::string{}) == name)
        {
            directory = &child;
            break;
        }
    }
    if (directory == nullptr)
    {
        Json node;
        node["name"] = name;
        node["type"] = "directory";
        node["children"] = Json::array();
        children.push_back(std::move(node));
        directory = &children.back();
    }
    if (max_depth && level >= *max_depth)
    {
        return;
    }
    insert_tree_node((*directory)["children"], segments, index + 1, file, max_depth, level + 1);
}

void prune_empty_directories(Json& node)
{
    if (node.value("type", std::string{}) != "directory" || !node.contains("children"))
    {
        return;
    }
    Json kept = Json::array();
    for (auto& child : node["children"])
    {
        prune_empty_directories(child);
        if (child.value("type", std::string{}) == "directory" &&
            (!child.contains("children") || child["children"].empty()))
        {
            continue;
        }
        kept.push_back(std::move(child));
    }
    node["children"] = std::move(kept);
}

[[nodiscard]] Json build_file_tree(const torrentutils::core::TorrentInfo& info,
                                   const std::optional<std::size_t>& max_depth)
{
    const auto& files = info.files();
    const bool single_file = files.size() == 1U && !files[0].attributes().padding &&
                             files[0].path().segments().size() == 1U &&
                             files[0].path().segments()[0] == info.name();
    if (single_file)
    {
        return make_file_tree_node(info.name(), files[0]);
    }

    Json root;
    root["name"] = info.name();
    root["type"] = "directory";
    root["children"] = Json::array();
    for (const auto& file : files)
    {
        // BEP 52 pad files are layout alignment artifacts, not real content;
        // hide them from the tree like qBittorrent's file list does.
        if (file.attributes().padding)
        {
            continue;
        }
        auto segments = file.path().segments();
        if (!segments.empty() && segments.front() == info.name())
        {
            segments.erase(segments.begin());
        }
        if (segments.empty())
        {
            root["children"].push_back(make_file_tree_node(info.name(), file));
            continue;
        }
        if (max_depth && *max_depth == 0U)
        {
            continue;
        }
        insert_tree_node(root["children"], segments, 0U, file, max_depth, 1U);
    }
    prune_empty_directories(root);
    return root;
}

struct TreeGlyphs
{
    std::string tee;
    std::string last;
    std::string vertical;
    std::string horizontal;
};

[[nodiscard]] TreeGlyphs tree_glyphs() noexcept
{
    // Human-readable output uses the explicit UTF-8/UTF-16 console path.
    // Keep it independent from the ASCII-only live progress bar.
    return {"├", "└", "│", "── "};
}
void print_tree_human(const Json& node, const std::string& prefix, const bool is_last,
                      const bool is_root, const TreeGlyphs& glyphs, std::ostream& output)
{
    if (!is_root)
    {
        output << prefix << (is_last ? glyphs.last : glyphs.tee) << glyphs.horizontal;
    }
    write_human_text(output, node.value("name", std::string{}));
    if (node.value("type", std::string{}) == "directory")
    {
        output << "/\n";
        if (node.contains("children"))
        {
            const auto& children = node["children"];
            const auto child_prefix =
                is_root ? std::string{} : prefix + (is_last ? "    " : glyphs.vertical + "   ");
            for (std::size_t index = 0; index < children.size(); ++index)
            {
                print_tree_human(children[index], child_prefix, index + 1U == children.size(),
                                 false, glyphs, output);
            }
        }
        return;
    }
    output << "  (" << node.value("length", std::uint64_t{}) << " bytes)";
    if (node.contains("symlink_target"))
    {
        output << " -> ";
        write_human_text(output, node["symlink_target"].get<std::string>());
    }
    output << '\n';
}

[[nodiscard]] const char*
metadata_field_scope_name(const torrentutils::core::MetadataFieldScope scope) noexcept
{
    switch (scope)
    {
    case torrentutils::core::MetadataFieldScope::TopLevel:
        return "top-level";
    case torrentutils::core::MetadataFieldScope::Info:
        return "info";
    case torrentutils::core::MetadataFieldScope::InfoV1File:
        return "info.files";
    case torrentutils::core::MetadataFieldScope::InfoV2FileTreeLeaf:
        return "info.file tree";
    }
    return "top-level";
}

[[nodiscard]] Json metadata_fields_json(const torrentutils::core::TorrentDocument& document)
{
    Json groups{
        {"standard", Json::array()}, {"extension", Json::array()}, {"unknown", Json::array()}};
    std::set<std::string> seen;
    for (const auto& field : document.metadata_fields())
    {
        std::string fingerprint = field.key;
        fingerprint.push_back('\0');
        fingerprint += metadata_field_scope_name(field.scope);
        fingerprint.push_back('\0');
        fingerprint += field.source;
        fingerprint.push_back('\0');
        fingerprint += field.type;
        fingerprint.push_back('\0');
        fingerprint.push_back(field.info_hash ? '1' : '0');
        if (!seen.insert(std::move(fingerprint)).second)
        {
            continue;
        }
        Json item{{"key", field.key},
                  {"scope", metadata_field_scope_name(field.scope)},
                  {"source", field.source},
                  {"type", field.type},
                  {"info_hash", field.info_hash}};
        switch (field.category)
        {
        case torrentutils::core::MetadataFieldCategory::Standard:
            groups["standard"].push_back(std::move(item));
            break;
        case torrentutils::core::MetadataFieldCategory::Extension:
            groups["extension"].push_back(std::move(item));
            break;
        case torrentutils::core::MetadataFieldCategory::Unknown:
            groups["unknown"].push_back(std::move(item));
            break;
        }
    }
    return groups;
}

void print_field_table(const Json& entries, std::ostream& output)
{
    if (entries.empty())
    {
        return;
    }
    const std::array<std::string_view, 5> headers{"key", "scope", "source", "type", "info_hash"};
    std::array<std::size_t, 5> widths{};
    for (std::size_t index = 0; index < headers.size(); ++index)
    {
        widths[index] = headers[index].size();
    }
    for (const auto& entry : entries)
    {
        const std::array<std::string, 5> cells{
            entry["key"].get<std::string>(), entry["scope"].get<std::string>(),
            entry["source"].get<std::string>(), entry["type"].get<std::string>(),
            entry["info_hash"].get<bool>() ? "yes" : "no"};
        for (std::size_t index = 0; index < cells.size(); ++index)
        {
            widths[index] = std::max(widths[index], cells[index].size());
        }
    }
    const auto emit_row = [&](const std::array<std::string, 5>& cells) {
        std::string line = "    ";
        for (std::size_t index = 0; index < cells.size(); ++index)
        {
            line += cells[index];
            if (index + 1U < cells.size())
            {
                line.append(widths[index] + 2U - cells[index].size(), ' ');
            }
        }
        write_human_text(output, line);
        output << "\n";
    };
    emit_row({std::string(headers[0]), std::string(headers[1]), std::string(headers[2]),
              std::string(headers[3]), std::string(headers[4])});
    for (const auto& entry : entries)
    {
        emit_row({entry["key"].get<std::string>(), entry["scope"].get<std::string>(),
                  entry["source"].get<std::string>(), entry["type"].get<std::string>(),
                  entry["info_hash"].get<bool>() ? "yes" : "no"});
    }
}

[[nodiscard]] int run_inspect_command(const std::vector<std::string>& args, std::ostream& output,
                                      std::ostream& diagnostics)
{
    if (help_requested(args))
    {
        output << kInspectHelp;
        return 0;
    }
    bool json = false;
    bool quiet = false;
    std::vector<std::string> positional;
    for (const auto& argument : args)
    {
        if (argument == "--json")
        {
            json = true;
        }
        else if (argument == "--quiet")
        {
            quiet = true;
        }
        else if (!argument.empty() && argument.front() == '-')
        {
            diagnostics << "error: unknown option: " << argument << '\n';
            return 2;
        }
        else
        {
            positional.push_back(argument);
        }
    }
    if (positional.size() != 1U)
    {
        diagnostics << "error: inspect requires exactly one torrent path\n";
        return 2;
    }

    torrentutils::core::FileTorrentRepository repository;
    torrentutils::core::SystemClock clock;
    torrentutils::core::TorrentService service(repository, clock);
    auto loaded = service.load(path_from_utf8(positional[0]));
    if (!loaded)
    {
        print_error(loaded.error(), json, diagnostics);
        return error_exit_code(loaded.error().code);
    }
    auto report = service.inspect(loaded.value().document());
    if (!report)
    {
        print_error(report.error(), json, diagnostics);
        return error_exit_code(report.error().code);
    }

    const auto& document = loaded.value().document();
    const auto& info = document.info();
    Json data;
    data["name"] = info.name();
    data["format"] = format_name(info.format());
    data["private"] = info.is_private();
    Json hashes;
    hashes["v1"] =
        info.info_hashes().v1() ? Json(info.info_hashes().v1()->to_hex()) : Json(nullptr);
    hashes["v2"] =
        info.info_hashes().v2() ? Json(info.info_hashes().v2()->to_hex()) : Json(nullptr);
    data["info_hashes"] = std::move(hashes);
    data["piece_length"] = info.pieces().piece_length();
    data["payload_bytes"] = info.pieces().total_size();
    data["file_count"] = info.files().size();
    data["verification_capability"] = report.value().verification_capability ==
                                              torrentutils::core::VerificationCapability::Supported
                                          ? "supported"
                                          : "unsupported";
    Json diagnostics_json = Json::array();
    for (const auto& diagnostic : report.value().diagnostics)
    {
        diagnostics_json.push_back(
            {{"code", capability_code_name(diagnostic.code)}, {"message", diagnostic.message}});
    }
    data["capability_diagnostics"] = std::move(diagnostics_json);
    Json warnings = Json::array();
    for (const auto& warning : document.warnings())
    {
        warnings.push_back({{"field", warning.field}, {"message", warning.message}});
    }
    data["warnings"] = std::move(warnings);
    data["metadata_fields"] = metadata_fields_json(document);

    if (json)
    {
        output << Json{{"ok", true}, {"data", std::move(data)}}.dump() << '\n';
        return 0;
    }
    if (!quiet)
    {
        output << "name: ";
        write_human_text(output, info.name());
        output << "\n";
        output << "format: " << format_name(info.format()) << '\n';
        output << "private: " << (info.is_private() ? "true" : "false") << '\n';
        if (info.info_hashes().v1())
        {
            output << "info_hash_v1: " << info.info_hashes().v1()->to_hex() << '\n';
        }
        if (info.info_hashes().v2())
        {
            output << "info_hash_v2: " << info.info_hashes().v2()->to_hex() << '\n';
        }
        output << "piece_length: " << info.pieces().piece_length() << '\n';
        output << "payload_bytes: " << info.pieces().total_size() << '\n';
        output << "file_count: " << info.files().size() << '\n';
        output << "verification_capability: "
               << (report.value().verification_capability ==
                           torrentutils::core::VerificationCapability::Supported
                       ? "supported"
                       : "unsupported")
               << '\n';
        for (const auto& diagnostic : report.value().diagnostics)
        {
            output << "capability: " << capability_code_name(diagnostic.code) << ": ";
            write_human_text(output, diagnostic.message);
            output << "\n";
        }
        for (const auto& warning : document.warnings())
        {
            output << "warning: " << warning.field << ": ";
            write_human_text(output, warning.message);
            output << "\n";
        }
        const auto metadata_fields = metadata_fields_json(document);
        output << "metadata fields:\n";
        for (const char* group : {"standard", "extension", "unknown"})
        {
            const auto& entries = metadata_fields[group];
            const bool list_all = group != std::string_view{"standard"} || entries.size() <= 50U;
            output << "  " << group << " (" << entries.size() << ")\n";
            if (list_all)
            {
                print_field_table(entries, output);
            }
        }
    }
    return 0;
}

[[nodiscard]] int run_tree_command(const std::vector<std::string>& args, std::ostream& output,
                                   std::ostream& diagnostics)
{
    if (help_requested(args))
    {
        output << kTreeHelp;
        return 0;
    }
    bool json = false;
    bool quiet = false;
    std::optional<std::size_t> max_depth;
    std::vector<std::string> positional;
    for (std::size_t index = 0; index < args.size(); ++index)
    {
        const auto& argument = args[index];
        if (argument == "--json")
        {
            json = true;
        }
        else if (argument == "--quiet")
        {
            quiet = true;
        }
        else if (argument == "--depth")
        {
            if (index + 1 >= args.size())
            {
                diagnostics << "error: --depth requires a value\n";
                return 2;
            }
            const auto parsed = parse_unsigned(args[++index]);
            if (!parsed)
            {
                diagnostics << "error: --depth must be a non-negative integer\n";
                return 2;
            }
            max_depth = static_cast<std::size_t>(*parsed);
        }
        else if (!argument.empty() && argument.front() == '-')
        {
            diagnostics << "error: unknown option: " << argument << '\n';
            return 2;
        }
        else
        {
            positional.push_back(argument);
        }
    }
    if (positional.size() != 1U)
    {
        diagnostics << "error: tree requires exactly one torrent path\n";
        return 2;
    }

    torrentutils::core::FileTorrentRepository repository;
    torrentutils::core::SystemClock clock;
    torrentutils::core::TorrentService service(repository, clock);
    auto loaded = service.load(path_from_utf8(positional[0]));
    if (!loaded)
    {
        print_error(loaded.error(), json, diagnostics);
        return error_exit_code(loaded.error().code);
    }
    const auto root = build_file_tree(loaded.value().document().info(), max_depth);
    if (json)
    {
        output << Json{{"ok", true}, {"data", {{"tree", root}}}}.dump() << '\n';
    }
    else if (!quiet)
    {
        const auto glyphs = tree_glyphs();
        print_tree_human(root, std::string{}, true, true, glyphs, output);
    }
    return 0;
}

[[nodiscard]] int run_verify_command(const std::vector<std::string>& args, std::ostream& output,
                                     std::ostream& diagnostics)
{
    if (help_requested(args))
    {
        output << kVerifyHelp;
        return 0;
    }
    bool json = false;
    bool quiet = false;
    std::optional<ProgressMode> progress_mode;
    std::optional<std::filesystem::path> config_path;
    std::optional<std::uint32_t> verify_workers;
    std::optional<std::uint64_t> verify_memory;
    std::optional<std::uint64_t> memory_working_set_limit_bytes;
    std::vector<std::string> positional;
    const auto parse_value_option = [&](const std::string& option,
                                        const std::string& value) -> bool {
        if (option == "--config")
        {
            if (config_path)
            {
                diagnostics << "error: --config was specified more than once\n";
                return false;
            }
            config_path = path_from_utf8(value);
            return true;
        }
        if (option == "--verify-workers")
        {
            const auto parsed = parse_unsigned(value);
            if (!parsed || *parsed == 0U || *parsed > (std::numeric_limits<std::uint32_t>::max)())
            {
                diagnostics << "error: --verify-workers must be a positive integer\n";
                return false;
            }
            if (verify_workers)
            {
                diagnostics << "error: --verify-workers was specified more than once\n";
                return false;
            }
            verify_workers = static_cast<std::uint32_t>(*parsed);
            return true;
        }
        if (option == "--verify-memory")
        {
            const auto memory = torrentutils::frontend::parse_memory_size(value);
            if (!memory)
            {
                diagnostics << "error: --verify-memory must be a positive byte count or a "
                               "size with KiB/MiB/GiB suffix\n";
                return false;
            }
            if (verify_memory)
            {
                diagnostics << "error: --verify-memory was specified more than once\n";
                return false;
            }
            verify_memory = memory;
            return true;
        }
        if (option == "--memory-working-set-limit")
        {
            const auto parsed = torrentutils::frontend::parse_memory_size(value);
            if (!parsed)
            {
                diagnostics
                    << "error: --memory-working-set-limit must be a positive byte count or a "
                       "size with KiB/MiB/GiB suffix\n";
                return false;
            }
            if (memory_working_set_limit_bytes)
            {
                diagnostics << "error: --memory-working-set-limit was specified more than once\n";
                return false;
            }
            memory_working_set_limit_bytes = parsed;
            return true;
        }
        diagnostics << "error: unknown option: " << option << '\n';
        return false;
    };
    for (std::size_t index = 0; index < args.size(); ++index)
    {
        const auto& argument = args[index];
        if (argument == "--json")
        {
            json = true;
        }
        else if (argument == "--quiet")
        {
            quiet = true;
        }
        else if (argument == "--progress")
        {
            if (index + 1 >= args.size())
            {
                diagnostics << "error: --progress requires a value\n";
                return 2;
            }
            ProgressMode mode;
            std::string parse_error;
            if (!parse_progress_mode(args[++index], mode, parse_error))
            {
                diagnostics << "error: " << parse_error << '\n';
                return 2;
            }
            if (progress_mode)
            {
                diagnostics << "error: --progress was specified more than once\n";
                return 2;
            }
            progress_mode = mode;
        }
        else if (argument.rfind("--progress=", 0) == 0)
        {
            ProgressMode mode;
            std::string parse_error;
            if (!parse_progress_mode(argument.substr(11), mode, parse_error))
            {
                diagnostics << "error: " << parse_error << '\n';
                return 2;
            }
            if (progress_mode)
            {
                diagnostics << "error: --progress was specified more than once\n";
                return 2;
            }
            progress_mode = mode;
        }
        else if (argument == "--config" || argument == "--verify-workers" ||
                 argument == "--verify-memory" || argument == "--memory-working-set-limit")
        {
            if (index + 1 >= args.size())
            {
                diagnostics << "error: " << argument << " requires a value\n";
                return 2;
            }
            if (!parse_value_option(argument, args[++index]))
            {
                return 2;
            }
        }
        else if (argument.rfind("--config=", 0) == 0)
        {
            if (!parse_value_option("--config",
                                    argument.substr(std::string_view("--config=").size())))
            {
                return 2;
            }
        }
        else if (argument.rfind("--verify-workers=", 0) == 0)
        {
            if (!parse_value_option("--verify-workers",
                                    argument.substr(std::string_view("--verify-workers=").size())))
            {
                return 2;
            }
        }
        else if (argument.rfind("--verify-memory=", 0) == 0)
        {
            if (!parse_value_option("--verify-memory",
                                    argument.substr(std::string_view("--verify-memory=").size())))
            {
                return 2;
            }
        }
        else if (argument.rfind("--memory-working-set-limit=", 0) == 0)
        {
            if (!parse_value_option(
                    "--memory-working-set-limit",
                    argument.substr(std::string_view("--memory-working-set-limit=").size())))
            {
                return 2;
            }
        }
        else if (!argument.empty() && argument.front() == '-')
        {
            diagnostics << "error: unknown option: " << argument << '\n';
            return 2;
        }
        else
        {
            positional.push_back(argument);
        }
    }
    if (positional.size() != 2U)
    {
        diagnostics << "error: verify requires a torrent path and a content path\n";
        return 2;
    }

    std::optional<ConfigFile> config;
    {
        std::error_code error;
        const auto working_directory = std::filesystem::current_path(error);
        if (error)
        {
            print_error({ErrorCode::IoFailure, "could not determine current directory", {}}, json,
                        diagnostics);
            return 8;
        }
        const auto paths =
            torrentutils::frontend::default_config_search_paths(config_path, working_directory);
        auto discovered = torrentutils::frontend::discover_config(paths);
        if (!discovered)
        {
            print_error(discovered.error(), json, diagnostics);
            return error_exit_code(discovered.error().code);
        }
        if (discovered.value())
        {
            auto loaded = ConfigFile::load(*discovered.value());
            if (!loaded)
            {
                print_error(loaded.error(), json, diagnostics);
                return error_exit_code(loaded.error().code);
            }
            config.emplace(std::move(loaded).value());
        }
    }

    const auto configured_working_set_limit =
        config && config->parsed().memory_working_set_limit_bytes
            ? *config->parsed().memory_working_set_limit_bytes
            : torrentutils::frontend::default_memory_working_set_limit_bytes;
    const auto effective_working_set_limit =
        memory_working_set_limit_bytes.value_or(configured_working_set_limit);
    auto working_set_applied =
        torrentutils::frontend::apply_memory_working_set_limit_bytes(effective_working_set_limit);
    if (!working_set_applied)
    {
        diagnostics << "warning: ";
        write_human_text(diagnostics, working_set_applied.error().message);
        diagnostics << "\n";
    }

    torrentutils::core::FileTorrentRepository repository;
    torrentutils::core::SystemClock clock;
    torrentutils::core::TorrentService service(repository, clock);
    auto loaded = service.load(path_from_utf8(positional[0]));
    if (!loaded)
    {
        print_error(loaded.error(), json, diagnostics);
        return error_exit_code(loaded.error().code);
    }

    torrentutils::core::VerifyRequest request(loaded.value().document(),
                                              path_from_utf8(positional[1]));
    if (config)
    {
        request.disk_io_mode = config->parsed().disk_io;
    }
    VerifyResourceSettings verify_settings;
    if (config && config->parsed().verify)
    {
        verify_settings = *config->parsed().verify;
    }
    verify_settings = torrentutils::frontend::overlay_verify_settings(
        verify_settings, VerifyResourceSettings{verify_workers, verify_memory});
    auto budget = torrentutils::frontend::resolve_verify_resource_budget(verify_settings);
    if (!budget)
    {
        print_error(budget.error(), json, diagnostics);
        return error_exit_code(budget.error().code);
    }
    request.resource_budget = std::move(budget).value();
    const auto stderr_is_tty = diagnostics_is_tty(diagnostics);
    const auto effective_progress =
        progress_mode.value_or(stderr_is_tty ? ProgressMode::Tty : ProgressMode::None);
    ProgressWriter writer(effective_progress, stderr_is_tty, quiet, diagnostics);
    const auto& info = loaded.value().document().info();
    std::uint64_t total_bytes = 0;
    for (const auto& file : info.files())
    {
        total_bytes += file.length();
    }
    writer.verify_start(static_cast<std::uint32_t>(info.pieces().piece_length()), total_bytes,
                        info.files().size());
    request.on_progress = [&writer](const torrentutils::core::VerificationProgress& progress) {
        writer.verify_event(progress);
    };

    auto report = service.verify(request);
    writer.finish();
    if (!report)
    {
        print_error(report.error(), json, diagnostics);
        return error_exit_code(report.error().code);
    }

    Json files = Json::array();
    for (const auto& file : report.value().files)
    {
        Json findings = Json::array();
        constexpr std::array<torrentutils::core::FileVerificationFinding, 7U> all_findings = {
            torrentutils::core::FileVerificationFinding::Missing,
            torrentutils::core::FileVerificationFinding::NotRegularFile,
            torrentutils::core::FileVerificationFinding::LengthMismatch,
            torrentutils::core::FileVerificationFinding::HashMismatch,
            torrentutils::core::FileVerificationFinding::SharedPieceMismatch,
            torrentutils::core::FileVerificationFinding::SymlinkMissing,
            torrentutils::core::FileVerificationFinding::SymlinkTargetMismatch};
        for (const auto finding : all_findings)
        {
            if (torrentutils::core::has_finding(file.findings, finding))
            {
                findings.push_back(finding_name(finding));
            }
        }
        files.push_back({{"path", file.path.to_string()},
                         {"expected_bytes", file.expected_bytes},
                         {"hashed_bytes", file.hashed_bytes},
                         {"verified_bytes", file.verified_bytes},
                         {"mismatched_bytes", file.mismatched_bytes},
                         {"findings", std::move(findings)}});
    }

    const auto& outcome = report.value().outcome;
    std::string outcome_name = "verified";
    int exit_code = 0;
    if (outcome == torrentutils::core::VerificationOutcome::Mismatched)
    {
        outcome_name = "mismatched";
        exit_code = 6;
    }
    else if (outcome == torrentutils::core::VerificationOutcome::Incomplete)
    {
        outcome_name = "incomplete";
        exit_code = 9;
    }

    if (json)
    {
        output << Json{{"ok", true},
                       {"data",
                        {{"outcome", outcome_name},
                         {"expected_bytes", report.value().expected_bytes},
                         {"hashed_bytes", report.value().hashed_bytes},
                         {"verified_bytes", report.value().verified_bytes},
                         {"mismatched_bytes", report.value().mismatched_bytes},
                         {"files", std::move(files)}}}}
                      .dump()
               << '\n';
    }
    else if (!quiet)
    {
        output << "verify: " << outcome_name << '\n';
        output << "expected_bytes: " << report.value().expected_bytes << '\n';
        output << "hashed_bytes: " << report.value().hashed_bytes << '\n';
        output << "verified_bytes: " << report.value().verified_bytes << '\n';
        output << "mismatched_bytes: " << report.value().mismatched_bytes << '\n';
        const auto& file_results = report.value().files;
        if (file_results.size() > kVerifyFileSummaryThreshold)
        {
            std::size_t failed_count = 0;
            for (const auto& file : file_results)
            {
                if (file.findings != torrentutils::core::FileVerificationFinding::None)
                {
                    ++failed_count;
                }
            }
            output << "files: " << file_results.size() << " total, " << failed_count << " failed\n";
            for (const auto& file : file_results)
            {
                if (file.findings == torrentutils::core::FileVerificationFinding::None)
                {
                    continue;
                }
                output << "file: ";
                write_human_text(output, file.path.to_string());
                output << " mismatched\n";
            }
        }
        else
        {
            for (const auto& file : file_results)
            {
                output << "file: ";
                write_human_text(output, file.path.to_string());
                if (file.findings == torrentutils::core::FileVerificationFinding::None)
                {
                    output << " ok\n";
                }
                else
                {
                    output << " mismatched\n";
                }
            }
        }
    }
    return exit_code;
}

[[nodiscard]] int run_validate_command(const std::vector<std::string>& args, std::ostream& output,
                                       std::ostream& diagnostics)
{
    if (help_requested(args))
    {
        output << kValidateHelp;
        return 0;
    }
    bool json = false;
    bool quiet = false;
    bool strict = false;
    std::vector<std::string> positional;
    for (const auto& argument : args)
    {
        if (argument == "--json")
        {
            json = true;
        }
        else if (argument == "--quiet")
        {
            quiet = true;
        }
        else if (argument == "--strict")
        {
            strict = true;
        }
        else if (!argument.empty() && argument.front() == '-')
        {
            diagnostics << "error: unknown option: " << argument << '\n';
            return 2;
        }
        else
        {
            positional.push_back(argument);
        }
    }
    if (positional.size() != 1U)
    {
        diagnostics << "error: validate requires exactly one torrent path\n";
        return 2;
    }

    torrentutils::core::FileTorrentRepository repository;
    torrentutils::core::SystemClock clock;
    torrentutils::core::TorrentService service(repository, clock);
    const auto options = torrentutils::core::LoadOptions{
        strict ? torrentutils::core::LoadMode::Strict : torrentutils::core::LoadMode::Lenient};
    auto loaded = service.load(path_from_utf8(positional[0]), options);
    if (!loaded)
    {
        print_error(loaded.error(), json, diagnostics);
        return error_exit_code(loaded.error().code);
    }

    Json warnings = Json::array();
    for (const auto& warning : loaded.value().document().warnings())
    {
        warnings.push_back({{"field", warning.field}, {"message", warning.message}});
    }
    Json diagnostics_json = Json::array();
    for (const auto& diagnostic : loaded.value().diagnostics())
    {
        diagnostics_json.push_back(
            {{"code", "retained_unsupported_field"},
             {"scope", diagnostic.scope == torrentutils::core::LoadDiagnosticScope::TopLevel
                           ? "top_level"
                           : "info"}});
    }

    if (json)
    {
        output << Json{{"ok", true},
                       {"data",
                        {{"valid", true},
                         {"strict", strict},
                         {"warnings", std::move(warnings)},
                         {"diagnostics", std::move(diagnostics_json)}}}}
                      .dump()
               << '\n';
    }
    else if (!quiet)
    {
        output << "valid: true\n";
        for (const auto& warning : warnings)
        {
            output << "warning: " << warning["field"].get<std::string>() << ": ";
            write_human_text(output, warning["message"].get<std::string>());
            output << "\n";
        }
        for (const auto& diagnostic : diagnostics_json)
        {
            output << "diagnostic: " << diagnostic["code"].get<std::string>() << ": "
                   << diagnostic["scope"].get<std::string>() << '\n';
        }
    }
    return 0;
}

[[nodiscard]] bool is_torrent_path(const std::string& path)
{
    const auto extension = path_from_utf8(path).extension().u8string();
    std::string lowered = extension;
    std::transform(
        lowered.begin(), lowered.end(), lowered.begin(),
        [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lowered == ".torrent";
}

[[nodiscard]] std::filesystem::path default_create_target(const std::string& content)
{
    return path_from_utf8(content + ".torrent");
}

[[nodiscard]] std::filesystem::file_type path_type(const std::string& path)
{
    std::error_code error;
    const auto status = std::filesystem::status(path_from_utf8(path), error);
    if (error)
    {
        return std::filesystem::file_type::none;
    }
    return status.type();
}

[[nodiscard]] int run_create_command(const std::vector<std::string>& args, std::ostream& output,
                                     std::ostream& diagnostics)
{
    if (help_requested(args))
    {
        output << kCreateHelp;
        return 0;
    }
    std::vector<const char*> argv;
    argv.reserve(args.size() + 2U);
    argv.push_back("torrentcraft");
    argv.push_back("create");
    for (const auto& argument : args)
    {
        argv.push_back(argument.c_str());
    }

    ParsedArguments parsed;
    std::string parse_error;
    if (!parse_create(static_cast<int>(argv.size()), argv.data(), parsed, parse_error))
    {
        diagnostics << "error: " << parse_error << "\n\n" << kCreateHelp;
        return 2;
    }

    std::optional<std::uint64_t> config_memory_working_set_limit_bytes;
    auto settings = resolve_create_settings(parsed.create, config_memory_working_set_limit_bytes);
    if (!settings)
    {
        print_error(settings.error(), parsed.create.json, diagnostics);
        return error_exit_code(settings.error().code);
    }
    const auto effective_working_set_limit = parsed.create.memory_working_set_limit_bytes.value_or(
        config_memory_working_set_limit_bytes.value_or(
            torrentutils::frontend::default_memory_working_set_limit_bytes));
    auto working_set_applied =
        torrentutils::frontend::apply_memory_working_set_limit_bytes(effective_working_set_limit);
    if (!working_set_applied)
    {
        diagnostics << "warning: ";
        write_human_text(diagnostics, working_set_applied.error().message);
        diagnostics << "\n";
    }
    if (parsed.create.creation_date)
    {
        settings.value().creation_metadata.creation_time_unix_seconds = parsed.create.creation_date;
    }
    else
    {
        settings.value().creation_metadata.creation_time_unix_seconds =
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count();
    }
    if (parsed.create.dry_run)
    {
        print_success(parsed.create, settings.value(), std::nullopt, output);
        return 0;
    }

    torrentutils::core::CreateRequest request{parsed.create.content,
                                              parsed.create.target,
                                              settings.value().options,
                                              parsed.create.overwrite,
                                              settings.value().creation_metadata,
                                              settings.value().create_info,
                                              settings.value().disk_io};
    torrentutils::core::FileTorrentRepository repository;
    torrentutils::core::SystemClock clock;
    torrentutils::core::TorrentService service(repository, clock);

    const auto stderr_is_tty = diagnostics_is_tty(diagnostics);
    const auto effective_progress =
        parsed.create.progress.value_or(stderr_is_tty ? ProgressMode::Tty : ProgressMode::None);
    std::uint64_t total_bytes = 0;
    {
        std::error_code error;
        if (std::filesystem::is_regular_file(parsed.create.content, error))
        {
            total_bytes = std::filesystem::file_size(parsed.create.content, error);
            if (error)
            {
                total_bytes = 0;
            }
        }
        else
        {
            error.clear();
            std::filesystem::recursive_directory_iterator iterator(
                parsed.create.content, std::filesystem::directory_options::skip_permission_denied,
                error);
            const std::filesystem::recursive_directory_iterator end;
            while (!error && iterator != end)
            {
                if (iterator->is_regular_file())
                {
                    const auto size = iterator->file_size();
                    if (!error)
                    {
                        total_bytes += size;
                    }
                }
                iterator.increment(error);
            }
        }
    }
    ProgressWriter writer(effective_progress, stderr_is_tty, parsed.create.quiet, diagnostics);
    writer.create_start(total_bytes);
    torrentutils::core::TaskContext context;
    context.on_progress = [&writer](const torrentutils::core::ProgressInfo& progress) {
        writer.create_event(progress);
    };
    auto created = service.create(request, context);
    writer.finish();
    if (!created)
    {
        print_error(created.error(), parsed.create.json, diagnostics);
        return error_exit_code(created.error().code);
    }
    print_success(parsed.create, settings.value(), std::move(created).value(), output);
    return 0;
}

[[nodiscard]] int run_inferred_command(const int argc, const char* const argv[],
                                       std::ostream& output, std::ostream& diagnostics)
{
    std::vector<std::string> paths;
    std::vector<std::string> flags;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--json" || argument == "--quiet" || argument == "--overwrite" ||
            argument == "--dry-run" || argument == "--private" || argument == "--no-private" ||
            argument == "--public")
        {
            flags.push_back(argument);
        }
        else if (!argument.empty() && argument.front() == '-')
        {
            diagnostics << "error: unknown option: " << argument << '\n';
            return 2;
        }
        else
        {
            paths.push_back(argument);
        }
    }

    if (paths.size() == 1U)
    {
        if (is_torrent_path(paths[0]))
        {
            auto args = flags;
            args.push_back(paths[0]);
            return run_inspect_command(args, output, diagnostics);
        }
        auto args = flags;
        args.push_back(paths[0]);
        args.push_back("-o");
        args.push_back(default_create_target(paths[0]).u8string());
        return run_create_command(args, output, diagnostics);
    }

    if (paths.size() == 2U)
    {
        const bool first_torrent = is_torrent_path(paths[0]);
        const bool second_torrent = is_torrent_path(paths[1]);
        if (first_torrent != second_torrent)
        {
            const std::string& torrent = first_torrent ? paths[0] : paths[1];
            const std::string& content = first_torrent ? paths[1] : paths[0];
            auto args = flags;
            args.push_back(torrent);
            args.push_back(content);
            return run_verify_command(args, output, diagnostics);
        }
        if (!first_torrent && !second_torrent)
        {
            const auto first_type = path_type(paths[0]);
            const auto second_type = path_type(paths[1]);
            if (first_type == std::filesystem::file_type::directory &&
                second_type == std::filesystem::file_type::regular)
            {
                const auto target = path_from_utf8(paths[0]) /
                                    (path_from_utf8(paths[1]).stem().u8string() + ".torrent");
                auto args = flags;
                args.push_back(paths[1]);
                args.push_back("-o");
                args.push_back(target.u8string());
                return run_create_command(args, output, diagnostics);
            }
            if (second_type == std::filesystem::file_type::directory &&
                first_type == std::filesystem::file_type::regular)
            {
                const auto target = path_from_utf8(paths[1]) /
                                    (path_from_utf8(paths[0]).stem().u8string() + ".torrent");
                auto args = flags;
                args.push_back(paths[0]);
                args.push_back("-o");
                args.push_back(target.u8string());
                return run_create_command(args, output, diagnostics);
            }
        }
    }

    diagnostics << "error: could not infer a command from the provided paths\n";
    return 2;
}

[[nodiscard]] Json trackers_json(const torrentutils::core::TrackerList& trackers)
{
    Json tiers = Json::array();
    for (const auto& tier : trackers.tiers())
    {
        Json urls = Json::array();
        for (const auto& url : tier.trackers())
        {
            urls.push_back(url.value());
        }
        tiers.push_back(std::move(urls));
    }
    return tiers;
}

void print_trackers_human(const torrentutils::core::TrackerList& trackers, std::ostream& output)
{
    for (std::size_t tier_index = 0; tier_index < trackers.tiers().size(); ++tier_index)
    {
        for (const auto& url : trackers.tiers()[tier_index].trackers())
        {
            output << "tier " << tier_index << ": ";
            write_human_text(output, url.value());
            output << "\n";
        }
    }
}

[[nodiscard]] Json metadata_json(const torrentutils::core::TorrentDocument& document)
{
    const auto& metadata = document.metadata();
    Json result;
    const auto& comment = metadata.comment();
    if (comment)
    {
        result["comment"] = *comment;
    }
    else
    {
        result["comment"] = nullptr;
    }
    const auto& creator = metadata.creator();
    if (creator)
    {
        result["creator"] = *creator;
    }
    else
    {
        result["creator"] = nullptr;
    }
    const auto& source = metadata.source();
    if (source)
    {
        result["source"] = *source;
    }
    else
    {
        result["source"] = nullptr;
    }
    const auto& creation_time = metadata.creation_time_unix_seconds();
    if (creation_time)
    {
        result["creation_time"] = *creation_time;
    }
    else
    {
        result["creation_time"] = nullptr;
    }
    Json web_seeds = Json::array();
    for (const auto& seed : metadata.web_seeds())
    {
        web_seeds.push_back(seed.value());
    }
    result["web_seeds"] = std::move(web_seeds);
    result["collections"] = metadata.collections();
    Json dht_nodes = Json::array();
    for (const auto& node : metadata.dht_nodes())
    {
        dht_nodes.push_back({{"host", node.host()}, {"port", node.port()}});
    }
    result["dht_nodes"] = std::move(dht_nodes);
    result["private"] = document.info().is_private();
    return result;
}

[[nodiscard]] int persist_edit(torrentutils::core::TorrentService& service,
                               const torrentutils::core::LoadedTorrent& loaded, const bool dry_run,
                               const bool backup, const bool json, std::ostream& diagnostics)
{
    if (dry_run)
    {
        return 0;
    }
    if (backup)
    {
        auto backup_result = make_backup(loaded.source_path());
        if (!backup_result)
        {
            print_error(backup_result.error(), json, diagnostics);
            return error_exit_code(backup_result.error().code);
        }
    }
    auto saved = service.save(loaded);
    if (!saved)
    {
        print_error(saved.error(), json, diagnostics);
        return error_exit_code(saved.error().code);
    }
    return 0;
}

[[nodiscard]] int run_tracker_command(const std::vector<std::string>& args, std::ostream& output,
                                      std::ostream& diagnostics)
{
    if (help_requested(args))
    {
        output << kTrackerHelp;
        return 0;
    }
    if (args.empty())
    {
        diagnostics << "error: tracker requires a subcommand\n";
        return 2;
    }
    const auto& subcommand = args[0];
    if (subcommand != "list" && subcommand != "add" && subcommand != "remove" &&
        subcommand != "replace")
    {
        diagnostics << "error: unknown tracker subcommand: " << subcommand << '\n';
        return 2;
    }

    bool json = false;
    bool quiet = false;
    bool dry_run = false;
    bool backup = false;
    std::vector<std::string> positional;
    std::optional<std::size_t> add_tier;
    std::vector<std::pair<std::size_t, std::string>> replace_trackers;

    for (std::size_t index = 1; index < args.size(); ++index)
    {
        const auto& argument = args[index];
        if (argument == "--json")
        {
            json = true;
        }
        else if (argument == "--quiet")
        {
            quiet = true;
        }
        else if (argument == "--dry-run")
        {
            dry_run = true;
        }
        else if (argument == "--backup")
        {
            backup = true;
        }
        else if (argument == "--tier")
        {
            if (subcommand != "add" && subcommand != "replace")
            {
                diagnostics << "error: --tier is only valid for tracker add or replace\n";
                return 2;
            }
            if (index + 1 >= args.size())
            {
                diagnostics << "error: --tier requires a value\n";
                return 2;
            }
            const auto parsed = parse_unsigned(args[++index]);
            if (!parsed || *parsed > 64U)
            {
                diagnostics << "error: --tier must be an integer between 0 and 64\n";
                return 2;
            }
            if (subcommand == "add")
            {
                add_tier = static_cast<std::size_t>(*parsed);
            }
            else
            {
                if (replace_trackers.empty())
                {
                    diagnostics << "error: --tier must follow a --tracker option\n";
                    return 2;
                }
                replace_trackers.back().first = static_cast<std::size_t>(*parsed);
            }
        }
        else if (argument == "--tracker")
        {
            if (subcommand != "replace")
            {
                diagnostics << "error: --tracker is only valid for tracker replace\n";
                return 2;
            }
            if (index + 1 >= args.size())
            {
                diagnostics << "error: --tracker requires a value\n";
                return 2;
            }
            replace_trackers.emplace_back(0U, args[++index]);
        }
        else if (!argument.empty() && argument.front() == '-')
        {
            diagnostics << "error: unknown option: " << argument << '\n';
            return 2;
        }
        else
        {
            positional.push_back(argument);
        }
    }

    if (positional.size() < 1U || (subcommand == "list" && positional.size() != 1U) ||
        (subcommand == "add" && positional.size() != 2U) ||
        (subcommand == "remove" && positional.size() != 3U) ||
        (subcommand == "replace" && positional.size() != 1U))
    {
        diagnostics << "error: tracker " << subcommand << " received invalid arguments\n";
        return 2;
    }

    torrentutils::core::FileTorrentRepository repository;
    torrentutils::core::SystemClock clock;
    torrentutils::core::TorrentService service(repository, clock);
    auto loaded = service.load(path_from_utf8(positional[0]));
    if (!loaded)
    {
        print_error(loaded.error(), json, diagnostics);
        return error_exit_code(loaded.error().code);
    }

    if (subcommand == "list")
    {
        const auto trackers = trackers_json(loaded.value().document().trackers());
        if (json)
        {
            output << Json{{"ok", true}, {"data", {{"trackers", trackers}}}}.dump() << '\n';
        }
        else if (!quiet)
        {
            print_trackers_human(loaded.value().document().trackers(), output);
        }
        return 0;
    }

    std::vector<torrentutils::core::EditAction> actions;
    if (subcommand == "add")
    {
        auto url = torrentutils::core::TrackerUrl::parse(positional[1]);
        if (!url)
        {
            print_error(url.error(), json, diagnostics);
            return error_exit_code(url.error().code);
        }
        const auto existing_tiers = loaded.value().document().trackers().tiers().size();
        const auto tier_index = add_tier.value_or(0U);
        if (tier_index < existing_tiers)
        {
            actions.push_back(
                torrentutils::core::AddTrackerToTier{tier_index, std::move(url).value()});
        }
        else if (tier_index == existing_tiers)
        {
            auto tier = torrentutils::core::TrackerTier::create(
                std::vector<torrentutils::core::TrackerUrl>{std::move(url).value()});
            if (!tier)
            {
                print_error(tier.error(), json, diagnostics);
                return error_exit_code(tier.error().code);
            }
            actions.push_back(torrentutils::core::AddTrackerTier{std::move(tier).value()});
        }
        else
        {
            print_error({ErrorCode::ValidationFailed,
                         "tier index is out of range",
                         {{"tracker.tiers", std::to_string(tier_index)}}},
                        json, diagnostics);
            return 3;
        }
    }
    else if (subcommand == "remove")
    {
        const auto tier = parse_unsigned(positional[1]);
        const auto tracker = parse_unsigned(positional[2]);
        if (!tier || !tracker)
        {
            diagnostics << "error: tracker remove requires integer tier and tracker indexes\n";
            return 2;
        }
        actions.push_back(torrentutils::core::RemoveTracker{static_cast<std::size_t>(*tier),
                                                            static_cast<std::size_t>(*tracker)});
    }
    else
    {
        std::map<std::size_t, std::vector<torrentutils::core::TrackerUrl>> tiers;
        for (const auto& entry : replace_trackers)
        {
            auto url = torrentutils::core::TrackerUrl::parse(entry.second);
            if (!url)
            {
                print_error(url.error(), json, diagnostics);
                return error_exit_code(url.error().code);
            }
            tiers[entry.first].push_back(std::move(url).value());
        }
        std::vector<torrentutils::core::TrackerTier> parsed_tiers;
        for (auto& entry : tiers)
        {
            auto tier = torrentutils::core::TrackerTier::create(std::move(entry.second));
            if (!tier)
            {
                print_error(tier.error(), json, diagnostics);
                return error_exit_code(tier.error().code);
            }
            parsed_tiers.push_back(std::move(tier).value());
        }
        auto list = torrentutils::core::TrackerList::create(std::move(parsed_tiers));
        if (!list)
        {
            print_error(list.error(), json, diagnostics);
            return error_exit_code(list.error().code);
        }
        actions.push_back(torrentutils::core::ReplaceTrackers{std::move(list).value()});
    }

    auto edited = service.edit(loaded.value(), actions);
    if (!edited)
    {
        print_error(edited.error(), json, diagnostics);
        return error_exit_code(edited.error().code);
    }
    if (edited.value().disposition == torrentutils::core::EditDisposition::NeedRebuild)
    {
        print_error(
            {ErrorCode::ValidationFailed, "tracker change requires re-creating the torrent", {}},
            json, diagnostics);
        return 3;
    }
    const auto persist_exit =
        persist_edit(service, edited.value().loaded, dry_run, backup, json, diagnostics);
    if (persist_exit != 0)
    {
        return persist_exit;
    }

    const auto trackers = trackers_json(edited.value().loaded.document().trackers());
    if (json)
    {
        output
            << Json{{"ok", true}, {"data", {{"trackers", trackers}, {"dry_run", dry_run}}}}.dump()
            << '\n';
    }
    else if (!quiet)
    {
        output << (dry_run ? "tracker dry-run valid\n" : "tracker updated\n");
        print_trackers_human(edited.value().loaded.document().trackers(), output);
    }
    return 0;
}

[[nodiscard]] int run_metadata_command(const std::vector<std::string>& args, std::ostream& output,
                                       std::ostream& diagnostics)
{
    if (help_requested(args))
    {
        output << kMetadataHelp;
        return 0;
    }
    if (args.empty())
    {
        diagnostics << "error: metadata requires a subcommand\n";
        return 2;
    }
    const auto& subcommand = args[0];
    if (subcommand != "show" && subcommand != "set" && subcommand != "clear")
    {
        diagnostics << "error: unknown metadata subcommand: " << subcommand << '\n';
        return 2;
    }

    bool json = false;
    bool quiet = false;
    bool dry_run = false;
    bool backup = false;
    std::vector<std::string> positional;
    std::vector<torrentutils::core::EditAction> actions;

    for (std::size_t index = 1; index < args.size(); ++index)
    {
        const auto& argument = args[index];
        if (argument == "--json")
        {
            json = true;
        }
        else if (argument == "--quiet")
        {
            quiet = true;
        }
        else if (argument == "--dry-run")
        {
            dry_run = true;
        }
        else if (argument == "--backup")
        {
            backup = true;
        }
        else if (subcommand == "set" && (argument == "--comment" || argument == "--created-by" ||
                                         argument == "--info-source" || argument == "--name" ||
                                         argument == "--creation-time" ||
                                         argument == "--web-seed" || argument == "--dht-node"))
        {
            if (index + 1 >= args.size())
            {
                diagnostics << "error: " << argument << " requires a value\n";
                return 2;
            }
            const std::string& value = args[++index];
            if (argument == "--comment")
            {
                actions.push_back(torrentutils::core::SetComment{value});
            }
            else if (argument == "--created-by")
            {
                actions.push_back(torrentutils::core::SetCreator{value});
            }
            else if (argument == "--info-source")
            {
                actions.push_back(torrentutils::core::SetInfoSource{value});
            }
            else if (argument == "--name")
            {
                actions.push_back(torrentutils::core::SetName{value});
            }
            else if (argument == "--creation-time")
            {
                if (value == "now")
                {
                    actions.push_back(torrentutils::core::SetCreationTimeNow{});
                }
                else
                {
                    const auto parsed = parse_unsigned(value);
                    if (!parsed)
                    {
                        diagnostics << "error: --creation-time must be an integer or now\n";
                        return 2;
                    }
                    actions.push_back(
                        torrentutils::core::SetCreationTime{static_cast<std::int64_t>(*parsed)});
                }
            }
            else if (argument == "--web-seed")
            {
                auto seed = torrentutils::core::WebSeedUrl::parse(value);
                if (!seed)
                {
                    print_error(seed.error(), json, diagnostics);
                    return error_exit_code(seed.error().code);
                }
                actions.push_back(torrentutils::core::ReplaceWebSeeds{
                    std::vector<torrentutils::core::WebSeedUrl>{std::move(seed).value()}});
            }
            else if (argument == "--dht-node")
            {
                const auto colon = value.find_last_of(':');
                if (colon == std::string::npos || colon == 0U || colon + 1U == value.size())
                {
                    diagnostics << "error: --dht-node must be HOST:PORT\n";
                    return 2;
                }
                const auto port = parse_unsigned(value.substr(colon + 1U));
                if (!port || *port > 65535U)
                {
                    diagnostics
                        << "error: --dht-node port must be an integer between 0 and 65535\n";
                    return 2;
                }
                auto node = torrentutils::core::DhtNode::create(value.substr(0U, colon),
                                                                static_cast<std::uint32_t>(*port));
                if (!node)
                {
                    print_error(node.error(), json, diagnostics);
                    return error_exit_code(node.error().code);
                }
                actions.push_back(torrentutils::core::ReplaceDhtNodes{
                    std::vector<torrentutils::core::DhtNode>{std::move(node).value()}});
            }
        }
        else if (subcommand == "set" && argument == "--private")
        {
            actions.push_back(torrentutils::core::SetPrivate{true});
        }
        else if (subcommand == "set" && (argument == "--no-private" || argument == "--public"))
        {
            actions.push_back(torrentutils::core::SetPrivate{false});
        }
        else if (subcommand == "clear" &&
                 (argument == "--comment" || argument == "--created-by" ||
                  argument == "--info-source" || argument == "--creation-time" ||
                  argument == "--web-seeds" || argument == "--dht-nodes"))
        {
            if (argument == "--comment")
            {
                actions.push_back(torrentutils::core::ClearComment{});
            }
            else if (argument == "--created-by")
            {
                actions.push_back(torrentutils::core::ClearCreator{});
            }
            else if (argument == "--info-source")
            {
                actions.push_back(torrentutils::core::ClearInfoSource{});
            }
            else if (argument == "--creation-time")
            {
                actions.push_back(torrentutils::core::ClearCreationTime{});
            }
            else if (argument == "--web-seeds")
            {
                actions.push_back(torrentutils::core::ReplaceWebSeeds{});
            }
            else if (argument == "--dht-nodes")
            {
                actions.push_back(torrentutils::core::ReplaceDhtNodes{});
            }
        }
        else if (!argument.empty() && argument.front() == '-')
        {
            diagnostics << "error: unknown option: " << argument << '\n';
            return 2;
        }
        else
        {
            positional.push_back(argument);
        }
    }

    if (positional.size() != 1U)
    {
        diagnostics << "error: metadata " << subcommand << " requires exactly one torrent path\n";
        return 2;
    }

    torrentutils::core::FileTorrentRepository repository;
    torrentutils::core::SystemClock clock;
    torrentutils::core::TorrentService service(repository, clock);
    auto loaded = service.load(path_from_utf8(positional[0]));
    if (!loaded)
    {
        print_error(loaded.error(), json, diagnostics);
        return error_exit_code(loaded.error().code);
    }

    if (subcommand == "show")
    {
        const auto metadata = metadata_json(loaded.value().document());
        if (json)
        {
            output << Json{{"ok", true}, {"data", {{"metadata", metadata}}}}.dump() << '\n';
        }
        else if (!quiet)
        {
            write_human_text(output, metadata.dump(2));
            output << "\n";
        }
        return 0;
    }

    if (actions.empty())
    {
        diagnostics << "error: metadata " << subcommand << " requires at least one field\n";
        return 2;
    }

    auto edited = service.edit(loaded.value(), actions);
    if (!edited)
    {
        print_error(edited.error(), json, diagnostics);
        return error_exit_code(edited.error().code);
    }
    if (edited.value().disposition == torrentutils::core::EditDisposition::NeedRebuild)
    {
        print_error(
            {ErrorCode::ValidationFailed, "metadata change requires re-creating the torrent", {}},
            json, diagnostics);
        return 3;
    }
    const auto persist_exit =
        persist_edit(service, edited.value().loaded, dry_run, backup, json, diagnostics);
    if (persist_exit != 0)
    {
        return persist_exit;
    }

    const auto metadata = metadata_json(edited.value().loaded.document());
    if (json)
    {
        output
            << Json{{"ok", true}, {"data", {{"metadata", metadata}, {"dry_run", dry_run}}}}.dump()
            << '\n';
    }
    else if (!quiet)
    {
        output << (dry_run ? "metadata dry-run valid\n" : "metadata updated\n");
        write_human_text(output, metadata.dump(2));
        output << "\n";
    }
    return 0;
}

[[nodiscard]] int run_completion_command(const std::vector<std::string>& args, std::ostream& output,
                                         std::ostream& diagnostics)
{
    if (help_requested(args))
    {
        output << kCompletionHelp;
        return 0;
    }
    if (args.size() != 1U)
    {
        diagnostics << "error: completion requires exactly one shell name\n";
        return 2;
    }
    const auto& shell = args[0];
    constexpr std::string_view commands =
        "create config preset inspect tree verify validate tracker metadata completion";
    constexpr std::string_view global_options = "--help --version --json --quiet";
    if (shell == "bash")
    {
        output << "complete -W '" << commands << " " << global_options << "' torrentcraft\n";
        return 0;
    }
    if (shell == "zsh")
    {
        output << "#compdef torrentcraft\n_arguments '1:command:(" << commands << ")'\n";
        return 0;
    }
    if (shell == "fish")
    {
        output << "complete -c torrentcraft -f -a '" << commands << "'\n";
        return 0;
    }
    diagnostics << "error: unknown shell: " << shell << '\n';
    return 2;
}

} // namespace

int run(const int argc, const char* const argv[], std::ostream& output, std::ostream& diagnostics)
{
    if (argc == 1)
    {
        output << kUsage;
        return 0;
    }
    if (argc == 2 && (std::string_view(argv[1]) == "--help" || argv[1] == std::string_view{"-h"}))
    {
        output << kUsage;
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--version")
    {
        output << torrentutils::core::version() << '\n';
        return 0;
    }
    const std::string_view command = argv[1];
    if (command == "inspect")
    {
        const std::vector<std::string> args(argv + 2, argv + argc);
        return run_inspect_command(args, output, diagnostics);
    }
    if (command == "tree")
    {
        const std::vector<std::string> args(argv + 2, argv + argc);
        return run_tree_command(args, output, diagnostics);
    }
    if (command == "verify")
    {
        const std::vector<std::string> args(argv + 2, argv + argc);
        return run_verify_command(args, output, diagnostics);
    }
    if (command == "validate")
    {
        const std::vector<std::string> args(argv + 2, argv + argc);
        return run_validate_command(args, output, diagnostics);
    }
    if (command == "tracker")
    {
        const std::vector<std::string> args(argv + 2, argv + argc);
        return run_tracker_command(args, output, diagnostics);
    }
    if (command == "metadata")
    {
        const std::vector<std::string> args(argv + 2, argv + argc);
        return run_metadata_command(args, output, diagnostics);
    }
    if (command == "completion")
    {
        const std::vector<std::string> args(argv + 2, argv + argc);
        return run_completion_command(args, output, diagnostics);
    }
    if (command == "config")
    {
        const std::vector<std::string> args(argv + 2, argv + argc);
        return run_config_command(args, output, diagnostics);
    }
    if (command == "preset")
    {
        const std::vector<std::string> args(argv + 2, argv + argc);
        return run_preset_command(args, output, diagnostics);
    }
    if (command == "create")
    {
        const std::vector<std::string> args(argv + 2, argv + argc);
        return run_create_command(args, output, diagnostics);
    }
    return run_inferred_command(argc, argv, output, diagnostics);
}

void set_console_utf8_native(const bool value) noexcept
{
    console_utf8_native() = value;
}

void set_console_output_cp(const unsigned int value) noexcept
{
    console_output_cp() = value;
}

void initialize_console_output() noexcept
{
#ifdef _WIN32
    const auto native_console_cp = ::GetConsoleOutputCP();
    if (native_console_cp == 0U)
    {
        set_console_utf8_native(false);
        set_console_output_cp(CP_UTF8);
        return;
    }

    // Keep progress bar glyph selection based on the code page that the host
    // reported, but make the actual console byte stream UTF-8. This preserves
    // characters that cannot be represented by CP936/CP437, such as Japanese
    // punctuation and kana.
    set_console_utf8_native(native_console_cp == CP_UTF8);
    if (::SetConsoleOutputCP(CP_UTF8) != 0)
    {
        set_console_output_cp(CP_UTF8);
    }
    else
    {
        set_console_output_cp(native_console_cp);
    }
    ::SetConsoleCP(CP_UTF8);
#endif
}

} // namespace torrentcraft::cli
