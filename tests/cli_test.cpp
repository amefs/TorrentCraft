#include "cli.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
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
        "defaults": {"piece_size": 16, "private": false, "created_by": "ConfigAuthor"},
        "presets": {"release": {"comment": "from preset", "private": false, "created_by": "PresetAuthor"}}
    })");

    std::string output;
    std::string diagnostics;
    const auto result = run_cli({"torrentcraft", "create", content.u8string(), "-o",
                                 target.u8string(), "--config", config.u8string(), "--preset",
                                 "release", "--private", "--created-by", "CliAuthor", "--json"},
                                output, diagnostics);

    REQUIRE(result == 0);
    REQUIRE(diagnostics.empty());
    REQUIRE(output.find("\"ok\":true") != std::string::npos);
    REQUIRE(run_cli({"torrentcraft", "metadata", "show", target.u8string(), "--json"}, output,
                    diagnostics) == 0);
    REQUIRE(output.find("\"creator\":\"CliAuthor\"") != std::string::npos);
    REQUIRE(std::filesystem::is_regular_file(target));
}

#ifndef _WIN32
TEST_CASE("given_non_utf8_target_name_when_created_with_json_then_valid_json_is_reported",
          "[integration][cli]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    write_file(content, "payload");
    // Linux file names are bytes; this one is not valid UTF-8.
    const auto target = temp.path() / std::string("out-\xff.torrent");

    std::string output;
    std::string diagnostics;
    const auto result =
        run_cli({"torrentcraft", "create", content.u8string(), "-o", target.string(), "--json"},
                output, diagnostics);

    REQUIRE(result == 0);
    REQUIRE(std::filesystem::is_regular_file(target));
    const auto document = nlohmann::json::parse(output, nullptr, false);
    REQUIRE_FALSE(document.is_discarded());
    REQUIRE(document.at("ok") == true);
}
#endif

