#include "cli_common.hpp"
#include "progress.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <torrentutils/core/error.hpp>
#include <torrentutils/frontend/config.hpp>
#include <torrentutils/frontend/settings.hpp>
#include <torrentutils/frontend/torrent_template.hpp>
#include <utility>
#include <vector>

namespace torrentcraft::cli::detail {

namespace {

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
  torrentcraft preset add --from-torrent <torrent> [--name NAME] [--fields LIST] [options]

Manage named creation presets.

Options:
  --config PATH             Read torrentcraft.json from PATH
  --from-torrent PATH       Add a preset from a torrent template's reusable settings
  --name NAME               Preset name (default: file name stem)
  --fields LIST|all         Template fields to adopt, comma-separated: trackers,
                            piece-size, private, source, web-seeds, format,
                            comment, created-by (default: all except comment
                            and created-by; unavailable fields are skipped
                            with a warning)
  --dry-run                 Validate without writing
  --backup                  Back up the config before writing
  --force                   Replace an existing preset
  --json                    Emit stable JSON output
  --quiet                   Suppress successful human output
  -h, --help                Show this help
)";

struct ConfigInvocation
{
    std::optional<std::filesystem::path> config_path;
    bool json{};
    bool quiet{};
    bool dry_run{};
    bool backup{};
    bool force{};
    std::optional<std::string> from_torrent;
    std::optional<std::string> preset_name;
    std::optional<std::string> template_fields;
    std::vector<std::string> positional;
};

[[nodiscard]] bool parse_config_invocation(const std::vector<std::string>& args,
                                           ConfigInvocation& result, std::string& error,
                                           const bool allow_template_options = false)
{
    const auto take_value = [&](std::size_t& index, std::optional<std::string>& target) {
        const auto& option = args[index];
        if (index + 1 >= args.size())
        {
            error = option + " requires a value";
            return false;
        }
        if (target)
        {
            error = option + " was specified more than once";
            return false;
        }
        target = args[++index];
        return true;
    };
    for (std::size_t index = 0; index < args.size(); ++index)
    {
        const auto& argument = args[index];
        if (allow_template_options && argument == "--from-torrent")
        {
            if (!take_value(index, result.from_torrent))
            {
                return false;
            }
        }
        else if (allow_template_options && argument == "--name")
        {
            if (!take_value(index, result.preset_name))
            {
                return false;
            }
        }
        else if (allow_template_options && argument == "--fields")
        {
            if (!take_value(index, result.template_fields))
            {
                return false;
            }
        }
        else if (argument == "--config")
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

[[nodiscard]] Result<ConfigFile>
load_management_config(const ConfigInvocation& invocation,
                       const std::optional<std::filesystem::path>& executable_directory)
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
    auto discovered = torrentutils::frontend::discover_config(paths, executable_directory);
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

} // namespace

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

namespace {

[[nodiscard]] std::string pretty_json(const std::string& compact)
{
    auto document = Json::parse(compact, nullptr, false);
    if (document.is_discarded())
    {
        return compact;
    }
    return dump_json(document, 2);
}

} // namespace

[[nodiscard]] int
run_config_command(const std::vector<std::string>& args, std::ostream& output,
                   std::ostream& diagnostics,
                   const std::optional<std::filesystem::path>& executable_directory)
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
        auto discovered = torrentutils::frontend::discover_config(paths, executable_directory);
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
            output << dump_json(
                          Json{{"ok", true}, {"data", {{"path", discovered_path->u8string()}}}})
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
            output << dump_json(Json{{"ok", true}, {"data", {{"path", target.u8string()}}}})
                   << '\n';
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
        auto loaded = load_management_config(invocation, executable_directory);
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
        auto loaded = load_management_config(invocation, executable_directory);
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
        auto loaded = load_management_config(invocation, executable_directory);
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
            output << dump_json(Json{{"ok", true},
                                     {"data",
                                      {{"path", loaded.value().path().u8string()},
                                       {"key", invocation.positional[1]},
                                       {"dry_run", invocation.dry_run}}}})
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

namespace {

struct TorrentTemplatePreset
{
    torrentutils::frontend::TemplateSelection selection;
    std::vector<torrentutils::frontend::TemplateWarning> warnings;
};

[[nodiscard]] Result<TorrentTemplatePreset>
select_torrent_template_preset(const std::string& torrent_path,
                               const std::optional<std::string>& field_list)
{
    namespace frontend = torrentutils::frontend;
    auto fields = frontend::default_template_fields();
    if (field_list)
    {
        auto parsed_fields = frontend::parse_template_field_list(*field_list);
        if (!parsed_fields)
        {
            return Result<TorrentTemplatePreset>::failure(std::move(parsed_fields).error());
        }
        fields = std::move(parsed_fields).value();
    }
    auto loaded = frontend::load_torrent_template(path_from_utf8(torrent_path));
    if (!loaded)
    {
        return Result<TorrentTemplatePreset>::failure(std::move(loaded).error());
    }
    auto selection = frontend::select_template_fields(loaded.value(), fields);
    if (!selection)
    {
        return Result<TorrentTemplatePreset>::failure(std::move(selection).error());
    }
    return Result<TorrentTemplatePreset>::success(
        {std::move(selection).value(), std::move(loaded.value().warnings)});
}

[[nodiscard]] Json preset_json_from_config(const ConfigFile& config, const std::string& name)
{
    auto document_result = config.document_json();
    if (!document_result)
    {
        return Json::object();
    }
    auto document = Json::parse(document_result.value(), nullptr, false);
    if (document.is_discarded() || !document.is_object() ||
        document.find("presets") == document.end() ||
        document["presets"].find(name) == document["presets"].end())
    {
        return Json::object();
    }
    return document["presets"][name];
}

} // namespace

[[nodiscard]] int
run_preset_command(const std::vector<std::string>& args, std::ostream& output,
                   std::ostream& diagnostics,
                   const std::optional<std::filesystem::path>& executable_directory)
{
    if (help_requested(args))
    {
        output << kPresetHelp;
        return 0;
    }
    ConfigInvocation invocation;
    std::string parse_error;
    if (!parse_config_invocation(args, invocation, parse_error, true))
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
    if (subcommand != "add" &&
        (invocation.from_torrent || invocation.preset_name || invocation.template_fields))
    {
        diagnostics
            << "error: --from-torrent, --name, and --fields are only valid for preset add\n";
        return 2;
    }
    if (!invocation.from_torrent && invocation.template_fields)
    {
        diagnostics << "error: --fields requires --from-torrent\n";
        return 2;
    }

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
            auto loaded = load_management_config(invocation, executable_directory);
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
            output << dump_json(Json{{"ok", true}, {"data", {{"presets", names}}}}) << '\n';
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
        auto loaded = load_management_config(invocation, executable_directory);
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
            output << dump_json(preset) << '\n';
        }
        else
        {
            write_human_text(output, dump_json(preset, 2));
            output << "\n";
        }
        return 0;
    }

