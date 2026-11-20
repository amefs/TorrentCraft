#include "cli_common.hpp"
#include "interrupt.hpp"
#include "progress.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <torrentutils/core/application.hpp>
#include <torrentutils/core/error.hpp>
#include <torrentutils/core/torrent_engine.hpp>
#include <torrentutils/frontend/config.hpp>
#include <torrentutils/frontend/settings.hpp>
#include <utility>
#include <vector>

namespace torrentcraft::cli::detail {

namespace {

struct CliFileFilterOverrides
{
    std::optional<torrentutils::core::FileFilterMode> mode;
    std::optional<bool> case_sensitive;
    std::optional<std::vector<std::string>> patterns;
};

struct CreateArguments
{
    std::filesystem::path content;
    std::filesystem::path target;
    std::optional<std::filesystem::path> config_path;
    std::optional<std::string> preset_name;
    std::optional<std::filesystem::path> preset_file;
    CreationSettingsPatch cli_settings;
    CliFileFilterOverrides cli_filter;
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
  --filter-mode MODE        Select disabled, common-artifacts, or custom-rules
  --exclude PATTERN         Exclude a custom Glob pattern; repeatable
  --filter-case-sensitive   Match filter patterns case-sensitively
  --filter-case-insensitive Match filter patterns case-insensitively
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

[[nodiscard]] bool parse_filter_mode(std::string_view value, CliFileFilterOverrides& settings,
                                     std::string& error)
{
    if (value == "disabled")
    {
        settings.mode = torrentutils::core::FileFilterMode::Disabled;
    }
    else if (value == "common-artifacts")
    {
        settings.mode = torrentutils::core::FileFilterMode::CommonArtifacts;
    }
    else if (value == "custom-rules")
    {
        settings.mode = torrentutils::core::FileFilterMode::CustomRules;
    }
    else
    {
        error = "--filter-mode must be disabled, common-artifacts, or custom-rules";
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
            argument == "--filter-mode" || argument == "--exclude" || argument == "--tracker" ||
            argument == "--tier" || argument == "--web-seed" || argument == "--comment" ||
            argument == "--created-by" || argument == "--source" || argument == "--creation-date")
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
            else if (argument == "--filter-mode")
            {
                if (!parse_filter_mode(value, arguments.cli_filter, error))
                {
                    return false;
                }
            }
            else if (argument == "--exclude")
            {
                if (!arguments.cli_filter.patterns)
                {
                    arguments.cli_filter.patterns = std::vector<std::string>{};
                }
                arguments.cli_filter.patterns->push_back(std::move(value));
                arguments.cli_filter.mode = torrentutils::core::FileFilterMode::CustomRules;
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
        if (argument == "--filter-case-sensitive")
        {
            arguments.cli_filter.case_sensitive = true;
            continue;
        }
        if (argument == "--filter-case-insensitive")
        {
            arguments.cli_filter.case_sensitive = false;
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

[[nodiscard]] std::string file_filter_mode_name(const torrentutils::core::FileFilterMode mode)
{
    switch (mode)
    {
    case torrentutils::core::FileFilterMode::Disabled:
        return "disabled";
    case torrentutils::core::FileFilterMode::CommonArtifacts:
        return "common-artifacts";
    case torrentutils::core::FileFilterMode::CustomRules:
        return "custom-rules";
    }
    return "disabled";
}

[[nodiscard]] std::string filtered_entry_kind_name(const torrentutils::core::FilteredEntryKind kind)
{
    switch (kind)
    {
    case torrentutils::core::FilteredEntryKind::File:
        return "file";
    case torrentutils::core::FilteredEntryKind::Directory:
        return "directory";
    case torrentutils::core::FilteredEntryKind::Symlink:
        return "symlink";
    case torrentutils::core::FilteredEntryKind::Other:
        return "other";
    }
    return "other";
}

void add_filter_report(Json& data, const torrentutils::core::FilterReport& report)
{
    data["filtered"]["count"] = report.entries.size();
    data["filtered"]["bytes"] = report.total_bytes();
    data["filtered"]["entries"] = Json::array();
    for (const auto& entry : report.entries)
    {
        data["filtered"]["entries"].push_back({{"path", entry.relative_path},
                                               {"kind", filtered_entry_kind_name(entry.kind)},
                                               {"bytes", entry.bytes},
                                               {"descendant_count", entry.descendant_count},
                                               {"descendant_bytes", entry.descendant_bytes},
                                               {"rule", entry.matched_rule}});
    }
}

void print_success(const CreateArguments& arguments, const ResolvedCreationSettings& settings,
                   const std::optional<torrentutils::core::CreateResult>& result,
                   std::ostream& output,
                   const torrentutils::core::FilterReport* planned_filter_report = nullptr)
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
        value["data"]["file_filter"]["mode"] =
            file_filter_mode_name(settings.options.file_filter().mode());
        value["data"]["file_filter"]["case_sensitive"] =
            settings.options.file_filter().case_sensitive();
        if (settings.creation_metadata.creation_time_unix_seconds)
        {
            value["data"]["creation_date"] = *settings.creation_metadata.creation_time_unix_seconds;
        }
        if (result)
        {
            add_filter_report(value["data"], result->filter_report);
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
        else if (planned_filter_report)
        {
            add_filter_report(value["data"], *planned_filter_report);
        }
        output << dump_json(value) << '\n';
        return;
    }
    if (!arguments.quiet)
    {
        output << (arguments.dry_run ? "create dry-run valid: " : "created: ");
        write_human_text(output, arguments.target.u8string());
        output << "\n";
        const auto* filter_report = result ? &result->filter_report : planned_filter_report;
        if (filter_report && !filter_report->entries.empty())
        {
            output << "filtered " << filter_report->entries.size() << " entries ("
                   << filter_report->total_bytes() << " bytes):\n";
            for (const auto& entry : filter_report->entries)
            {
                output << "  " << entry.relative_path << " ["
                       << filtered_entry_kind_name(entry.kind) << "] rule=" << entry.matched_rule
                       << "\n";
            }
        }
    }
}

void apply_cli_file_filter_overrides(CreationSettingsPatch& settings,
                                     const CliFileFilterOverrides& overrides)
{
    if (!overrides.mode && !overrides.case_sensitive && !overrides.patterns)
    {
        return;
    }

    auto filter = settings.file_filter.value_or(torrentutils::core::FileFilterInput{});
    if (overrides.mode)
    {
        filter.mode = *overrides.mode;
    }
    if (overrides.case_sensitive)
    {
        filter.case_sensitive = *overrides.case_sensitive;
    }
    if (overrides.patterns)
    {
        filter.patterns = *overrides.patterns;
    }
    settings.file_filter = std::move(filter);
}

[[nodiscard]] Result<ResolvedCreationSettings>
resolve_create_settings(const CreateArguments& arguments,
                        std::optional<std::uint64_t>& memory_working_set_limit_bytes,
                        const std::optional<std::filesystem::path>& executable_directory)
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
        auto discovered = torrentutils::frontend::discover_config(paths, executable_directory);
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
    if (config && !arguments.preset_name && !arguments.preset_file)
    {
        const auto& parsed = config->parsed();
        if (parsed.default_preset)
        {
            const auto iterator = parsed.presets.find(*parsed.default_preset);
            if (iterator != parsed.presets.end())
            {
                effective = torrentutils::frontend::overlay_settings(effective, iterator->second);
            }
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
    apply_cli_file_filter_overrides(effective, arguments.cli_filter);
    auto resolved = torrentutils::frontend::resolve_settings(effective);
    if (resolved)
    {
        resolved.value().disk_io = config ? config->parsed().disk_io : std::nullopt;
    }
    return resolved;
}

} // namespace

[[nodiscard]] int
run_create_command(const std::vector<std::string>& args, std::ostream& output,
                   std::ostream& diagnostics,
                   const std::optional<std::filesystem::path>& executable_directory)
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
    auto settings = resolve_create_settings(parsed.create, config_memory_working_set_limit_bytes,
                                            executable_directory);
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
    torrentutils::core::FileTorrentRepository repository;
    torrentutils::core::SystemClock clock;
    torrentutils::core::TorrentService service(repository, clock);

    if (parsed.create.dry_run)
    {
        std::error_code content_status_error;
        if (std::filesystem::is_directory(parsed.create.content, content_status_error))
        {
            const torrentutils::core::CreatePlanRequest plan_request{parsed.create.content,
                                                                     settings.value().options};
            auto planned = service.plan_create(plan_request);
            if (!planned)
            {
                print_error(planned.error(), parsed.create.json, diagnostics);
                return error_exit_code(planned.error().code);
            }
            print_success(parsed.create, settings.value(), std::nullopt, output,
                          &planned.value().filter_report);
            return 0;
        }
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
    const auto stderr_is_tty = diagnostics_is_tty(diagnostics);
    const auto effective_progress =
        parsed.create.progress.value_or(stderr_is_tty ? ProgressMode::Tty : ProgressMode::None);
    const std::uint64_t total_bytes = 0;
    if (!parsed.create.json && !parsed.create.quiet)
    {
        print_cancel_hint(diagnostics);
    }
    const InterruptScope interrupt;
    ProgressWriter writer(effective_progress, stderr_is_tty, parsed.create.quiet, diagnostics);
    writer.create_start(total_bytes);
    torrentutils::core::TaskContext context;
    context.cancellation = interrupt.token();
    context.on_progress = [&writer](const torrentutils::core::ProgressInfo& progress) {
        writer.create_event(progress);
    };
    auto created = service.create(request, context);
    if (!created)
    {
        writer.abandon();
        print_error(created.error(), parsed.create.json, diagnostics);
        return error_exit_code(created.error().code);
    }
    writer.finish();
    print_success(parsed.create, settings.value(), std::move(created).value(), output);
    return 0;
}

} // namespace torrentcraft::cli::detail