TEST_CASE("given_config_default_preset_when_created_then_explicit_presets_replace_it",
          "[integration][cli]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    const auto config = temp.path() / "torrentcraft.json";
    const auto external = temp.path() / "external.json";
    const auto target = temp.path() / "payload.torrent";
    write_file(content, "payload");
    write_file(config, R"({"schema":"torrentcraft.config/v1",
        "default_preset":"release","defaults":{"private":false},
        "presets":{"release":{"private":true},"explicit":{"private":false}}})");
    write_file(external, R"({"private":false})");

    std::string output;
    std::string diagnostics;
    auto result = run_cli({"torrentcraft", "create", content.u8string(), "-o", target.u8string(),
                           "--config", config.u8string(), "--dry-run", "--json"},
                          output, diagnostics);
    REQUIRE(result == 0);
    REQUIRE(diagnostics.empty());
    REQUIRE(output.find("\"private\":true") != std::string::npos);

    result = run_cli({"torrentcraft", "create", content.u8string(), "-o", target.u8string(),
                      "--config", config.u8string(), "--preset", "explicit", "--dry-run", "--json"},
                     output, diagnostics);
    REQUIRE(result == 0);
    REQUIRE(diagnostics.empty());
    REQUIRE(output.find("\"private\":false") != std::string::npos);

    result =
        run_cli({"torrentcraft", "create", content.u8string(), "-o", target.u8string(), "--config",
                 config.u8string(), "--preset-file", external.u8string(), "--dry-run", "--json"},
                output, diagnostics);
    REQUIRE(result == 0);
    REQUIRE(diagnostics.empty());
    REQUIRE(output.find("\"private\":false") != std::string::npos);
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

    std::string output;
    std::string diagnostics;
    write_file(config, R"({"schema":"torrentcraft.config/v1","defaults":{"piece_size":4096}})");

    REQUIRE(run_cli({"torrentcraft", "config", "set", "default_preset", "\"release\"", "--config",
                     config.u8string(), "--json"},
                    output, diagnostics) == 0);
    REQUIRE(run_cli({"torrentcraft", "config", "get", "default_preset", "--config",
                     config.u8string(), "--json"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("release") != std::string::npos);

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

std::filesystem::path cli_metadata_fixture(const char* name)
{
    return std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" / "metadata" / name;
}

TEST_CASE("given_torrent_template_when_preset_added_then_default_fields_are_saved", "[unit][cli]")
{
    const TempDirectory temp;
    const auto config = temp.path() / "torrentcraft.json";
    write_file(config, R"({"schema":"torrentcraft.config/v1"})");
    std::string output;
    std::string diagnostics;

    REQUIRE(run_cli({"torrentcraft", "preset", "add", "--from-torrent",
                     cli_metadata_fixture("valid-v1.torrent").u8string(), "--config",
                     config.u8string()},
                    output, diagnostics) == 0);
    REQUIRE(output.find("preset added: valid-v1") != std::string::npos);
    REQUIRE(diagnostics.empty());

    REQUIRE(run_cli({"torrentcraft", "preset", "show", "valid-v1", "--config", config.u8string(),
                     "--json"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("\"format\":\"v1\"") != std::string::npos);
    REQUIRE(output.find("\"piece_size\":16") != std::string::npos);
    REQUIRE(output.find("\"private\":true") != std::string::npos);
    REQUIRE(output.find("\"source\":\"SRC\"") != std::string::npos);
    REQUIRE(output.find("https://backup.example/announce") != std::string::npos);
    REQUIRE(output.find("https://seed.example/web") != std::string::npos);
    REQUIRE(output.find("comment") == std::string::npos);
    REQUIRE(output.find("created_by") == std::string::npos);
}

TEST_CASE("given_torrent_template_when_name_and_fields_given_then_only_those_are_saved",
          "[unit][cli]")
{
    const TempDirectory temp;
    const auto config = temp.path() / "torrentcraft.json";
    write_file(config, R"({"schema":"torrentcraft.config/v1"})");
    std::string output;
    std::string diagnostics;

    REQUIRE(run_cli({"torrentcraft", "preset", "add", "--from-torrent",
                     cli_metadata_fixture("unsupported-piece-size-v1.torrent").u8string(), "--name",
                     "release", "--fields", "trackers,comment", "--config", config.u8string(),
                     "--json"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("\"preset\":\"release\"") != std::string::npos);
    REQUIRE(output.find("\"skipped_fields\":[]") != std::string::npos);

    REQUIRE(run_cli({"torrentcraft", "preset", "show", "release", "--config", config.u8string(),
                     "--json"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("\"comment\":\"release notes\"") != std::string::npos);
    REQUIRE(output.find("https://tracker.example/announce") != std::string::npos);
    REQUIRE(output.find("format") == std::string::npos);
    REQUIRE(output.find("private") == std::string::npos);
}

TEST_CASE("given_torrent_template_when_named_field_unavailable_then_it_is_skipped_with_warning",
          "[unit][cli]")
{
    const TempDirectory temp;
    const auto config = temp.path() / "torrentcraft.json";
    write_file(config, R"({"schema":"torrentcraft.config/v1"})");
    std::string output;
    std::string diagnostics;

    REQUIRE(run_cli({"torrentcraft", "preset", "add", "--from-torrent",
                     cli_metadata_fixture("valid-hybrid.torrent").u8string(), "--fields",
                     "trackers,web-seeds,private", "--config", config.u8string()},
                    output, diagnostics) == 0);
    REQUIRE(diagnostics.find("warning: template field trackers skipped") != std::string::npos);
    REQUIRE(diagnostics.find("warning: template field web-seeds skipped") != std::string::npos);
    REQUIRE(output.find("preset added: valid-hybrid") != std::string::npos);
}

TEST_CASE("given_torrent_template_when_no_named_field_available_then_nothing_is_written",
          "[unit][cli]")
{
    const TempDirectory temp;
    const auto config = temp.path() / "torrentcraft.json";
    const std::string original = R"({"schema":"torrentcraft.config/v1"})";
    write_file(config, original);
    std::string output;
    std::string diagnostics;

    REQUIRE(run_cli({"torrentcraft", "preset", "add", "--from-torrent",
                     cli_metadata_fixture("unsupported-piece-size-v1.torrent").u8string(),
                     "--fields", "piece-size", "--config", config.u8string()},
                    output, diagnostics) == 3);
    std::ifstream input(config, std::ios::binary);
    const std::string persisted{std::istreambuf_iterator<char>(input),
                                std::istreambuf_iterator<char>()};
    REQUIRE(persisted == original);
}

TEST_CASE("given_torrent_template_with_non_standard_metadata_when_added_then_warnings_are_reported",
          "[unit][cli]")
{
    const TempDirectory temp;
    const auto config = temp.path() / "torrentcraft.json";
    write_file(config, R"({"schema":"torrentcraft.config/v1"})");
    std::string output;
    std::string diagnostics;

    REQUIRE(run_cli({"torrentcraft", "preset", "add", "--from-torrent",
                     cli_metadata_fixture("unknown-field.torrent").u8string(), "--config",
                     config.u8string(), "--dry-run"},
                    output, diagnostics) == 0);
    REQUIRE(diagnostics.find("warning: ignored non-standard metadata: zzz-key, info.qqq-key") !=
            std::string::npos);

    REQUIRE(run_cli({"torrentcraft", "preset", "add", "--from-torrent",
                     cli_metadata_fixture("partially-invalid-template-v1.torrent").u8string(),
                     "--config", config.u8string(), "--dry-run", "--json"},
                    output, diagnostics) == 0);
    REQUIRE(diagnostics.empty());
    const auto document = nlohmann::json::parse(output);
    REQUIRE(document["data"]["load_warnings"] ==
            nlohmann::json::array(
                {"trackers: ignored 2 invalid entries", "web-seeds: ignored 1 invalid entry"}));
    REQUIRE(document["data"]["settings"]["tracker_tiers"] ==
            nlohmann::json::array({nlohmann::json::array({"https://tracker.example/announce"})}));
}

TEST_CASE("given_torrent_template_when_default_field_unavailable_then_it_is_skipped_with_warning",
          "[unit][cli]")
{
    const TempDirectory temp;
    const auto config = temp.path() / "torrentcraft.json";
    write_file(config, R"({"schema":"torrentcraft.config/v1"})");
    std::string output;
    std::string diagnostics;

    REQUIRE(run_cli({"torrentcraft", "preset", "add", "--from-torrent",
                     cli_metadata_fixture("unsupported-piece-size-v1.torrent").u8string(),
                     "--config", config.u8string(), "--dry-run"},
                    output, diagnostics) == 0);
    REQUIRE(diagnostics.find("warning: template field piece-size skipped") != std::string::npos);
    REQUIRE(output.find("preset dry-run valid: unsupported-piece-size-v1") != std::string::npos);
    REQUIRE(output.find("\"tracker_tiers\"") != std::string::npos);
    REQUIRE(output.find("\"piece_size\"") == std::string::npos);

    REQUIRE(run_cli({"torrentcraft", "preset", "list", "--config", config.u8string(), "--json"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("unsupported-piece-size-v1") == std::string::npos);
}

TEST_CASE("given_torrent_template_when_arguments_conflict_then_usage_error_is_returned",
          "[unit][cli]")
{
    const TempDirectory temp;
    const auto config = temp.path() / "torrentcraft.json";
    write_file(config, R"({"schema":"torrentcraft.config/v1"})");
    const auto torrent = cli_metadata_fixture("valid-v1.torrent").u8string();
    std::string output;
    std::string diagnostics;

    REQUIRE(run_cli({"torrentcraft", "preset", "add", "extra.json", "--from-torrent", torrent,
                     "--config", config.u8string()},
                    output, diagnostics) == 2);
    REQUIRE(run_cli({"torrentcraft", "preset", "list", "--from-torrent", torrent, "--config",
                     config.u8string()},
                    output, diagnostics) == 2);
    REQUIRE(run_cli({"torrentcraft", "preset", "add", "x.json", "--fields", "trackers", "--config",
                     config.u8string()},
                    output, diagnostics) == 2);
    REQUIRE(run_cli({"torrentcraft", "config", "show", "--from-torrent", torrent, "--config",
                     config.u8string()},
                    output, diagnostics) == 2);
    REQUIRE(run_cli({"torrentcraft", "preset", "add", "--from-torrent", torrent, "--fields",
                     "bogus", "--config", config.u8string()},
                    output, diagnostics) == 3);
}

TEST_CASE("given_invalid_torrent_template_when_preset_added_then_error_is_reported", "[unit][cli]")
{
    const TempDirectory temp;
    const auto config = temp.path() / "torrentcraft.json";
    write_file(config, R"({"schema":"torrentcraft.config/v1"})");
    std::string output;
    std::string diagnostics;

    REQUIRE(run_cli({"torrentcraft", "preset", "add", "--from-torrent",
                     cli_metadata_fixture("invalid-path.torrent").u8string(), "--config",
                     config.u8string()},
                    output, diagnostics) != 0);
    REQUIRE_FALSE(diagnostics.empty());
}

TEST_CASE("given_legacy_config_when_torrent_template_added_then_preset_is_rejected", "[unit][cli]")
{
    const TempDirectory temp;
    const auto config = temp.path() / "torrentcraft.json";
    write_file(config, R"({"piece_size":4096})");
    std::string output;
    std::string diagnostics;

    REQUIRE(run_cli({"torrentcraft", "preset", "add", "--from-torrent",
                     cli_metadata_fixture("valid-v1.torrent").u8string(), "--config",
                     config.u8string()},
                    output, diagnostics) != 0);
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

TEST_CASE("given_v1_directory_torrent_with_one_file_when_tree_then_rendered_as_directory",
          "[integration][cli][layout]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    const auto torrent = temp.path() / "payload.torrent";
    std::filesystem::create_directory(content);
    write_file(content / "only.bin", "0123456789abcdef");

    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", "create", content.u8string(), "-o", torrent.u8string(),
                     "--format", "v1", "--piece-size", "16", "--json"},
                    output, diagnostics) == 0);

    REQUIRE(run_cli({"torrentcraft", "tree", torrent.u8string()}, output, diagnostics) == 0);
    REQUIRE(output.find("payload/") != std::string::npos);
    REQUIRE(output.find("only.bin") != std::string::npos);

    REQUIRE(run_cli({"torrentcraft", "verify", torrent.u8string(), content.u8string(), "--json"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("\"ok\":true") != std::string::npos);

    REQUIRE(
        run_cli({"torrentcraft", "verify", torrent.u8string(), (content / "only.bin").u8string()},
                output, diagnostics) == 3);
    REQUIRE(diagnostics.find("verify.content_root") != std::string::npos);
    REQUIRE(diagnostics.find("directory") != std::string::npos);
}

TEST_CASE("given_single_file_content_with_trailing_separator_when_verified_then_succeeds",
          "[integration][cli][layout]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    const auto torrent = temp.path() / "payload.torrent";
    write_file(content, "0123456789abcdef");

    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", "create", content.u8string(), "-o", torrent.u8string(),
                     "--format", "v1", "--piece-size", "16", "--json"},
                    output, diagnostics) == 0);

    const auto trailing = content.u8string() + "/";
    REQUIRE(run_cli({"torrentcraft", "verify", torrent.u8string(), trailing, "--json"}, output,
                    diagnostics) == 0);
    REQUIRE(output.find("\"outcome\":\"verified\"") != std::string::npos);
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

TEST_CASE("given_inferred_verify_options_when_run_then_verify_parser_is_reused",
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
    const auto result = run_cli(
        {"torrentcraft", content.u8string(), torrent.u8string(), "--verify-workers", "1", "--json"},
        output, diagnostics);

    REQUIRE(result == 6);
    REQUIRE(diagnostics.empty());
    REQUIRE(output.find("\"mismatched\"") != std::string::npos);
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

TEST_CASE("given_default_preset_when_path_inference_create_then_explicit_create_matches",
          "[integration][cli]")
{
    const TempDirectory temp;
    write_file(temp.path() / "payload.bin", "payload");
    write_file(temp.path() / "torrentcraft.json",
               R"({"schema":"torrentcraft.config/v1","default_preset":"release",
                   "defaults":{"private":false,"format":"hybrid"},
                   "presets":{"release":{"private":true,"format":"v1"}}})");
    CurrentDirectory current_directory(temp.path());

    std::string inferred_output;
    std::string inferred_diagnostics;
    REQUIRE(run_cli({"torrentcraft", "payload.bin", "--dry-run", "--json"}, inferred_output,
                    inferred_diagnostics) == 0);
    REQUIRE(inferred_diagnostics.empty());
    const auto inferred = nlohmann::json::parse(inferred_output);

    std::string explicit_output;
    std::string explicit_diagnostics;
    REQUIRE(run_cli({"torrentcraft", "create", "payload.bin", "-o", "payload.bin.torrent",
                     "--dry-run", "--json"},
                    explicit_output, explicit_diagnostics) == 0);
    REQUIRE(explicit_diagnostics.empty());
    const auto explicit_create = nlohmann::json::parse(explicit_output);

    REQUIRE(inferred["data"]["target"] == explicit_create["data"]["target"]);
    REQUIRE(inferred["data"]["format"] == explicit_create["data"]["format"]);
    REQUIRE(inferred["data"]["private"] == explicit_create["data"]["private"]);
    REQUIRE(inferred["data"]["file_filter"] == explicit_create["data"]["file_filter"]);
}

TEST_CASE("given_executable_config_when_path_inference_create_then_default_preset_is_applied",
          "[integration][cli]")
{
    const TempDirectory temp;
    const auto executable_directory = temp.path() / "bin";
    const auto working_directory = temp.path() / "working";
    const auto content = working_directory / "payload.bin";
    std::filesystem::create_directories(executable_directory);
    std::filesystem::create_directories(working_directory);
    write_file(content, "payload");
    write_file(executable_directory / "torrentcraft.json",
               R"({"schema":"torrentcraft.config/v1","default_preset":"release",
                   "defaults":{"private":false,"format":"hybrid"},
                   "presets":{"release":{"private":true,"format":"v1"}}})");
    CurrentDirectory current_directory(working_directory);

    std::string output;
    std::string diagnostics;
    const auto executable = executable_directory / "torrentcraft-gui.exe";
    REQUIRE(run_cli({executable.u8string(), content.u8string(), "--dry-run", "--json"}, output,
                    diagnostics) == 0);
    REQUIRE(diagnostics.empty());
    const auto result = nlohmann::json::parse(output);
    REQUIRE(result["data"]["format"] == "v1");
    REQUIRE(result["data"]["private"] == true);
}

TEST_CASE("given_inferred_create_options_when_run_then_target_and_settings_are_forwarded",
          "[unit][cli]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    const auto config = temp.path() / "torrentcraft.json";
    const auto target = temp.path() / "chosen.torrent";
    write_file(content, "payload");
    write_file(config,
               R"({"schema":"torrentcraft.config/v1","presets":{"release":{"private":true}}})");

    std::string output;
    std::string diagnostics;
    const auto result = run_cli({"torrentcraft", content.u8string(), "--config", config.u8string(),
                                 "--preset", "release", "--format", "v1", "--piece-size", "16",
                                 "--output", target.u8string(), "--dry-run", "--json"},
                                output, diagnostics);

    REQUIRE(result == 0);
    REQUIRE(diagnostics.empty());
    const auto json = nlohmann::json::parse(output);
    REQUIRE(json["data"]["target"] == target.u8string());
    REQUIRE(json["data"]["format"] == "v1");
    REQUIRE(json["data"]["private"] == true);
}

TEST_CASE("given_inferred_create_option_without_value_when_run_then_value_error_is_preserved",
          "[unit][cli]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    write_file(content, "payload");

    std::string output;
    std::string diagnostics;
    const auto result =
        run_cli({"torrentcraft", content.u8string(), "--config"}, output, diagnostics);

    REQUIRE(result == 2);
    REQUIRE(diagnostics.find("--config requires a value") != std::string::npos);
}

TEST_CASE("given_inferred_directory_and_file_with_output_when_run_then_output_wins", "[unit][cli]")
{
    const TempDirectory temp;
    const auto directory = temp.path() / "destination";
    const auto content = temp.path() / "payload.bin";
    const auto target = temp.path() / "chosen.torrent";
    std::filesystem::create_directories(directory);
    write_file(content, "payload");

    std::string output;
    std::string diagnostics;
    const auto result = run_cli({"torrentcraft", directory.u8string(), content.u8string(),
                                 "--output", target.u8string(), "--dry-run", "--json"},
                                output, diagnostics);

    REQUIRE(result == 0);
    REQUIRE(diagnostics.empty());
    REQUIRE(nlohmann::json::parse(output)["data"]["target"] == target.u8string());
}

TEST_CASE("given_inferred_create_help_when_run_then_create_help_is_printed", "[unit][cli]")
{
    std::string output;
    std::string diagnostics;
    const auto result = run_cli({"torrentcraft", "payload.bin", "--help"}, output, diagnostics);

    REQUIRE(result == 0);
    REQUIRE(diagnostics.empty());
    REQUIRE(output.find("Usage:\n  torrentcraft create") != std::string::npos);
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

TEST_CASE("given_rewrite_rules_when_tracker_rewrite_runs_then_folders_are_rewritten_in_batch",
          "[unit][cli][tracker][rewrite]")
{
    const TempDirectory temp;
    const auto fixtures =
        std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" / "metadata";
    const auto input = temp.path() / "in";
    std::filesystem::create_directories(input / "sub");
    std::filesystem::copy_file(fixtures / "valid-v1.torrent", input / "a.torrent");
    std::filesystem::copy_file(fixtures / "valid-v1-multifile.torrent",
                               input / "sub" / "b.torrent");
    const auto original = std::filesystem::file_size(input / "a.torrent");
    const std::string rule = R"(s#^(https?://)tracker\.example(/.*)$#\1tracker.new.example\2#)";

    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", "tracker", "rewrite", input.u8string(), "-e", rule,
                     "--dry-run", "--json"},
                    output, diagnostics) == 0);
    const auto dry_run = nlohmann::json::parse(output);
    CHECK(dry_run["ok"] == true);
    CHECK(dry_run["data"]["dry_run"] == true);
    REQUIRE(dry_run["data"]["files"].size() == 1U);
    const auto& change = dry_run["data"]["files"][0]["changes"][0];
    CHECK(change["from"] == "https://tracker.example/announce");
    CHECK(change["to"] == "https://tracker.new.example/announce");
    CHECK(change["status"] == "changed");
    CHECK(std::filesystem::file_size(input / "a.torrent") == original);

    const auto out = temp.path() / "out";
    REQUIRE(run_cli({"torrentcraft", "tracker", "rewrite", input.u8string(), "-r", "-e", rule,
                     "--output-dir", out.u8string(), "--json"},
                    output, diagnostics) == 0);
    CHECK(nlohmann::json::parse(output)["data"]["summary"]["changed"] == 2);
    REQUIRE(std::filesystem::exists(out / "a.torrent"));
    REQUIRE(std::filesystem::exists(out / "sub" / "b.torrent"));
    REQUIRE(run_cli({"torrentcraft", "tracker", "list", (out / "sub" / "b.torrent").u8string(),
                     "--json"},
                    output, diagnostics) == 0);
    CHECK(output.find("https://tracker.new.example/announce") != std::string::npos);

    // Existing targets are conflicts unless --force is given.
    CHECK(run_cli({"torrentcraft", "tracker", "rewrite", input.u8string(), "-r", "-e", rule,
                   "--output-dir", out.u8string()},
                  output, diagnostics) == 6);
    CHECK(run_cli({"torrentcraft", "tracker", "rewrite", input.u8string(), "-r", "-e", rule,
                   "--output-dir", out.u8string(), "--force", "--quiet"},
                  output, diagnostics) == 0);

    // In place, with one file succeeding and one failing validation.
    std::filesystem::copy_file(fixtures / "valid-v2.torrent", input / "c.torrent");
    REQUIRE(run_cli({"torrentcraft", "tracker", "rewrite", input.u8string(), "-e", "s#^udp:#ftp:#",
                     "--json"},
                    output, diagnostics) == 3);
    const auto mixed = nlohmann::json::parse(output);
    CHECK(mixed["ok"] == false);
    CHECK(mixed["data"]["summary"]["failed"] == 1);
    CHECK(mixed["data"]["summary"]["unchanged"] == 1);
    CHECK(std::filesystem::file_size(input / "a.torrent") == original);
}

TEST_CASE("given_invalid_tracker_rewrite_arguments_when_run_then_usage_errors_are_reported",
          "[unit][cli][tracker][rewrite]")
{
    const TempDirectory temp;
    const auto torrent = temp.path() / "a.torrent";
    std::filesystem::copy_file(std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" /
                                   "metadata" / "valid-v1.torrent",
                               torrent);
    std::string output;
    std::string diagnostics;

    CHECK(run_cli({"torrentcraft", "tracker", "rewrite", torrent.u8string()}, output,
                  diagnostics) == 2);
    CHECK(run_cli({"torrentcraft", "tracker", "rewrite", "-e", "s/a/b/"}, output, diagnostics) ==
          2);
    CHECK(run_cli(
              {"torrentcraft", "tracker", "rewrite", torrent.u8string(), "-e", "s/a/b/", "--force"},
              output, diagnostics) == 2);
    diagnostics.clear();
    CHECK(run_cli({"torrentcraft", "tracker", "rewrite", torrent.u8string(), "-e", "s/ok/fine/",
                   "-e", "s/(passkey/x/"},
                  output, diagnostics) == 2);
    CHECK(diagnostics.find("rule[1]") != std::string::npos);
    CHECK(diagnostics.find("passkey") == std::string::npos);
    CHECK(run_cli({"torrentcraft", "tracker", "rewrite", (temp.path() / "missing").u8string(), "-e",
                   "s/a/b/"},
                  output, diagnostics) == 4);
}

TEST_CASE("given_tracker_rewrite_human_output_when_run_then_each_url_outcome_is_listed",
          "[unit][cli][tracker][rewrite]")
{
    const TempDirectory temp;
    const auto torrent = temp.path() / "a.torrent";
    std::filesystem::copy_file(std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" /
                                   "metadata" / "valid-v1.torrent",
                               torrent);
    std::string output;
    std::string diagnostics;

    // The udp tracker collapses onto the https one in the same tier and is merged.
    const std::string merge = R"(s#^udp://(tracker\.example):6969#https://\1#)";
    REQUIRE(run_cli({"torrentcraft", "tracker", "rewrite", torrent.u8string(), "-e", merge,
                     "--dry-run"},
                    output, diagnostics) == 0);
    CHECK(output.find(": changed\n") != std::string::npos);
    CHECK(output.find("  tier 0: udp://tracker.example:6969/announce -> "
                      "https://tracker.example/announce (merged)") != std::string::npos);
    CHECK(output.find("backup.example") == std::string::npos);
    CHECK(output.find("dry run: 1 changed, 0 unchanged, 0 failed") != std::string::npos);

    REQUIRE(run_cli({"torrentcraft", "tracker", "rewrite", torrent.u8string(), "-e", merge,
                     "--dry-run", "--json"},
                    output, diagnostics) == 0);
    const auto merged = nlohmann::json::parse(output)["data"]["files"][0];
    CHECK(merged["status"] == "changed");
    REQUIRE(merged["changes"].size() == 1U);
    CHECK(merged["changes"][0]["status"] == "merged");
    CHECK(merged["merged"] == 1);

    // Invalid results go to diagnostics with the reason, and the file is left untouched.
    const auto original = std::filesystem::file_size(torrent);
    diagnostics.clear();
    CHECK(run_cli({"torrentcraft", "tracker", "rewrite", torrent.u8string(), "-e", "s#^udp:#ftp:#",
                   "--quiet"},
                  output, diagnostics) == 3);
    CHECK(diagnostics.find(": failed (") != std::string::npos);
    CHECK(diagnostics.find("  tier 0: udp://tracker.example:6969/announce -> invalid (") !=
          std::string::npos);
    CHECK(diagnostics.find("0 changed, 0 unchanged, 1 failed") != std::string::npos);
    CHECK(std::filesystem::file_size(torrent) == original);

    REQUIRE(run_cli({"torrentcraft", "tracker", "rewrite", torrent.u8string(), "-e", "s/nomatch/x/",
                     "--json"},
                    output, diagnostics) == 0);
    const auto unchanged = nlohmann::json::parse(output)["data"];
    CHECK(unchanged["files"][0]["status"] == "unchanged");
    CHECK_FALSE(unchanged["files"][0].contains("target"));
    CHECK(unchanged["summary"]["unchanged"] == 1);

    // Quiet success prints nothing; --backup keeps a copy of the original.
    output.clear();
    REQUIRE(run_cli({"torrentcraft", "tracker", "rewrite", torrent.u8string(), "-e", merge,
                     "--backup", "--quiet"},
                    output, diagnostics) == 0);
    CHECK(output.empty());
    std::size_t backups = 0;
    for (const auto& entry : std::filesystem::directory_iterator(temp.path()))
    {
        backups += entry.path().filename().string().rfind("a.torrent.bak", 0) == 0U ? 1U : 0U;
    }
    CHECK(backups == 1U);
    REQUIRE(run_cli({"torrentcraft", "tracker", "list", torrent.u8string(), "--json"}, output,
                    diagnostics) == 0);
    CHECK(output.find("udp://tracker.example") == std::string::npos);
}

TEST_CASE("given_overlapping_tracker_rewrite_inputs_when_run_then_duplicates_are_resolved",
          "[unit][cli][tracker][rewrite]")
{
    const TempDirectory temp;
    const auto fixture = std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" /
                         "metadata" / "valid-v1.torrent";
    std::filesystem::create_directories(temp.path() / "one");
    std::filesystem::create_directories(temp.path() / "two");
    std::filesystem::copy_file(fixture, temp.path() / "one" / "same.torrent");
    std::filesystem::copy_file(fixture, temp.path() / "two" / "same.torrent");
    const std::string rule = R"(s#tracker\.example#tracker.new.example#g)";
    std::string output;
    std::string diagnostics;

    // The same file named twice, directly and through its folder, is processed once.
    REQUIRE(run_cli({"torrentcraft", "tracker", "rewrite", (temp.path() / "one").u8string(),
                     (temp.path() / "one" / "." / "same.torrent").u8string(), "-e", rule,
                     "--dry-run", "--json"},
                    output, diagnostics) == 0);
    CHECK(nlohmann::json::parse(output)["data"]["summary"]["files"] == 1);

    // Two different files that map to the same output path: the second is a conflict.
    const auto out = temp.path() / "out";
    CHECK(run_cli({"torrentcraft", "tracker", "rewrite", (temp.path() / "one").u8string(),
                   (temp.path() / "two").u8string(), "-e", rule, "--output-dir", out.u8string(),
                   "--json"},
                  output, diagnostics) == 6);
    const auto conflict = nlohmann::json::parse(output);
    CHECK(conflict["ok"] == false);
    CHECK(conflict["data"]["summary"]["changed"] == 1);
    CHECK(conflict["data"]["summary"]["failed"] == 1);
    CHECK(conflict["data"]["files"][1]["status"] == "failed");
    CHECK(std::filesystem::exists(out / "same.torrent"));
}

TEST_CASE("given_malformed_tracker_rewrite_options_when_run_then_usage_errors_are_reported",
          "[unit][cli][tracker][rewrite]")
{
    const TempDirectory temp;
    const auto torrent = temp.path() / "a.torrent";
    std::filesystem::copy_file(std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" /
                                   "metadata" / "valid-v1.torrent",
                               torrent);
    std::string output;
    std::string diagnostics;

    for (const std::string option : {"-e", "--expression", "--output-dir"})
    {
        diagnostics.clear();
        CHECK(run_cli({"torrentcraft", "tracker", "rewrite", torrent.u8string(), option}, output,
                      diagnostics) == 2);
        CHECK(diagnostics.find("requires a value") != std::string::npos);
    }
    diagnostics.clear();
    CHECK(run_cli(
              {"torrentcraft", "tracker", "rewrite", torrent.u8string(), "-e", "s/a/b/", "--bogus"},
              output, diagnostics) == 2);
    CHECK(diagnostics.find("unknown option: --bogus") != std::string::npos);

    diagnostics.clear();
    CHECK(run_cli({"torrentcraft", "tracker", "rewrite", torrent.u8string(), "--expression",
                   "s/a/b/z", "--json"},
                  output, diagnostics) == 2);
    CHECK(diagnostics.find("rule[0]") != std::string::npos);

    diagnostics.clear();
    CHECK(run_cli({"torrentcraft", "tracker"}, output, diagnostics) == 2);
    CHECK(diagnostics.find("requires a subcommand") != std::string::npos);

    output.clear();
    CHECK(run_cli({"torrentcraft", "tracker", "rewrite", "--help"}, output, diagnostics) == 0);
    CHECK(output.find("--expression") != std::string::npos);
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

TEST_CASE("given_common_filter_when_creating_directory_then_cli_reports_filtered_entries",
          "[integration][cli][file-filter]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    const auto target = temp.path() / "payload.torrent";
    std::filesystem::create_directories(content);
    write_file(content / "keep.bin", "keep");
    write_file(content / ".DS_Store", "junk");
    write_file(content / "Thumbs.db", "junk");
    std::filesystem::create_directories(content / "__MACOSX" / "nested");
    write_file(content / "__MACOSX" / "nested" / "ignored.bin", "junk");

    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", "create", content.u8string(), "-o", target.u8string(),
                     "--filter-mode", "common-artifacts", "--json"},
                    output, diagnostics) == 0);

    const auto parsed = nlohmann::json::parse(output, nullptr, false);
    REQUIRE_FALSE(parsed.is_discarded());
    REQUIRE(parsed["data"]["payload_bytes"] == 4);
    REQUIRE(parsed["data"]["filtered"]["count"] == 3);
    REQUIRE(parsed["data"]["filtered"]["bytes"] == 12);
    REQUIRE(std::filesystem::exists(target));

    std::string tree_output;
    std::string tree_diagnostics;
    REQUIRE(run_cli({"torrentcraft", "tree", target.u8string()}, tree_output, tree_diagnostics) ==
            0);
    REQUIRE(tree_output.find("keep.bin") != std::string::npos);
    REQUIRE(tree_output.find(".DS_Store") == std::string::npos);
    REQUIRE(tree_output.find("Thumbs.db") == std::string::npos);
    REQUIRE(tree_output.find("__MACOSX") == std::string::npos);
}

TEST_CASE("given_custom_filter_when_cli_arguments_are_used_then_patterns_and_case_policy_apply",
          "[unit][cli][file-filter]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    std::filesystem::create_directories(content);
    write_file(content / "keep.bin", "keep");
    write_file(content / "skip.tmp", "junk");

    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", "create", content.u8string(), "-o",
                     (temp.path() / "payload.torrent").u8string(), "--exclude", "*.tmp",
                     "--filter-case-sensitive", "--dry-run", "--json"},
                    output, diagnostics) == 0);
    REQUIRE(output.find("\"mode\":\"custom-rules\"") != std::string::npos);
    REQUIRE(output.find("\"case_sensitive\":true") != std::string::npos);

    REQUIRE(output.find("skip.tmp") != std::string::npos);
}

