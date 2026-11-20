#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <torrentutils/frontend/frontend.hpp>
#include <utility>
#include <vector>

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

class TempDirectory
{
  public:
    TempDirectory()
    {
        static std::atomic<std::uint64_t> sequence{};
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("torrentutils-frontend-config-" + std::to_string(tick) + "-" +
                 std::to_string(sequence.fetch_add(1)));
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

void write_file(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output);
    output << content;
    REQUIRE(output);
}

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

TEST_CASE("given_config_search_paths_when_discovered_then_explicit_and_project_precedence_apply",
          "[unit][frontend][config]")
{
    const TempDirectory temp;
    const auto project = temp.path() / "project";
    const auto user = temp.path() / "user" / "torrentcraft.json";
    std::filesystem::create_directories(project);
    std::filesystem::create_directories(user.parent_path());
    write_file(project / "torrentcraft.json", "{}");
    write_file(user, "{}");

    auto project_result = discover_config({std::nullopt, project, user});
    auto explicit_result = discover_config({user, project, std::nullopt});
    auto missing_result = discover_config({temp.path() / "missing.json", project, user});

    REQUIRE(project_result);
    REQUIRE(project_result.value() == project / "torrentcraft.json");
    REQUIRE(explicit_result);
    REQUIRE(explicit_result.value() == user);
    REQUIRE_FALSE(missing_result);
    REQUIRE(missing_result.error().code == ErrorCode::FileNotFound);

    std::filesystem::remove(project / "torrentcraft.json");
    std::filesystem::remove(user);
    auto absent_result = discover_config({std::nullopt, project, user});
    REQUIRE(absent_result);
    REQUIRE_FALSE(absent_result.value().has_value());
}

TEST_CASE("given_executable_config_when_project_config_is_absent_then_it_precedes_user_config",
          "[unit][frontend][config]")
{
    const TempDirectory temp;
    const auto project = temp.path() / "project";
    const auto executable = temp.path() / "bin";
    const auto user = temp.path() / "user" / "torrentcraft.json";
    std::filesystem::create_directories(project);
    std::filesystem::create_directories(executable);
    std::filesystem::create_directories(user.parent_path());
    write_file(executable / "torrentcraft.json", "{}");
    write_file(user, "{}");

    const auto executable_result = discover_config({std::nullopt, project, user}, executable);

    REQUIRE(executable_result);
    REQUIRE(executable_result.value() == executable / "torrentcraft.json");

    write_file(project / "torrentcraft.json", "{}");
    const auto project_result = discover_config({std::nullopt, project, user}, executable);
    REQUIRE(project_result);
    REQUIRE(project_result.value() == project / "torrentcraft.json");
}

TEST_CASE("given_canonical_config_when_mutated_then_unknown_members_are_preserved_atomically",
          "[unit][frontend][config]")
{
    const TempDirectory temp;
    const auto path = temp.path() / "torrentcraft.json";
    write_file(path, R"({
        "schema": "torrentcraft.config/v1",
        "defaults": {"private": false, "vendor_default": {"keep": [1, 2]}},
        "presets": {"Existing": {"piece_size": 4096, "vendor_preset": "retained"}},
        "gui": {"future": {"opaque": true}},
        "vendor_root": ["retained"]
    })");

    auto config = ConfigFile::load(path);
    REQUIRE(config);
    CreationSettingsPatch defaults;
    defaults.is_private = true;
    defaults.file_filter =
        FileFilterInput{FileFilterMode::CommonArtifacts, true, std::vector<std::string>{"*.tmp"}};
    CreationSettingsPatch added;
    added.piece_size = PieceSizeSetting{16384U};
    added.file_filter =
        FileFilterInput{FileFilterMode::CustomRules, false, std::vector<std::string>{"cache/"}};

    REQUIRE(config.value().set_defaults(defaults));
    auto add_result = config.value().add_preset("Added", added);
    REQUIRE(add_result);
    REQUIRE(config.value().save());

    for (const auto& entry : std::filesystem::directory_iterator(temp.path()))
    {
        REQUIRE(entry.path().filename().string().find(".torrentutils.tmp.") == std::string::npos);
    }

    const auto saved = read_file(path);
    REQUIRE(saved.find("vendor_root") != std::string::npos);
    REQUIRE(saved.find("vendor_default") != std::string::npos);
    REQUIRE(saved.find("vendor_preset") != std::string::npos);
    REQUIRE(saved.find("\"future\"") != std::string::npos);
    REQUIRE(saved.find("\"file_filter\"") != std::string::npos);
    REQUIRE(saved.find("\"case_sensitive\"") != std::string::npos);

    auto reloaded = ConfigFile::load(path);
    REQUIRE(reloaded);
    REQUIRE(reloaded.value().parsed().defaults.is_private == true);
    const auto& default_filter = require_optional(reloaded.value().parsed().defaults.file_filter);
    REQUIRE(default_filter.mode == FileFilterMode::CommonArtifacts);
    REQUIRE(default_filter.case_sensitive);
    REQUIRE(default_filter.patterns == std::vector<std::string>{"*.tmp"});
    REQUIRE(reloaded.value().parsed().presets.find("Existing") !=
            reloaded.value().parsed().presets.end());
    REQUIRE(reloaded.value().parsed().presets.find("Added") !=
            reloaded.value().parsed().presets.end());
    REQUIRE(
        require_optional(
            require_optional(reloaded.value().parsed().presets.at("Added").piece_size).fixed_kib) ==
        16384U);
    const auto& added_filter =
        require_optional(reloaded.value().parsed().presets.at("Added").file_filter);
    REQUIRE(added_filter.mode == FileFilterMode::CustomRules);
    REQUIRE_FALSE(added_filter.case_sensitive);
    REQUIRE(added_filter.patterns == std::vector<std::string>{"cache/"});
}

