#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <torrentutils/frontend/frontend.hpp>

namespace {

using namespace torrentutils::core;
using namespace torrentutils::frontend;

template <typename Value> const Value& require_optional(const std::optional<Value>& value)
{
    if (!value)
    {
        throw std::logic_error("expected optional test value");
    }
    return *value;
}

std::string frontend_fixture(const std::string& name)
{
    const auto path =
        std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" / "frontend" / name;
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

TEST_CASE("given_canonical_config_when_parsed_then_defaults_and_presets_are_retained",
          "[unit][frontend]")
{
    constexpr auto input = R"({
        "schema": "torrentcraft.config/v1",
        "defaults": {
            "format": "hybrid",
            "piece_size": "auto",
            "private": false,
            "tracker_list": ["https://tracker.example/announce"]
        },
        "presets": {
            "private": {
                "private": true,
                "piece_size": 4096,
                "tracker_tiers": [["https://one.example/announce"],
                                  ["udp://two.example:80/announce"]]
            }
        },
        "gui": {"future": true}
    })";

    auto result = parse_config_json(input);

    REQUIRE(result);
    REQUIRE_FALSE(result.value().legacy);
    REQUIRE(result.value().defaults.format == TorrentFormat::Hybrid);
    REQUIRE_FALSE(require_optional(result.value().defaults.piece_size).fixed_kib.has_value());
    REQUIRE(require_optional(result.value().defaults.tracker_tiers).size() == 1U);
    const auto& private_preset = result.value().presets.at("private");
    REQUIRE(private_preset.is_private == true);
    REQUIRE(require_optional(require_optional(private_preset.piece_size).fixed_kib) == 4096U);
    REQUIRE(require_optional(private_preset.tracker_tiers).size() == 2U);
    REQUIRE(result.value().diagnostics.empty());
}

TEST_CASE("given_legacy_preset_when_parsed_then_common_fields_and_migration_are_reported",
          "[unit][frontend]")
{
    constexpr auto input = R"({
        "comment": "",
        "created_by": "https://github.com/airium/TorrentUtils",
        "piece_size": 4096,
        "private": 0,
        "encoding": "UTF-8",
        "tracker_list": ["https://tracker.example/announce"],
        "format": "v2"
    })";

    auto result = parse_preset_json(input);

    REQUIRE(result);
    REQUIRE(result.value().settings.comment == "");
    REQUIRE(result.value().settings.created_by == "https://github.com/airium/TorrentUtils");
    REQUIRE(require_optional(require_optional(result.value().settings.piece_size).fixed_kib) ==
            4096U);
    REQUIRE(result.value().settings.is_private == false);
    REQUIRE_FALSE(result.value().settings.format.has_value());
    REQUIRE(require_optional(result.value().settings.tracker_tiers).size() == 1U);
    REQUIRE(result.value().diagnostics.size() == 1U);
    REQUIRE(result.value().diagnostics.front().code ==
            SettingsDiagnosticCode::LegacyEncodingIgnored);
}

TEST_CASE("given_repository_legacy_presets_when_parsed_then_real_values_map_to_core",
          "[unit][frontend]")
{
    struct TestCase
    {
        const char* fixture;
        std::uint32_t piece_size_kib;
        bool is_private;
        std::size_t tracker_count;
        std::size_t diagnostic_count;
    };
    const std::vector<TestCase> test_cases{
        {"tu.json", 16384U, false, 10U, 0U},
        {"preset_example.json", 4096U, true, 1U, 1U},
    };

    for (const auto& test_case : test_cases)
    {
        auto parsed = parse_preset_json(frontend_fixture(test_case.fixture));
        REQUIRE(parsed);
        REQUIRE(parsed.value().diagnostics.size() == test_case.diagnostic_count);

        auto resolved = resolve_settings(parsed.value().settings);
        REQUIRE(resolved);
        REQUIRE(resolved.value().options.fixed_piece_length() == test_case.piece_size_kib * 1024U);
        REQUIRE(resolved.value().options.is_private() == test_case.is_private);
        REQUIRE(resolved.value().options.trackers().tiers().size() == 1U);
        REQUIRE(resolved.value().options.trackers().tiers().front().trackers().size() ==
                test_case.tracker_count);
    }
}