TEST_CASE("given_config_and_preset_filters_when_cli_resolves_then_preset_replaces_default",
          "[integration][cli][file-filter]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    const auto config = temp.path() / "torrentcraft.json";
    std::filesystem::create_directories(content);
    write_file(content / "keep.bin", "keep");
    write_file(content / ".DS_Store", "junk");
    write_file(content / "skip.tmp", "junk");
    write_file(config, R"({
        "schema": "torrentcraft.config/v1",
        "defaults": {
            "file_filter": {
                "mode": "custom-rules",
                "case_sensitive": true,
                "patterns": ["*.tmp"]
            }
        },
        "presets": {
            "common": {
                "file_filter": {
                    "mode": "common-artifacts"
                }
            }
        }
    })");

    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", "create", content.u8string(), "-o",
                     (temp.path() / "payload.torrent").u8string(), "--config", config.u8string(),
                     "--preset", "common", "--dry-run", "--json"},
                    output, diagnostics) == 0);

    const auto parsed = nlohmann::json::parse(output, nullptr, false);
    REQUIRE_FALSE(parsed.is_discarded());
    REQUIRE(parsed["data"]["file_filter"]["mode"] == "common-artifacts");
    REQUIRE(parsed["data"]["file_filter"]["case_sensitive"] == false);
    REQUIRE(parsed["data"]["filtered"]["count"] == 1);
    REQUIRE(parsed["data"]["filtered"]["entries"][0]["path"] == ".DS_Store");
}