TEST_CASE(
    "given_gui_language_when_mutated_then_typed_value_is_persisted_and_unknown_values_fallback",
    "[unit][frontend][config]")
{
    const TempDirectory temp;
    const auto path = temp.path() / "torrentcraft.json";
    write_file(path, R"({
        "schema": "torrentcraft.config/v1",
        "defaults": {},
        "gui": {"future": {"keep": true}, "language": "unsupported"}
    })");

    auto config = ConfigFile::load(path);
    REQUIRE(config);
    REQUIRE(config.value().gui_language() == GuiLanguage::English);
    REQUIRE(config.value().set_gui_language(GuiLanguage::SimplifiedChinese));
    REQUIRE(config.value().save());

    auto reloaded = ConfigFile::load(path);
    REQUIRE(reloaded);
    REQUIRE(reloaded.value().gui_language() == GuiLanguage::SimplifiedChinese);
    const auto json = reloaded.value().document_json();
    REQUIRE(json);
    REQUIRE(json.value().find("keep") != std::string::npos);
    REQUIRE(json.value().find("zh_CN") != std::string::npos);
}

TEST_CASE("given_gui_preferences_when_mutated_then_nested_values_are_persisted_and_validated",
          "[unit][frontend][config]")
{
    const TempDirectory temp;
    const auto path = temp.path() / "torrentcraft.json";
    write_file(path, R"({
        "schema": "torrentcraft.config/v1",
        "gui": {"future": {"keep": true}, "default_preset": "legacy"}
    })");

    auto config = ConfigFile::load(path);
    REQUIRE(config);
    GuiPreferences preferences;
    preferences.default_save_location = GuiSaveLocationMode::Specified;
    preferences.font_family = "DejaVu Sans";
    preferences.style = "Fusion";
    preferences.default_save_path = "/tmp/torrent-output";
    preferences.show_padding_files = true;
    preferences.logging_enabled = true;
    preferences.log_level = GuiLogLevel::Debug;
    preferences.log_path = "/tmp/torrentcraft.log";
    preferences.default_preset = "release";
    REQUIRE(config.value().set_gui_preferences(preferences));
    REQUIRE(config.value().save());

    auto reloaded = ConfigFile::load(path);
    REQUIRE(reloaded);
    const auto actual = reloaded.value().gui_preferences();
    REQUIRE(actual.default_save_location == GuiSaveLocationMode::Specified);
    REQUIRE(require_optional(actual.font_family) == "DejaVu Sans");
    REQUIRE(require_optional(actual.style) == "Fusion");
    REQUIRE(require_optional(actual.default_save_path) == "/tmp/torrent-output");
    REQUIRE(actual.show_padding_files);
    REQUIRE(actual.logging_enabled);
    REQUIRE(actual.log_level == GuiLogLevel::Debug);
    REQUIRE(require_optional(actual.log_path) == "/tmp/torrentcraft.log");
    const auto json = reloaded.value().document_json();
    REQUIRE(json);
    REQUIRE(json.value().find("\"keep\"") != std::string::npos);
    REQUIRE(json.value().find("\"default_preset\":\"release\"") != std::string::npos);
    REQUIRE(json.value().find("\"default_preset\":\"legacy\"") == std::string::npos);
    REQUIRE(require_optional(actual.default_preset) == "release");

    preferences.default_save_path.reset();
    REQUIRE_FALSE(config.value().set_gui_preferences(preferences));
}

TEST_CASE("given_canonical_config_when_presets_are_added_or_removed_then_name_rules_are_enforced",
          "[unit][frontend][config]")
{
    const TempDirectory temp;
    const auto path = temp.path() / "torrentcraft.json";
    write_file(path, R"({"schema": "torrentcraft.config/v1", "presets": {"U2": {}}})");

    auto config = ConfigFile::load(path);
    REQUIRE(config);
    REQUIRE_FALSE(config.value().add_preset("U2", {}));
    REQUIRE_FALSE(config.value().add_preset("bad/name", {}));
    REQUIRE_FALSE(config.value().remove_preset("missing"));
    REQUIRE(config.value().remove_preset("U2"));
    REQUIRE(config.value().save());

    auto reloaded = ConfigFile::load(path);
    REQUIRE(reloaded);
    REQUIRE(reloaded.value().parsed().presets.empty());
}

TEST_CASE("given_config_near_size_limit_when_edits_succeed_then_saved_file_still_loads",
          "[unit][frontend][config]")
{
    const TempDirectory temp;
    const auto path = temp.path() / "torrentcraft.json";
    write_file(path, R"({"schema": "torrentcraft.config/v1"})");
    auto config = ConfigFile::load(path);
    REQUIRE(config);

    // Short array items gain proportionally the most from the indented form that save() writes.
    CreationSettingsPatch preset;
    preset.web_seeds = std::vector<std::string>(200U, "https://a.example/x");
    auto accepted = 0;
    for (auto index = 0; index < 1000; ++index)
    {
        if (!config.value().add_preset("P" + std::to_string(index), preset))
        {
            break;
        }
        ++accepted;
    }
    REQUIRE(accepted > 0);
    REQUIRE(accepted < 1000);
    REQUIRE(config.value().save());
    REQUIRE(std::filesystem::file_size(path) <= std::uintmax_t{1024U} * 1024U);
    auto reloaded = ConfigFile::load(path);
    REQUIRE(reloaded);
    REQUIRE(reloaded.value().parsed().presets.size() == static_cast<std::size_t>(accepted));
}

