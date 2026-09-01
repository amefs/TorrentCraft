#include "cli.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr const char* unicode_console_sample_name =
    u8"[EAC][201209][UZCL-2200][田渕夏海・中村巴奈重・櫻井美希]["
    u8"TVアニメ「安達としまむら」オリジナル・サウンドトラック]";

class TempDirectory
{
  public:
    TempDirectory()
    {
        static std::atomic<std::uint64_t> sequence{};
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ =
            std::filesystem::temp_directory_path() / ("torrentcraft-cli-" + std::to_string(tick) +
                                                      "-" + std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

class CurrentDirectory
{
  public:
    explicit CurrentDirectory(const std::filesystem::path& path)
        : original_(std::filesystem::current_path())
    {
        std::filesystem::current_path(path);
    }

    ~CurrentDirectory()
    {
        std::error_code ignored;
        std::filesystem::current_path(original_, ignored);
    }

  private:
    std::filesystem::path original_;
};

void write_file(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output);
    output << content;
    REQUIRE(output);
}

int run_cli(const std::vector<std::string>& arguments, std::string& output,
            std::string& diagnostics)
{
    std::vector<const char*> argv;
    argv.reserve(arguments.size());
    for (const auto& argument : arguments)
    {
        argv.push_back(argument.c_str());
    }
    std::ostringstream output_stream;
    std::ostringstream diagnostics_stream;
    const auto result = torrentcraft::cli::run(static_cast<int>(argv.size()), argv.data(),
                                               output_stream, diagnostics_stream);
    output = output_stream.str();
    diagnostics = diagnostics_stream.str();
    return result;
}

TEST_CASE("given_no_cli_arguments_when_run_then_help_is_printed_and_success_returned",
          "[unit][cli]")
{
    std::string output;
    std::string diagnostics;

    const auto result = run_cli({"torrentcraft"}, output, diagnostics);

    REQUIRE(result == 0);
    REQUIRE(output.find("Usage:") != std::string::npos);
    REQUIRE(diagnostics.empty());
}

TEST_CASE("given_unknown_option_when_run_then_usage_error_is_returned", "[unit][cli]")
{
    std::string output;
    std::string diagnostics;

    const auto result = run_cli({"torrentcraft", "--unknown"}, output, diagnostics);

    REQUIRE(result == 2);
    REQUIRE(diagnostics.find("unknown option") != std::string::npos);
}

TEST_CASE("given_create_without_target_when_run_then_usage_error_is_returned", "[unit][cli]")
{
    std::string output;
    std::string diagnostics;

    const auto result = run_cli({"torrentcraft", "create", "content"}, output, diagnostics);

    REQUIRE(result == 2);
    REQUIRE(diagnostics.find("requires -o") != std::string::npos);
}

TEST_CASE("given_missing_explicit_config_when_run_then_io_exit_code_is_returned", "[unit][cli]")
{
    const TempDirectory temp;
    std::string output;
    std::string diagnostics;

    const auto result = run_cli({"torrentcraft", "create", "content", "-o", "output.torrent",
                                 "--config", (temp.path() / "missing.json").u8string(), "--json"},
                                output, diagnostics);

    REQUIRE(result == 4);
    REQUIRE(output.empty());
    REQUIRE(diagnostics.find("file_not_found") != std::string::npos);
}

TEST_CASE("given_project_config_without_explicit_path_when_run_then_it_is_discovered",
          "[unit][cli]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    write_file(content, "payload");
    write_file(temp.path() / "torrentcraft.json",
               R"({"schema":"torrentcraft.config/v1","defaults":{"private":true}})");
    CurrentDirectory current_directory(temp.path());

    std::string output;
    std::string diagnostics;
    const auto result =
        run_cli({"torrentcraft", "create", content.u8string(), "-o",
                 (temp.path() / "payload.torrent").u8string(), "--dry-run", "--json"},
                output, diagnostics);

    REQUIRE(result == 0);
    REQUIRE(diagnostics.empty());
    REQUIRE(output.find("\"private\":true") != std::string::npos);
}

TEST_CASE("given_config_preset_and_cli_overrides_when_created_then_torrent_is_written",
          "[integration][cli]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    const auto config = temp.path() / "torrentcraft.json";
    const auto target = temp.path() / "payload.torrent";
    write_file(content, "payload");
    write_file(config, R"({
        "schema": "torrentcraft.config/v1",
        "defaults": {"piece_size": 16, "private": false},
        "presets": {"release": {"comment": "from preset", "private": false}}
    })");

    std::string output;
    std::string diagnostics;
    const auto result =
        run_cli({"torrentcraft", "create", content.u8string(), "-o", target.u8string(), "--config",
                 config.u8string(), "--preset", "release", "--private", "--json"},
                output, diagnostics);

    REQUIRE(result == 0);
    REQUIRE(diagnostics.empty());
    REQUIRE(output.find("\"ok\":true") != std::string::npos);
    REQUIRE(std::filesystem::is_regular_file(target));
}

TEST_CASE("given_external_preset_file_when_dry_run_then_settings_are_resolved_without_output",
          "[unit][cli]")
{
    const TempDirectory temp;
    const auto preset = temp.path() / "preset.json";
    const auto target = temp.path() / "output.torrent";
    write_file(preset, R"({"piece_size": 4096, "private": true})");

    std::string output;
    std::string diagnostics;
    const auto result = run_cli({"torrentcraft", "create", "content", "-o", target.u8string(),
                                 "--preset-file", preset.u8string(), "--dry-run"},
                                output, diagnostics);

    REQUIRE(result == 0);
    REQUIRE(diagnostics.empty());
    REQUIRE(output.find("dry-run valid") != std::string::npos);
    REQUIRE_FALSE(std::filesystem::exists(target));
}

TEST_CASE("given_create_target_exists_without_overwrite_when_run_then_conflict_exit_code",
          "[unit][cli]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    const auto target = temp.path() / "payload.torrent";
    write_file(content, "payload");
    write_file(target, "existing");

    std::string output;
    std::string diagnostics;
    const auto result =
        run_cli({"torrentcraft", "create", content.u8string(), "-o", target.u8string(), "--json"},
                output, diagnostics);

    REQUIRE(result == 6);
    REQUIRE(output.empty());
    REQUIRE(diagnostics.find("\"code\":\"conflict\"") != std::string::npos);
}

TEST_CASE("given_config_when_show_get_set_then_values_are_managed", "[unit][cli]")
{
    const TempDirectory temp;
    const auto config = temp.path() / "torrentcraft.json";
    write_file(config, R"({"schema":"torrentcraft.config/v1","defaults":{"piece_size":4096}})");

    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", "config", "get", "defaults.piece_size", "--config",
                     config.u8string(), "--json"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("4096") != std::string::npos);