TEST_CASE("given_config_filter_when_cli_case_override_is_used_then_patterns_are_preserved",
          "[integration][cli][file-filter]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    const auto config = temp.path() / "torrentcraft.json";
    std::filesystem::create_directories(content);
    write_file(content / "keep.bin", "keep");
    write_file(content / "skip.tmp", "junk");
    write_file(config, R"({
        "schema": "torrentcraft.config/v1",
        "defaults": {
            "file_filter": {
                "mode": "custom-rules",
                "case_sensitive": false,
                "patterns": ["*.TMP"]
            }
        }
    })");

    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", "create", content.u8string(), "-o",
                     (temp.path() / "payload.torrent").u8string(), "--config", config.u8string(),
                     "--filter-case-sensitive", "--dry-run", "--json"},
                    output, diagnostics) == 0);

    const auto parsed = nlohmann::json::parse(output, nullptr, false);
    REQUIRE_FALSE(parsed.is_discarded());
    REQUIRE(parsed["data"]["file_filter"]["mode"] == "custom-rules");
    REQUIRE(parsed["data"]["file_filter"]["case_sensitive"] == true);
    REQUIRE(parsed["data"]["filtered"]["count"] == 0);
}

TEST_CASE("given_torrent_when_metadata_set_without_dry_run_then_fields_are_written_with_backup",
          "[unit][cli][metadata]")
{
    const TempDirectory temp;
    const auto torrent = temp.path() / "payload.torrent";
    std::filesystem::copy_file(std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" /
                                   "metadata" / "valid-v1-multifile.torrent",
                               torrent);

    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", "metadata", "set", torrent.u8string(), "--created-by",
                     "TorrentCraft test", "--creation-time", "1700000000", "--web-seed",
                     "https://seed.example/files/", "--dht-node", "router.example:6881", "--backup",
                     "--quiet"},
                    output, diagnostics) == 0);
    CHECK(output.empty());
    int backups = 0;
    for (const auto& entry : std::filesystem::directory_iterator(temp.path()))
    {
        backups += entry.path().filename().u8string().find(".bak-") != std::string::npos ? 1 : 0;
    }
    CHECK(backups == 1);

    REQUIRE(run_cli({"torrentcraft", "metadata", "show", torrent.u8string(), "--json"}, output,
                    diagnostics) == 0);
    const auto metadata = nlohmann::json::parse(output)["data"]["metadata"];
    CHECK(metadata["creator"] == "TorrentCraft test");
    CHECK(metadata["creation_time"] == 1700000000);
    REQUIRE(metadata["web_seeds"].size() == 1U);
    CHECK(metadata["web_seeds"][0] == "https://seed.example/files/");
    REQUIRE(metadata["dht_nodes"].size() == 1U);
    CHECK(metadata["dht_nodes"][0]["host"] == "router.example");
    CHECK(metadata["dht_nodes"][0]["port"] == 6881);

    REQUIRE(run_cli({"torrentcraft", "metadata", "show", torrent.u8string()}, output,
                    diagnostics) == 0);
    CHECK(output.find("TorrentCraft test") != std::string::npos);
    REQUIRE(run_cli({"torrentcraft", "metadata", "set", torrent.u8string(), "--creation-time",
                     "now", "--dry-run"},
                    output, diagnostics) == 0);
    CHECK(output.find("metadata dry-run valid") != std::string::npos);
}