TEST_CASE("given_config_without_presets_when_invalid_preset_rejected_then_no_section_is_left",
          "[unit][frontend][config]")
{
    const TempDirectory temp;
    const auto path = temp.path() / "torrentcraft.json";
    write_file(path, R"({"schema": "torrentcraft.config/v1"})");
    auto config = ConfigFile::load(path);
    REQUIRE(config);

    CreationSettingsPatch invalid;
    invalid.comment = std::string(std::size_t{2} * 1024U * 1024U, 'x');
    REQUIRE_FALSE(config.value().add_preset("Broken", invalid));
    REQUIRE(config.value().save());
    REQUIRE(read_file(path).find("presets") == std::string::npos);
}

TEST_CASE("given_legacy_config_when_defaults_mutated_then_presets_are_rejected",
          "[unit][frontend][config]")
{
    const TempDirectory temp;
    const auto path = temp.path() / "tu.json";
    write_file(path, R"({"private": 0, "vendor_root": "retained"})");

    auto config = ConfigFile::load(path);
    REQUIRE(config);
    CreationSettingsPatch defaults;
    defaults.is_private = true;

    REQUIRE(config.value().set_defaults(defaults));
    REQUIRE(config.value().save());
    auto preset_result = config.value().add_preset("U2", {});

    REQUIRE_FALSE(preset_result);
    REQUIRE(preset_result.error().code == ErrorCode::ValidationFailed);
    REQUIRE(read_file(path).find("vendor_root") != std::string::npos);
    auto reloaded = ConfigFile::load(path);
    REQUIRE(reloaded);
    REQUIRE(reloaded.value().parsed().legacy);
    REQUIRE(reloaded.value().parsed().defaults.is_private == true);
}

TEST_CASE("given_canonical_config_when_verify_settings_are_mutated_then_object_is_managed",
          "[unit][frontend][config]")
{
    const TempDirectory temp;
    const auto path = temp.path() / "torrentcraft.json";
    write_file(path, R"({
        "schema": "torrentcraft.config/v1",
        "defaults": {"piece_size": 4096},
        "verify": {"workers": 2}
    })");

    auto config = ConfigFile::load(path);
    REQUIRE(config);
    const auto& verify = require_optional(config.value().parsed().verify);
    REQUIRE(require_optional(verify.hashing_workers) == 2U);

    auto workers_key = parse_config_key("verify.workers");
    REQUIRE(workers_key);
    auto get_result = config.value().get_key(workers_key.value());
    REQUIRE(get_result);
    REQUIRE(get_result.value() == "2");

    auto memory_key = parse_config_key("verify.memory");
    REQUIRE(memory_key);
    REQUIRE(config.value().set_key(memory_key.value(), "\"64 MiB\""));
    auto get_memory = config.value().get_key(memory_key.value());
    REQUIRE(get_memory);
    REQUIRE(get_memory.value().find("64 MiB") != std::string::npos);
    REQUIRE(config.value().save());

    auto reloaded = ConfigFile::load(path);
    REQUIRE(reloaded);
    const auto& reloaded_verify = require_optional(reloaded.value().parsed().verify);
    REQUIRE(require_optional(reloaded_verify.checking_memory_bytes) == 64ULL * 1024ULL * 1024ULL);
    REQUIRE(require_optional(reloaded_verify.hashing_workers) == 2U);

    REQUIRE(config.value().set_key(workers_key.value(), std::nullopt));
    REQUIRE(config.value().save());
    auto after_remove = ConfigFile::load(path);
    REQUIRE(after_remove);
    const auto& remaining_verify = require_optional(after_remove.value().parsed().verify);
    REQUIRE_FALSE(remaining_verify.hashing_workers.has_value());
    REQUIRE(require_optional(remaining_verify.checking_memory_bytes) == 64ULL * 1024ULL * 1024ULL);
}

TEST_CASE("given_legacy_config_when_verify_settings_are_mutated_then_they_are_rejected",
          "[unit][frontend][config]")
{
    const TempDirectory temp;
    const auto path = temp.path() / "tu.json";
    write_file(path, R"({"piece_size": 4096})");

    auto config = ConfigFile::load(path);
    REQUIRE(config);
    auto key = parse_config_key("verify.workers");
    REQUIRE(key);
    auto set_result = config.value().set_key(key.value(), "2");
    REQUIRE_FALSE(set_result);
    REQUIRE(set_result.error().code == ErrorCode::ValidationFailed);

    auto get_result = config.value().get_key(key.value());
    REQUIRE_FALSE(get_result);
    REQUIRE(get_result.error().code == ErrorCode::ValidationFailed);
}

TEST_CASE("given_config_key_when_verify_member_is_unknown_then_key_is_rejected",
          "[unit][frontend][config]")
{
    auto key = parse_config_key("verify.max_files");
    REQUIRE_FALSE(key);
    REQUIRE(key.error().code == ErrorCode::ValidationFailed);
    REQUIRE(key.error().issues.front().message.find("unknown verify member") != std::string::npos);
}

TEST_CASE("given_canonical_config_when_disk_io_is_mutated_then_value_is_managed",
          "[unit][frontend][config]")
{
    const TempDirectory temp;
    const auto path = temp.path() / "torrentcraft.json";
    write_file(path, R"({"schema":"torrentcraft.config/v1"})");

    auto config = ConfigFile::load(path);
    REQUIRE(config);
    auto key = parse_config_key("disk_io");
    REQUIRE(key);
    REQUIRE(key.value().scope == ConfigScope::DiskIo);

    REQUIRE(config.value().set_key(key.value(), "\"mmap\""));
    auto get = config.value().get_key(key.value());
    REQUIRE(get);
    REQUIRE(get.value() == "\"mmap\"");
    REQUIRE(config.value().save());

    auto reloaded = ConfigFile::load(path);
    REQUIRE(reloaded);
    REQUIRE(reloaded.value().parsed().disk_io == DiskIoMode::Mmap);

    REQUIRE(config.value().set_key(key.value(), std::nullopt));
    REQUIRE(config.value().save());
    auto after_remove = ConfigFile::load(path);
    REQUIRE(after_remove);
    REQUIRE_FALSE(after_remove.value().parsed().disk_io.has_value());
}

