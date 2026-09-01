#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <system_error>
#include <torrentutils/frontend/config.hpp>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace torrentutils::frontend {
namespace {

constexpr std::size_t kMaximumConfigBytes = std::size_t{1} * 1024U * 1024U;
constexpr std::string_view kConfigFilename = "torrentcraft.json";

using Json = nlohmann::json;

[[nodiscard]] core::Error filesystem_error(const std::error_code& error, std::string message)
{
    if (error == std::errc::no_such_file_or_directory)
    {
        return {core::ErrorCode::FileNotFound, std::move(message), {}};
    }
    if (error == std::errc::permission_denied || error == std::errc::operation_not_permitted)
    {
        return {core::ErrorCode::AccessDenied, std::move(message), {}};
    }
    return {core::ErrorCode::IoFailure, std::move(message), {}};
}

[[nodiscard]] core::Error stream_error(const int error, std::string message)
{
    if (error == ENOENT)
    {
        return {core::ErrorCode::FileNotFound, std::move(message), {}};
    }
    if (error == EACCES || error == EPERM)
    {
        return {core::ErrorCode::AccessDenied, std::move(message), {}};
    }
    return {core::ErrorCode::IoFailure, std::move(message), {}};
}

[[nodiscard]] core::Error validation_error(std::string field, std::string message)
{
    return {core::ErrorCode::ValidationFailed,
            "frontend config validation failed",
            {{std::move(field), std::move(message)}}};
}

class PendingFile
{
  public:
    explicit PendingFile(std::filesystem::path path) : path_(std::move(path)) {}

    ~PendingFile()
    {
        if (!committed_)
        {
            std::error_code ignored;
            std::filesystem::remove(path_, ignored);
        }
    }

