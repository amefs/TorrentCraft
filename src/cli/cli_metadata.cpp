#include "cli_common.hpp"
#include "progress.hpp"

#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <ostream>
#include <string>
#include <string_view>
#include <torrentutils/core/application.hpp>
#include <torrentutils/core/error.hpp>
#include <utility>
#include <vector>

namespace torrentcraft::cli::detail {

namespace {

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

} // namespace

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
            output << dump_json(Json{{"ok", true}, {"data", {{"metadata", metadata}}}}) << '\n';
        }
        else if (!quiet)
        {
            write_human_text(output, dump_json(metadata, 2));
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
        output << dump_json(
                      Json{{"ok", true}, {"data", {{"metadata", metadata}, {"dry_run", dry_run}}}})
               << '\n';
    }
    else if (!quiet)
    {
        output << (dry_run ? "metadata dry-run valid\n" : "metadata updated\n");
        write_human_text(output, dump_json(metadata, 2));
        output << "\n";
    }
    return 0;
}

} // namespace torrentcraft::cli::detail
