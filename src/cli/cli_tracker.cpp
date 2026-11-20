#include "cli_common.hpp"
#include "interrupt.hpp"
#include "progress.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <torrentutils/core/application.hpp>
#include <torrentutils/core/error.hpp>
#include <torrentutils/core/tracker_rewrite.hpp>
#include <torrentutils/frontend/tracker_batch.hpp>
#include <utility>
#include <vector>

namespace torrentcraft::cli::detail {

namespace {

constexpr std::string_view kTrackerHelp = R"(Usage:
  torrentcraft tracker list|add|remove|replace <torrent> ... [options]
  torrentcraft tracker rewrite <torrent|folder>... -e RULE [-e RULE]... [options]

Manage tracker tiers.

tracker rewrite applies sed-style rules (s#pattern#replacement#flags, RE2 syntax,
flags g and i) to every tracker URL, in order. A torrent whose rewritten URL is
invalid fails without being written; other torrents continue.

Options:
  --tracker URL             Replace the tier (tracker replace)
  --tier N                  Assign the preceding tracker to tier N
  -e, --expression RULE     Add a rewrite rule (tracker rewrite)
  -r, --recursive           Include subfolders of folder inputs (tracker rewrite)
  --output-dir DIR          Write rewritten torrents below DIR (tracker rewrite)
  --force                   Overwrite existing files in --output-dir
  --dry-run                 Validate without writing
  --backup                  Back up the torrent before writing
  --json                    Emit stable JSON output
  --quiet                   Suppress successful human output
  -h, --help                Show this help
)";

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

[[nodiscard]] std::string_view
tracker_rewrite_status_name(const torrentutils::core::TrackerRewriteStatus status)
{
    switch (status)
    {
    case torrentutils::core::TrackerRewriteStatus::Unchanged:
        return "unchanged";
    case torrentutils::core::TrackerRewriteStatus::Changed:
        return "changed";
    case torrentutils::core::TrackerRewriteStatus::Merged:
        return "merged";
    case torrentutils::core::TrackerRewriteStatus::Invalid:
        return "invalid";
    }
    return "invalid";
}

[[nodiscard]] std::string_view
tracker_batch_status_name(const torrentutils::frontend::TrackerBatchFileStatus status)
{
    switch (status)
    {
    case torrentutils::frontend::TrackerBatchFileStatus::Changed:
        return "changed";
    case torrentutils::frontend::TrackerBatchFileStatus::Unchanged:
        return "unchanged";
    case torrentutils::frontend::TrackerBatchFileStatus::Failed:
        return "failed";
    }
    return "failed";
}

[[nodiscard]] Json
tracker_rewrite_file_json(const std::filesystem::path& path,
                          const torrentutils::frontend::TrackerBatchFileResult& result)
{
    Json file{{"path", path.u8string()}, {"status", tracker_batch_status_name(result.status)}};
    Json changes = Json::array();
    std::size_t merged = 0;
    if (result.preview)
    {
        merged = result.preview->count(torrentutils::core::TrackerRewriteStatus::Merged);
        for (const auto& change : result.preview->changes)
        {
            if (change.status == torrentutils::core::TrackerRewriteStatus::Unchanged)
            {
                continue;
            }
            Json entry{{"tier", change.tier},
                       {"from", change.from},
                       {"to", change.to},
                       {"status", tracker_rewrite_status_name(change.status)}};
            if (!change.reason.empty())
            {
                entry["reason"] = change.reason;
            }
            changes.push_back(std::move(entry));
        }
    }
    file["changes"] = std::move(changes);
    file["merged"] = merged;
    if (result.status == torrentutils::frontend::TrackerBatchFileStatus::Changed)
    {
        file["target"] = result.target.u8string();
    }
    if (result.error)
    {
        Json issues = Json::array();
        for (const auto& issue : result.error->issues)
        {
            issues.push_back({{"field", issue.field}, {"message", issue.message}});
        }
        file["error"] = {{"code", error_code_name(result.error->code)},
                         {"message", result.error->message},
                         {"issues", std::move(issues)}};
    }
    return file;
}

void print_tracker_rewrite_file_human(const std::filesystem::path& path,
                                      const torrentutils::frontend::TrackerBatchFileResult& result,
                                      std::ostream& output)
{
    write_human_text(output, path.u8string());
    output << ": " << tracker_batch_status_name(result.status);
    if (result.error)
    {
        output << " (";
        write_human_text(output, result.error->message);
        output << ")";
    }
    output << "\n";
    if (!result.preview)
    {
        return;
    }
    for (const auto& change : result.preview->changes)
    {
        if (change.status == torrentutils::core::TrackerRewriteStatus::Unchanged)
        {
            continue;
        }
        output << "  tier " << change.tier << ": ";
        write_human_text(output, change.from);
        output << " -> ";
        if (change.status == torrentutils::core::TrackerRewriteStatus::Invalid)
        {
            output << "invalid (";
            write_human_text(output, change.reason);
            output << ")";
        }
        else
        {
            write_human_text(output, change.to);
            if (change.status == torrentutils::core::TrackerRewriteStatus::Merged)
            {
                output << " (merged)";
            }
        }
        output << "\n";
    }
}

[[nodiscard]] int run_tracker_rewrite_command(const std::vector<std::string>& args,
                                              std::ostream& output, std::ostream& diagnostics)
{
    namespace frontend = torrentutils::frontend;

    bool json = false;
    bool quiet = false;
    bool recursive = false;
    frontend::TrackerBatchOptions options;
    std::vector<std::string> expressions;
    std::vector<std::string> inputs;
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
            options.dry_run = true;
        }
        else if (argument == "--backup")
        {
            options.backup = true;
        }
        else if (argument == "--force")
        {
            options.overwrite = true;
        }
        else if (argument == "-r" || argument == "--recursive")
        {
            recursive = true;
        }
        else if (argument == "-e" || argument == "--expression" || argument == "--output-dir")
        {
            if (index + 1 >= args.size())
            {
                diagnostics << "error: " << argument << " requires a value\n";
                return 2;
            }
            if (argument == "--output-dir")
            {
                options.output_dir = path_from_utf8(args[++index]);
            }
            else
            {
                expressions.push_back(args[++index]);
            }
        }
        else if (!argument.empty() && argument.front() == '-')
        {
            diagnostics << "error: unknown option: " << argument << '\n';
            return 2;
        }
        else
        {
            inputs.push_back(argument);
        }
    }
    if (inputs.empty())
    {
        diagnostics << "error: tracker rewrite requires at least one torrent or folder\n";
        return 2;
    }
    if (expressions.empty())
    {
        diagnostics << "error: tracker rewrite requires at least one -e RULE\n";
        return 2;
    }
    if (options.overwrite && options.output_dir.empty())
    {
        diagnostics << "error: --force requires --output-dir\n";
        return 2;
    }
    auto rules = torrentutils::core::TrackerRewriteRules::parse(expressions);
    if (!rules)
    {
        print_error(rules.error(), json, diagnostics);
        return 2;
    }

    std::vector<frontend::TorrentBatchFile> files;
    std::set<std::filesystem::path> seen_sources;
    for (const auto& input : inputs)
    {
        auto listed = frontend::enumerate_torrent_files(path_from_utf8(input), recursive);
        if (!listed)
        {
            auto error = std::move(listed).error();
            error.message += ": " + input;
            print_error(error, json, diagnostics);
            return error_exit_code(error.code);
        }
        for (auto& file : listed.value())
        {
            std::error_code error;
            auto key = std::filesystem::weakly_canonical(file.path, error);
            if (seen_sources.insert(error ? file.path.lexically_normal() : key).second)
            {
                files.push_back(std::move(file));
            }
        }
    }

    torrentutils::core::FileTorrentRepository repository;
    torrentutils::core::SystemClock clock;
    torrentutils::core::TorrentService service(repository, clock);
    const frontend::TrackerBatchEdit edit = std::move(rules).value();

    std::set<std::filesystem::path> seen_targets;
    Json reported = Json::array();
    std::size_t changed = 0;
    std::size_t unchanged = 0;
    std::size_t failed = 0;
    int exit_code = 0;
    if (!json && !quiet)
    {
        print_cancel_hint(diagnostics);
    }
    const InterruptScope interrupt;
    torrentutils::core::TaskContext context;
    context.cancellation = interrupt.token();
    bool cancelled = false;
    for (const auto& file : files)
    {
        if (context.cancellation.is_cancelled())
        {
            // Files already handled keep their results; the rest are left untouched.
            cancelled = true;
            exit_code = std::max(exit_code, error_exit_code(ErrorCode::Cancelled));
            break;
        }
        frontend::TrackerBatchFileResult result;
        if (!options.output_dir.empty() &&
            !seen_targets.insert(frontend::batch_output_path(options.output_dir, file)).second)
        {
            result.error = torrentutils::core::Error{
                ErrorCode::Conflict, "another input maps to the same output path", {}};
        }
        else
        {
            result = frontend::apply_tracker_batch_file(service, file, edit, options, context);
        }
        switch (result.status)
        {
        case frontend::TrackerBatchFileStatus::Changed:
            ++changed;
            break;
        case frontend::TrackerBatchFileStatus::Unchanged:
            ++unchanged;
            break;
        case frontend::TrackerBatchFileStatus::Failed:
            ++failed;
            exit_code = std::max(exit_code, error_exit_code(result.error->code));
            break;
        }
        if (json)
        {
            reported.push_back(tracker_rewrite_file_json(file.path, result));
        }
        else if (!quiet || result.status == frontend::TrackerBatchFileStatus::Failed)
        {
            print_tracker_rewrite_file_human(
                file.path, result,
                result.status == frontend::TrackerBatchFileStatus::Failed ? diagnostics : output);
        }
    }

    if (json)
    {
        output << dump_json(Json{{"ok", failed == 0U && !cancelled},
                                 {"data",
                                  {{"dry_run", options.dry_run},
                                   {"cancelled", cancelled},
                                   {"files", std::move(reported)},
                                   {"summary",
                                    {{"files", files.size()},
                                     {"changed", changed},
                                     {"unchanged", unchanged},
                                     {"failed", failed}}}}}})
               << '\n';
    }
    else if (!quiet || failed != 0U || cancelled)
    {
        (failed == 0U && !cancelled ? output : diagnostics)
            << (options.dry_run ? "dry run: " : "") << changed << " changed, " << unchanged
            << " unchanged, " << failed << " failed"
            << (cancelled ? "; cancelled before the remaining files" : "") << '\n';
    }
    return exit_code;
}

} // namespace

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
    if (subcommand == "rewrite")
    {
        return run_tracker_rewrite_command(args, output, diagnostics);
    }
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
            output << dump_json(Json{{"ok", true}, {"data", {{"trackers", trackers}}}}) << '\n';
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
        output << dump_json(
                      Json{{"ok", true}, {"data", {{"trackers", trackers}, {"dry_run", dry_run}}}})
               << '\n';
    }
    else if (!quiet)
    {
        output << (dry_run ? "tracker dry-run valid\n" : "tracker updated\n");
        print_trackers_human(edited.value().loaded.document().trackers(), output);
    }
    return 0;
}

} // namespace torrentcraft::cli::detail
