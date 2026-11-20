#pragma once

// Helpers shared by the CLI command files, and each command's entry point. Internal to the CLI.

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <torrentutils/core/application.hpp>
#include <torrentutils/core/error.hpp>
#include <torrentutils/core/torrent_engine.hpp>
#include <torrentutils/frontend/config.hpp>
#include <torrentutils/frontend/settings.hpp>
#include <torrentutils/frontend/torrent_template.hpp>
#include <vector>

namespace torrentcraft::cli::detail {

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
using torrentutils::frontend::template_field_unavailable_reason;
using torrentutils::frontend::VerifyResourceSettings;

// Shared helpers.

[[nodiscard]] std::filesystem::path path_from_utf8(const std::string& value);

[[nodiscard]] bool help_requested(const std::vector<std::string>& args) noexcept;

[[nodiscard]] std::optional<std::uint64_t> parse_unsigned(std::string_view value);

[[nodiscard]] std::string format_name(torrentutils::core::TorrentFormat format);

[[nodiscard]] std::string file_order_name(torrentutils::core::FileOrderPolicy policy);

[[nodiscard]] int error_exit_code(ErrorCode code);

[[nodiscard]] std::string error_code_name(ErrorCode code);

void print_error(const Error& error, bool json, std::ostream& diagnostics);

[[nodiscard]] Result<void> make_backup(const std::filesystem::path& path);

[[nodiscard]] int persist_edit(torrentutils::core::TorrentService& service,
                               const torrentutils::core::LoadedTorrent& loaded, bool dry_run,
                               bool backup, bool json, std::ostream& diagnostics);

[[nodiscard]] std::string
capability_code_name(torrentutils::core::VerificationCapabilityDiagnosticCode code);

// Command entry points; each returns the process exit code.

// cli_config.cpp
[[nodiscard]] int
run_config_command(const std::vector<std::string>& args, std::ostream& output,
                   std::ostream& diagnostics,
                   const std::optional<std::filesystem::path>& executable_directory);

[[nodiscard]] int
run_preset_command(const std::vector<std::string>& args, std::ostream& output,
                   std::ostream& diagnostics,
                   const std::optional<std::filesystem::path>& executable_directory);

// cli_create.cpp
[[nodiscard]] int
run_create_command(const std::vector<std::string>& args, std::ostream& output,
                   std::ostream& diagnostics,
                   const std::optional<std::filesystem::path>& executable_directory);

// cli_inspect.cpp
[[nodiscard]] int run_inspect_command(const std::vector<std::string>& args, std::ostream& output,
                                      std::ostream& diagnostics);

[[nodiscard]] int run_tree_command(const std::vector<std::string>& args, std::ostream& output,
                                   std::ostream& diagnostics);

[[nodiscard]] int run_validate_command(const std::vector<std::string>& args, std::ostream& output,
                                       std::ostream& diagnostics);

// cli_metadata.cpp
[[nodiscard]] int run_metadata_command(const std::vector<std::string>& args, std::ostream& output,
                                       std::ostream& diagnostics);

// cli_tracker.cpp
[[nodiscard]] int run_tracker_command(const std::vector<std::string>& args, std::ostream& output,
                                      std::ostream& diagnostics);

// cli_verify.cpp
[[nodiscard]] int
run_verify_command(const std::vector<std::string>& args, std::ostream& output,
                   std::ostream& diagnostics,
                   const std::optional<std::filesystem::path>& executable_directory);

} // namespace torrentcraft::cli::detail