    REQUIRE(run_cli({"torrentcraft", "config", "set", "defaults.private", "true", "--config",
                     config.u8string(), "--json"},
                    output, diagnostics) == 0);

    std::ifstream input(config);
    std::stringstream contents;
    contents << input.rdbuf();
    REQUIRE(contents.str().find("\"private\": true") != std::string::npos);
}

TEST_CASE("given_config_init_when_run_then_canonical_file_is_created_and_conflicts_when_exists",
          "[unit][cli]")
{
    const TempDirectory temp;
    const auto config = temp.path() / "torrentcraft.json";
    std::string output;
    std::string diagnostics;

    REQUIRE(run_cli({"torrentcraft", "config", "init", "--config", config.u8string()}, output,
                    diagnostics) == 0);
    REQUIRE(std::filesystem::is_regular_file(config));
    REQUIRE(run_cli({"torrentcraft", "config", "init", "--config", config.u8string()}, output,
                    diagnostics) == 6);
    REQUIRE(run_cli({"torrentcraft", "config", "init", "--config", config.u8string(), "--force"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("config initialized") != std::string::npos);
}

TEST_CASE("given_legacy_config_when_init_forced_then_canonical_template_replaces_it", "[unit][cli]")
{
    const TempDirectory temp;
    const auto config = temp.path() / "torrentcraft.json";
    write_file(config, R"({"private": 0})");
    std::string output;
    std::string diagnostics;

    REQUIRE(run_cli({"torrentcraft", "config", "init", "--config", config.u8string()}, output,
                    diagnostics) == 6);
    REQUIRE(run_cli({"torrentcraft", "config", "init", "--config", config.u8string(), "--force"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("config initialized") != std::string::npos);
    REQUIRE(run_cli({"torrentcraft", "config", "-h"}, output, diagnostics) == 0);
    REQUIRE(output.find("--force") != std::string::npos);
}

TEST_CASE("given_existing_preset_when_add_forced_then_preset_is_replaced", "[unit][cli]")
{
    const TempDirectory temp;
    const auto config = temp.path() / "torrentcraft.json";
    write_file(
        config,
        R"({"schema":"torrentcraft.config/v1","presets":{"example":{"piece_size":16384,"private":true}}})");
    const auto preset = temp.path() / "example.json";
    write_file(preset, R"({"piece_size":4096})");
    std::string output;
    std::string diagnostics;

    REQUIRE(
        run_cli({"torrentcraft", "preset", "add", preset.u8string(), "--config", config.u8string()},
                output, diagnostics) == 3);
    REQUIRE(run_cli({"torrentcraft", "preset", "add", preset.u8string(), "--config",
                     config.u8string(), "--force", "--json"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("example") != std::string::npos);
    REQUIRE(run_cli({"torrentcraft", "config", "show", "--config", config.u8string(), "--json"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("16384") == std::string::npos);
    REQUIRE(output.find("\"piece_size\":4096") != std::string::npos);

    write_file(preset, R"({"private":true})");
    REQUIRE(run_cli({"torrentcraft", "preset", "add", preset.u8string(), "--config",
                     config.u8string(), "--force", "--dry-run"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("preset dry-run valid") != std::string::npos);
    REQUIRE(run_cli({"torrentcraft", "config", "show", "--config", config.u8string(), "--json"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("16384") == std::string::npos);
    REQUIRE(output.find("\"piece_size\":4096") != std::string::npos);
    REQUIRE(output.find("\"private\":true") == std::string::npos);
}

TEST_CASE("given_preset_file_when_added_then_named_preset_is_listed", "[unit][cli]")
{
    const TempDirectory temp;
    const auto config = temp.path() / "torrentcraft.json";
    const auto preset = temp.path() / "preset_example.json";
    write_file(config, R"({"schema":"torrentcraft.config/v1"})");
    write_file(preset, R"({"piece_size":4096,"private":true})");

    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", "preset", "add", preset.u8string(), "--config",
                     config.u8string(), "--json"},
                    output, diagnostics) == 0);
    REQUIRE(run_cli({"torrentcraft", "preset", "list", "--config", config.u8string(), "--json"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("example") != std::string::npos);
}

TEST_CASE("given_valid_torrent_when_inspect_tree_validate_then_stable_output", "[unit][cli]")
{
    const auto torrent = std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" /
                         "metadata" / "valid-v1-multifile.torrent";
    std::string output;
    std::string diagnostics;

    REQUIRE(run_cli({"torrentcraft", "inspect", torrent.u8string(), "--json"}, output,
                    diagnostics) == 0);
    REQUIRE(output.find("\"ok\":true") != std::string::npos);
    REQUIRE(output.find("multi-v1") != std::string::npos);

    REQUIRE(run_cli({"torrentcraft", "tree", torrent.u8string(), "--depth", "0"}, output,
                    diagnostics) == 0);
    REQUIRE(output.find("multi-v1") != std::string::npos);

    REQUIRE(run_cli({"torrentcraft", "tree", torrent.u8string()}, output, diagnostics) == 0);
    REQUIRE(output.find("├── a.bin") != std::string::npos);
    REQUIRE(output.find("└── dir/") != std::string::npos);

    REQUIRE(run_cli({"torrentcraft", "validate", torrent.u8string(), "--json"}, output,
                    diagnostics) == 0);
    REQUIRE(output.find("\"valid\":true") != std::string::npos);
}

TEST_CASE("given_torrent_and_mismatched_content_when_verify_then_conflict_exit_code",
          "[integration][cli]")
{
    const TempDirectory temp;
    const auto torrent = temp.path() / "payload.torrent";
    const auto content = temp.path() / "content";
    std::filesystem::copy_file(std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" /
                                   "metadata" / "valid-v1-multifile.torrent",
                               torrent);
    std::filesystem::create_directories(content / "dir");
    write_file(content / "a.bin", "abc");
    write_file(content / "dir" / "b.bin", "wxyz");

    std::string output;
    std::string diagnostics;
    const auto result =
        run_cli({"torrentcraft", "verify", torrent.u8string(), content.u8string(), "--json"},
                output, diagnostics);

    REQUIRE(result == 6);
    REQUIRE(output.find("\"mismatched\"") != std::string::npos);
}

TEST_CASE("given_extension_and_unknown_fields_when_inspected_then_metadata_fields_grouped",
          "[unit][cli]")
{
    const auto extension_torrent = std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) /
                                   "fixtures" / "metadata" / "similar-extension.torrent";
    const auto unknown_torrent = std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" /
                                 "metadata" / "unknown-field.torrent";
    std::string output;
    std::string diagnostics;

    REQUIRE(run_cli({"torrentcraft", "inspect", extension_torrent.u8string(), "--json"}, output,
                    diagnostics) == 0);
    REQUIRE(output.find("\"metadata_fields\"") != std::string::npos);
    REQUIRE(output.find("\"extension\"") != std::string::npos);
    REQUIRE(output.find("\"similar\"") != std::string::npos);
    REQUIRE(output.find("\"update-url\"") != std::string::npos);

    REQUIRE(run_cli({"torrentcraft", "inspect", unknown_torrent.u8string()}, output, diagnostics) ==
            0);
    REQUIRE(output.find("metadata fields:") != std::string::npos);
    REQUIRE(output.find("unknown (2)") != std::string::npos);
    REQUIRE(output.find("info_hash") != std::string::npos);
    REQUIRE(output.find("zzz-key") != std::string::npos);
    REQUIRE(output.find("qqq-key") != std::string::npos);
    REQUIRE(output.find("unknown") != std::string::npos);
}

TEST_CASE("given_multifile_torrent_when_inspected_then_file_level_fields_are_deduplicated",
          "[unit][cli]")
{
    const auto torrent = std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" /
                         "metadata" / "valid-v1-multifile.torrent";
    std::string output;
    std::string diagnostics;

    REQUIRE(run_cli({"torrentcraft", "inspect", torrent.u8string()}, output, diagnostics) == 0);
    std::size_t path_rows = 0;
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line))
    {
        if (line.rfind("    path", 0) == 0)
        {
            ++path_rows;
            REQUIRE(line.find("info.files") != std::string::npos);
        }
    }
    REQUIRE(path_rows == 1);

    REQUIRE(run_cli({"torrentcraft", "inspect", torrent.u8string(), "--json"}, output,
                    diagnostics) == 0);
    const std::string json_needle = "\"key\":\"path\"";
    std::size_t occurrences = 0;
    for (std::size_t position = 0;
         (position = output.find(json_needle, position)) != std::string::npos;
         position += json_needle.size())
    {
        ++occurrences;
    }
    REQUIRE(occurrences == 1);
}

TEST_CASE("given_single_torrent_when_no_subcommand_then_infer_inspect", "[unit][cli]")
{
    const auto torrent = std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" /
                         "metadata" / "valid-v1-multifile.torrent";
    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", torrent.u8string(), "--json"}, output, diagnostics) == 0);
    REQUIRE(output.find("\"ok\":true") != std::string::npos);
}

TEST_CASE("given_two_torrents_when_no_subcommand_then_usage_error", "[unit][cli]")
{
    const auto torrent = std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" /
                         "metadata" / "valid-v1-multifile.torrent";
    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", torrent.u8string(), torrent.u8string()}, output,
                    diagnostics) == 2);
}

TEST_CASE("given_single_file_when_no_subcommand_then_infer_create_dry_run", "[unit][cli]")
{
    const TempDirectory temp;
    write_file(temp.path() / "payload.txt", "payload");
    CurrentDirectory current_directory(temp.path());

    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", "payload.txt", "--dry-run", "--json"}, output, diagnostics) ==
            0);
    REQUIRE(output.find("payload.txt.torrent") != std::string::npos);
}

TEST_CASE("given_torrent_when_tracker_and_metadata_commands_then_expected_results", "[unit][cli]")
{
    const TempDirectory temp;
    const auto torrent = temp.path() / "payload.torrent";
    std::filesystem::copy_file(std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" /
                                   "metadata" / "valid-v1-multifile.torrent",
                               torrent);

    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", "tracker", "list", torrent.u8string(), "--json"}, output,
                    diagnostics) == 0);
    REQUIRE(output.find("tracker.example") != std::string::npos);

    REQUIRE(run_cli({"torrentcraft", "metadata", "set", torrent.u8string(), "--comment", "hello",
                     "--dry-run", "--json"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("hello") != std::string::npos);

    REQUIRE(run_cli({"torrentcraft", "metadata", "set", torrent.u8string(), "--private",
                     "--dry-run", "--json"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("\"private\":true") != std::string::npos);

    REQUIRE(run_cli({"torrentcraft", "metadata", "set", torrent.u8string(), "--public", "--dry-run",
                     "--json"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("\"private\":false") != std::string::npos);
    REQUIRE(run_cli({"torrentcraft", "metadata", "set", torrent.u8string(), "--name",
                     "renamed-root", "--dry-run", "--json"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("\"dry_run\":true") != std::string::npos);

    REQUIRE(run_cli({"torrentcraft", "metadata", "set", torrent.u8string(), "--info-source",
                     "client-extension", "--dry-run", "--json"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("\"source\":\"client-extension\"") != std::string::npos);
}

TEST_CASE("given_completion_shells_when_run_then_scripts_are_printed", "[unit][cli]")
{
    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", "completion", "bash"}, output, diagnostics) == 0);
    REQUIRE(output.find("create") != std::string::npos);
    REQUIRE(run_cli({"torrentcraft", "completion", "unknown"}, output, diagnostics) == 2);
}

TEST_CASE("given_create_with_plain_progress_when_run_then_progress_lines_are_emitted",
          "[unit][cli]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    write_file(content, "payload");

    std::string output;
    std::string diagnostics;
    const auto result =
        run_cli({"torrentcraft", "create", content.u8string(), "-o",
                 (temp.path() / "payload.torrent").u8string(), "--progress=plain", "--json"},
                output, diagnostics);

    REQUIRE(result == 0);
    REQUIRE(diagnostics.find("[HASH] 100%") != std::string::npos);
    REQUIRE(diagnostics.find("\"stage\":\"hashing\"") == std::string::npos);
}

TEST_CASE("given_create_with_json_progress_when_run_then_json_events_are_emitted", "[unit][cli]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    write_file(content, "payload");

    std::string output;
    std::string diagnostics;
    const auto result =
        run_cli({"torrentcraft", "create", content.u8string(), "-o",
                 (temp.path() / "payload.torrent").u8string(), "--progress=json", "--json"},
                output, diagnostics);

    REQUIRE(result == 0);
    REQUIRE(diagnostics.find("\"stage\":\"hashing\"") != std::string::npos);
}

TEST_CASE("given_verify_with_plain_progress_when_run_then_verify_lines_are_emitted",
          "[integration][cli]")
{
    const TempDirectory temp;
    const auto torrent = temp.path() / "payload.torrent";
    const auto content = temp.path() / "content";
    std::filesystem::copy_file(std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" /
                                   "metadata" / "valid-v1-multifile.torrent",
                               torrent);
    std::filesystem::create_directories(content / "dir");
    write_file(content / "a.bin", "abc");
    write_file(content / "dir" / "b.bin", "wxyz");

    std::string output;
    std::string diagnostics;
    const auto result = run_cli({"torrentcraft", "verify", torrent.u8string(), content.u8string(),
                                 "--progress=plain", "--json"},
                                output, diagnostics);

    REQUIRE(result == 6);
    REQUIRE(diagnostics.find("[VERIFY]") != std::string::npos);
}

TEST_CASE("given_create_with_quiet_and_progress_when_run_then_no_progress_is_emitted",
          "[unit][cli]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    write_file(content, "payload");

    std::string output;
    std::string diagnostics;
    const auto result = run_cli({"torrentcraft", "create", content.u8string(), "-o",
                                 (temp.path() / "payload.torrent").u8string(), "--progress=plain",
                                 "--quiet", "--json"},
                                output, diagnostics);

    REQUIRE(result == 0);
    REQUIRE(diagnostics.empty());
}

TEST_CASE("given_invalid_progress_mode_when_run_then_usage_error", "[unit][cli]")
{
    const TempDirectory temp;
    std::string output;
    std::string diagnostics;
    const auto result = run_cli({"torrentcraft", "create", "content", "-o", "output.torrent",
                                 "--progress=xml", "--dry-run"},
                                output, diagnostics);

    REQUIRE(result == 2);
    REQUIRE(diagnostics.find("--progress must be") != std::string::npos);
}

TEST_CASE("given_unicode_content_path_when_created_then_torrent_is_written", "[integration][cli]")
{
    const TempDirectory temp;
    const auto content = temp.path() / u8"声 ～Cover ch.～.bin";
    write_file(content, "payload");
    if (!std::filesystem::is_regular_file(content))
    {
        SKIP("the filesystem cannot round-trip the Unicode content name");
    }
    const auto target = temp.path() / u8"声 ～Cover ch.～.torrent";

    std::string output;
    std::string diagnostics;
    const auto result =
        run_cli({"torrentcraft", "create", content.u8string(), "-o", target.u8string(), "--json"},
                output, diagnostics);
    INFO("diagnostics: " << diagnostics);
    if (result == 4 && diagnostics.find("could not hash create content") != std::string::npos)
    {
        SKIP("libtorrent cannot open the Unicode content name on this environment even though "
             "std::filesystem sees the file (set_piece_hashes returns ENOENT)");
    }
    REQUIRE(result == 0);
    REQUIRE(std::filesystem::is_regular_file(target));
}

TEST_CASE("given_mixed_unicode_content_path_when_created_then_human_output_preserves_target",
          "[integration][cli]")
{
    const TempDirectory temp;
    const auto content = temp.path() / std::filesystem::u8path(unicode_console_sample_name);
    write_file(content, "payload");
    if (!std::filesystem::is_regular_file(content))
    {
        SKIP("the filesystem cannot round-trip the Unicode content name");
    }
    const auto target = temp.path() / std::filesystem::u8path(
                                          std::string(unicode_console_sample_name) + ".torrent");

    std::string output;
    std::string diagnostics;
    const auto result = run_cli(
        {"torrentcraft", "create", content.u8string(), "-o", target.u8string(), "--progress=plain"},
        output, diagnostics);
    INFO("diagnostics: " << diagnostics);
    if (result == 4 && diagnostics.find("could not hash create content") != std::string::npos)
    {
        SKIP("libtorrent cannot open the Unicode content name on this environment even though "
             "std::filesystem sees the file (set_piece_hashes returns ENOENT)");
    }
    REQUIRE(result == 0);
    REQUIRE(std::filesystem::is_regular_file(target));
    REQUIRE(output.find("created: " + target.u8string()) != std::string::npos);
}

TEST_CASE("given_create_creation_date_when_run_then_top_level_date_is_emitted", "[unit][cli]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    write_file(content, "payload");

    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", "create", content.u8string(), "-o",
                     (temp.path() / "explicit.torrent").u8string(), "--creation-date", "1234567890",
                     "--dry-run", "--json"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("\"creation_date\":1234567890") != std::string::npos);

    REQUIRE(run_cli({"torrentcraft", "create", content.u8string(), "-o",
                     (temp.path() / "default.torrent").u8string(), "--dry-run", "--json"},
                    output, diagnostics) == 0);
    const auto parsed = nlohmann::json::parse(output, nullptr, false);
    REQUIRE_FALSE(parsed.is_discarded());
    const auto date = parsed["data"]["creation_date"].get<std::int64_t>();
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    REQUIRE(date >= now - 60);
    REQUIRE(date <= now + 60);
}

TEST_CASE("given_create_file_order_when_run_then_policy_is_emitted", "[unit][cli]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    write_file(content, "payload");

    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", "create", content.u8string(), "-o",
                     (temp.path() / "payload.torrent").u8string(), "--file-order", "natural",
                     "--dry-run", "--json"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("\"file_order\":\"natural\"") != std::string::npos);

    REQUIRE(run_cli({"torrentcraft", "create", content.u8string(), "-o",
                     (temp.path() / "payload.torrent").u8string(), "--file-order", "bogus",
                     "--dry-run"},
                    output, diagnostics) == 2);
    REQUIRE(diagnostics.find("--file-order must be") != std::string::npos);
}

TEST_CASE("given_create_working_set_limit_when_run_then_memory_size_syntax_is_accepted",
          "[unit][cli]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    write_file(content, "payload");

    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", "create", content.u8string(), "-o",
                     (temp.path() / "bytes.torrent").u8string(), "--memory-working-set-limit=512",
                     "--dry-run", "--json"},
                    output, diagnostics) == 0);
    REQUIRE(run_cli({"torrentcraft", "create", content.u8string(), "-o",
                     (temp.path() / "mib.torrent").u8string(), "--memory-working-set-limit", "1MiB",
                     "--dry-run", "--json"},
                    output, diagnostics) == 0);
}

TEST_CASE("given_verify_budget_options_when_run_then_invalid_values_are_usage_errors",
          "[unit][cli]")
{
    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", "verify", "input.torrent", "content", "--verify-workers", "0",
                     "--json"},
                    output, diagnostics) == 2);
    REQUIRE(diagnostics.find("--verify-workers must be a positive integer") != std::string::npos);

    REQUIRE(run_cli({"torrentcraft", "verify", "input.torrent", "content", "--verify-memory=32 KB",
                     "--json"},
                    output, diagnostics) == 2);
    REQUIRE(diagnostics.find("--verify-memory must be") != std::string::npos);

    REQUIRE(run_cli({"torrentcraft", "verify", "input.torrent", "content",
                     "--memory-working-set-limit=0", "--json"},
                    output, diagnostics) == 2);
    REQUIRE(diagnostics.find("--memory-working-set-limit must be") != std::string::npos);

    REQUIRE(run_cli({"torrentcraft", "verify", "input.torrent", "content",
                     "--memory-working-set-limit=512 MB", "--json"},
                    output, diagnostics) == 2);
    REQUIRE(diagnostics.find("--memory-working-set-limit must be") != std::string::npos);

    REQUIRE(run_cli({"torrentcraft", "verify", "input.torrent", "content", "--verify-workers", "2",
                     "--verify-workers", "3", "--json"},
                    output, diagnostics) == 2);
    REQUIRE(diagnostics.find("more than once") != std::string::npos);
}

TEST_CASE("given_verify_budget_options_when_run_then_verification_still_completes",
          "[integration][cli]")
{
    const TempDirectory temp;
    const auto torrent = temp.path() / "payload.torrent";
    const auto content = temp.path() / "content";
    std::filesystem::copy_file(std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" /
                                   "metadata" / "valid-v1-multifile.torrent",
                               torrent);
    std::filesystem::create_directories(content / "dir");
    write_file(content / "a.bin", "abc");
    write_file(content / "dir" / "b.bin", "wxyz");

    std::string output;
    std::string diagnostics;
    const auto result = run_cli({"torrentcraft", "verify", torrent.u8string(), content.u8string(),
                                 "--verify-workers", "2", "--verify-memory", "1MiB",
                                 "--memory-working-set-limit", "768", "--progress=plain", "--json"},
                                output, diagnostics);

    REQUIRE(result == 6);
    REQUIRE(output.find("\"mismatched\"") != std::string::npos);
    REQUIRE(diagnostics.find("[VERIFY]") != std::string::npos);
}

TEST_CASE("given_config_when_verify_keys_are_set_then_values_are_managed", "[unit][cli]")
{
    const TempDirectory temp;
    const auto config = temp.path() / "torrentcraft.json";
    write_file(config, R"({"schema":"torrentcraft.config/v1"})");

    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", "config", "set", "verify.workers", "2", "--config",
                     config.u8string(), "--json"},
                    output, diagnostics) == 0);
    REQUIRE(run_cli({"torrentcraft", "config", "get", "verify.workers", "--config",
                     config.u8string(), "--json"},
                    output, diagnostics) == 0);
    REQUIRE(output.find('2') != std::string::npos);

    REQUIRE(run_cli({"torrentcraft", "config", "set", "verify.memory", "\"64 MiB\"", "--config",
                     config.u8string(), "--json"},
                    output, diagnostics) == 0);
    REQUIRE(run_cli({"torrentcraft", "config", "get", "verify.memory", "--config",
                     config.u8string(), "--json"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("64 MiB") != std::string::npos);

    REQUIRE(run_cli({"torrentcraft", "config", "set", "memory_working_set_limit", "768", "--config",
                     config.u8string(), "--json"},
                    output, diagnostics) == 0);
    REQUIRE(run_cli({"torrentcraft", "config", "get", "memory_working_set_limit", "--config",
                     config.u8string(), "--json"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("768") != std::string::npos);
}

TEST_CASE("given_config_when_disk_io_is_set_then_value_is_managed", "[unit][cli]")
{
    const TempDirectory temp;
    const auto config = temp.path() / "torrentcraft.json";
    write_file(config, R"({"schema":"torrentcraft.config/v1"})");

    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", "config", "set", "disk_io", "\"mmap\"", "--config",
                     config.u8string(), "--json"},
                    output, diagnostics) == 0);
    REQUIRE(run_cli({"torrentcraft", "config", "get", "disk_io", "--config", config.u8string(),
                     "--json"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("mmap") != std::string::npos);
}

TEST_CASE("given_config_with_invalid_disk_io_when_create_then_validation_error", "[unit][cli]")
{
    const TempDirectory temp;
    write_file(temp.path() / "payload.bin", "payload");
    write_file(temp.path() / "torrentcraft.json",
               R"({"schema":"torrentcraft.config/v1","disk_io":"bogus"})");
    CurrentDirectory current_directory(temp.path());

    std::string output;
    std::string diagnostics;
    const auto result = run_cli(
        {"torrentcraft", "create", "payload.bin", "-o", "payload.torrent", "--dry-run", "--json"},
        output, diagnostics);

    REQUIRE(result == 3);
    REQUIRE(diagnostics.find("disk_io") != std::string::npos);
}

TEST_CASE("given_config_with_mmap_disk_io_when_created_and_verified_then_commands_succeed",
          "[integration][cli]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    const auto target = temp.path() / "payload.torrent";
    write_file(content, "payload");
    write_file(temp.path() / "torrentcraft.json",
               R"({"schema":"torrentcraft.config/v1","disk_io":"mmap"})");
    CurrentDirectory current_directory(temp.path());

    std::string output;
    std::string diagnostics;
    REQUIRE(
        run_cli({"torrentcraft", "create", content.u8string(), "-o", target.u8string(), "--json"},
                output, diagnostics) == 0);
    REQUIRE(std::filesystem::is_regular_file(target));

    REQUIRE(run_cli({"torrentcraft", "verify", target.u8string(), content.u8string(), "--json"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("\"verified\"") != std::string::npos);
}

void make_many_file_torrent(const std::filesystem::path& content, const std::size_t count,
                            const std::filesystem::path& torrent)
{
    std::filesystem::create_directories(content);
    for (std::size_t index = 0; index < count; ++index)
    {
        std::ostringstream name;
        name << "file_" << std::setw(3) << std::setfill('0') << index << ".bin";
        write_file(content / name.str(), std::string(std::size_t{16U} * 1024U, 'a'));
    }
    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", "create", content.u8string(), "-o", torrent.u8string(),
                     "--format", "v1", "--piece-size", "16", "--json"},
                    output, diagnostics) == 0);
}

TEST_CASE("given_verify_many_files_when_human_output_then_summary_is_printed", "[integration][cli]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "content";
    const auto torrent = temp.path() / "many.torrent";
    make_many_file_torrent(content, 51U, torrent);
    write_file(content / "file_000.bin", std::string(std::size_t{16U} * 1024U, 'b'));

    std::string output;
    std::string diagnostics;
    const auto result = run_cli({"torrentcraft", "verify", torrent.u8string(), content.u8string()},
                                output, diagnostics);

    REQUIRE(result == 6);
    REQUIRE(output.find("files: 51 total, 1 failed") != std::string::npos);
    REQUIRE(output.find("file: file_000.bin mismatched") != std::string::npos);
    REQUIRE(output.find("file_001.bin") == std::string::npos);
}

TEST_CASE("given_verify_many_files_when_json_then_all_files_are_returned", "[integration][cli]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "content";
    const auto torrent = temp.path() / "many.torrent";
    make_many_file_torrent(content, 51U, torrent);

    std::string output;
    std::string diagnostics;
    const auto result =
        run_cli({"torrentcraft", "verify", torrent.u8string(), content.u8string(), "--json"},
                output, diagnostics);

    REQUIRE(result == 0);
    const auto parsed = nlohmann::json::parse(output, nullptr, false);
    REQUIRE_FALSE(parsed.is_discarded());
    REQUIRE(parsed["ok"] == true);
    REQUIRE(parsed["data"]["files"].size() == 51U);
}

TEST_CASE("given_verify_at_summary_threshold_when_human_output_then_listing_policy_applies",
          "[integration][cli]")
{
    const TempDirectory temp;
    const auto under_content = temp.path() / "under";
    const auto under_torrent = temp.path() / "under.torrent";
    make_many_file_torrent(under_content, 50U, under_torrent);

    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", "verify", under_torrent.u8string(), under_content.u8string()},
                    output, diagnostics) == 0);
    REQUIRE(output.find("file: file_000.bin ok") != std::string::npos);
    REQUIRE(output.find("file: file_049.bin ok") != std::string::npos);
    REQUIRE(output.find("files: 50 total") == std::string::npos);

    const auto over_content = temp.path() / "over";
    const auto over_torrent = temp.path() / "over.torrent";
    make_many_file_torrent(over_content, 51U, over_torrent);

    output.clear();
    diagnostics.clear();
    REQUIRE(run_cli({"torrentcraft", "verify", over_torrent.u8string(), over_content.u8string()},
                    output, diagnostics) == 0);
    REQUIRE(output.find("files: 51 total, 0 failed") != std::string::npos);
    REQUIRE(output.find("file: file_000.bin") == std::string::npos);
}

TEST_CASE("given_command_help_when_run_then_per_command_help_is_printed", "[unit][cli]")
{
    const std::vector<std::string> commands = {"create",   "inspect",   "tree",     "verify",
                                               "validate", "tracker",   "metadata", "config",
                                               "preset",   "completion"};
    for (const auto& command : commands)
    {
        std::string output;
        std::string diagnostics;
        REQUIRE(run_cli({"torrentcraft", command, "-h"}, output, diagnostics) == 0);
        REQUIRE(output.find("Usage:") != std::string::npos);
        REQUIRE(diagnostics.empty());
    }

    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", "verify", "--help"}, output, diagnostics) == 0);
    REQUIRE(output.find("--verify-workers") != std::string::npos);
    REQUIRE(output.find("--piece-size") == std::string::npos);

    output.clear();
    REQUIRE(run_cli({"torrentcraft", "create", "-h"}, output, diagnostics) == 0);
    REQUIRE(output.find("--piece-size") != std::string::npos);
    REQUIRE(output.find("--verify-workers") == std::string::npos);

    output.clear();
    REQUIRE(run_cli({"torrentcraft", "--help"}, output, diagnostics) == 0);
    REQUIRE(output.find("Run 'torrentcraft <command> --help'") != std::string::npos);
    REQUIRE(output.find("--piece-size") == std::string::npos);
}

TEST_CASE("given_padded_hybrid_torrent_when_tree_then_pad_files_are_hidden", "[unit][cli]")
{
    const auto torrent = std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" /
                         "torrent-engine" / "retained-hybrid-padding.torrent";

    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", "tree", torrent.u8string()}, output, diagnostics) == 0);
    REQUIRE(output.find("├── a.bin") != std::string::npos);
    REQUIRE(output.find("└── b.bin") != std::string::npos);
    REQUIRE(output.find(".pad") == std::string::npos);
    REQUIRE(output.find("a.bin") != std::string::npos);
    REQUIRE(output.find("b.bin") != std::string::npos);

    output.clear();
    REQUIRE(run_cli({"torrentcraft", "tree", torrent.u8string(), "--json"}, output, diagnostics) ==
            0);
    REQUIRE(output.find(".pad") == std::string::npos);
    REQUIRE(output.find("a.bin") != std::string::npos);
    REQUIRE(output.find("b.bin") != std::string::npos);
}

} // namespace
