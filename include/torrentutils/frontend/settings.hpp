#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <torrentutils/core/result.hpp>
#include <torrentutils/core/torrent_engine.hpp>
#include <vector>

namespace torrentutils::frontend {

inline constexpr std::uint32_t default_verify_hashing_workers = 1U;
inline constexpr std::uint64_t default_verify_checking_memory_bytes = 32ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t default_memory_working_set_limit_bytes = 512ULL * 1024ULL * 1024ULL;

enum class SettingsDiagnosticCode
{
    LegacyEncodingIgnored
};

struct SettingsDiagnostic
{
    SettingsDiagnosticCode code;
    std::string key;
};

struct PieceSizeSetting
{
    std::optional<std::uint32_t> fixed_kib;
};

struct CreationSettingsPatch
{
    std::optional<core::TorrentFormat> format;
    std::optional<core::FileOrderPolicy> file_order;
    std::optional<PieceSizeSetting> piece_size;
    std::optional<bool> is_private;
    std::optional<std::vector<std::vector<std::string>>> tracker_tiers;
    std::optional<std::vector<std::string>> web_seeds;
    std::optional<std::string> comment;
    std::optional<std::string> created_by;
    std::optional<std::string> info_source;
};

struct VerifyResourceSettings
{
    std::optional<std::uint32_t> hashing_workers;
    std::optional<std::uint64_t> checking_memory_bytes;
};

enum class GuiSaveLocationMode
{
    Current,
    Recent,
    Specified,
};

enum class GuiLogLevel
{
    Debug,
    Info,
    Warning,
    Error,
};

struct GuiPreferences
{
    GuiSaveLocationMode default_save_location{GuiSaveLocationMode::Current};
    std::optional<std::string> default_save_path;
    std::optional<std::string> recent_save_path;
    std::optional<std::string> default_preset;
    std::optional<std::string> font_family;
    std::optional<std::string> style;
    bool show_padding_files{};
    bool logging_enabled{};
    GuiLogLevel log_level{GuiLogLevel::Info};
    std::optional<std::string> log_path;
};

struct ParsedConfig
{
    CreationSettingsPatch defaults;
    std::map<std::string, CreationSettingsPatch> presets;
    std::optional<VerifyResourceSettings> verify;
    std::optional<core::DiskIoMode> disk_io;
    std::optional<std::uint64_t> memory_working_set_limit_bytes;
    std::vector<SettingsDiagnostic> diagnostics;
    bool legacy{};
};

struct ParsedPreset
{
    CreationSettingsPatch settings;
    std::vector<SettingsDiagnostic> diagnostics;
};

struct ResolvedCreationSettings
{
    core::CreateOptions options;
    core::CreationMetadataInput creation_metadata;
    core::CreateInfoInput create_info;
    std::optional<core::DiskIoMode> disk_io;
};

[[nodiscard]] core::Result<ParsedConfig> parse_config_json(std::string_view input);
[[nodiscard]] core::Result<ParsedPreset> parse_preset_json(std::string_view input);
[[nodiscard]] core::Result<VerifyResourceSettings>
parse_verify_settings_json(std::string_view input);

[[nodiscard]] CreationSettingsPatch overlay_settings(CreationSettingsPatch lower,
                                                     const CreationSettingsPatch& higher);

[[nodiscard]] VerifyResourceSettings overlay_verify_settings(VerifyResourceSettings lower,
                                                             const VerifyResourceSettings& higher);

[[nodiscard]] core::Result<ResolvedCreationSettings>
resolve_settings(const CreationSettingsPatch& settings);

[[nodiscard]] core::Result<std::optional<core::VerificationResourceBudget>>
resolve_verify_resource_budget(const VerifyResourceSettings& settings);

[[nodiscard]] std::optional<std::uint64_t> parse_memory_size(std::string_view value);

enum class ConfigScope
{
    Defaults,
    Preset,
    Verify,
    DiskIo,
    MemoryWorkingSetLimit
};

struct ConfigKey
{
    ConfigScope scope;
    std::optional<std::string> preset_name;
    std::string setting;
};

[[nodiscard]] core::Result<ConfigKey> parse_config_key(std::string_view input);

/**
 * Applies a process working-set hard maximum on Windows. Other platforms accept the setting as a
 * no-op so one canonical config remains portable.
 */
[[nodiscard]] core::Result<void> apply_memory_working_set_limit_bytes(std::uint64_t limit_bytes);

} // namespace torrentutils::frontend