TEST_CASE("given_torrent_with_metadata_when_cleared_then_fields_are_removed",
          "[unit][cli][metadata]")
{
    const TempDirectory temp;
    const auto torrent = temp.path() / "payload.torrent";
    std::filesystem::copy_file(std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" /
                                   "metadata" / "valid-v1-multifile.torrent",
                               torrent);
    std::string output;
    std::string diagnostics;
    REQUIRE(run_cli({"torrentcraft", "metadata", "set", torrent.u8string(), "--comment", "note",
                     "--created-by", "someone", "--creation-time", "1700000000", "--web-seed",
                     "https://seed.example/", "--dht-node", "router.example:6881", "--quiet"},
                    output, diagnostics) == 0);

    REQUIRE(run_cli({"torrentcraft", "metadata", "clear", torrent.u8string(), "--comment",
                     "--created-by", "--creation-time", "--web-seeds", "--dht-nodes"},
                    output, diagnostics) == 0);
    CHECK(output.find("metadata updated") != std::string::npos);
    REQUIRE(run_cli({"torrentcraft", "metadata", "show", torrent.u8string(), "--json"}, output,
                    diagnostics) == 0);
    const auto metadata = nlohmann::json::parse(output)["data"]["metadata"];
    CHECK(metadata["comment"].is_null());
    CHECK(metadata["creator"].is_null());
    CHECK(metadata["creation_time"].is_null());
    CHECK(metadata["web_seeds"].empty());
    CHECK(metadata["dht_nodes"].empty());
}