    PendingFile(const PendingFile&) = delete;
    PendingFile& operator=(const PendingFile&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

    void commit() noexcept
    {
        committed_ = true;
    }

  private:
    std::filesystem::path path_;
    bool committed_{};
};

[[nodiscard]] std::filesystem::path temporary_sibling(const std::filesystem::path& target)
{
    static std::atomic<std::uint64_t> sequence{};
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    auto name = target.filename();
    name +=
        (".torrentutils.tmp." + std::to_string(tick) + "." + std::to_string(sequence.fetch_add(1)));
    return target.parent_path() / name;
}

[[nodiscard]] std::error_code atomic_replace(const std::filesystem::path& source,
                                             const std::filesystem::path& target)
{
#ifdef _WIN32
    if (MoveFileExW(source.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
    {
        return {static_cast<int>(GetLastError()), std::system_category()};
    }
    return {};
#else
    std::error_code error;
    std::filesystem::rename(source, target, error);
    return error;
#endif
}

[[nodiscard]] core::Result<std::string> read_config_file(const std::filesystem::path& path)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error)
    {
        return core::Result<std::string>::failure(
            filesystem_error(error, "could not inspect config file"));
    }
    if (size > kMaximumConfigBytes ||
        size > static_cast<std::uintmax_t>((std::numeric_limits<std::streamsize>::max)()))
    {
        return core::Result<std::string>::failure(
            validation_error("frontend.config", "input exceeds the 1 MiB limit"));
    }

    errno = 0;
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return core::Result<std::string>::failure(
            stream_error(errno, "could not open config file"));
    }
    std::string contents(static_cast<std::size_t>(size), '\0');
    if (!contents.empty())
    {
        input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    }
    if (!input && !input.eof())
    {
        return core::Result<std::string>::failure(
            stream_error(errno, "could not read config file"));
    }
    return core::Result<std::string>::success(std::move(contents));
}

[[nodiscard]] std::string format_name(const core::TorrentFormat format)
{
    switch (format)
    {
    case core::TorrentFormat::V1:
        return "v1";
    case core::TorrentFormat::V2:
        return "v2";
    case core::TorrentFormat::Hybrid:
        return "hybrid";
    }
    return "hybrid";
}

[[nodiscard]] std::string file_order_name(const core::FileOrderPolicy policy)
{
    switch (policy)
    {
    case core::FileOrderPolicy::Lexicographical:
        return "lexicographical";
    case core::FileOrderPolicy::CanonicalAlignment:
        return "canonical_alignment";
    case core::FileOrderPolicy::Natural:
        return "natural";
    case core::FileOrderPolicy::BreadthFirst:
        return "breadth_first";
    }
    return "lexicographical";
}

void apply_patch(Json& object, const CreationSettingsPatch& patch)
{
    if (patch.format)
    {
        object["format"] = format_name(*patch.format);
    }
    if (patch.file_order)
    {
        object["file_order"] = file_order_name(*patch.file_order);
    }
    if (patch.piece_size)
    {
        if (patch.piece_size->fixed_kib)
        {
            object["piece_size"] = *patch.piece_size->fixed_kib;
        }
        else
        {
            object["piece_size"] = "auto";
        }
    }
    if (patch.is_private)
    {
        object["private"] = *patch.is_private;
    }
    if (patch.tracker_tiers)
    {
        object.erase("tracker_list");
        object["tracker_tiers"] = *patch.tracker_tiers;
    }
    if (patch.web_seeds)
    {
        object["web_seeds"] = *patch.web_seeds;
    }
    if (patch.comment)
    {
        object["comment"] = *patch.comment;
    }
    if (patch.created_by)
    {
        object["created_by"] = *patch.created_by;
    }
    if (patch.info_source)
    {
        object["source"] = *patch.info_source;
    }
}

[[nodiscard]] core::Result<ParsedConfig> parse_document(const Json& document)
{
    return parse_config_json(document.dump());
}

[[nodiscard]] const Json* gui_member(const Json& document, const char* key)
{
    const auto gui = document.find("gui");
    if (gui == document.end() || !gui->is_object())
    {
        return nullptr;
    }
    const auto member = gui->find(key);
    return member == gui->end() ? nullptr : &*member;
}

[[nodiscard]] std::string gui_save_location_name(const GuiSaveLocationMode mode)
{
    switch (mode)
    {
    case GuiSaveLocationMode::Current:
        return "current";
    case GuiSaveLocationMode::Recent:
        return "recent";
    case GuiSaveLocationMode::Specified:
        return "specified";
    }
    return "current";
}

[[nodiscard]] std::string gui_log_level_name(const GuiLogLevel level)
{
    switch (level)
    {
    case GuiLogLevel::Debug:
        return "debug";
    case GuiLogLevel::Info:
        return "info";
    case GuiLogLevel::Warning:
        return "warning";
    case GuiLogLevel::Error:
        return "error";
    }
    return "info";
}

[[nodiscard]] bool valid_preset_name(const std::string& name) noexcept
{
    return !name.empty() && name.find('\0') == std::string::npos &&
           name.find('/') == std::string::npos && name.find('\\') == std::string::npos;
}

[[nodiscard]] bool is_known_setting(const std::string& setting) noexcept
{
    static const std::unordered_set<std::string> known = {
        "format",        "file_order", "piece_size", "private",    "tracker_list",
        "tracker_tiers", "web_seeds",  "comment",    "created_by", "source"};
    return known.count(setting) != 0;
}

[[nodiscard]] bool setting_requires_canonical(const std::string& setting) noexcept
{
    return setting == "format" || setting == "file_order" || setting == "tracker_tiers" ||
           setting == "web_seeds" || setting == "source";
}

#ifdef _WIN32
[[nodiscard]] std::optional<std::filesystem::path> windows_environment_path(const wchar_t* name)
{
    const auto required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0U)
    {
        return std::nullopt;
    }

    std::vector<wchar_t> value(required);
    const auto copied = GetEnvironmentVariableW(name, value.data(), required);
    if (copied == 0U || copied >= required)
    {
        return std::nullopt;
    }
    return std::filesystem::path(value.data());
}
#endif

