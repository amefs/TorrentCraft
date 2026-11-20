#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <torrentutils/frontend/frontend.hpp>
#include <vector>

namespace {

using namespace torrentutils::core;
using namespace torrentutils::frontend;

std::filesystem::path metadata_fixture(const char* name)
{
    return std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" / "metadata" / name;
}

TorrentTemplate load_fixture_template(const char* name)
{
    auto loaded = load_torrent_template(metadata_fixture(name));
    if (!loaded)
    {
        throw std::runtime_error("fixture template failed to load: " + loaded.error().message);
    }
    return std::move(loaded).value();
}

template <typename Value> const Value& require_optional(const std::optional<Value>& value)
{
    if (!value)
    {
        throw std::logic_error("expected optional test value");
    }
    return *value;
}

TEST_CASE("given_v1_torrent_when_template_is_read_then_reusable_settings_are_adoptable",
          "[unit][frontend][template]")
{
    const auto source = load_fixture_template("valid-v1.torrent");

    REQUIRE(source.fields.size() == all_template_fields().size());
    const auto& patch = source.adoptable;
    REQUIRE(require_optional(patch.format) == TorrentFormat::V1);
    REQUIRE(require_optional(patch.piece_size).fixed_kib == std::optional<std::uint32_t>(16U));
    REQUIRE(require_optional(patch.is_private));
    REQUIRE(require_optional(patch.info_source) == "SRC");
    REQUIRE(require_optional(patch.tracker_tiers) ==
            std::vector<std::vector<std::string>>{
                {"https://tracker.example/announce", "udp://tracker.example:6969/announce"},
                {"https://backup.example/announce"}});
    REQUIRE(require_optional(patch.web_seeds) ==
            std::vector<std::string>{"https://seed.example/web", "https://seed.example/http"});
    // Settings a torrent cannot describe are never part of a template.
    REQUIRE_FALSE(patch.file_order.has_value());
    REQUIRE_FALSE(patch.file_filter.has_value());
}

TEST_CASE("given_default_selection_when_selected_then_comment_and_creator_are_opt_in",
          "[unit][frontend][template]")
{
    const auto source = load_fixture_template("unsupported-piece-size-v1.torrent");
    REQUIRE(source.candidate(TemplateField::Comment).available());
    REQUIRE(source.candidate(TemplateField::CreatedBy).available());

    const auto selection = select_template_fields(source, default_template_fields());

    REQUIRE(selection);
    REQUIRE_FALSE(selection.value().settings.comment.has_value());
    REQUIRE_FALSE(selection.value().settings.created_by.has_value());

    const auto opted_in =
        select_template_fields(source, {TemplateField::Comment, TemplateField::CreatedBy});
    REQUIRE(opted_in);
    REQUIRE(require_optional(opted_in.value().settings.comment) == "release notes");
    REQUIRE(require_optional(opted_in.value().settings.created_by) == "OtherClient 1.0");
    REQUIRE_FALSE(opted_in.value().settings.tracker_tiers.has_value());
    REQUIRE_FALSE(opted_in.value().settings.format.has_value());
}

TEST_CASE("given_top_level_source_only_when_template_is_read_then_source_is_not_adopted",
          "[unit][frontend][template]")
{
    const auto source = load_fixture_template("top-level-source-v1.torrent");

    REQUIRE(source.candidate(TemplateField::Source).unavailability ==
            TemplateFieldUnavailability::NotPresent);
    REQUIRE_FALSE(source.adoptable.info_source.has_value());
}

TEST_CASE("given_public_torrent_when_template_is_read_then_private_false_is_adoptable",
          "[unit][frontend][template]")
{
    const auto source = load_fixture_template("valid-hybrid.torrent");

    REQUIRE(source.candidate(TemplateField::Private).available());
    REQUIRE_FALSE(require_optional(source.adoptable.is_private));
    REQUIRE(require_optional(source.adoptable.format) == TorrentFormat::Hybrid);
}

TEST_CASE("given_unsupported_piece_length_when_template_is_read_then_piece_size_is_rejected",
          "[unit][frontend][template]")
{
    const auto source = load_fixture_template("unsupported-piece-size-v1.torrent");
    const auto& piece_size = source.candidate(TemplateField::PieceSize);

    REQUIRE(piece_size.unavailability == TemplateFieldUnavailability::UnsupportedPieceSize);
    REQUIRE(piece_size.rejected_piece_length ==
            std::optional<std::uint64_t>(32ULL * 1024ULL * 1024ULL));
    REQUIRE_FALSE(source.adoptable.piece_size.has_value());
}

TEST_CASE("given_unavailable_default_fields_when_selected_then_they_are_skipped",
          "[unit][frontend][template]")
{
    const auto source = load_fixture_template("unsupported-piece-size-v1.torrent");

    const auto selection = select_template_fields(source, default_template_fields());

    REQUIRE(selection);
    std::vector<TemplateField> skipped;
    for (const auto& field : selection.value().skipped)
    {
        skipped.push_back(field.field);
    }
    REQUIRE(skipped == std::vector<TemplateField>{TemplateField::PieceSize, TemplateField::Source,
                                                  TemplateField::WebSeeds});
    REQUIRE_FALSE(selection.value().settings.piece_size.has_value());
    REQUIRE(selection.value().settings.tracker_tiers.has_value());
}

TEST_CASE("given_unavailable_named_fields_when_selected_then_they_are_skipped_without_error",
          "[unit][frontend][template]")
{
    const auto source = load_fixture_template("unsupported-piece-size-v1.torrent");

    const auto selection = select_template_fields(
        source, {TemplateField::Trackers, TemplateField::PieceSize, TemplateField::WebSeeds});

    REQUIRE(selection);
    REQUIRE(selection.value().skipped.size() == 2U);
    REQUIRE(selection.value().skipped.front().field == TemplateField::PieceSize);
    REQUIRE(selection.value().settings.tracker_tiers.has_value());
    REQUIRE_FALSE(selection.value().settings.piece_size.has_value());
}

TEST_CASE("given_only_unavailable_fields_when_selected_then_selection_fails",
          "[unit][frontend][template]")
{
    const auto source = load_fixture_template("unsupported-piece-size-v1.torrent");

    const auto selection =
        select_template_fields(source, {TemplateField::PieceSize, TemplateField::WebSeeds});

    REQUIRE_FALSE(selection);
    REQUIRE(selection.error().code == ErrorCode::ValidationFailed);
}

TEST_CASE("given_empty_selection_when_selected_then_selection_fails", "[unit][frontend][template]")
{
    const auto source = load_fixture_template("valid-v1.torrent");

    REQUIRE_FALSE(select_template_fields(source, {}));
}

TEST_CASE("given_field_list_when_parsed_then_tokens_map_to_fields", "[unit][frontend][template]")
{
    const auto parsed = parse_template_field_list("trackers, piece-size,trackers,created-by");
    REQUIRE(parsed);
    REQUIRE(parsed.value() == std::vector<TemplateField>{TemplateField::Trackers,
                                                         TemplateField::PieceSize,
                                                         TemplateField::CreatedBy});

    const auto all = parse_template_field_list("all");
    REQUIRE(all);
    REQUIRE(all.value() == all_template_fields());

    for (const auto field : all_template_fields())
    {
        const auto round_trip = parse_template_field_list(template_field_name(field));
        REQUIRE(round_trip);
        REQUIRE(round_trip.value() == std::vector<TemplateField>{field});
    }

    REQUIRE_FALSE(parse_template_field_list("trackers,bogus"));
    REQUIRE_FALSE(parse_template_field_list(""));
    REQUIRE_FALSE(parse_template_field_list("trackers,"));
}

TEST_CASE("given_structurally_invalid_torrent_when_template_is_loaded_then_it_is_rejected",
          "[unit][frontend][template]")
{
    REQUIRE_FALSE(load_torrent_template(metadata_fixture("invalid-path.torrent")));
    REQUIRE_FALSE(load_torrent_template(metadata_fixture("invalid-v2-piece-layer.torrent")));
    REQUIRE_FALSE(load_torrent_template(metadata_fixture("does-not-exist.torrent")));
}

TEST_CASE("given_non_standard_keys_when_template_is_read_then_they_are_ignored_with_warning",
          "[unit][frontend][template]")
{
    for (const auto* name : {"unknown-field.torrent", "unknown-extensions.torrent"})
    {
        const auto source = load_fixture_template(name);

        REQUIRE(source.warnings.size() == 1U);
        const auto& warning = source.warnings.front();
        REQUIRE(warning.kind == TemplateWarning::Kind::NonStandardFields);
        REQUIRE(warning.keys.size() == 2U);
        REQUIRE(warning.keys.back().rfind("info.", 0) == 0U);
        REQUIRE(template_warning_text(warning).find(warning.keys.front()) != std::string::npos);
        REQUIRE(select_template_fields(source, default_template_fields()));
    }
}

TEST_CASE("given_invalid_utf8_text_when_template_is_read_then_field_is_unreadable",
          "[unit][frontend][template]")
{
    const auto source = load_fixture_template("invalid-utf8-comment.torrent");

    REQUIRE(source.candidate(TemplateField::Comment).unavailability ==
            TemplateFieldUnavailability::Unreadable);
    REQUIRE(source.candidate(TemplateField::CreatedBy).unavailability ==
            TemplateFieldUnavailability::NotPresent);
    REQUIRE(source.warnings.empty());
}

TEST_CASE("given_partially_invalid_lists_when_template_is_read_then_valid_entries_are_adopted",
          "[unit][frontend][template]")
{
    const auto source = load_fixture_template("partially-invalid-template-v1.torrent");

    REQUIRE(source.candidate(TemplateField::Trackers).available());
    REQUIRE(require_optional(source.adoptable.tracker_tiers) ==
            std::vector<std::vector<std::string>>{{"https://tracker.example/announce"}});
    REQUIRE(require_optional(source.adoptable.web_seeds) ==
            std::vector<std::string>{"https://seed.example/web"});
    // An unreadable info source never falls back to anything else.
    REQUIRE(source.candidate(TemplateField::Source).unavailability ==
            TemplateFieldUnavailability::Unreadable);
    REQUIRE(source.warnings.size() == 2U);
    REQUIRE(source.warnings[0].kind == TemplateWarning::Kind::InvalidEntriesDropped);
    REQUIRE(source.warnings[0].field == std::optional<TemplateField>(TemplateField::Trackers));
    REQUIRE(source.warnings[0].count == 2U);
    REQUIRE(source.warnings[1].field == std::optional<TemplateField>(TemplateField::WebSeeds));
    REQUIRE(source.warnings[1].count == 1U);
}

TEST_CASE("given_template_preset_when_resolved_then_create_options_are_valid",
          "[unit][frontend][template]")
{
    const auto source = load_fixture_template("valid-v1.torrent");
    const auto selection = select_template_fields(source, all_template_fields());
    REQUIRE(selection);

    const auto resolved = resolve_settings(selection.value().settings);

    REQUIRE(resolved);
    REQUIRE(resolved.value().options.is_private());
    REQUIRE(resolved.value().options.trackers().tiers().size() == 2U);
}

} // namespace