TEST_CASE("given_invalid_schema_and_known_values_when_parsed_then_field_issues_are_returned",
          "[unit][frontend]")
{
    constexpr auto input = R"({
        "schema": "torrentcraft.config/v2",
        "defaults": {"piece_size": 17, "private": 2}
    })";

    auto result = parse_config_json(input);

    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == ErrorCode::ValidationFailed);
    REQUIRE(result.error().issues.size() == 3U);
    REQUIRE(result.error().issues[0].field == "frontend.config.schema");
    REQUIRE(result.error().issues[1].field == "frontend.config.defaults.piece_size");
    REQUIRE(result.error().issues[2].field == "frontend.config.defaults.private");
}

TEST_CASE("given_unbounded_piece_size_when_parsed_then_validation_fails_without_throwing",
          "[unit][frontend]")
{
    constexpr auto input = R"({"piece_size": 18446744073709551615})";

    auto result = parse_preset_json(input);

    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == ErrorCode::ValidationFailed);
    REQUIRE(result.error().issues.front().field == "frontend.preset.piece_size");
}

TEST_CASE("given_duplicate_fields_when_parsed_then_duplicates_are_rejected", "[unit][frontend]")
{
    constexpr auto input = R"({
        "schema": "torrentcraft.config/v1",
        "defaults": {"private": false, "private": true}
    })";

    auto result = parse_config_json(input);

    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == ErrorCode::ValidationFailed);
    REQUIRE(result.error().issues.front().field == "frontend.config.private");
    REQUIRE(result.error().issues.front().message == "must not appear more than once");
}

TEST_CASE("given_layered_settings_when_overlaid_then_each_present_key_replaces_the_lower_value",
          "[unit][frontend]")
{
    CreationSettingsPatch defaults;
    defaults.is_private = false;
    defaults.tracker_tiers =
        std::vector<std::vector<std::string>>{{"https://default.example/announce"}};
    defaults.comment = "default";
    CreationSettingsPatch preset;
    preset.is_private = true;
    preset.tracker_tiers =
        std::vector<std::vector<std::string>>{{"https://preset.example/announce"}};
    CreationSettingsPatch cli;
    cli.tracker_tiers = std::vector<std::vector<std::string>>{};
    cli.comment = "cli";

    const auto effective = overlay_settings(overlay_settings(defaults, preset), cli);

    REQUIRE(effective.is_private == true);
    REQUIRE(require_optional(effective.tracker_tiers).empty());
    REQUIRE(effective.comment == "cli");
}

TEST_CASE("given_effective_settings_when_resolved_then_core_creation_values_are_valid",
          "[unit][frontend]")
{
    CreationSettingsPatch settings;
    settings.format = TorrentFormat::V2;
    settings.piece_size = PieceSizeSetting{4096U};
    settings.is_private = true;
    settings.tracker_tiers =
        std::vector<std::vector<std::string>>{{"https://tracker.example/announce"}};
    settings.web_seeds = std::vector<std::string>{"https://seed.example/content/"};
    settings.comment = "release";
    settings.info_source = "private-tracker";

    auto result = resolve_settings(settings);

    REQUIRE(result);
    REQUIRE(result.value().options.format() == TorrentFormat::V2);
    REQUIRE(result.value().options.piece_length_strategy() == PieceLengthStrategy::Fixed);
    REQUIRE(result.value().options.fixed_piece_length() == 4096U * 1024U);
    REQUIRE(result.value().options.is_private());
    REQUIRE(result.value().options.trackers().tiers().size() == 1U);
    REQUIRE(result.value().options.web_seeds().size() == 1U);
    REQUIRE(result.value().creation_metadata.comment == "release");
    REQUIRE(result.value().creation_metadata.created_by == "TorrentCraft");
    REQUIRE(result.value().create_info.source == "private-tracker");
}

