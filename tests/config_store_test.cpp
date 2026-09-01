#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
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
    CreationSettingsPatch added;
    added.piece_size = PieceSizeSetting{16384U};

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

    auto reloaded = ConfigFile::load(path);
    REQUIRE(reloaded);
    REQUIRE(reloaded.value().parsed().defaults.is_private == true);
    REQUIRE(reloaded.value().parsed().presets.find("Existing") !=
            reloaded.value().parsed().presets.end());
    REQUIRE(reloaded.value().parsed().presets.find("Added") !=
            reloaded.value().parsed().presets.end());
    REQUIRE(
        require_optional(
            require_optional(reloaded.value().parsed().presets.at("Added").piece_size).fixed_kib) ==
        16384U);
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
        "gui": {"future": {"keep": true}}
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