TEST_CASE("given_legacy_config_when_disk_io_is_mutated_then_it_is_rejected",
          "[unit][frontend][config]")
{
    const TempDirectory temp;
    const auto path = temp.path() / "tu.json";
    write_file(path, R"({"piece_size": 4096})");

    auto config = ConfigFile::load(path);
    REQUIRE(config);
    auto key = parse_config_key("disk_io");
    REQUIRE(key);
    auto set_result = config.value().set_key(key.value(), "\"posix\"");
    REQUIRE_FALSE(set_result);
    REQUIRE(set_result.error().code == ErrorCode::ValidationFailed);

    auto get_result = config.value().get_key(key.value());
    REQUIRE_FALSE(get_result);
    REQUIRE(get_result.error().code == ErrorCode::ValidationFailed);
}

} // namespace

TEST_CASE("given_existing_config_when_created_with_overwrite_then_minimal_template_is_restored",
          "[unit][frontend][config]")
{
    const TempDirectory temp;
    const auto path = temp.path() / "torrentcraft.json";
    write_file(
        path,
        R"({"schema": "torrentcraft.config/v1", "defaults": {"private": true}, "presets": {"Old": {}}})");

    auto conflicted = ConfigFile::create(path);
    REQUIRE_FALSE(conflicted);
    REQUIRE(conflicted.error().code == ErrorCode::Conflict);

    auto recreated = ConfigFile::create(path, true);
    REQUIRE(recreated);
    auto reloaded = ConfigFile::load(path);
    REQUIRE(reloaded);
    REQUIRE_FALSE(reloaded.value().parsed().defaults.is_private);
    REQUIRE(reloaded.value().parsed().presets.empty());
}

TEST_CASE("given_legacy_config_when_created_with_overwrite_then_canonical_template_replaces_it",
          "[unit][frontend][config]")
{
    const TempDirectory temp;
    const auto path = temp.path() / "tu.json";
    write_file(path, R"({"private": 0})");

    auto recreated = ConfigFile::create(path, true);
    REQUIRE(recreated);
    auto reloaded = ConfigFile::load(path);
    REQUIRE(reloaded);
    REQUIRE_FALSE(reloaded.value().parsed().legacy);
}

TEST_CASE("given_existing_preset_when_added_with_overwrite_then_preset_is_replaced",
          "[unit][frontend][config]")
{
    const TempDirectory temp;
    const auto path = temp.path() / "torrentcraft.json";
    write_file(
        path,
        R"({"schema": "torrentcraft.config/v1", "presets": {"Old": {"piece_size": 16384, "private": true}}})");

    auto config = ConfigFile::load(path);
    REQUIRE(config);
    CreationSettingsPatch replacement;
    replacement.piece_size = PieceSizeSetting{4096U};

    REQUIRE_FALSE(config.value().add_preset("Old", replacement));
    REQUIRE(config.value().add_preset("Old", replacement, true));
    REQUIRE(config.value().save());

    auto reloaded = ConfigFile::load(path);
    REQUIRE(reloaded);
    REQUIRE(reloaded.value().parsed().presets.count("Old") == 1);
    const auto saved = read_file(path);
    REQUIRE(saved.find("piece_size") != std::string::npos);
    REQUIRE(saved.find("4096") != std::string::npos);
    REQUIRE(saved.find("private") == std::string::npos);
}

TEST_CASE(
    "given_default_preset_key_when_mutated_then_public_storage_and_legacy_cleanup_are_applied",
    "[unit][frontend][config]")
{
    const TempDirectory temp;
    const auto path = temp.path() / "torrentcraft.json";
    write_file(path, R"({"schema":"torrentcraft.config/v1",
        "gui":{"default_preset":"legacy"}})");

    auto config = ConfigFile::load(path);
    REQUIRE(config);
    auto key = parse_config_key("default_preset");
    REQUIRE(key);
    REQUIRE(key.value().scope == ConfigScope::DefaultPreset);
    auto legacy = config.value().get_key(key.value());
    REQUIRE(legacy);
    REQUIRE(legacy.value() == "\"legacy\"");

    REQUIRE(config.value().set_key(key.value(), "\"release\""));
    auto json = config.value().document_json();
    REQUIRE(json);
    REQUIRE(json.value().find("\"default_preset\":\"release\"") != std::string::npos);
    REQUIRE(json.value().find("\"default_preset\":\"legacy\"") == std::string::npos);
    REQUIRE(config.value().save());

    auto reloaded = ConfigFile::load(path);
    REQUIRE(reloaded);
    REQUIRE(reloaded.value().parsed().default_preset == "release");
    REQUIRE(reloaded.value().set_key(key.value(), std::nullopt));
    REQUIRE(reloaded.value().save());
    auto cleared = ConfigFile::load(path);
    REQUIRE(cleared);
    REQUIRE_FALSE(cleared.value().parsed().default_preset.has_value());
}