TEST_CASE("given_auto_piece_size_and_explicit_empty_text_when_resolved_then_intent_is_preserved",
          "[unit][frontend]")
{
    auto parsed = parse_config_json(R"({
        "schema": "torrentcraft.config/v1",
        "defaults": {"piece_size": 0, "comment": "", "created_by": "", "source": ""}
    })");
    REQUIRE(parsed);

    auto result = resolve_settings(parsed.value().defaults);

    REQUIRE(result);
    REQUIRE(result.value().options.piece_length_strategy() == PieceLengthStrategy::Auto);
    REQUIRE_FALSE(result.value().options.fixed_piece_length().has_value());
    REQUIRE(require_optional(result.value().creation_metadata.comment).empty());
    REQUIRE(require_optional(result.value().creation_metadata.created_by).empty());
    REQUIRE(require_optional(result.value().create_info.source).empty());
}

TEST_CASE("given_invalid_tracker_and_web_seed_when_resolved_then_all_issues_are_returned",
          "[unit][frontend]")
{
    CreationSettingsPatch settings;
    settings.tracker_tiers =
        std::vector<std::vector<std::string>>{{"ftp://tracker.example/announce"}};
    settings.web_seeds = std::vector<std::string>{"udp://seed.example/content"};

    auto result = resolve_settings(settings);

    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == ErrorCode::ValidationFailed);
    REQUIRE(result.error().issues.size() == 2U);
    REQUIRE(result.error().issues[0].field == "frontend.settings.tracker_tiers[0][0]");
    REQUIRE(result.error().issues[1].field == "frontend.settings.web_seeds[0]");
}

TEST_CASE("given_canonical_config_when_file_order_and_verify_are_present_then_they_are_parsed",
          "[unit][frontend]")
{
    constexpr auto input = R"({
        "schema": "torrentcraft.config/v1",
        "defaults": {"file_order": "natural"},
        "verify": {"workers": 2, "memory": "64 MiB"},
        "memory_working_set_limit": "768 MiB"
    })";

    auto result = parse_config_json(input);

    REQUIRE(result);
    REQUIRE(require_optional(result.value().defaults.file_order) == FileOrderPolicy::Natural);
    const auto& verify = require_optional(result.value().verify);
    REQUIRE(require_optional(verify.hashing_workers) == 2U);
    REQUIRE(require_optional(verify.checking_memory_bytes) == 64ULL * 1024ULL * 1024ULL);
    REQUIRE(require_optional(result.value().memory_working_set_limit_bytes) ==
            768ULL * 1024ULL * 1024ULL);
    REQUIRE(result.value().diagnostics.empty());
}

TEST_CASE("given_legacy_config_with_verify_when_parsed_then_verification_settings_are_rejected",
          "[unit][frontend]")
{
    constexpr auto input = R"({"piece_size": 4096, "verify": {"workers": 2}})";

    auto result = parse_config_json(input);

    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == ErrorCode::ValidationFailed);
    REQUIRE(result.error().issues.front().field == "frontend.config.verify");
    REQUIRE(result.error().issues.front().message == "requires a canonical config");
}

