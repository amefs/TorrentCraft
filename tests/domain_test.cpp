#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <torrentutils/core/domain.hpp>
#include <utility>
#include <vector>

namespace {

using namespace torrentutils::core;

constexpr std::uint64_t kV2PieceLength = std::uint64_t{16} * 1024U;

Sha1Digest sha1(std::uint8_t fill)
{
    Sha1Digest::Bytes bytes{};
    bytes.fill(fill);
    return Sha1Digest::from_bytes(bytes);
}

Sha256Digest sha256(std::uint8_t fill)
{
    Sha256Digest::Bytes bytes{};
    bytes.fill(fill);
    return Sha256Digest::from_bytes(bytes);
}

LogicalPath path(std::vector<std::string> segments)
{
    auto result = LogicalPath::from_segments(std::move(segments));
    REQUIRE(result.has_value());
    return std::move(result).value();
}

FileEntry v1_file(std::string name, std::uint64_t length)
{
    auto result = FileEntry::create(path({std::move(name)}), length);
    REQUIRE(result.has_value());
    return std::move(result).value();
}

FileEntry v2_file(std::string name, std::uint64_t length, std::uint8_t root_fill)
{
    auto result = FileEntry::create(path({std::move(name)}), length, {}, sha256(root_fill));
    REQUIRE(result.has_value());
    return std::move(result).value();
}

TorrentInfo v1_info(std::string name = "payload")
{
    auto hashes = InfoHashes::create(TorrentFormat::V1, sha1(1), std::nullopt);
    REQUIRE(hashes.has_value());
    auto pieces = PieceInfo::create(TorrentFormat::V1, 16, 16, {sha1(2)});
    REQUIRE(pieces.has_value());
    auto info = TorrentInfo::create(std::move(name), TorrentFormat::V1, std::move(hashes).value(),
                                    std::move(pieces).value(), {v1_file("file.bin", 16)});
    REQUIRE(info.has_value());
    return std::move(info).value();
}

TorrentMetadata empty_metadata()
{
    auto metadata = TorrentMetadata::create();
    REQUIRE(metadata.has_value());
    return std::move(metadata).value();
}

TrackerList empty_trackers()
{
    auto trackers = TrackerList::create({});
    REQUIRE(trackers.has_value());
    return std::move(trackers).value();
}

} // namespace

TEST_CASE("given_digest_hex_when_parsed_then_fixed_bytes_and_lowercase_hex_are_exposed",
          "[unit][domain][hash]")
{
    auto digest = Sha1Digest::from_hex("00112233445566778899AABBCCDDEEFF00112233");

    REQUIRE(digest.has_value());
    REQUIRE(digest.value().to_hex() == "00112233445566778899aabbccddeeff00112233");
}

TEST_CASE("given_incompatible_info_hashes_when_created_then_validation_failure_is_returned",
          "[unit][domain][hash]")
{
    auto hashes = InfoHashes::create(TorrentFormat::Hybrid, sha1(1), std::nullopt);

    REQUIRE_FALSE(hashes.has_value());
    REQUIRE(hashes.error().code == ErrorCode::ValidationFailed);
    REQUIRE(hashes.error().issues.front().field == "info.hashes");
}

TEST_CASE("given_valid_unicode_segments_when_path_created_then_portable_form_is_preserved",
          "[unit][domain][path]")
{
    auto result = LogicalPath::from_segments({"目录", "file.txt"});

    REQUIRE(result.has_value());
    REQUIRE(result.value().to_string() == "目录/file.txt");
    REQUIRE(result.value().segments().size() == 2);
}

TEST_CASE(
    "given_bep47_symlink_entry_when_created_then_public_attributes_hint_and_target_are_preserved",
    "[unit][domain][bep47]")
{
    FileAttributes attributes;
    attributes.hidden = true;
    attributes.symlink = true;
    const auto sha1_hint = sha1(42);
    const auto target = path({"docs", "manual.txt"});

    auto entry = FileEntry::create(path({"links", "manual"}), 0, attributes, std::nullopt,
                                   sha1_hint, target);

    REQUIRE(entry);
    REQUIRE(entry.value().attributes().hidden);
    REQUIRE(entry.value().attributes().symlink);
    REQUIRE(entry.value().sha1_hint() == sha1_hint);
    REQUIRE(entry.value().symlink_target() == target);
}