TEST_CASE("given_canonical_config_when_working_set_limit_is_mutated_then_value_is_managed",
          "[unit][frontend][config]")
{
    const TempDirectory temp;
    const auto path = temp.path() / "torrentcraft.json";
    write_file(path, R"({"schema":"torrentcraft.config/v1"})");

    auto config = ConfigFile::load(path);
    REQUIRE(config);
    auto key = parse_config_key("memory_working_set_limit");
    REQUIRE(key);
    REQUIRE(key.value().scope == ConfigScope::MemoryWorkingSetLimit);

    REQUIRE(config.value().set_key(key.value(), "\"768 MiB\""));
    auto get = config.value().get_key(key.value());
    REQUIRE(get);
    REQUIRE(get.value() == "\"768 MiB\"");
    REQUIRE(config.value().save());

    auto reloaded = ConfigFile::load(path);
    REQUIRE(reloaded);
    REQUIRE(require_optional(reloaded.value().parsed().memory_working_set_limit_bytes) ==
            768ULL * 1024ULL * 1024ULL);

    REQUIRE(config.value().set_key(key.value(), std::nullopt));
    REQUIRE(config.value().save());
    auto after_remove = ConfigFile::load(path);
    REQUIRE(after_remove);
    REQUIRE_FALSE(after_remove.value().parsed().memory_working_set_limit_bytes.has_value());
}

namespace {

[[nodiscard]] ConfigFile load_config(const std::filesystem::path& path, const std::string& content)
{
    write_file(path, content);
    auto config = ConfigFile::load(path);
    if (!config)
    {
        throw std::logic_error("expected the config to load: " + config.error().message);
    }
    return std::move(config).value();
}

[[nodiscard]] ConfigKey key(const std::string& text)
{
    auto parsed = parse_config_key(text);
    if (!parsed)
    {
        throw std::logic_error("expected a valid config key: " + text);
    }
    return std::move(parsed).value();
}

[[nodiscard]] bool permissions_enforced(const std::filesystem::path& locked)
{
    std::error_code error;
    std::filesystem::create_directory(locked / "probe", error);
    if (!error)
    {
        std::filesystem::remove(locked / "probe", error);
        return false;
    }
    return true;
}

TEST_CASE("given_every_creation_enum_when_defaults_are_saved_then_each_value_round_trips",
          "[unit][frontend][config]")
{
    const TempDirectory temp;
    const auto path = temp.path() / "torrentcraft.json";
    auto config = load_config(path, R"({"schema": "torrentcraft.config/v1"})");

    for (const auto format : {TorrentFormat::V1, TorrentFormat::V2, TorrentFormat::Hybrid})
    {
        CreationSettingsPatch patch;
        patch.format = format;
        REQUIRE(config.set_defaults(patch));
        REQUIRE(config.save());
        CHECK(require_optional(ConfigFile::load(path).value().parsed().defaults.format) == format);
    }
    for (const auto order : {FileOrderPolicy::Lexicographical, FileOrderPolicy::CanonicalAlignment,
                             FileOrderPolicy::Natural, FileOrderPolicy::BreadthFirst})
    {
        CreationSettingsPatch patch;
        patch.file_order = order;
        REQUIRE(config.set_defaults(patch));
        REQUIRE(config.save());
        CHECK(require_optional(ConfigFile::load(path).value().parsed().defaults.file_order) ==
              order);
    }
    for (const auto mode :
         {FileFilterMode::Disabled, FileFilterMode::CommonArtifacts, FileFilterMode::CustomRules})
    {
        CreationSettingsPatch patch;
        patch.file_filter = FileFilterInput{mode, true, {"*.tmp"}};
        REQUIRE(config.set_defaults(patch));
        REQUIRE(config.save());
        const auto filter =
            require_optional(ConfigFile::load(path).value().parsed().defaults.file_filter);
        CHECK(filter.mode == mode);
        CHECK(filter.case_sensitive);
        CHECK(filter.patterns == std::vector<std::string>{"*.tmp"});
    }
}

TEST_CASE("given_full_creation_patch_when_saved_then_every_field_is_written",
          "[unit][frontend][config]")
{
    const TempDirectory temp;
    const auto path = temp.path() / "torrentcraft.json";
    auto config = load_config(path, R"({"schema": "torrentcraft.config/v1",
        "defaults": {"tracker_list": ["https://legacy.example/announce"]}})");

    CreationSettingsPatch patch;
    patch.piece_size = PieceSizeSetting{};
    patch.tracker_tiers = std::vector<std::vector<std::string>>{{"https://one.example/announce"},
                                                                {"udp://two.example:80/announce"}};
    patch.web_seeds = std::vector<std::string>{"https://seed.example/files/"};
    patch.comment = "comment text";
    patch.created_by = "TorrentCraft tests";
    patch.info_source = "SRC";
    REQUIRE(config.set_defaults(patch));
    REQUIRE(config.save());

    const auto text = read_file(path);
    CHECK(text.find("tracker_list") == std::string::npos);
    CHECK(text.find("\"piece_size\": \"auto\"") != std::string::npos);
    const auto reloaded = ConfigFile::load(path);
    REQUIRE(reloaded);
    const auto& defaults = reloaded.value().parsed().defaults;
    CHECK_FALSE(require_optional(defaults.piece_size).fixed_kib.has_value());
    CHECK(require_optional(defaults.tracker_tiers).size() == 2U);
    CHECK(require_optional(defaults.web_seeds) ==
          std::vector<std::string>{"https://seed.example/files/"});
    CHECK(require_optional(defaults.comment) == "comment text");
    CHECK(require_optional(defaults.created_by) == "TorrentCraft tests");
    CHECK(require_optional(defaults.info_source) == "SRC");

    // A canonical config without a defaults object gets one on first write.
    auto bare = load_config(temp.path() / "bare.json", R"({"schema": "torrentcraft.config/v1"})");
    CHECK(bare.set_defaults(patch));
}

