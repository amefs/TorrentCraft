#include "cli_common.hpp"
#include "progress.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <torrentutils/core/application.hpp>
#include <utility>
#include <vector>

namespace torrentcraft::cli::detail {

namespace {

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

constexpr std::string_view kValidateHelp = R"(Usage:
  torrentcraft validate <torrent> [options]

Validate a torrent file.

Options:
  --strict                  Reject leniently loaded torrents
  --json                    Emit stable JSON output
  --quiet                   Suppress successful human output
  -h, --help                Show this help
)";

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
    const bool single_file = info.layout() == torrentutils::core::FileLayout::SingleFile;
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

} // namespace

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
        output << dump_json(Json{{"ok", true}, {"data", std::move(data)}}) << '\n';
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
        output << dump_json(Json{{"ok", true}, {"data", {{"tree", root}}}}) << '\n';
    }
    else if (!quiet)
    {
        const auto glyphs = tree_glyphs();
        print_tree_human(root, std::string{}, true, true, glyphs, output);
    }
    return 0;
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
        output << dump_json(Json{{"ok", true},
                                 {"data",
                                  {{"valid", true},
                                   {"strict", strict},
                                   {"warnings", std::move(warnings)},
                                   {"diagnostics", std::move(diagnostics_json)}}}})
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

} // namespace torrentcraft::cli::detail