TEST_CASE("given_invalid_bep47_symlink_shape_when_file_entry_created_then_validation_fails",
          "[unit][domain][bep47]")
{
    FileAttributes symlink_attributes;
    symlink_attributes.symlink = true;
    const auto target = path({"docs", "manual.txt"});

    const auto nonzero_link = FileEntry::create(path({"links", "manual"}), 1, symlink_attributes,
                                                std::nullopt, std::nullopt, target);
    const auto missing_target = FileEntry::create(path({"links", "manual"}), 0, symlink_attributes);
    const auto link_with_pieces_root = FileEntry::create(
        path({"links", "manual"}), 0, symlink_attributes, sha256(9), std::nullopt, target);
    const auto target_on_regular =
        FileEntry::create(path({"manual"}), 1, {}, std::nullopt, std::nullopt, target);

    for (const auto* result :
         {&nonzero_link, &missing_target, &link_with_pieces_root, &target_on_regular})
    {
        REQUIRE_FALSE(result->has_value());
        REQUIRE(result->error().code == ErrorCode::ValidationFailed);
    }
}

TEST_CASE("given_unsafe_path_segments_when_created_then_each_is_rejected", "[unit][domain][path]")
{
    const std::vector<std::vector<std::string>> invalid_paths{{},
                                                              {""},
                                                              {"."},
                                                              {".."},
                                                              {"/absolute"},
                                                              {"folder\\file"},
                                                              {"C:"},
                                                              {std::string("nul\0byte", 8)}};

    for (const auto& segments : invalid_paths)
    {
        auto result = LogicalPath::from_segments(segments);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == ErrorCode::ValidationFailed);
        REQUIRE(result.error().issues.front().field == "file.path");
    }
}

TEST_CASE("given_equivalent_tracker_spellings_when_tier_created_then_first_spelling_wins",
          "[unit][domain][tracker]")
{
    auto first = TrackerUrl::parse("HTTP://Tracker.Example:80/announce?token=%2F");
    auto duplicate = TrackerUrl::parse("http://tracker.example/announce?token=%2F");
    REQUIRE(first.has_value());
    REQUIRE(duplicate.has_value());

    auto tier = TrackerTier::create({std::move(first).value(), std::move(duplicate).value()});

    REQUIRE(tier.has_value());
    REQUIRE(tier.value().trackers().size() == 1);
    REQUIRE(tier.value().trackers().front().value() ==
            "HTTP://Tracker.Example:80/announce?token=%2F");
}

TEST_CASE("given_semantically_distinct_tracker_suffixes_when_tier_created_then_all_are_kept",
          "[unit][domain][tracker]")
{
    auto encoded_upper = TrackerUrl::parse("https://tracker.example/a?key=%2F");
    auto encoded_lower = TrackerUrl::parse("https://tracker.example/a?key=%2f");
    auto other_path = TrackerUrl::parse("https://tracker.example/A?key=%2F");
    REQUIRE(encoded_upper.has_value());
    REQUIRE(encoded_lower.has_value());
    REQUIRE(other_path.has_value());

    auto tier =
        TrackerTier::create({std::move(encoded_upper).value(), std::move(encoded_lower).value(),
                             std::move(other_path).value()});

    REQUIRE(tier.has_value());
    REQUIRE(tier.value().trackers().size() == 3);
}

TEST_CASE("given_same_tracker_in_different_tiers_when_list_created_then_both_are_kept",
          "[unit][domain][tracker]")
{
    auto first_url = TrackerUrl::parse("udp://tracker.example:6969/announce");
    auto second_url = TrackerUrl::parse("UDP://TRACKER.EXAMPLE:6969/announce");
    REQUIRE(first_url.has_value());
    REQUIRE(second_url.has_value());
    auto first_tier = TrackerTier::create({std::move(first_url).value()});
    auto second_tier = TrackerTier::create({std::move(second_url).value()});
    REQUIRE(first_tier.has_value());
    REQUIRE(second_tier.has_value());

    auto list =
        TrackerList::create({std::move(first_tier).value(), std::move(second_tier).value()});

    REQUIRE(list.has_value());
    REQUIRE(list.value().tiers().size() == 2);
}