[[nodiscard]] std::optional<std::filesystem::path> platform_user_config_path()
{
#ifdef _WIN32
    if (const auto app_data = windows_environment_path(L"APPDATA"))
    {
        return *app_data / "torrentcraft" / kConfigFilename;
    }
#else
    if (const auto* xdg_config = std::getenv("XDG_CONFIG_HOME");
        xdg_config != nullptr && *xdg_config != '\0')
    {
        return std::filesystem::path(xdg_config) / "torrentcraft" / kConfigFilename;
    }
    if (const auto* home = std::getenv("HOME"); home != nullptr && *home != '\0')
    {
        return std::filesystem::path(home) / ".config" / "torrentcraft" / kConfigFilename;
    }
#endif
    return std::nullopt;
}

} // namespace

struct ConfigFile::Details
{
    std::filesystem::path path;
    Json document;
    ParsedConfig parsed;
};

ConfigSearchPaths default_config_search_paths(std::optional<std::filesystem::path> explicit_path,
                                              std::filesystem::path working_directory)
{
    return {std::move(explicit_path), std::move(working_directory), platform_user_config_path()};
}

core::Result<std::optional<std::filesystem::path>> discover_config(const ConfigSearchPaths& paths)
{
    std::vector<std::filesystem::path> candidates;
    if (paths.explicit_path)
    {
        candidates.push_back(*paths.explicit_path);
    }
    else
    {
        candidates.push_back(paths.working_directory / kConfigFilename);
        if (paths.user_config_path)
        {
            candidates.push_back(*paths.user_config_path);
        }
    }

    for (const auto& candidate : candidates)
    {
        std::error_code error;
        const auto status = std::filesystem::status(candidate, error);
        if (error)
        {
            if (error == std::errc::no_such_file_or_directory)
            {
                if (paths.explicit_path)
                {
                    return core::Result<std::optional<std::filesystem::path>>::failure(
                        filesystem_error(error, "explicit config file does not exist"));
                }
                continue;
            }
            return core::Result<std::optional<std::filesystem::path>>::failure(
                filesystem_error(error, "could not inspect config file"));
        }
        if (status.type() == std::filesystem::file_type::not_found)
        {
            if (paths.explicit_path)
            {
                return core::Result<std::optional<std::filesystem::path>>::failure(
                    filesystem_error(std::make_error_code(std::errc::no_such_file_or_directory),
                                     "explicit config file does not exist"));
            }
            continue;
        }
        if (!std::filesystem::is_regular_file(status))
        {
            return core::Result<std::optional<std::filesystem::path>>::failure(
                validation_error("frontend.config", "config path must resolve to a regular file"));
        }
        return core::Result<std::optional<std::filesystem::path>>::success(candidate);
    }
    return core::Result<std::optional<std::filesystem::path>>::success(std::nullopt);
}

core::Result<ParsedPreset> load_preset_file(const std::filesystem::path& path)
{
    auto contents = read_config_file(path);
    if (!contents)
    {
        return core::Result<ParsedPreset>::failure(std::move(contents).error());
    }
    return parse_preset_json(contents.value());
}

ConfigFile::ConfigFile(std::unique_ptr<Details> details) noexcept : details_(std::move(details)) {}

ConfigFile::ConfigFile(ConfigFile&&) noexcept = default;
ConfigFile& ConfigFile::operator=(ConfigFile&&) noexcept = default;
ConfigFile::~ConfigFile() = default;

core::Result<ConfigFile> ConfigFile::load(const std::filesystem::path& path)
{
    auto contents = read_config_file(path);
    if (!contents)
    {
        return core::Result<ConfigFile>::failure(std::move(contents).error());
    }
    auto parsed = parse_config_json(contents.value());
    if (!parsed)
    {
        return core::Result<ConfigFile>::failure(std::move(parsed).error());
    }
    auto document = Json::parse(contents.value(), nullptr, false);
    if (document.is_discarded())
    {
        return core::Result<ConfigFile>::failure(
            validation_error("frontend.config", "invalid JSON syntax"));
    }
    return core::Result<ConfigFile>::success(ConfigFile(
        std::make_unique<Details>(Details{path, std::move(document), std::move(parsed).value()})));
}

const std::filesystem::path& ConfigFile::path() const noexcept
{
    return details_->path;
}