TEST_CASE("given_invalid_edits_when_applied_then_the_document_is_rolled_back",
          "[unit][frontend][config]")
{
    const TempDirectory temp;
    const auto path = temp.path() / "torrentcraft.json";
    auto config = load_config(path, R"({"schema": "torrentcraft.config/v1",
        "default_preset": "U2", "presets": {"U2": {}}})");
    const auto before = config.document_json().value();

    CreationSettingsPatch invalid;
    invalid.piece_size = PieceSizeSetting{3U};
    CHECK_FALSE(config.set_defaults(invalid));
    CHECK(config.document_json().value() == before);
    CHECK_FALSE(config.add_preset("Broken", invalid));
    CHECK(config.document_json().value() == before);
    CHECK(config.add_preset("U2", {}, true));

    CHECK_FALSE(config.set_key(key("defaults.piece_size"), std::string{"\"huge\""}));
    CHECK_FALSE(config.set_key(key("defaults.comment"), std::string{"{not json"}));
    CHECK(config.document_json().value() == before);
    CHECK_FALSE(config.set_key(key("presets.New.comment"), std::string{"{not json"}));
    CHECK(config.document_json().value() == before);
    CHECK(config.parsed().presets.count("U2") == 1U);
}

TEST_CASE("given_gui_preference_variants_when_round_tripped_then_values_and_absences_hold",
          "[unit][frontend][config]")
{
    const TempDirectory temp;
    const auto path = temp.path() / "torrentcraft.json";

    // Without a gui object every preference keeps its default.
    auto plain = load_config(path, R"({"schema": "torrentcraft.config/v1"})");
    CHECK(plain.gui_language() == GuiLanguage::English);
    const auto defaults = plain.gui_preferences();
    CHECK(defaults.default_save_location == GuiSaveLocationMode::Current);
    CHECK_FALSE(defaults.default_preset);
    CHECK(defaults.log_level == GuiLogLevel::Info);

    // The legacy gui.default_preset is read when the top-level key is absent; a gui without a
    // logging object keeps the logging defaults.
    auto legacy = load_config(path, R"({"schema": "torrentcraft.config/v1",
        "gui": {"default_preset": "legacy"}})");
    CHECK(require_optional(legacy.gui_preferences().default_preset) == "legacy");
    CHECK_FALSE(legacy.gui_preferences().logging_enabled);

    for (const auto level :
         {GuiLogLevel::Debug, GuiLogLevel::Info, GuiLogLevel::Warning, GuiLogLevel::Error})
    {
        auto config = load_config(path, R"({"schema": "torrentcraft.config/v1"})");
        GuiPreferences preferences;
        preferences.default_save_location = GuiSaveLocationMode::Recent;
        preferences.recent_save_path = "/tmp/recent";
        preferences.log_level = level;
        preferences.log_path = "/tmp/log.txt";
        preferences.font_family = "Sans";
        preferences.style = "Fusion";
        preferences.default_preset = "kept";
        REQUIRE(config.set_gui_preferences(preferences));
        REQUIRE(config.save());
        const auto actual = ConfigFile::load(path).value().gui_preferences();
        CHECK(actual.default_save_location == GuiSaveLocationMode::Recent);
        CHECK(require_optional(actual.recent_save_path) == "/tmp/recent");
        CHECK(actual.log_level == level);
    }

    // Clearing optional values removes their members again.
    auto config = ConfigFile::load(path).value();
    GuiPreferences cleared;
    cleared.default_save_location = GuiSaveLocationMode::Current;
    REQUIRE(config.set_gui_preferences(cleared));
    const auto json = config.document_json().value();
    for (const auto* member : {"recent_save_path", "default_save_path", "font_family", "\"style\"",
                               "\"path\"", "default_preset"})
    {
        INFO(member);
        CHECK(json.find(member) == std::string::npos);
    }
    CHECK(json.find("\"current\"") != std::string::npos);

    // A missing gui object is created when the language is written.
    auto scalar_gui = load_config(path, R"({"schema": "torrentcraft.config/v1"})");
    REQUIRE(scalar_gui.set_gui_language(GuiLanguage::SimplifiedChinese));
    CHECK(scalar_gui.gui_language() == GuiLanguage::SimplifiedChinese);
    REQUIRE(scalar_gui.set_gui_language(GuiLanguage::English));
    CHECK(scalar_gui.document_json().value().find("\"en\"") != std::string::npos);
}

TEST_CASE("given_config_key_text_when_parsed_then_every_form_is_classified",
          "[unit][frontend][config]")
{
    const auto preset = parse_config_key("presets.My Preset.comment");
    REQUIRE(preset);
    CHECK(preset.value().scope == ConfigScope::Preset);
    CHECK(require_optional(preset.value().preset_name) == "My Preset");
    CHECK(preset.value().setting == "comment");
    CHECK(parse_config_key("presets.a.b.web_seeds").value().preset_name == std::string{"a.b"});

    CHECK(parse_config_key("disk_io").value().scope == ConfigScope::DiskIo);
    CHECK(parse_config_key("memory_working_set_limit").value().scope ==
          ConfigScope::MemoryWorkingSetLimit);
    CHECK(parse_config_key("default_preset").value().scope == ConfigScope::DefaultPreset);

    for (const auto* invalid :
         {"defaults.unknown", "presets.comment", "presets..comment", "presets.a/b.comment",
          "presets.a.unknown", "verify.other", "gui.language", ""})
    {
        INFO(invalid);
        const auto parsed = parse_config_key(invalid);
        REQUIRE_FALSE(parsed);
        CHECK(parsed.error().code == ErrorCode::ValidationFailed);
    }
}