TEST_CASE("given_invalid_metadata_arguments_when_run_then_usage_or_validation_errors",
          "[unit][cli][metadata]")
{
    const TempDirectory temp;
    const auto torrent = temp.path() / "payload.torrent";
    std::filesystem::copy_file(std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" /
                                   "metadata" / "valid-v1-multifile.torrent",
                               torrent);
    const auto path = torrent.u8string();
    std::string output;
    std::string diagnostics;
    const auto exit_code = [&](const std::vector<std::string>& arguments) {
        std::vector<std::string> full{"torrentcraft", "metadata"};
        full.insert(full.end(), arguments.begin(), arguments.end());
        return run_cli(full, output, diagnostics);
    };

    CHECK(exit_code({}) == 2);
    CHECK(exit_code({"rename", path}) == 2);
    CHECK(exit_code({"set", path}) == 2);
    CHECK(exit_code({"set", path, path, "--comment", "x"}) == 2);
    CHECK(exit_code({"set", path, "--bogus"}) == 2);
    CHECK(exit_code({"set", path, "--comment"}) == 2);
    CHECK(exit_code({"set", path, "--creation-time", "soon"}) == 2);
    CHECK(exit_code({"set", path, "--dht-node", "no-port"}) == 2);
    CHECK(exit_code({"set", path, "--dht-node", "router.example:99999"}) == 2);
    // --json is read in argument order, so it must precede the value that fails to parse.
    CHECK(exit_code({"set", path, "--json", "--web-seed", "not a url"}) == 3);
    CHECK(nlohmann::json::parse(diagnostics)["ok"] == false);
    CHECK(exit_code({"set", (temp.path() / "missing.torrent").u8string(), "--comment", "x"}) == 4);
    // A comment edit leaves the info dictionary alone, so the file round-trips unchanged.
    CHECK(exit_code({"set", path, "--comment", "x", "--dry-run", "--quiet"}) == 0);
    CHECK(output.empty());
}