const ParsedConfig& ConfigFile::parsed() const noexcept
{
    return details_->parsed;
}

GuiLanguage ConfigFile::gui_language() const
{
    const auto gui = details_->document.find("gui");
    if (gui == details_->document.end() || !gui->is_object())
    {
        return GuiLanguage::English;
    }
    const auto language = gui->find("language");
    if (language != gui->end() && language->is_string() && language->get<std::string>() == "zh_CN")
    {
        return GuiLanguage::SimplifiedChinese;
    }
    return GuiLanguage::English;
}

core::Result<void> ConfigFile::set_gui_language(const GuiLanguage language)
{
    const auto original = details_->document;
    auto& gui = details_->document["gui"];
    if (!gui.is_object())
    {
        gui = Json::object();
    }
    gui["language"] = language == GuiLanguage::SimplifiedChinese ? "zh_CN" : "en";

    auto parsed = parse_document(details_->document);
    if (!parsed)
    {
        details_->document = original;
        return core::Result<void>::failure(std::move(parsed).error());
    }
    details_->parsed = std::move(parsed).value();
    return core::Result<void>::success();
}

GuiPreferences ConfigFile::gui_preferences() const
{
    GuiPreferences preferences;
    if (const auto value = gui_member(details_->document, "default_save_location");
        value != nullptr && value->is_string())
    {
        const auto mode = value->get<std::string>();
        if (mode == "recent")
        {
            preferences.default_save_location = GuiSaveLocationMode::Recent;
        }
        else if (mode == "specified")
        {
            preferences.default_save_location = GuiSaveLocationMode::Specified;
        }
    }
    if (const auto value = gui_member(details_->document, "default_save_path");
        value != nullptr && value->is_string() && !value->get<std::string>().empty())
    {
        preferences.default_save_path = value->get<std::string>();
    }
    if (const auto value = gui_member(details_->document, "recent_save_path");
        value != nullptr && value->is_string() && !value->get<std::string>().empty())
    {
        preferences.recent_save_path = value->get<std::string>();
    }
    if (const auto value = gui_member(details_->document, "default_preset");
        value != nullptr && value->is_string() && !value->get<std::string>().empty())
    {
        preferences.default_preset = value->get<std::string>();
    }

    const auto gui = details_->document.find("gui");
    if (gui == details_->document.end() || !gui->is_object())
    {
        return preferences;
    }
    if (const auto value = gui->find("font_family");
        value != gui->end() && value->is_string() && !value->get<std::string>().empty())
    {
        preferences.font_family = value->get<std::string>();
    }
    if (const auto value = gui->find("style");
        value != gui->end() && value->is_string() && !value->get<std::string>().empty())
    {
        preferences.style = value->get<std::string>();
    }
    const auto file_tree = gui->find("file_tree");
    if (file_tree != gui->end() && file_tree->is_object())
    {
        const auto show_padding = file_tree->find("show_padding_files");
        if (show_padding != file_tree->end() && show_padding->is_boolean())
        {
            preferences.show_padding_files = show_padding->get<bool>();
        }
    }
    const auto logging = gui->find("logging");
    if (logging == gui->end() || !logging->is_object())
    {
        return preferences;
    }
    const auto enabled = logging->find("enabled");
    if (enabled != logging->end() && enabled->is_boolean())
    {
        preferences.logging_enabled = enabled->get<bool>();
    }
    const auto level = logging->find("level");
    if (level != logging->end() && level->is_string())
    {
        const auto value = level->get<std::string>();
        if (value == "debug")
            preferences.log_level = GuiLogLevel::Debug;
        else if (value == "warning")
            preferences.log_level = GuiLogLevel::Warning;
        else if (value == "error")
            preferences.log_level = GuiLogLevel::Error;
        else if (value == "info")
            preferences.log_level = GuiLogLevel::Info;
    }
    const auto path = logging->find("path");
    if (path != logging->end() && path->is_string() && !path->get<std::string>().empty())
    {
        preferences.log_path = path->get<std::string>();
    }
    return preferences;
}