TEST_CASE("given_verify_settings_json_when_parsed_then_values_and_errors_are_validated",
          "[unit][frontend]")
{
    auto valid = parse_verify_settings_json(R"({"workers": 4, "memory": 33554432})");
    REQUIRE(valid);
    REQUIRE(require_optional(valid.value().hashing_workers) == 4U);
    REQUIRE(require_optional(valid.value().checking_memory_bytes) == 33554432U);

    auto suffix = parse_verify_settings_json(R"({"memory": "1 GiB"})");
    REQUIRE(suffix);
    REQUIRE(require_optional(suffix.value().checking_memory_bytes) == 1024ULL * 1024ULL * 1024ULL);

    auto empty = parse_verify_settings_json(R"({})");
    REQUIRE(empty);
    REQUIRE_FALSE(empty.value().hashing_workers.has_value());
    REQUIRE_FALSE(empty.value().checking_memory_bytes.has_value());

    auto zero_workers = parse_verify_settings_json(R"({"workers": 0})");
    REQUIRE_FALSE(zero_workers);
    REQUIRE(zero_workers.error().issues.front().field == "frontend.verify.workers");

    auto unknown = parse_verify_settings_json(R"({"workers": 2, "max_files": 1})");
    REQUIRE_FALSE(unknown);
    REQUIRE(unknown.error().issues.front().field == "frontend.verify.max_files");

    auto bad_memory = parse_verify_settings_json(R"({"memory": "32 KB"})");
    REQUIRE_FALSE(bad_memory);
    REQUIRE(bad_memory.error().issues.front().field == "frontend.verify.memory");

    auto overflow = parse_verify_settings_json(R"({"memory": 18446744073709551616})");
    REQUIRE_FALSE(overflow);
    REQUIRE(overflow.error().issues.front().field == "frontend.verify.memory");

    auto config_unknown = parse_config_json(
        R"({"schema":"torrentcraft.config/v1","verify":{"workers":2,"extra":1}})");
    REQUIRE_FALSE(config_unknown);
    REQUIRE(config_unknown.error().issues.front().field == "frontend.config.verify.extra");
}

TEST_CASE("given_layered_verify_settings_when_overlaid_then_present_fields_replace",
          "[unit][frontend]")
{
    VerifyResourceSettings defaults;
    defaults.hashing_workers = 1U;
    defaults.checking_memory_bytes = 32ULL * 1024ULL * 1024ULL;
    VerifyResourceSettings cli;
    cli.hashing_workers = 4U;

    const auto effective = overlay_verify_settings(defaults, cli);

    REQUIRE(require_optional(effective.hashing_workers) == 4U);
    REQUIRE(require_optional(effective.checking_memory_bytes) == 32ULL * 1024ULL * 1024ULL);
}

TEST_CASE(
    "given_verify_resource_settings_when_resolved_then_budget_defaults_are_qbittorrent_aligned",
    "[unit][frontend]")
{
    auto absent = resolve_verify_resource_budget({});
    REQUIRE(absent);
    const auto& default_budget = require_optional(absent.value());
    REQUIRE(default_budget.hashing_workers() == default_verify_hashing_workers);
    REQUIRE(default_budget.checking_memory_bytes() == default_verify_checking_memory_bytes);

    VerifyResourceSettings workers_only;
    workers_only.hashing_workers = 2U;
    auto budget = resolve_verify_resource_budget(workers_only);
    REQUIRE(budget);
    const auto& resolved = require_optional(budget.value());
    REQUIRE(resolved.hashing_workers() == 2U);
    REQUIRE(resolved.checking_memory_bytes() == 32ULL * 1024ULL * 1024ULL);
    REQUIRE(resolved.max_logical_files() == (std::numeric_limits<std::uint64_t>::max)());
    REQUIRE(resolved.max_pieces() == (std::numeric_limits<std::uint64_t>::max)());

    VerifyResourceSettings memory_only;
    memory_only.checking_memory_bytes = 1024ULL;
    auto memory_budget = resolve_verify_resource_budget(memory_only);
    REQUIRE(memory_budget);
    const auto& resolved_memory = require_optional(memory_budget.value());
    REQUIRE(resolved_memory.hashing_workers() == 1U);
    REQUIRE(resolved_memory.checking_memory_bytes() == 1024ULL);
    REQUIRE(resolved_memory.max_logical_files() == (std::numeric_limits<std::uint64_t>::max)());
    REQUIRE(resolved_memory.max_pieces() == (std::numeric_limits<std::uint64_t>::max)());
}