TEST_CASE("given_config_when_managed_with_human_output_then_values_and_errors_are_reported",
          "[unit][cli][config]")
{
    const TempDirectory temp;
    const auto config = temp.path() / "torrentcraft.json";
    write_file(config, R"({"schema":"torrentcraft.config/v1","defaults":{"piece_size":4096}})");
    const auto path = config.u8string();
    std::string output;
    std::string diagnostics;

    REQUIRE(run_cli({"torrentcraft", "config", "path", "--config", path}, output, diagnostics) ==
            0);
    CHECK(output.find("torrentcraft.json") != std::string::npos);
    REQUIRE(run_cli({"torrentcraft", "config", "path", "--config", path, "--json"}, output,
                    diagnostics) == 0);
    CHECK(nlohmann::json::parse(output)["ok"] == true);
    REQUIRE(run_cli({"torrentcraft", "config", "show", "--config", path}, output, diagnostics) ==
            0);
    CHECK(output.find("4096") != std::string::npos);
    REQUIRE(run_cli({"torrentcraft", "config", "get", "defaults.piece_size", "--config", path},
                    output, diagnostics) == 0);
    CHECK(output.find("4096") != std::string::npos);
    REQUIRE(run_cli({"torrentcraft", "config", "set", "defaults.piece_size", "8192", "--config",
                     path, "--dry-run"},
                    output, diagnostics) == 0);
    CHECK(output.find("config dry-run valid") != std::string::npos);
    REQUIRE(run_cli({"torrentcraft", "config", "set", "defaults.piece_size", "8192", "--config",
                     path, "--backup"},
                    output, diagnostics) == 0);
    CHECK(output.find("config updated") != std::string::npos);
    int backups = 0;
    for (const auto& entry : std::filesystem::directory_iterator(temp.path()))
    {
        backups += entry.path().filename().u8string().find(".bak-") != std::string::npos ? 1 : 0;
    }
    CHECK(backups == 1);

    CHECK(run_cli({"torrentcraft", "config"}, output, diagnostics) == 2);
    CHECK(run_cli({"torrentcraft", "config", "frob", "--config", path}, output, diagnostics) == 2);
    CHECK(run_cli({"torrentcraft", "config", "get", "--config", path}, output, diagnostics) == 2);
    CHECK(run_cli({"torrentcraft", "config", "set", "defaults.private", "--config", path}, output,
                  diagnostics) == 2);
}