core::Result<void> ConfigFile::set_gui_preferences(const GuiPreferences& preferences)
{
    if (preferences.default_save_location == GuiSaveLocationMode::Specified &&
        (!preferences.default_save_path || preferences.default_save_path->empty()))
    {
        return core::Result<void>::failure(validation_error(
            "frontend.gui.default_save_path", "is required for the specified save location"));
    }
    const auto original = details_->document;
    auto& gui = details_->document["gui"];
    if (!gui.is_object())
    {
        gui = Json::object();
    }
    gui["default_save_location"] = gui_save_location_name(preferences.default_save_location);
    if (preferences.default_save_path && !preferences.default_save_path->empty())
    {
        gui["default_save_path"] = *preferences.default_save_path;
    }
    else
    {
        gui.erase("default_save_path");
    }

    if (preferences.recent_save_path && !preferences.recent_save_path->empty())
    {
        gui["recent_save_path"] = *preferences.recent_save_path;
    }
    else
    {
        gui.erase("recent_save_path");
    }
    if (preferences.default_preset && !preferences.default_preset->empty())
    {
        gui["default_preset"] = *preferences.default_preset;
    }
    else
    {
        gui.erase("default_preset");
    }
    if (preferences.font_family && !preferences.font_family->empty())
    {
        gui["font_family"] = *preferences.font_family;
    }
    else
    {
        gui.erase("font_family");
    }
    if (preferences.style && !preferences.style->empty())
    {
        gui["style"] = *preferences.style;
    }
    else
    {
        gui.erase("style");
    }

    auto& file_tree = gui["file_tree"];
    if (!file_tree.is_object())
    {
        file_tree = Json::object();
    }
    file_tree["show_padding_files"] = preferences.show_padding_files;

    auto& logging = gui["logging"];
    if (!logging.is_object())
    {
        logging = Json::object();
    }
    logging["enabled"] = preferences.logging_enabled;
    logging["level"] = gui_log_level_name(preferences.log_level);
    if (preferences.log_path && !preferences.log_path->empty())
    {
        logging["path"] = *preferences.log_path;
    }
    else
    {
        logging.erase("path");
    }

    auto parsed = parse_document(details_->document);
    if (!parsed)
    {
        details_->document = original;
        return core::Result<void>::failure(std::move(parsed).error());
    }
    details_->parsed = std::move(parsed).value();
    return core::Result<void>::success();
}

core::Result<void> ConfigFile::set_defaults(const CreationSettingsPatch& settings)
{
    const auto original = details_->document;
    Json& defaults = details_->parsed.legacy ? details_->document : details_->document["defaults"];
    if (!defaults.is_object())
    {
        defaults = Json::object();
    }
    apply_patch(defaults, settings);
    auto parsed = parse_document(details_->document);
    if (!parsed)
    {
        details_->document = original;
        return core::Result<void>::failure(std::move(parsed).error());
    }
    details_->parsed = std::move(parsed).value();
    return core::Result<void>::success();
}

core::Result<void> ConfigFile::add_preset(std::string name, const CreationSettingsPatch& settings,
                                          const bool overwrite)
{
    if (details_->parsed.legacy)
    {
        return core::Result<void>::failure(validation_error(
            "frontend.config.presets", "legacy config does not support named presets"));
    }
    if (!valid_preset_name(name))
    {
        return core::Result<void>::failure(
            validation_error("frontend.config.preset_name", "must be a non-empty simple name"));
    }
    Json& presets = details_->document["presets"];
    if (!presets.is_object())
    {
        presets = Json::object();
    }
    if (presets.find(name) != presets.end() && !overwrite)
    {
        return core::Result<void>::failure(
            validation_error("frontend.config.preset_name", "must not already exist"));
    }

    const auto original = details_->document;
    Json preset = Json::object();
    apply_patch(preset, settings);
    presets[std::move(name)] = std::move(preset);
    auto parsed = parse_document(details_->document);
    if (!parsed)
    {
        details_->document = original;
        return core::Result<void>::failure(std::move(parsed).error());
    }
    details_->parsed = std::move(parsed).value();
    return core::Result<void>::success();
}