TEST_CASE("given_canonical_config_when_keys_are_read_and_written_then_scopes_are_honoured",
          "[unit][frontend][config]")
{
    const TempDirectory temp;
    const auto path = temp.path() / "torrentcraft.json";
    auto config = load_config(path, R"({"schema": "torrentcraft.config/v1"})");

    // Nothing is set yet.
    for (const auto* text : {"defaults.comment", "presets.U2.comment", "default_preset",
                             "verify.workers", "disk_io", "memory_working_set_limit"})
    {
        INFO(text);
        const auto value = config.get_key(key(text));
        REQUIRE_FALSE(value);
        CHECK(value.error().code == ErrorCode::FileNotFound);
    }

    // Reading never changes the document, so later edits still validate.
    CHECK(config.document_json().value() == R"({"schema":"torrentcraft.config/v1"})");

    // Writing into a missing preset creates it.
    REQUIRE(config.set_key(key("presets.U2.comment"), std::string{"\"hello\""}));
    CHECK(config.get_key(key("presets.U2.comment")).value() == "\"hello\"");
    CHECK(config.get_key(key("presets.U2.private")).error().code == ErrorCode::FileNotFound);
    CHECK(config.get_key(key("presets.Other.comment")).error().code == ErrorCode::FileNotFound);

    REQUIRE(config.set_key(key("verify.workers"), std::string{"2"}));
    CHECK(config.get_key(key("verify.workers")).value() == "2");
    CHECK(config.get_key(key("verify.memory")).error().code == ErrorCode::FileNotFound);

    REQUIRE(config.set_key(key("defaults.comment"), std::string{"\"default\""}));
    CHECK(config.get_key(key("defaults.comment")).value() == "\"default\"");
    REQUIRE(config.set_key(key("defaults.comment"), std::nullopt));
    CHECK(config.get_key(key("defaults.comment")).error().code == ErrorCode::FileNotFound);

    // Hand-built keys that bypass the parser are still validated.
    ConfigKey unnamed{ConfigScope::Preset, std::nullopt, "comment"};
    CHECK(config.get_key(unnamed).error().code == ErrorCode::ValidationFailed);
    CHECK(config.set_key(unnamed, std::string{"\"x\""}).error().code ==
          ErrorCode::ValidationFailed);
    ConfigKey bad_name{ConfigScope::Preset, std::string{"a/b"}, "comment"};
    CHECK_FALSE(config.set_key(bad_name, std::string{"\"x\""}));

    // A preset entry that is not an object is replaced; a non-object presets member too.
    auto odd = load_config(temp.path() / "odd.json", R"({"schema": "torrentcraft.config/v1",
        "presets": {"P": {}}})");
    REQUIRE(odd.set_key(key("presets.Q.comment"), std::string{"\"q\""}));
    CHECK(odd.parsed().presets.count("Q") == 1U);
}

TEST_CASE("given_legacy_config_when_canonical_only_keys_are_used_then_they_are_rejected",
          "[unit][frontend][config]")
{
    const TempDirectory temp;
    auto legacy = load_config(temp.path() / "tu.json", R"({"comment": "legacy"})");
    REQUIRE(legacy.parsed().legacy);

    CHECK(legacy.get_key(key("defaults.comment")).value() == "\"legacy\"");
    REQUIRE(legacy.set_key(key("defaults.comment"), std::string{"\"changed\""}));
    CHECK(legacy.get_key(key("defaults.comment")).value() == "\"changed\"");
    CHECK(legacy.get_key(key("defaults.private")).error().code == ErrorCode::FileNotFound);

    for (const auto* text : {"presets.U2.comment", "default_preset", "verify.workers", "disk_io",
                             "memory_working_set_limit"})
    {
        INFO(text);
        CHECK(legacy.get_key(key(text)).error().code == ErrorCode::ValidationFailed);
        CHECK(legacy.set_key(key(text), std::string{"1"}).error().code ==
              ErrorCode::ValidationFailed);
    }
    for (const auto* text : {"defaults.format", "defaults.file_order", "defaults.file_filter",
                             "defaults.tracker_tiers", "defaults.web_seeds", "defaults.source"})
    {
        INFO(text);
        CHECK(legacy.set_key(key(text), std::string{"\"v1\""}).error().code ==
              ErrorCode::ValidationFailed);
    }
    CHECK(legacy.remove_preset("U2").error().code == ErrorCode::ValidationFailed);
}

// A path whose parent is a regular file is ENOTDIR on POSIX but "path not found" on Windows.
#ifdef _WIN32
constexpr ErrorCode kThroughFileError = ErrorCode::FileNotFound;
#else
constexpr ErrorCode kThroughFileError = ErrorCode::IoFailure;
#endif

TEST_CASE("given_unusable_config_files_when_loaded_then_errors_are_classified",
          "[unit][frontend][config]")
{
    const TempDirectory temp;

    const auto missing = ConfigFile::load(temp.path() / "missing.json");
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == ErrorCode::FileNotFound);
    CHECK(load_preset_file(temp.path() / "missing.json").error().code == ErrorCode::FileNotFound);

    const auto oversized = temp.path() / "big.json";
    write_file(oversized, std::string(1024U * 1024U + 1U, ' '));
    const auto big = ConfigFile::load(oversized);
    REQUIRE_FALSE(big);
    CHECK(big.error().code == ErrorCode::ValidationFailed);
    CHECK(big.error().issues.front().message == "input exceeds the 1 MiB limit");

    const auto syntax = temp.path() / "syntax.json";
    write_file(syntax, "{");
    CHECK(ConfigFile::load(syntax).error().code == ErrorCode::ValidationFailed);

    write_file(temp.path() / "file", "x");
    const auto not_directory = ConfigFile::load(temp.path() / "file" / "torrentcraft.json");
    REQUIRE_FALSE(not_directory);
    CHECK(not_directory.error().code == kThroughFileError);

    const auto unreadable = temp.path() / "unreadable.json";
    write_file(unreadable, R"({"schema": "torrentcraft.config/v1"})");
    std::filesystem::permissions(unreadable, std::filesystem::perms::none);
    if (static_cast<bool>(std::ifstream(unreadable)))
    {
        std::filesystem::permissions(unreadable, std::filesystem::perms::owner_all);
        SKIP("the test filesystem does not enforce unreadable file permissions");
    }
    const auto denied = ConfigFile::load(unreadable);
    std::filesystem::permissions(unreadable, std::filesystem::perms::owner_all);
    REQUIRE_FALSE(denied);
    CHECK(denied.error().code == ErrorCode::AccessDenied);
}