TEST_CASE("given_invalid_tracker_or_empty_tier_when_created_then_validation_failure_is_returned",
          "[unit][domain][tracker]")
{
    auto tracker = TrackerUrl::parse("ftp://tracker.example/announce");
    auto oversized_port =
        TrackerUrl::parse("https://tracker.example:999999999999999999999999/announce");
    auto tier = TrackerTier::create({});

    REQUIRE_FALSE(tracker.has_value());
    REQUIRE(tracker.error().issues.front().field == "tracker.url");
    REQUIRE_FALSE(oversized_port.has_value());
    REQUIRE(oversized_port.error().issues.front().field == "tracker.url");
    REQUIRE_FALSE(tier.has_value());
    REQUIRE(tier.error().issues.front().field == "tracker.tier");
}

TEST_CASE("given_metadata_values_when_created_then_validated_values_are_preserved",
          "[unit][domain][metadata]")
{
    auto seed = WebSeedUrl::parse("https://seed.example/content/");
    auto node = DhtNode::create("router.example", 6881);
    REQUIRE(seed.has_value());
    REQUIRE(node.has_value());
    TorrentMetadataInput input;
    input.comment = "说明";
    input.creator = "TorrentCraft";
    input.source = "release";
    input.creation_time_unix_seconds = 1'700'000'000;
    input.web_seeds.push_back(std::move(seed).value());
    input.collections.push_back("archive");
    input.dht_nodes.push_back(std::move(node).value());

    auto metadata = TorrentMetadata::create(std::move(input));

    REQUIRE(metadata.has_value());
    REQUIRE(metadata.value().comment() == "说明");
    REQUIRE(metadata.value().web_seeds().front().value() == "https://seed.example/content/");
    REQUIRE(metadata.value().dht_nodes().front().port() == 6881);
}

TEST_CASE("given_invalid_text_web_seed_or_dht_port_when_created_then_each_is_rejected",
          "[unit][domain][metadata]")
{
    TorrentMetadataInput invalid_text;
    invalid_text.comment = std::string("\xC3\x28", 2);
    auto metadata = TorrentMetadata::create(std::move(invalid_text));
    auto web_seed = WebSeedUrl::parse("udp://seed.example/content");
    auto node = DhtNode::create("router.example", 65536);

    REQUIRE_FALSE(metadata.has_value());
    REQUIRE(metadata.error().issues.front().field == "metadata.comment");
    REQUIRE_FALSE(web_seed.has_value());
    REQUIRE(web_seed.error().issues.front().field == "metadata.web_seed");
    REQUIRE_FALSE(node.has_value());
    REQUIRE(node.error().issues.front().field == "metadata.dht_node.port");
}

TEST_CASE("given_format_specific_piece_layouts_when_info_created_then_invariants_hold",
          "[unit][domain][info]")
{
    auto v2_hashes = InfoHashes::create(TorrentFormat::V2, std::nullopt, sha256(3));
    auto v2_pieces = PieceInfo::create(TorrentFormat::V2, kV2PieceLength, 16);
    REQUIRE(v2_hashes.has_value());
    REQUIRE(v2_pieces.has_value());
    auto v2 = TorrentInfo::create("v2", TorrentFormat::V2, std::move(v2_hashes).value(),
                                  std::move(v2_pieces).value(), {v2_file("file.bin", 16, 4)}, true);

    auto hybrid_hashes = InfoHashes::create(TorrentFormat::Hybrid, sha1(5), sha256(6));
    auto hybrid_pieces = PieceInfo::create(TorrentFormat::Hybrid, kV2PieceLength, 16, {sha1(7)});
    REQUIRE(hybrid_hashes.has_value());
    REQUIRE(hybrid_pieces.has_value());
    auto hybrid =
        TorrentInfo::create("hybrid", TorrentFormat::Hybrid, std::move(hybrid_hashes).value(),
                            std::move(hybrid_pieces).value(), {v2_file("file.bin", 16, 8)});

    REQUIRE(v2.has_value());
    REQUIRE(v2.value().is_private());
    REQUIRE(hybrid.has_value());
    REQUIRE(hybrid.value().info_hashes().v1().has_value());
    REQUIRE(hybrid.value().info_hashes().v2().has_value());
}