core::Result<void> ConfigFile::remove_preset(const std::string& name)
{
    if (details_->parsed.legacy)
    {
        return core::Result<void>::failure(validation_error(
            "frontend.config.presets", "legacy config does not support named presets"));
    }
    const auto presets = details_->document.find("presets");
    if (presets == details_->document.end() || !presets->is_object() ||
        presets->find(name) == presets->end())
    {
        return core::Result<void>::failure(
            validation_error("frontend.config.preset_name", "must identify an existing preset"));
    }

    const auto original = details_->document;
    details_->document["presets"].erase(name);
    auto parsed = parse_document(details_->document);
    if (!parsed)
    {
        details_->document = original;
        return core::Result<void>::failure(std::move(parsed).error());
    }
    details_->parsed = std::move(parsed).value();
    return core::Result<void>::success();
}

core::Result<void> ConfigFile::save() const
{
    std::string contents;
    try
    {
        contents = details_->document.dump(2);
        contents.push_back('\n');
    }
    catch (const std::exception& error)
    {
        return core::Result<void>::failure(
            {core::ErrorCode::Internal,
             "could not encode config JSON: " + std::string(error.what()),
             {}});
    }

    PendingFile pending(temporary_sibling(details_->path));
    errno = 0;
    std::ofstream output(pending.path(), std::ios::binary | std::ios::trunc);
    if (!output)
    {
        return core::Result<void>::failure(
            stream_error(errno, "could not create temporary config file"));
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.close();
    if (!output)
    {
        return core::Result<void>::failure(
            stream_error(errno, "could not write temporary config file"));
    }

    const auto error = atomic_replace(pending.path(), details_->path);
    if (error)
    {
        return core::Result<void>::failure(
            filesystem_error(error, "could not atomically replace config file"));
    }
    pending.commit();
    return core::Result<void>::success();
}

core::Result<ConfigKey> parse_config_key(const std::string_view input)
{
    const std::string text(input);
    if (text.rfind("defaults.", 0) == 0)
    {
        const std::string setting = text.substr(9);
        if (!is_known_setting(setting))
        {
            return core::Result<ConfigKey>::failure(
                validation_error("frontend.config.key", "unknown setting: " + setting));
        }
        return core::Result<ConfigKey>::success(
            ConfigKey{ConfigScope::Defaults, std::nullopt, setting});
    }
    if (text.rfind("presets.", 0) == 0)
    {
        const auto dot = text.find_last_of('.');
        if (dot == std::string::npos || dot <= 8)
        {
            return core::Result<ConfigKey>::failure(validation_error(
                "frontend.config.key", "preset key must be presets.<name>.<setting>"));
        }
        std::string name = text.substr(8, dot - 8);
        std::string setting = text.substr(dot + 1);
        if (!valid_preset_name(name))
        {
            return core::Result<ConfigKey>::failure(
                validation_error("frontend.config.preset_name", "must be a non-empty simple name"));
        }
        if (!is_known_setting(setting))
        {
            return core::Result<ConfigKey>::failure(
                validation_error("frontend.config.key", "unknown setting: " + setting));
        }
        return core::Result<ConfigKey>::success(
            ConfigKey{ConfigScope::Preset, std::move(name), std::move(setting)});
    }
    if (text.rfind("verify.", 0) == 0)
    {
        const std::string member = text.substr(7);
        if (member != "workers" && member != "memory")
        {
            return core::Result<ConfigKey>::failure(
                validation_error("frontend.config.key", "unknown verify member: " + member));
        }
        return core::Result<ConfigKey>::success(
            ConfigKey{ConfigScope::Verify, std::nullopt, member});
    }
    if (text == "disk_io")
    {
        return core::Result<ConfigKey>::success(
            ConfigKey{ConfigScope::DiskIo, std::nullopt, "disk_io"});
    }
    if (text == "memory_working_set_limit")
    {
        return core::Result<ConfigKey>::success(ConfigKey{
            ConfigScope::MemoryWorkingSetLimit, std::nullopt, "memory_working_set_limit"});
    }
    return core::Result<ConfigKey>::failure(validation_error(
        "frontend.config.key",
        "must be defaults.<setting>, presets.<name>.<setting>, verify.<member>, disk_io, or "
        "memory_working_set_limit"));
}

core::Result<std::string> ConfigFile::get_key(const ConfigKey& key) const
{
    const Json* object = nullptr;
    if (key.scope == ConfigScope::Preset)
    {
        if (details_->parsed.legacy)
        {
            return core::Result<std::string>::failure(validation_error(
                "frontend.config.presets", "legacy config does not support named presets"));
        }
        if (!key.preset_name)
        {
            return core::Result<std::string>::failure(
                validation_error("frontend.config.key", "preset scope requires a name"));
        }
        const auto presets = details_->document.find("presets");
        if (presets == details_->document.end() || !presets->is_object())
        {
            return core::Result<std::string>::failure({core::ErrorCode::FileNotFound,
                                                       "preset was not found",
                                                       {{"preset", *key.preset_name}}});
        }
        const auto preset = presets->find(*key.preset_name);
        if (preset == presets->end())
        {
            return core::Result<std::string>::failure({core::ErrorCode::FileNotFound,
                                                       "preset was not found",
                                                       {{"preset", *key.preset_name}}});
        }
        object = &*preset;
    }
    else if (key.scope == ConfigScope::Verify)
    {
        if (details_->parsed.legacy)
        {
            return core::Result<std::string>::failure(validation_error(
                "frontend.config.key", "verify settings require a canonical config"));
        }
        const auto verify = details_->document.find("verify");
        if (verify == details_->document.end() || !verify->is_object())
        {
            return core::Result<std::string>::failure(
                {core::ErrorCode::FileNotFound, "verify is not set", {{"setting", key.setting}}});
        }
        const auto value = verify->find(key.setting);
        if (value == verify->end())
        {
            return core::Result<std::string>::failure(
                {core::ErrorCode::FileNotFound, "setting is not set", {{"setting", key.setting}}});
        }
        return core::Result<std::string>::success(value->dump());
    }
    else if (key.scope == ConfigScope::DiskIo)
    {
        if (details_->parsed.legacy)
        {
            return core::Result<std::string>::failure(
                validation_error("frontend.config.key", "disk_io requires a canonical config"));
        }
        const auto value = details_->document.find("disk_io");
        if (value == details_->document.end())
        {
            return core::Result<std::string>::failure(
                {core::ErrorCode::FileNotFound, "setting is not set", {{"setting", "disk_io"}}});
        }
        return core::Result<std::string>::success(value->dump());
    }
    else if (key.scope == ConfigScope::MemoryWorkingSetLimit)
    {
        if (details_->parsed.legacy)
        {
            return core::Result<std::string>::failure(validation_error(
                "frontend.config.key", "memory_working_set_limit requires a canonical config"));
        }
        const auto value = details_->document.find("memory_working_set_limit");
        if (value == details_->document.end())
        {
            return core::Result<std::string>::failure({core::ErrorCode::FileNotFound,
                                                       "setting is not set",
                                                       {{"setting", "memory_working_set_limit"}}});
        }
        return core::Result<std::string>::success(value->dump());
    }
    else
    {
        const Json& defaults =
            details_->parsed.legacy ? details_->document : details_->document["defaults"];
        if (!defaults.is_object())
        {
            return core::Result<std::string>::failure(
                {core::ErrorCode::FileNotFound, "defaults are not set", {}});
        }
        object = &defaults;
    }

    const auto value = object->find(key.setting);
    if (value == object->end())
    {
        return core::Result<std::string>::failure(
            {core::ErrorCode::FileNotFound, "setting is not set", {{"setting", key.setting}}});
    }
    return core::Result<std::string>::success(value->dump());
}

core::Result<void> ConfigFile::set_key(const ConfigKey& key, std::optional<std::string> json_value)
{
    const auto original = details_->document;
    Json* target = nullptr;
    if (key.scope == ConfigScope::Preset)
    {
        if (details_->parsed.legacy)
        {
            return core::Result<void>::failure(validation_error(
                "frontend.config.presets", "legacy config does not support named presets"));
        }
        if (!key.preset_name || !valid_preset_name(*key.preset_name))
        {
            return core::Result<void>::failure(
                validation_error("frontend.config.preset_name", "must be a non-empty simple name"));
        }
        Json& presets = details_->document["presets"];
        if (!presets.is_object())
        {
            presets = Json::object();
        }
        Json& preset = presets[*key.preset_name];
        if (!preset.is_object())
        {
            preset = Json::object();
        }
        target = &preset;
    }
    else if (key.scope == ConfigScope::Verify)
    {
        if (details_->parsed.legacy)
        {
            return core::Result<void>::failure(validation_error(
                "frontend.config.key", "verify settings require a canonical config"));
        }
        Json& verify = details_->document["verify"];
        if (!verify.is_object())
        {
            verify = Json::object();
        }
        target = &verify;
    }
    else if (key.scope == ConfigScope::DiskIo)
    {
        if (details_->parsed.legacy)
        {
            return core::Result<void>::failure(
                validation_error("frontend.config.key", "disk_io requires a canonical config"));
        }
        target = &details_->document;
    }
    else if (key.scope == ConfigScope::MemoryWorkingSetLimit)
    {
        if (details_->parsed.legacy)
        {
            return core::Result<void>::failure(validation_error(
                "frontend.config.key", "memory_working_set_limit requires a canonical config"));
        }
        target = &details_->document;
    }
    else
    {
        if (setting_requires_canonical(key.setting) && details_->parsed.legacy)
        {
            return core::Result<void>::failure(
                validation_error("frontend.config.key", "setting requires a canonical config"));
        }
        Json& defaults =
            details_->parsed.legacy ? details_->document : details_->document["defaults"];
        if (!defaults.is_object())
        {
            defaults = Json::object();
        }
        target = &defaults;
    }

    if (!json_value)
    {
        target->erase(key.setting);
    }
    else
    {
        Json value = Json::parse(*json_value, nullptr, false);
        if (value.is_discarded())
        {
            return core::Result<void>::failure(
                validation_error("frontend.config.value", "must be valid JSON"));
        }
        (*target)[key.setting] = std::move(value);
    }

    auto parsed = parse_document(details_->document);
    if (!parsed)
    {
        details_->document = original;
        return core::Result<void>::failure(std::move(parsed).error());
    }
    details_->parsed = std::move(parsed).value();
    return core::Result<void>::success();
}

core::Result<std::string> ConfigFile::document_json() const
{
    return core::Result<std::string>::success(details_->document.dump());
}

core::Result<ConfigFile> ConfigFile::create(const std::filesystem::path& path, const bool overwrite)
{
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (!error && status.type() != std::filesystem::file_type::not_found && !overwrite)
    {
        return core::Result<ConfigFile>::failure(
            {core::ErrorCode::Conflict, "config file already exists", {}});
    }
    if (error && error != std::errc::no_such_file_or_directory)
    {
        return core::Result<ConfigFile>::failure(
            filesystem_error(error, "could not inspect config path"));
    }
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
        {
            return core::Result<ConfigFile>::failure(
                filesystem_error(error, "could not create config directory"));
        }
    }

    Json document = Json::object();
    document["schema"] = "torrentcraft.config/v1";
    std::string contents = document.dump(2);
    contents.push_back('\n');

    PendingFile pending(temporary_sibling(path));
    errno = 0;
    std::ofstream output(pending.path(), std::ios::binary | std::ios::trunc);
    if (!output)
    {
        return core::Result<ConfigFile>::failure(
            stream_error(errno, "could not create temporary config file"));
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.close();
    if (!output)
    {
        return core::Result<ConfigFile>::failure(
            stream_error(errno, "could not write temporary config file"));
    }
    const auto replace_error = atomic_replace(pending.path(), path);
    if (replace_error)
    {
        return core::Result<ConfigFile>::failure(
            filesystem_error(replace_error, "could not atomically replace config file"));
    }
    pending.commit();
    return ConfigFile::load(path);
}

} // namespace torrentutils::frontend