TEST_CASE("given_canonical_config_when_disk_io_is_present_then_it_is_parsed", "[unit][frontend]")
{
    auto mmap = parse_config_json(R"({"schema":"torrentcraft.config/v1","disk_io":"mmap"})");
    REQUIRE(mmap);
    REQUIRE(mmap.value().disk_io == DiskIoMode::Mmap);

    auto posix = parse_config_json(R"({"schema":"torrentcraft.config/v1","disk_io":"posix"})");
    REQUIRE(posix);
    REQUIRE(posix.value().disk_io == DiskIoMode::Posix);

    auto absent = parse_config_json(R"({"schema":"torrentcraft.config/v1"})");
    REQUIRE(absent);
    REQUIRE_FALSE(absent.value().disk_io.has_value());

    auto invalid = parse_config_json(R"({"schema":"torrentcraft.config/v1","disk_io":"nope"})");
    REQUIRE_FALSE(invalid);
    REQUIRE(invalid.error().issues.front().field == "frontend.config.disk_io");
    REQUIRE(invalid.error().issues.front().message == "must be posix or mmap");

    auto typed = parse_config_json(R"({"schema":"torrentcraft.config/v1","disk_io":3})");
    REQUIRE_FALSE(typed);
    REQUIRE(typed.error().issues.front().field == "frontend.config.disk_io");

    auto legacy = parse_config_json(R"({"disk_io":"posix"})");
    REQUIRE_FALSE(legacy);
    REQUIRE(legacy.error().issues.front().field == "frontend.config.disk_io");
    REQUIRE(legacy.error().issues.front().message == "requires a canonical config");
}

TEST_CASE("given_memory_working_set_limit_when_parsed_then_byte_and_iec_values_are_validated",
          "[unit][frontend]")
{
    auto absent = parse_config_json(R"({"schema":"torrentcraft.config/v1"})");
    REQUIRE(absent);
    REQUIRE_FALSE(absent.value().memory_working_set_limit_bytes.has_value());

    auto zero =
        parse_config_json(R"({"schema":"torrentcraft.config/v1","memory_working_set_limit":0})");
    REQUIRE_FALSE(zero);
    REQUIRE(zero.error().issues.front().field == "frontend.config.memory_working_set_limit");

    auto bare =
        parse_config_json(R"({"schema":"torrentcraft.config/v1","memory_working_set_limit":512})");
    REQUIRE(bare);
    REQUIRE(require_optional(bare.value().memory_working_set_limit_bytes) == 512U);

    auto suffix = parse_config_json(
        R"({"schema":"torrentcraft.config/v1","memory_working_set_limit":"512 MiB"})");
    REQUIRE(suffix);
    REQUIRE(require_optional(suffix.value().memory_working_set_limit_bytes) ==
            512ULL * 1024ULL * 1024ULL);

    auto invalid_suffix = parse_config_json(
        R"({"schema":"torrentcraft.config/v1","memory_working_set_limit":"512 MB"})");
    REQUIRE_FALSE(invalid_suffix);
    REQUIRE(invalid_suffix.error().issues.front().message ==
            "must be a positive byte count or a size with KiB/MiB/GiB suffix");

    auto legacy = parse_config_json(R"({"memory_working_set_limit":512})");
    REQUIRE_FALSE(legacy);
    REQUIRE(legacy.error().issues.front().field == "frontend.config.memory_working_set_limit");
    REQUIRE(legacy.error().issues.front().message == "requires a canonical config");
}

TEST_CASE("given_memory_size_when_parsed_then_bare_values_are_bytes_and_suffixes_are_iec",
          "[unit][frontend]")
{
    REQUIRE(parse_memory_size("512") == 512U);
    REQUIRE(parse_memory_size("512 KiB") == 512ULL * 1024ULL);
    REQUIRE(parse_memory_size("512 MiB") == 512ULL * 1024ULL * 1024ULL);
    REQUIRE(parse_memory_size("512 GiB") == 512ULL * 1024ULL * 1024ULL * 1024ULL);
    REQUIRE_FALSE(parse_memory_size("512 MB"));
}

} // namespace