TEST_CASE("given_inline_preset_when_listed_shown_and_removed_then_config_is_updated",
          "[unit][cli][config]")
{
    const TempDirectory temp;
    const auto config = temp.path() / "torrentcraft.json";
    write_file(config, R"({"schema":"torrentcraft.config/v1",)"
                       R"("presets":{"release":{"piece_size":4096,"private":true}}})");
    const auto path = config.u8string();
    std::string output;
    std::string diagnostics;

    REQUIRE(run_cli({"torrentcraft", "preset", "list", "--config", path}, output, diagnostics) ==
            0);
    CHECK(output.find("release") != std::string::npos);
    REQUIRE(run_cli({"torrentcraft", "preset", "show", "release", "--config", path}, output,
                    diagnostics) == 0);
    CHECK(output.find("4096") != std::string::npos);
    REQUIRE(run_cli({"torrentcraft", "preset", "remove", "release", "--config", path, "--dry-run"},
                    output, diagnostics) == 0);
    CHECK(output.find("preset dry-run valid") != std::string::npos);
    REQUIRE(run_cli({"torrentcraft", "preset", "remove", "release", "--config", path, "--json"},
                    output, diagnostics) == 0);
    CHECK(nlohmann::json::parse(output)["ok"] == true);
    REQUIRE(run_cli({"torrentcraft", "preset", "list", "--config", path, "--json"}, output,
                    diagnostics) == 0);
    CHECK(output.find("release") == std::string::npos);

    CHECK(run_cli({"torrentcraft", "preset"}, output, diagnostics) == 2);
    CHECK(run_cli({"torrentcraft", "preset", "frob", "--config", path}, output, diagnostics) == 2);
    CHECK(run_cli({"torrentcraft", "preset", "show", "--config", path}, output, diagnostics) == 2);
    CHECK(run_cli({"torrentcraft", "preset", "remove", "--config", path}, output, diagnostics) ==
          2);
}

TEST_CASE("given_console_shared_with_shell_when_long_operation_runs_then_ctrl_break_is_suggested",
          "[unit][cli][interrupt]")
{
    const TempDirectory temp;
    const auto payload = temp.path() / "payload.bin";
    write_file(payload, "payload");
    std::string output;
    std::string diagnostics;

    torrentcraft::cli::set_console_shared_with_shell(true);
    struct ResetConsoleFlag
    {
        ResetConsoleFlag() = default;
        ResetConsoleFlag(const ResetConsoleFlag&) = delete;
        ResetConsoleFlag& operator=(const ResetConsoleFlag&) = delete;
        ResetConsoleFlag(ResetConsoleFlag&&) = delete;
        ResetConsoleFlag& operator=(ResetConsoleFlag&&) = delete;
        ~ResetConsoleFlag()
        {
            torrentcraft::cli::set_console_shared_with_shell(false);
        }
    } const reset;

    REQUIRE(run_cli({"torrentcraft", "create", payload.u8string(), "-o",
                     (temp.path() / "a.torrent").u8string()},
                    output, diagnostics) == 0);
    CHECK(diagnostics.find("Ctrl+Break") != std::string::npos);

    // JSON consumers read stderr too, and --quiet asks for no extra output.
    REQUIRE(run_cli({"torrentcraft", "create", payload.u8string(), "-o",
                     (temp.path() / "b.torrent").u8string(), "--json"},
                    output, diagnostics) == 0);
    CHECK(diagnostics.find("Ctrl+Break") == std::string::npos);
    REQUIRE(run_cli({"torrentcraft", "verify", (temp.path() / "a.torrent").u8string(),
                     payload.u8string(), "--quiet"},
                    output, diagnostics) == 0);
    CHECK(diagnostics.find("Ctrl+Break") == std::string::npos);

    torrentcraft::cli::set_console_shared_with_shell(false);
    REQUIRE(run_cli({"torrentcraft", "verify", (temp.path() / "a.torrent").u8string(),
                     payload.u8string()},
                    output, diagnostics) == 0);
    CHECK(diagnostics.find("Ctrl+Break") == std::string::npos);
}