TEST_CASE("given_unusable_search_paths_when_discovered_then_errors_are_classified",
          "[unit][frontend][config]")
{
    const TempDirectory temp;
    const auto folder = temp.path() / "folder.json";
    std::filesystem::create_directories(folder);
    write_file(temp.path() / "file", "x");

    const auto directory = discover_config({folder, temp.path(), std::nullopt});
    REQUIRE_FALSE(directory);
    CHECK(directory.error().code == ErrorCode::ValidationFailed);

    const auto through_file =
        discover_config({temp.path() / "file" / "torrentcraft.json", temp.path(), std::nullopt});
    REQUIRE_FALSE(through_file);
    CHECK(through_file.error().code == kThroughFileError);

    // A user path equal to the working directory candidate is checked only once.
    const auto duplicate = discover_config(
        {std::nullopt, temp.path(), temp.path() / "." / "torrentcraft.json"}, temp.path());
    REQUIRE(duplicate);
    CHECK_FALSE(duplicate.value().has_value());
}

#ifndef _WIN32
class ScopedEnvironment
{
  public:
    explicit ScopedEnvironment(std::string name) : name_(std::move(name))
    {
        if (const auto* value = std::getenv(name_.c_str()); value != nullptr)
        {
            previous_ = value;
        }
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;
    ScopedEnvironment(ScopedEnvironment&&) = delete;
    ScopedEnvironment& operator=(ScopedEnvironment&&) = delete;

    ~ScopedEnvironment()
    {
        if (previous_)
        {
            setenv(name_.c_str(), previous_->c_str(), 1);
        }
        else
        {
            unsetenv(name_.c_str());
        }
    }

    void set(const char* value) const
    {
        if (value == nullptr)
        {
            unsetenv(name_.c_str());
        }
        else
        {
            setenv(name_.c_str(), value, 1);
        }
    }

  private:
    std::string name_;
    std::optional<std::string> previous_;
};

TEST_CASE("given_environment_when_default_search_paths_are_built_then_user_path_follows_xdg_rules",
          "[unit][frontend][config]")
{
    const ScopedEnvironment xdg("XDG_CONFIG_HOME");
    const ScopedEnvironment home("HOME");

    xdg.set("/xdg");
    home.set("/home/user");
    CHECK(require_optional(default_config_search_paths(std::nullopt, "/work").user_config_path) ==
          std::filesystem::path("/xdg/torrentcraft/torrentcraft.json"));

    xdg.set("");
    CHECK(require_optional(default_config_search_paths(std::nullopt, "/work").user_config_path) ==
          std::filesystem::path("/home/user/.config/torrentcraft/torrentcraft.json"));

    xdg.set(nullptr);
    home.set("");
    const auto none = default_config_search_paths(std::filesystem::path("/explicit.json"), "/work");
    CHECK_FALSE(none.user_config_path.has_value());
    CHECK(require_optional(none.explicit_path) == std::filesystem::path("/explicit.json"));
    CHECK(none.working_directory == std::filesystem::path("/work"));
}
#endif

TEST_CASE("given_unwritable_locations_when_config_is_created_or_saved_then_errors_are_reported",
          "[unit][frontend][config]")
{
    const TempDirectory temp;
    write_file(temp.path() / "file", "x");

    const auto under_file = ConfigFile::create(temp.path() / "file" / "torrentcraft.json", false);
    REQUIRE_FALSE(under_file);
    CHECK(under_file.error().code == ErrorCode::IoFailure);

    // Saving onto a path that has become a directory cannot replace it atomically.
    const auto path = temp.path() / "torrentcraft.json";
    auto config = load_config(path, R"({"schema": "torrentcraft.config/v1"})");
    std::filesystem::remove(path);
    std::filesystem::create_directories(path / "child");
    const auto replaced = config.save();
    REQUIRE_FALSE(replaced);
    CHECK(replaced.error().code != ErrorCode::Internal);
    std::size_t leftovers = 0;
    for (const auto& entry : std::filesystem::directory_iterator(temp.path()))
    {
        leftovers += entry.path().filename().string().find(".tmp.") != std::string::npos ? 1U : 0U;
    }
    CHECK(leftovers == 0U);
    std::filesystem::remove_all(path);

    // Read-only folders reject the temporary file and new subfolders.
    const auto locked = temp.path() / "locked";
    std::filesystem::create_directories(locked);
    write_file(locked / "torrentcraft.json", R"({"schema": "torrentcraft.config/v1"})");
    auto inside = ConfigFile::load(locked / "torrentcraft.json").value();
    using std::filesystem::perms;
    std::filesystem::permissions(locked, perms::owner_read | perms::owner_exec);
    if (!permissions_enforced(locked))
    {
        std::filesystem::permissions(locked, perms::owner_all);
        SKIP("the test filesystem does not enforce unwritable directory permissions");
    }
    const auto save_denied = inside.save();
    const auto create_denied = ConfigFile::create(locked / "new.json", false);
    const auto folder_denied = ConfigFile::create(locked / "sub" / "new.json", false);
    std::filesystem::permissions(locked, perms::owner_all);

    REQUIRE_FALSE(save_denied);
    CHECK(save_denied.error().code == ErrorCode::AccessDenied);
    REQUIRE_FALSE(create_denied);
    CHECK(create_denied.error().code == ErrorCode::AccessDenied);
    REQUIRE_FALSE(folder_denied);
    CHECK(folder_denied.error().code == ErrorCode::AccessDenied);
}

} // namespace