TEST_CASE("given_unknown_format_or_short_v2_piece_length_when_piece_info_created_then_rejected",
          "[unit][domain][info]")
{
    // Exercise the defensive enum boundary used by foreign-language and serialized inputs.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    auto unknown = PieceInfo::create(static_cast<TorrentFormat>(99), 16, 16, {sha1(1)});
    auto short_v2 = PieceInfo::create(TorrentFormat::V2, 16, 16);

    REQUIRE_FALSE(unknown.has_value());
    REQUIRE(unknown.error().issues.front().field == "info.format");
    REQUIRE_FALSE(short_v2.has_value());
    REQUIRE(short_v2.error().issues.front().field == "info.piece_length");
}

TEST_CASE("given_hybrid_padding_file_when_info_created_then_v2_root_is_not_required",
          "[unit][domain][info]")
{
    auto hashes = InfoHashes::create(TorrentFormat::Hybrid, sha1(1), sha256(2));
    auto pieces = PieceInfo::create(TorrentFormat::Hybrid, kV2PieceLength, 16, {sha1(3)});
    auto payload = FileEntry::create(path({"payload.bin"}), 8, {}, sha256(4));
    FileAttributes padding_attributes;
    padding_attributes.padding = true;
    auto padding = FileEntry::create(path({".pad", "8"}), 8, padding_attributes);

    REQUIRE(hashes.has_value());
    REQUIRE(pieces.has_value());
    REQUIRE(payload.has_value());
    REQUIRE(padding.has_value());

    auto info = TorrentInfo::create("hybrid", TorrentFormat::Hybrid, std::move(hashes).value(),
                                    std::move(pieces).value(),
                                    {std::move(payload).value(), std::move(padding).value()});

    REQUIRE(info.has_value());
    REQUIRE_FALSE(info.value().files().back().pieces_root().has_value());
}

TEST_CASE("given_duplicate_padding_paths_when_info_created_then_bep47_ignores_path_identity",
          "[unit][domain][info]")
{
    auto hashes = InfoHashes::create(TorrentFormat::V1, sha1(1), std::nullopt);
    auto pieces = PieceInfo::create(TorrentFormat::V1, 16, 2, {sha1(2)});
    FileAttributes attributes;
    attributes.padding = true;
    auto first = FileEntry::create(path({".pad", "1"}), 1, attributes);
    auto second = FileEntry::create(path({".pad", "1"}), 1, attributes);

    REQUIRE(hashes.has_value());
    REQUIRE(pieces.has_value());
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());

    auto info = TorrentInfo::create("padding", TorrentFormat::V1, std::move(hashes).value(),
                                    std::move(pieces).value(),
                                    {std::move(first).value(), std::move(second).value()});

    REQUIRE(info.has_value());
    REQUIRE(info.value().files().size() == 2);
}

TEST_CASE("given_invalid_piece_count_or_file_roots_when_info_created_then_rejected",
          "[unit][domain][info]")
{
    auto bad_pieces = PieceInfo::create(TorrentFormat::V1, 16, 32, {sha1(1)});
    REQUIRE_FALSE(bad_pieces.has_value());
    REQUIRE(bad_pieces.error().issues.front().field == "info.pieces");

    auto hashes = InfoHashes::create(TorrentFormat::V2, std::nullopt, sha256(2));
    auto pieces = PieceInfo::create(TorrentFormat::V2, kV2PieceLength, 16);
    REQUIRE(hashes.has_value());
    REQUIRE(pieces.has_value());
    auto info = TorrentInfo::create("v2", TorrentFormat::V2, std::move(hashes).value(),
                                    std::move(pieces).value(), {v1_file("missing-root.bin", 16)});

    REQUIRE_FALSE(info.has_value());
    REQUIRE(info.error().issues.front().field == "info.files");
}

