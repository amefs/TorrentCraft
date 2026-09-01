#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <torrentutils/frontend/settings.hpp>

namespace torrentutils::frontend {

struct ConfigSearchPaths
{
    std::optional<std::filesystem::path> explicit_path;
    std::filesystem::path working_directory;
    std::optional<std::filesystem::path> user_config_path;
};

[[nodiscard]] ConfigSearchPaths
default_config_search_paths(std::optional<std::filesystem::path> explicit_path,
                            std::filesystem::path working_directory);

[[nodiscard]] core::Result<std::optional<std::filesystem::path>>
discover_config(const ConfigSearchPaths& paths);

[[nodiscard]] core::Result<ParsedPreset> load_preset_file(const std::filesystem::path& path);

/** GUI-only language preference persisted under the config document's gui object. */
enum class GuiLanguage
{
    English,
    SimplifiedChinese,
};

class ConfigFile
{
  public:
    [[nodiscard]] static core::Result<ConfigFile> load(const std::filesystem::path& path);

    ConfigFile(const ConfigFile&) = delete;
    ConfigFile(ConfigFile&&) noexcept;
    ConfigFile& operator=(const ConfigFile&) = delete;
    ConfigFile& operator=(ConfigFile&&) noexcept;
    ~ConfigFile();

    [[nodiscard]] const std::filesystem::path& path() const noexcept;
    [[nodiscard]] const ParsedConfig& parsed() const noexcept;
    /** Returns English for a missing or unsupported persisted value. */
    [[nodiscard]] GuiLanguage gui_language() const;
    [[nodiscard]] core::Result<void> set_gui_language(GuiLanguage language);
    [[nodiscard]] GuiPreferences gui_preferences() const;
    [[nodiscard]] core::Result<void> set_gui_preferences(const GuiPreferences& preferences);

    [[nodiscard]] core::Result<void> set_defaults(const CreationSettingsPatch& settings);
    [[nodiscard]] core::Result<void>
    add_preset(std::string name, const CreationSettingsPatch& settings, bool overwrite = false);
    [[nodiscard]] core::Result<void> remove_preset(const std::string& name);
    [[nodiscard]] core::Result<void> save() const;
    [[nodiscard]] core::Result<std::string> get_key(const ConfigKey& key) const;
    [[nodiscard]] core::Result<void> set_key(const ConfigKey& key,
                                             std::optional<std::string> json_value);
    [[nodiscard]] core::Result<std::string> document_json() const;
    [[nodiscard]] static core::Result<ConfigFile> create(const std::filesystem::path& path,
                                                         bool overwrite = false);

  private:
    struct Details;

    explicit ConfigFile(std::unique_ptr<Details> details) noexcept;

    std::unique_ptr<Details> details_;
};

} // namespace torrentutils::frontend