    if (subcommand == "add")
    {
        const std::size_t expected_positionals = invocation.from_torrent ? 1U : 2U;
        if (invocation.positional.size() != expected_positionals)
        {
            diagnostics << (invocation.from_torrent
                                ? "error: preset add --from-torrent does not take a file argument\n"
                                : "error: preset add requires exactly one file\n");
            return 2;
        }
        auto loaded = load_management_config(invocation, executable_directory);
        if (!loaded)
        {
            print_error(loaded.error(), invocation.json, diagnostics);
            return error_exit_code(loaded.error().code);
        }
        CreationSettingsPatch preset_settings;
        std::string name;
        std::vector<torrentutils::frontend::TemplateFieldCandidate> skipped_fields;
        std::vector<torrentutils::frontend::TemplateWarning> load_warnings;
        if (invocation.from_torrent)
        {
            auto selected = select_torrent_template_preset(*invocation.from_torrent,
                                                           invocation.template_fields);
            if (!selected)
            {
                print_error(selected.error(), invocation.json, diagnostics);
                return error_exit_code(selected.error().code);
            }
            preset_settings = std::move(selected.value().selection.settings);
            skipped_fields = std::move(selected.value().selection.skipped);
            load_warnings = std::move(selected.value().warnings);
            name = path_from_utf8(*invocation.from_torrent).stem().u8string();
        }
        else
        {
            const auto& file = invocation.positional[1];
            auto parsed = torrentutils::frontend::load_preset_file(path_from_utf8(file));
            if (!parsed)
            {
                print_error(parsed.error(), invocation.json, diagnostics);
                return error_exit_code(parsed.error().code);
            }
            preset_settings = std::move(parsed.value().settings);
            name = path_from_utf8(file).stem().u8string();
            if (name.rfind("preset_", 0) == 0)
            {
                name = name.substr(7);
            }
        }
        if (invocation.preset_name)
        {
            name = *invocation.preset_name;
        }
        if (!invocation.json)
        {
            // JSON output reports these in data.load_warnings and data.skipped_fields instead.
            for (const auto& warning : load_warnings)
            {
                diagnostics << "warning: ";
                write_human_text(diagnostics,
                                 torrentutils::frontend::template_warning_text(warning));
                diagnostics << '\n';
            }
            for (const auto& skipped : skipped_fields)
            {
                diagnostics << "warning: template field "
                            << torrentutils::frontend::template_field_name(skipped.field)
                            << " skipped: " << template_field_unavailable_reason(skipped) << '\n';
            }
        }
        if (name.empty())
        {
            print_error({ErrorCode::ValidationFailed, "preset name must not be empty", {}},
                        invocation.json, diagnostics);
            return 3;
        }
        auto added = loaded.value().add_preset(name, preset_settings, invocation.force);
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
            Json data{{"path", loaded.value().path().u8string()},
                      {"preset", name},
                      {"dry_run", invocation.dry_run}};
            if (invocation.from_torrent)
            {
                Json skipped = Json::array();
                for (const auto& field : skipped_fields)
                {
                    skipped.push_back(
                        {{"field",
                          std::string(torrentutils::frontend::template_field_name(field.field))},
                         {"reason", template_field_unavailable_reason(field)}});
                }
                Json warnings = Json::array();
                for (const auto& warning : load_warnings)
                {
                    warnings.push_back(torrentutils::frontend::template_warning_text(warning));
                }
                data["settings"] = preset_json_from_config(loaded.value(), name);
                data["load_warnings"] = std::move(warnings);
                data["skipped_fields"] = std::move(skipped);
            }
            output << dump_json(Json{{"ok", true}, {"data", std::move(data)}}) << '\n';
        }
        else if (!invocation.quiet)
        {
            output << (invocation.dry_run ? "preset dry-run valid: " : "preset added: ");
            write_human_text(output, name);
            output << "\n";
            if (invocation.from_torrent && invocation.dry_run)
            {
                write_human_text(output,
                                 dump_json(preset_json_from_config(loaded.value(), name), 2));
                output << "\n";
            }
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
        auto loaded = load_management_config(invocation, executable_directory);
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
            output << dump_json(Json{{"ok", true},
                                     {"data",
                                      {{"path", loaded.value().path().u8string()},
                                       {"preset", name},
                                       {"dry_run", invocation.dry_run}}}})
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

} // namespace torrentcraft::cli::detail