TEST_CASE("given_duplicate_file_paths_when_info_created_then_rejected", "[unit][domain][info]")
{
    auto hashes = InfoHashes::create(TorrentFormat::V1, sha1(1), std::nullopt);
    auto pieces = PieceInfo::create(TorrentFormat::V1, 16, 32, {sha1(2), sha1(3)});
    REQUIRE(hashes.has_value());
    REQUIRE(pieces.has_value());
    auto info = TorrentInfo::create("duplicate", TorrentFormat::V1, std::move(hashes).value(),
                                    std::move(pieces).value(),
                                    {v1_file("same.bin", 16), v1_file("same.bin", 16)});

    REQUIRE_FALSE(info.has_value());
    REQUIRE(info.error().issues.front().field == "info.files");
}

TEST_CASE("given_document_when_metadata_and_trackers_replaced_then_original_snapshot_is_unchanged",
          "[unit][domain][document]")
{
    auto original = TorrentDocument::create(v1_info(), empty_metadata(), empty_trackers(),
                                            {{"metadata.comment", "unavailable text"}});
    REQUIRE(original.has_value());

    TorrentMetadataInput changed_input;
    changed_input.comment = "changed";
    auto changed_metadata = TorrentMetadata::create(std::move(changed_input));
    auto url = TrackerUrl::parse("https://tracker.example/announce");
    REQUIRE(changed_metadata.has_value());
    REQUIRE(url.has_value());
    auto tier = TrackerTier::create({std::move(url).value()});
    REQUIRE(tier.has_value());
    auto trackers = TrackerList::create({std::move(tier).value()});
    REQUIRE(trackers.has_value());

    auto metadata_candidate = original.value().with_metadata(std::move(changed_metadata).value());
    auto tracker_candidate = metadata_candidate.value().with_trackers(std::move(trackers).value());

    REQUIRE(metadata_candidate.has_value());
    REQUIRE(tracker_candidate.has_value());
    REQUIRE_FALSE(original.value().metadata().comment().has_value());
    REQUIRE(original.value().trackers().tiers().empty());
    REQUIRE(original.value().warnings().size() == 1);
    REQUIRE_FALSE(original.value().has_retained_extensions());
    REQUIRE(tracker_candidate.value().metadata().comment() == "changed");
    REQUIRE(tracker_candidate.value().trackers().tiers().size() == 1);
    REQUIRE(tracker_candidate.value().info().info_hashes().v1() ==
            original.value().info().info_hashes().v1());
}

TEST_CASE("given_default_create_options_when_created_then_hybrid_auto_policy_is_exposed",
          "[unit][domain][create]")
{
    auto options = CreateOptions::create();

    REQUIRE(options.has_value());
    REQUIRE(options.value().format() == TorrentFormat::Hybrid);
    REQUIRE(options.value().piece_length_strategy() == PieceLengthStrategy::Auto);
    REQUIRE(options.value().file_order_policy() == FileOrderPolicy::Lexicographical);
    REQUIRE_FALSE(options.value().fixed_piece_length().has_value());
    REQUIRE_FALSE(options.value().is_private());
    REQUIRE(options.value().trackers().tiers().empty());
    REQUIRE(options.value().web_seeds().empty());
    REQUIRE(options.value().piece_length_for(0) == 16U * 1024U);
    REQUIRE(options.value().piece_length_for(687194767ULL) == 256U * 1024U);
    REQUIRE(options.value().piece_length_for(687194768ULL) == 512U * 1024U);
    REQUIRE(options.value().piece_length_for(703687441777ULL + 1ULL) == 16U * 1024U * 1024U);
}

TEST_CASE("given_fixed_create_options_when_created_then_requested_values_are_preserved",
          "[unit][domain][create]")
{
    auto tracker = TrackerUrl::parse("https://tracker.example/announce");
    auto seed = WebSeedUrl::parse("https://seed.example/content/");
    REQUIRE(tracker.has_value());
    REQUIRE(seed.has_value());
    auto tier = TrackerTier::create({std::move(tracker).value()});
    REQUIRE(tier.has_value());

    CreateOptionsInput input;
    input.format = TorrentFormat::V1;
    input.piece_length_strategy = PieceLengthStrategy::Fixed;
    input.file_order_policy = FileOrderPolicy::Natural;
    input.fixed_piece_length = 16U * 1024U;
    input.is_private = true;
    input.tracker_tiers.push_back(std::move(tier).value());
    input.web_seeds.push_back(std::move(seed).value());
    auto options = CreateOptions::create(std::move(input));

    REQUIRE(options.has_value());
    REQUIRE(options.value().format() == TorrentFormat::V1);
    REQUIRE(options.value().piece_length_strategy() == PieceLengthStrategy::Fixed);
    REQUIRE(options.value().file_order_policy() == FileOrderPolicy::Natural);
    REQUIRE(options.value().fixed_piece_length() == 16U * 1024U);
    REQUIRE(options.value().is_private());
    REQUIRE(options.value().trackers().tiers().size() == 1);
    REQUIRE(options.value().web_seeds().size() == 1);
    REQUIRE(options.value().piece_length_for(9'999'999ULL) == 16U * 1024U);

    CreateOptionsInput maximum;
    maximum.piece_length_strategy = PieceLengthStrategy::Fixed;
    maximum.fixed_piece_length = 16U * 1024U * 1024U;
    auto maximum_options = CreateOptions::create(maximum);

    REQUIRE(maximum_options.has_value());
    REQUIRE(maximum_options.value().piece_length_for(1) == 16U * 1024U * 1024U);
}

TEST_CASE("given_invalid_create_options_when_created_then_validation_failure_is_returned",
          "[unit][domain][create]")
{
    CreateOptionsInput fixed_without_length;
    fixed_without_length.piece_length_strategy = PieceLengthStrategy::Fixed;
    auto missing_length = CreateOptions::create(fixed_without_length);

    CreateOptionsInput non_power_of_two;
    non_power_of_two.piece_length_strategy = PieceLengthStrategy::Fixed;
    non_power_of_two.fixed_piece_length = 24U * 1024U;
    auto invalid_length = CreateOptions::create(non_power_of_two);

    CreateOptionsInput automatic_with_fixed;
    automatic_with_fixed.fixed_piece_length = 16U * 1024U;
    auto redundant_length = CreateOptions::create(automatic_with_fixed);

    // Exercise defensive boundaries used by foreign-language and serialized inputs.
    CreateOptionsInput unknown_format;
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    unknown_format.format = static_cast<TorrentFormat>(99);
    auto invalid_format = CreateOptions::create(unknown_format);

    CreateOptionsInput unknown_strategy;
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    unknown_strategy.piece_length_strategy = static_cast<PieceLengthStrategy>(99);
    auto invalid_strategy = CreateOptions::create(unknown_strategy);

    CreateOptionsInput unknown_file_order_policy;
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    unknown_file_order_policy.file_order_policy = static_cast<FileOrderPolicy>(99);
    auto invalid_file_order_policy = CreateOptions::create(unknown_file_order_policy);

    REQUIRE_FALSE(missing_length.has_value());
    REQUIRE(missing_length.error().issues.front().field == "create.fixed_piece_length");
    REQUIRE_FALSE(invalid_length.has_value());
    REQUIRE(invalid_length.error().issues.front().field == "create.fixed_piece_length");
    REQUIRE_FALSE(redundant_length.has_value());
    REQUIRE(redundant_length.error().issues.front().field == "create.fixed_piece_length");
    REQUIRE_FALSE(invalid_format.has_value());
    REQUIRE(invalid_format.error().issues.front().field == "create.format");
    REQUIRE_FALSE(invalid_strategy.has_value());
    REQUIRE(invalid_strategy.error().issues.front().field == "create.piece_length_strategy");
    REQUIRE_FALSE(invalid_file_order_policy.has_value());
    REQUIRE(invalid_file_order_policy.error().issues.front().field == "create.file_order_policy");
}

TEST_CASE("given_invalid_utf8_url_when_parsed_then_public_url_types_reject_it",
          "[unit][domain][text]")
{
    const auto invalid_suffix = std::string("\xC3\x28", 2);
    auto tracker = TrackerUrl::parse("https://tracker.example/" + invalid_suffix);
    auto web_seed = WebSeedUrl::parse("https://seed.example/" + invalid_suffix);

    REQUIRE_FALSE(tracker.has_value());
    REQUIRE(tracker.error().issues.front().field == "tracker.url");
    REQUIRE_FALSE(web_seed.has_value());
    REQUIRE(web_seed.error().issues.front().field == "metadata.web_seed");
}
