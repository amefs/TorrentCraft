#include "../src/metadata_engine.hpp"
#include "../src/metadata_field_registry.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace torrentutils::core;
using namespace torrentutils::core::detail;

namespace {
template <typename Value>
[[nodiscard]] const Value& require_optional(const std::optional<Value>& value)
{
    if (!value.has_value())
    {
        throw std::logic_error("expected optional test value");
    }
    return value.value();
}

[[nodiscard]] std::vector<std::uint8_t> fixture(const std::string& name)
{
    const auto path =
        std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" / "metadata" / name;
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[nodiscard]] const MetadataFieldInfo* find_any_field(const TorrentDocument& document,
                                                      const std::string_view key)
{
    for (const auto& field : document.metadata_fields())
    {
        if (field.key == key)
        {
            return &field;
        }
    }
    return nullptr;
}

[[nodiscard]] const MetadataFieldValue*
find_field_value(const std::vector<MetadataFieldValue>& values, const std::string_view key,
                 const MetadataFieldScope scope)
{
    const auto value = std::find_if(values.begin(), values.end(), [&](const auto& candidate) {
        return candidate.key == key && candidate.scope == scope;
    });
    if (value == values.end())
    {
        return nullptr;
    }
    return &*value;
}

[[nodiscard]] std::ptrdiff_t registry_entry_count(const FieldDictionary dictionary)
{
    return std::count_if(
        kMetadataFieldRegistry.begin(), kMetadataFieldRegistry.end(),
        [&](const RegistryFieldSpec& spec) { return spec.dictionary == dictionary; });
}

[[nodiscard]] TorrentMetadata updated_metadata()
{
    TorrentMetadataInput input;
    input.comment = "updated comment";
    input.source = "SRC";
    input.creation_time_unix_seconds = 1700001234;
    input.web_seeds.push_back(WebSeedUrl::parse("https://cdn.example/content").value());
    input.collections = {"release"};
    input.dht_nodes.push_back(DhtNode::create("bootstrap.example", 49001).value());
    return TorrentMetadata::create(std::move(input)).value();
}

[[nodiscard]] std::vector<std::string> top_level_keys(const std::vector<std::uint8_t>& bytes)
{
    auto decoded = BencodeAdapter::decode(bytes);
    REQUIRE(decoded);
    std::vector<std::string> keys;
    for (const auto& entry : std::get<BencodeDictionary>(decoded.value().root.children))
    {
        keys.emplace_back(reinterpret_cast<const char*>(bytes.data() + entry.first.offset),
                          entry.first.size);
    }
    return keys;
}

[[nodiscard]] std::vector<std::uint8_t> raw_info(const std::vector<std::uint8_t>& bytes)
{
    auto decoded = BencodeAdapter::decode(bytes);
    REQUIRE(decoded);
    for (const auto& entry : std::get<BencodeDictionary>(decoded.value().root.children))
    {
        const std::string key(reinterpret_cast<const char*>(bytes.data() + entry.first.offset),
                              entry.first.size);
        if (key == "info")
        {
            const auto span = entry.second.encoded_span;
            return {bytes.begin() + static_cast<std::ptrdiff_t>(span.offset),
                    bytes.begin() + static_cast<std::ptrdiff_t>(span.offset + span.size)};
        }
    }
    return {};
}

[[nodiscard]] TorrentMetadata metadata_with_comment(const std::string& comment)
{
    TorrentMetadataInput input;
    input.comment = comment;
    return TorrentMetadata::create(std::move(input)).value();
}

[[nodiscard]] TorrentMetadata metadata_with_comment(const TorrentMetadata& original,
                                                    const std::string& comment)
{
    TorrentMetadataInput input;
    input.comment = comment;
    input.creator = original.creator();
    input.source = original.source();
    input.creation_time_unix_seconds = original.creation_time_unix_seconds();
    input.web_seeds = original.web_seeds();
    input.collections = original.collections();
    input.dht_nodes = original.dht_nodes();
    return TorrentMetadata::create(std::move(input)).value();
}

[[nodiscard]] TrackerList updated_trackers()
{
    std::vector<TrackerUrl> urls;
    urls.push_back(TrackerUrl::parse("https://new.example/announce").value());
    std::vector<TrackerTier> tiers;
    tiers.push_back(TrackerTier::create(std::move(urls)).value());
    return TrackerList::create(std::move(tiers)).value();
}
} // namespace

TEST_CASE("given_full_v1_fixture_when_decoded_then_domain_snapshot_is_complete", "[unit][metadata]")
{
    const auto bytes = fixture("valid-v1.torrent");
    auto document = decode_torrent(bytes);

    REQUIRE(document);
    const auto& value = document.value();
    REQUIRE(value.info().format() == TorrentFormat::V1);
    REQUIRE(value.info().name() == "test.bin");
    REQUIRE(value.info().is_private());
    REQUIRE(require_optional(value.info().info_hashes().v1()).to_hex() ==
            "f8baeab808eb4ddda341070f076fbb1f9fc32807");
    REQUIRE(value.info().files().size() == 1);
    REQUIRE(value.info().files().front().path().to_string() == "test.bin");
    REQUIRE(value.info().files().front().length() == 4);
    REQUIRE(value.info().pieces().piece_length() == 16384);
    REQUIRE(value.info().pieces().v1_piece_hashes().size() == 1);

    REQUIRE(value.metadata().comment() == "original comment");
    REQUIRE(value.metadata().creator() == "TorrentCraft fixture");
    REQUIRE(value.metadata().source() == "SRC");
    REQUIRE(value.metadata().creation_time_unix_seconds() == 1700000000);
    REQUIRE(value.metadata().web_seeds().size() == 2);
    REQUIRE(value.metadata().collections().size() == 2);
    REQUIRE(value.metadata().dht_nodes().size() == 2);

    REQUIRE(value.trackers().tiers().size() == 2);
    REQUIRE(value.trackers().tiers().front().trackers().size() == 2);
    REQUIRE_FALSE(value.has_retained_extensions());
    REQUIRE(value.warnings().empty());
}

TEST_CASE("given_v1_multifile_fixture_when_decoded_then_all_paths_and_lengths_are_mapped",
          "[unit][metadata]")
{
    const auto bytes = fixture("valid-v1-multifile.torrent");
    auto document = decode_torrent(bytes, MetadataReadMode::Strict);

    REQUIRE(document);
    const auto& info = document.value().info();
    REQUIRE(info.format() == TorrentFormat::V1);
    REQUIRE(info.name() == "multi-v1");
    REQUIRE(require_optional(info.info_hashes().v1()).to_hex() ==
            "6132234443d5fa3de1a37b85d47b18fe20d5fbe2");
    REQUIRE(info.pieces().total_size() == 7);
    REQUIRE(info.pieces().v1_piece_hashes().size() == 1);
    REQUIRE(info.files().size() == 2);
    REQUIRE(info.files()[0].path().to_string() == "a.bin");
    REQUIRE(info.files()[0].length() == 3);
    REQUIRE(info.files()[1].path().to_string() == "dir/b.bin");
    REQUIRE(info.files()[1].length() == 4);
}

TEST_CASE("given_valid_unicode_metadata_when_decoded_strictly_then_text_is_preserved",
          "[unit][metadata][text]")
{
    auto document =
        decode_torrent(fixture("valid-unicode-metadata.torrent"), MetadataReadMode::Strict);

    REQUIRE(document);
    REQUIRE(document.value().info().name() == "测试.bin");
    REQUIRE(document.value().metadata().comment() == "注释");
    REQUIRE(document.value().metadata().creator() == "创建者");
    REQUIRE(document.value().metadata().source() == "源");
    REQUIRE_FALSE(document.value().has_retained_extensions());
    REQUIRE(document.value().warnings().empty());
}

TEST_CASE("given_unchanged_retained_v1_document_when_encoded_then_bytes_are_identical",
          "[unit][metadata]")
{
    const auto bytes = fixture("valid-v1.torrent");
    auto document = decode_torrent(bytes);
    REQUIRE(document);

    auto encoded = encode_top_level_patch(document.value());

    REQUIRE(encoded);
    REQUIRE(encoded.value().disposition == MetadataEncodeDisposition::Encoded);
    REQUIRE(encoded.value().bytes == bytes);
}

TEST_CASE("given_safe_v1_metadata_and_tracker_edits_when_encoded_then_golden_patch_matches",
          "[unit][metadata]")
{
    auto document = decode_torrent(fixture("valid-v1.torrent"));
    REQUIRE(document);
    auto with_metadata = document.value().with_metadata(updated_metadata());
    REQUIRE(with_metadata);
    auto candidate = with_metadata.value().with_trackers(updated_trackers());
    REQUIRE(candidate);

    auto encoded = encode_top_level_patch(candidate.value());

    REQUIRE(encoded);
    REQUIRE(encoded.value().disposition == MetadataEncodeDisposition::Encoded);
    REQUIRE(encoded.value().bytes == fixture("patched-v1.torrent"));
    auto decoded = decode_torrent(encoded.value().bytes);
    REQUIRE(decoded);
    REQUIRE(require_optional(decoded.value().info().info_hashes().v1()).to_hex() ==
            "f8baeab808eb4ddda341070f076fbb1f9fc32807");
    REQUIRE(decoded.value().metadata().comment() == "updated comment");
    REQUIRE(decoded.value().trackers().tiers().front().trackers().front().value() ==
            "https://new.example/announce");
}

TEST_CASE("given_info_scoped_source_edit_when_encoded_then_need_rebuild_has_no_output",
          "[unit][metadata]")
{
    auto document = decode_torrent(fixture("valid-v1.torrent"));
    REQUIRE(document);
    TorrentMetadataInput input;
    input.source = "DIFFERENT";
    auto metadata = TorrentMetadata::create(std::move(input));
    REQUIRE(metadata);
    auto candidate = document.value().with_metadata(std::move(metadata).value());
    REQUIRE(candidate);

    auto encoded = encode_top_level_patch(candidate.value());

    REQUIRE(encoded);
    REQUIRE(encoded.value().disposition == MetadataEncodeDisposition::NeedRebuild);
    REQUIRE(encoded.value().bytes.empty());
}

TEST_CASE("given_existing_private_when_identity_patched_then_value_and_hash_are_recomputed",
          "[unit][metadata][identity-patch]")
{
    auto document = decode_torrent(fixture("valid-v1.torrent"), MetadataReadMode::Strict);
    REQUIRE(document);
    const auto original_hash = document.value().info().info_hashes().v1();

    auto patched =
        patch_info_identity(document.value(), {InfoIdentityField::Private, false, {}, false});

    std::string patch_error;
    if (!patched)
    {
        patch_error = patched.error().message;
    }
    INFO(patch_error);
    REQUIRE(patched);
    REQUIRE_FALSE(patched.value().info().is_private());
    REQUIRE(patched.value().info().info_hashes().v1() != original_hash);
    const auto patched_bytes = retained_torrent_bytes(patched.value());
    REQUIRE(patched_bytes);
    const std::string wire(reinterpret_cast<const char*>(patched_bytes->data()),
                           patched_bytes->size());
    REQUIRE(wire.find("7:privatei0e") != std::string::npos);
    REQUIRE(wire.find("7:privatei0e") == wire.rfind("7:privatei0e"));
    REQUIRE(decode_torrent(*patched_bytes, MetadataReadMode::Strict));
}

TEST_CASE("given_missing_private_when_enabled_then_sorted_seven_byte_key_is_inserted",
          "[unit][metadata][identity-patch]")
{
    auto document = decode_torrent(fixture("valid-v1-multifile.torrent"), MetadataReadMode::Strict);
    REQUIRE(document);
    REQUIRE_FALSE(document.value().info().is_private());

    auto patched =
        patch_info_identity(document.value(), {InfoIdentityField::Private, true, {}, false});

    REQUIRE(patched);
    REQUIRE(patched.value().info().is_private());
    const auto patched_bytes = retained_torrent_bytes(patched.value());
    REQUIRE(patched_bytes);
    const auto info_bytes = raw_info(*patched_bytes);
    const std::string info(reinterpret_cast<const char*>(info_bytes.data()), info_bytes.size());
    REQUIRE(info.find("7:privatei1e") != std::string::npos);
    REQUIRE(info.find("6:pieces") < info.find("7:private"));
    REQUIRE(info.find("7:private") < info.rfind('e'));
    REQUIRE(decode_torrent(*patched_bytes, MetadataReadMode::Strict));
}

TEST_CASE("given_top_level_source_when_info_identity_patched_then_source_bytes_are_retained",
          "[unit][metadata][identity-patch][fidelity]")
{
    auto document =
        decode_torrent(fixture("top-level-source-v1.torrent"), MetadataReadMode::Strict);
    REQUIRE(document);
    REQUIRE(document.value().metadata().source() == "OLD");

    auto patched =
        patch_info_identity(document.value(), {InfoIdentityField::Private, true, {}, false});

    REQUIRE(patched);
    REQUIRE(patched.value().metadata().source() == "OLD");
    const auto patched_bytes = retained_torrent_bytes(patched.value());
    REQUIRE(patched_bytes);
    const auto original_bytes = fixture("top-level-source-v1.torrent");
    REQUIRE(patched_bytes->size() == original_bytes.size() + 12U);
    const std::string source_marker = "3:OLD";
    REQUIRE(std::search(patched_bytes->begin(), patched_bytes->end(), source_marker.begin(),
                        source_marker.end()) != patched_bytes->end());
    REQUIRE(decode_torrent(*patched_bytes, MetadataReadMode::Strict));
}

TEST_CASE("given_top_level_source_when_info_source_is_explicitly_patched_then_both_are_retained",
          "[unit][metadata][identity-patch][fidelity]")
{
    auto document =
        decode_torrent(fixture("top-level-source-v1.torrent"), MetadataReadMode::Strict);
    REQUIRE(document);
    REQUIRE_FALSE(source_is_in_info(document.value()));

    auto patched = patch_info_identity(
        document.value(), {InfoIdentityField::Source, false, "client-extension", false});

    REQUIRE(patched);
    REQUIRE(source_is_in_info(patched.value()));
    REQUIRE(patched.value().metadata().source() == "client-extension");
    const auto patched_bytes = retained_torrent_bytes(patched.value());
    REQUIRE(patched_bytes);
    const std::string top_level_marker = "3:OLD";
    const std::string info_marker = "6:source16:client-extension";
    REQUIRE(std::search(patched_bytes->begin(), patched_bytes->end(), top_level_marker.begin(),
                        top_level_marker.end()) != patched_bytes->end());
    REQUIRE(std::search(patched_bytes->begin(), patched_bytes->end(), info_marker.begin(),
                        info_marker.end()) != patched_bytes->end());
    REQUIRE(decode_torrent(*patched_bytes, MetadataReadMode::Strict));
}

TEST_CASE("given_v2_or_hybrid_private_patch_when_reparsed_then_hashes_change_and_payload_survives",
          "[unit][metadata][identity-patch]")
{
    for (const auto& name : {std::string("valid-v2.torrent"), std::string("valid-hybrid.torrent")})
    {
        INFO(name);
        auto document = decode_torrent(fixture(name), MetadataReadMode::Strict);
        REQUIRE(document);
        const auto original_v1 = document.value().info().info_hashes().v1();
        const auto original_v2 = document.value().info().info_hashes().v2();
        const auto original_files = document.value().info().files();

        auto patched = patch_info_identity(
            document.value(),
            {InfoIdentityField::Private, !document.value().info().is_private(), {}, false});

        REQUIRE(patched);
        REQUIRE(patched.value().info().info_hashes().v1().has_value() == original_v1.has_value());
        if (original_v1)
        {
            REQUIRE(patched.value().info().info_hashes().v1() != original_v1);
        }
        REQUIRE(patched.value().info().info_hashes().v2() != original_v2);
        REQUIRE(patched.value().info().files().size() == original_files.size());
        for (std::size_t index = 0; index < original_files.size(); ++index)
        {
            REQUIRE(patched.value().info().files()[index].path() == original_files[index].path());
            REQUIRE(patched.value().info().files()[index].pieces_root() ==
                    original_files[index].pieces_root());
        }
        const auto patched_bytes = retained_torrent_bytes(patched.value());
        REQUIRE(patched_bytes);
        REQUIRE(decode_torrent(*patched_bytes, MetadataReadMode::Strict));
    }
}

TEST_CASE(
    "given_name_patch_when_decoded_then_single_file_path_follows_name_and_multifile_paths_do_not",
    "[unit][metadata][identity-patch]")
{
    auto single = decode_torrent(fixture("valid-v1.torrent"), MetadataReadMode::Strict);
    REQUIRE(single);
    auto renamed =
        patch_info_identity(single.value(), {InfoIdentityField::Name, false, "renamed.bin", false});
    std::string rename_error;
    if (!renamed)
    {
        rename_error = renamed.error().message;
    }
    INFO(rename_error);
    REQUIRE(renamed);
    REQUIRE(renamed.value().info().name() == "renamed.bin");
    REQUIRE(renamed.value().info().files().front().path().to_string() == "renamed.bin");

    auto multi = decode_torrent(fixture("valid-v1-multifile.torrent"), MetadataReadMode::Strict);
    REQUIRE(multi);
    const auto original_paths = multi.value().info().files();
    auto renamed_multi =
        patch_info_identity(multi.value(), {InfoIdentityField::Name, false, "renamed-root", false});
    REQUIRE(renamed_multi);
    REQUIRE(renamed_multi.value().info().name() == "renamed-root");
    REQUIRE(renamed_multi.value().info().files().size() == original_paths.size());
    for (std::size_t index = 0; index < original_paths.size(); ++index)
    {
        REQUIRE(renamed_multi.value().info().files()[index].path() == original_paths[index].path());
    }
}

TEST_CASE("given_duplicate_dictionary_key_when_decoded_then_invalid_bencode", "[unit][metadata]")
{
    const std::string data =
        "d4:infod4:name1:a4:name1:b6:lengthi1e12:piece lengthi1e6:pieces20:12345678901234567890ee";
    auto document = decode_torrent({data.begin(), data.end()});
    REQUIRE_FALSE(document);
    REQUIRE(document.error().code == ErrorCode::InvalidBencode);
}

TEST_CASE("given_unretained_document_when_encoded_then_need_rebuild", "[unit][metadata]")
{
    auto metadata = TorrentMetadata::create();
    auto trackers = TrackerList::create({});
    auto hashes = InfoHashes::create(TorrentFormat::V1, Sha1Digest::from_bytes({}), std::nullopt);
    auto pieces = PieceInfo::create(TorrentFormat::V1, std::uint64_t{16U} * 1024U, 0, {});
    auto path = LogicalPath::from_segments({"file"});
    auto file = FileEntry::create(std::move(path).value(), 0);
    auto info = TorrentInfo::create("file", TorrentFormat::V1, std::move(hashes).value(),
                                    std::move(pieces).value(), {std::move(file).value()});
    auto document = TorrentDocument::create(std::move(info).value(), std::move(metadata).value(),
                                            std::move(trackers).value());
    REQUIRE(document.value().metadata_field_values().empty());
    auto encoded = encode_top_level_patch(document.value());
    REQUIRE(encoded);
    REQUIRE(encoded.value().disposition == MetadataEncodeDisposition::NeedRebuild);
    REQUIRE(encoded.value().bytes.empty());
}

TEST_CASE("given_v2_fixture_with_piece_layer_when_decoded_then_hash_and_file_tree_are_validated",
          "[unit][metadata]")
{
    const auto bytes = fixture("valid-v2.torrent");
    auto document = decode_torrent(bytes, MetadataReadMode::Strict);

    REQUIRE(document);
    const auto& value = document.value();
    REQUIRE(value.info().format() == TorrentFormat::V2);
    REQUIRE_FALSE(value.info().info_hashes().v1());
    REQUIRE(require_optional(value.info().info_hashes().v2()).to_hex() ==
            "44a661ac3079f4a8478e9175e44ca3df74c2cbe1ed4a1fac2750d4eed00f48e5");
    REQUIRE(value.info().pieces().piece_length() == 16384);
    REQUIRE(value.info().pieces().total_size() == 20000);
    REQUIRE(value.info().pieces().v1_piece_hashes().empty());
    REQUIRE(value.info().files().size() == 1);
    REQUIRE(value.info().files().front().path().to_string() == "large.bin");
    REQUIRE(require_optional(value.info().files().front().pieces_root()).to_hex() ==
            "a684d57c62ca0d689198aaf1f35cf63bbebc6aff3e75ac7c5c63fe72a9814413");
    REQUIRE(value.has_retained_extensions());

    auto encoded = encode_top_level_patch(value);
    REQUIRE(encoded);
    REQUIRE(encoded.value().disposition == MetadataEncodeDisposition::Encoded);
    REQUIRE(encoded.value().bytes == bytes);
}

TEST_CASE("given_v2_multifile_fixture_when_decoded_then_file_tree_paths_are_mapped",
          "[unit][metadata]")
{
    const auto bytes = fixture("valid-v2-multifile.torrent");
    auto document = decode_torrent(bytes, MetadataReadMode::Strict);

    REQUIRE(document);
    const auto& info = document.value().info();
    REQUIRE(info.format() == TorrentFormat::V2);
    REQUIRE(info.name() == "multi-v2");
    REQUIRE(require_optional(info.info_hashes().v2()).to_hex() ==
            "0b0c2d2d48eac569fa628aa2f22d060489c5e1bf4952b9261e9c169fc665611f");
    REQUIRE(info.pieces().total_size() == 10);
    REQUIRE(info.files().size() == 2);
    REQUIRE(info.files()[0].path().to_string() == "a.bin");
    REQUIRE(info.files()[0].length() == 4);
    REQUIRE(require_optional(info.files()[0].pieces_root()).to_hex() ==
            "1111111111111111111111111111111111111111111111111111111111111111");
    REQUIRE(info.files()[1].path().to_string() == "dir/b.bin");
    REQUIRE(info.files()[1].length() == 6);
    REQUIRE(require_optional(info.files()[1].pieces_root()).to_hex() ==
            "2222222222222222222222222222222222222222222222222222222222222222");
}

TEST_CASE("given_bep47_v2_symlink_when_read_then_link_identity_is_exposed_without_piece_payload",
          "[unit][metadata][attributes]")
{
    const auto bytes = fixture("valid-bep47-v2.torrent");
    const auto expected_sha1 =
        Sha1Digest::from_hex("5353535353535353535353535353535353535353").value();

    for (const auto mode : {MetadataReadMode::Lenient, MetadataReadMode::Strict})
    {
        auto document = decode_torrent(bytes, mode);

        REQUIRE(document);
        REQUIRE(document.value().warnings().empty());
        const auto& info = document.value().info();
        REQUIRE(info.format() == TorrentFormat::V2);
        REQUIRE(info.pieces().total_size() == 1);
        REQUIRE(info.files().size() == 2);
        const auto& link = info.files().back();
        REQUIRE(link.path().to_string() == "links/manual");
        REQUIRE(link.length() == 0);
        REQUIRE(link.attributes().hidden);
        REQUIRE(link.attributes().symlink);
        REQUIRE_FALSE(link.pieces_root().has_value());
        REQUIRE(link.sha1_hint() == expected_sha1);
        REQUIRE(link.symlink_target().has_value());
        REQUIRE(require_optional(link.symlink_target()).segments() ==
                std::vector<std::string>{"docs", "manual.txt"});
    }
}

TEST_CASE("given_bep47_single_file_attributes_when_read_then_x_and_h_are_exposed",
          "[unit][metadata][attributes]")
{
    const auto bytes = fixture("valid-bep47-single-file-attributes.torrent");

    for (const auto mode : {MetadataReadMode::Lenient, MetadataReadMode::Strict})
    {
        auto document = decode_torrent(bytes, mode);

        REQUIRE(document);
        REQUIRE(document.value().warnings().empty());
        const auto& file = document.value().info().files().front();
        REQUIRE(file.path().to_string() == "a.bin");
        REQUIRE(file.length() == 1);
        REQUIRE(file.attributes().executable);
        REQUIRE(file.attributes().hidden);
        REQUIRE_FALSE(file.attributes().padding);
        REQUIRE_FALSE(file.attributes().symlink);
    }
}

TEST_CASE("given_bep47_padding_paths_when_read_then_path_is_not_required_or_unique",
          "[unit][metadata][attributes]")
{
    struct Case
    {
        const char* fixture_name;
        std::size_t file_count;
    };
    const std::vector<Case> cases{
        {"valid-bep47-padding-without-path.torrent", 1},
        {"valid-bep47-duplicate-padding-paths.torrent", 2},
    };

    for (const auto& test_case : cases)
    {
        INFO(test_case.fixture_name);
        const auto bytes = fixture(test_case.fixture_name);
        for (const auto mode : {MetadataReadMode::Lenient, MetadataReadMode::Strict})
        {
            auto document = decode_torrent(bytes, mode);

            REQUIRE(document);
            REQUIRE(document.value().info().files().size() == test_case.file_count);
            for (const auto& file : document.value().info().files())
            {
                REQUIRE(file.attributes().padding);
                REQUIRE(file.path().to_string() == ".pad/1");
                REQUIRE(file.length() == 1);
            }
            auto encoded = encode_top_level_patch(document.value());
            REQUIRE(encoded);
            REQUIRE(encoded.value().bytes == bytes);
        }
    }
}

TEST_CASE("given_bep47_symlink_without_length_when_read_then_all_formats_imply_zero_length",
          "[unit][metadata][attributes]")
{
    struct Case
    {
        const char* fixture_name;
        TorrentFormat format;
        const char* link_path;
        std::uint64_t payload_size;
    };
    const std::vector<Case> cases{
        {"valid-bep47-v1-symlink-without-length.torrent", TorrentFormat::V1, "link", 1},
        {"valid-bep47-v2-symlink-without-length.torrent", TorrentFormat::V2, "links/manual", 1},
        {"valid-bep47-hybrid-symlink-without-length.torrent", TorrentFormat::Hybrid, "link-to-a",
         16484},
    };

    for (const auto& test_case : cases)
    {
        INFO(test_case.fixture_name);
        const auto bytes = fixture(test_case.fixture_name);
        for (const auto mode : {MetadataReadMode::Lenient, MetadataReadMode::Strict})
        {
            auto document = decode_torrent(bytes, mode);

            REQUIRE(document);
            const auto& info = document.value().info();
            REQUIRE(info.format() == test_case.format);
            REQUIRE(info.pieces().total_size() == test_case.payload_size);
            const auto link =
                std::find_if(info.files().begin(), info.files().end(),
                             [](const FileEntry& file) { return file.attributes().symlink; });
            REQUIRE(link != info.files().end());
            REQUIRE(link->path().to_string() == test_case.link_path);
            REQUIRE(link->length() == 0);
            REQUIRE(link->symlink_target().has_value());
            auto encoded = encode_top_level_patch(document.value());
            REQUIRE(encoded);
            REQUIRE(encoded.value().bytes == bytes);
        }
    }
}

TEST_CASE("given_v2_piece_layer_with_wrong_merkle_root_when_decoded_then_invalid_torrent",
          "[unit][metadata]")
{
    auto document = decode_torrent(fixture("invalid-v2-piece-layer.torrent"));

    REQUIRE_FALSE(document);
    REQUIRE(document.error().code == ErrorCode::InvalidTorrent);
}

TEST_CASE("given_hybrid_fixture_when_decoded_then_both_hashes_and_padding_layout_are_validated",
          "[unit][metadata]")
{
    const auto bytes = fixture("valid-hybrid.torrent");
    auto document = decode_torrent(bytes, MetadataReadMode::Strict);

    REQUIRE(document);
    const auto& info = document.value().info();
    REQUIRE(info.format() == TorrentFormat::Hybrid);
    REQUIRE(require_optional(info.info_hashes().v1()).to_hex() ==
            "8e50ec8648f10b05744e2bd10acad2ccb97dc2dd");
    REQUIRE(require_optional(info.info_hashes().v2()).to_hex() ==
            "c9807f71c38ded171f6b938e2dfdf9f11a76ba727dc94f40bc03b20461294840");
    REQUIRE(info.pieces().piece_length() == 16384);
    REQUIRE(info.pieces().total_size() == 16484);
    REQUIRE(info.pieces().v1_piece_hashes().size() == 2);
    REQUIRE(info.files().size() == 3);
    REQUIRE(info.files()[0].path().to_string() == "a.bin");
    REQUIRE(info.files()[0].length() == 10000);
    REQUIRE(require_optional(info.files()[0].pieces_root()).to_hex() ==
            "2121212121212121212121212121212121212121212121212121212121212121");
    REQUIRE(info.files()[1].path().to_string() == ".pad/6384");
    REQUIRE(info.files()[1].length() == 6384);
    REQUIRE(info.files()[1].attributes().padding);
    REQUIRE_FALSE(info.files()[1].pieces_root());
    REQUIRE(info.files()[2].path().to_string() == "b.bin");
    REQUIRE(info.files()[2].length() == 100);
    REQUIRE(require_optional(info.files()[2].pieces_root()).to_hex() ==
            "2222222222222222222222222222222222222222222222222222222222222222");

    auto encoded = encode_top_level_patch(document.value());
    REQUIRE(encoded);
    REQUIRE(encoded.value().disposition == MetadataEncodeDisposition::Encoded);
    REQUIRE(encoded.value().bytes == bytes);
}

TEST_CASE("given_unknown_extensions_when_read_leniently_then_opaque_bytes_are_retained",
          "[unit][metadata]")
{
    const auto bytes = fixture("unknown-extensions.torrent");
    auto document = decode_torrent(bytes, MetadataReadMode::Lenient);

    REQUIRE(document);
    REQUIRE(document.value().has_retained_extensions());
    REQUIRE(document.value().warnings().size() == 2);
    auto encoded = encode_top_level_patch(document.value());
    REQUIRE(encoded);
    REQUIRE(encoded.value().bytes == bytes);
}

TEST_CASE("given_unknown_file_attribute_when_read_then_both_modes_ignore_and_retain_it",
          "[unit][metadata][attributes]")
{
    const auto bytes = fixture("unknown-file-attribute.torrent");

    for (const auto mode : {MetadataReadMode::Lenient, MetadataReadMode::Strict})
    {
        auto document = decode_torrent(bytes, mode);

        REQUIRE(document);
        REQUIRE(document.value().has_retained_extensions());
        REQUIRE(document.value().warnings().size() == 1);
        REQUIRE(document.value().warnings().front().field == "info.files.attr");
        const auto& attributes = document.value().info().files().front().attributes();
        REQUIRE_FALSE(attributes.padding);
        REQUIRE_FALSE(attributes.executable);
        REQUIRE_FALSE(attributes.hidden);
        REQUIRE_FALSE(attributes.symlink);
        auto encoded = encode_top_level_patch(document.value());
        REQUIRE(encoded);
        REQUIRE(encoded.value().bytes == bytes);
    }
}

TEST_CASE("given_bep47_symlink_target_when_read_then_both_modes_expose_link_identity",
          "[unit][metadata][attributes]")
{
    const auto bytes = fixture("valid-bep47-symlink.torrent");
    const auto expected_sha1 =
        Sha1Digest::from_hex("5353535353535353535353535353535353535353").value();

    for (const auto mode : {MetadataReadMode::Lenient, MetadataReadMode::Strict})
    {
        auto document = decode_torrent(bytes, mode);

        REQUIRE(document);
        REQUIRE(document.value().warnings().empty());
        const auto& file = document.value().info().files().front();
        REQUIRE(file.attributes().hidden);
        REQUIRE(file.attributes().symlink);
        REQUIRE(file.length() == 0);
        REQUIRE(file.sha1_hint() == expected_sha1);
        REQUIRE(file.symlink_target().has_value());
        REQUIRE(require_optional(file.symlink_target()).segments() ==
                std::vector<std::string>{"docs", "manual.txt"});
    }
}

TEST_CASE("given_x_extension_fields_when_read_strictly_then_retained_not_rejected",
          "[unit][metadata]")
{
    const auto bytes = fixture("unknown-extensions.torrent");
    auto document = decode_torrent(bytes, MetadataReadMode::Strict);

    REQUIRE(document);
    REQUIRE(document.value().has_retained_extensions());
    REQUIRE(document.value().warnings().size() == 2);
    auto encoded = encode_top_level_patch(document.value());
    REQUIRE(encoded);
    REQUIRE(encoded.value().bytes == bytes);
}

TEST_CASE("given_bep38_and_bep39_extension_fields_when_read_then_retained_in_both_modes",
          "[unit][metadata]")
{
    const auto bytes = fixture("similar-extension.torrent");
    for (const auto mode : {MetadataReadMode::Lenient, MetadataReadMode::Strict})
    {
        auto document = decode_torrent(bytes, mode);

        REQUIRE(document);
        REQUIRE(document.value().has_retained_extensions());
        REQUIRE(document.value().warnings().size() == 3);
        auto encoded = encode_top_level_patch(document.value());
        REQUIRE(encoded);
        REQUIRE(encoded.value().bytes == bytes);
    }
}

TEST_CASE("given_unknown_field_when_read_leniently_then_retained_and_strictly_rejected",
          "[unit][metadata]")
{
    const auto bytes = fixture("unknown-field.torrent");
    auto lenient = decode_torrent(bytes, MetadataReadMode::Lenient);

    REQUIRE(lenient);
    REQUIRE(lenient.value().has_retained_extensions());
    REQUIRE(lenient.value().warnings().size() == 2);

    auto strict = decode_torrent(bytes, MetadataReadMode::Strict);
    REQUIRE_FALSE(strict);
    REQUIRE(strict.error().code == ErrorCode::UnsupportedFeature);
}

TEST_CASE("given_invalid_utf8_comment_when_read_leniently_then_raw_value_is_retained",
          "[unit][metadata]")
{
    const auto bytes = fixture("invalid-utf8-comment.torrent");
    auto document = decode_torrent(bytes, MetadataReadMode::Lenient);

    REQUIRE(document);
    REQUIRE_FALSE(document.value().metadata().comment());
    REQUIRE(document.value().has_retained_extensions());
    REQUIRE(document.value().warnings().size() == 1);
    REQUIRE(document.value().warnings().front().field == "comment");
    auto encoded = encode_top_level_patch(document.value());
    REQUIRE(encoded);
    REQUIRE(encoded.value().bytes == bytes);
}

TEST_CASE("given_invalid_utf8_comment_when_read_strictly_then_invalid_torrent", "[unit][metadata]")
{
    auto document =
        decode_torrent(fixture("invalid-utf8-comment.torrent"), MetadataReadMode::Strict);

    REQUIRE_FALSE(document);
    REQUIRE(document.error().code == ErrorCode::InvalidTorrent);
}

TEST_CASE("given_invalid_utf8_optional_text_when_read_then_mode_controls_raw_retention",
          "[unit][metadata][text]")
{
    struct InvalidTextCase
    {
        std::string fixture_name;
        std::string field;
    };
    const std::vector<InvalidTextCase> cases{{"invalid-utf8-creator.torrent", "created by"},
                                             {"invalid-utf8-source.torrent", "source"}};

    for (const auto& test_case : cases)
    {
        INFO(test_case.fixture_name);
        const auto bytes = fixture(test_case.fixture_name);
        auto lenient = decode_torrent(bytes, MetadataReadMode::Lenient);
        REQUIRE(lenient);
        REQUIRE(lenient.value().has_retained_extensions());
        REQUIRE(lenient.value().warnings().size() == 1);
        REQUIRE(lenient.value().warnings().front().field == test_case.field);
        if (test_case.field == "created by")
        {
            REQUIRE_FALSE(lenient.value().metadata().creator());
        }
        else
        {
            REQUIRE_FALSE(lenient.value().metadata().source());
        }
        auto encoded = encode_top_level_patch(lenient.value());
        REQUIRE(encoded);
        REQUIRE(encoded.value().bytes == bytes);

        auto strict = decode_torrent(bytes, MetadataReadMode::Strict);
        REQUIRE_FALSE(strict);
        REQUIRE(strict.error().code == ErrorCode::InvalidTorrent);
    }
}

TEST_CASE("given_invalid_utf8_info_name_when_read_leniently_then_public_name_is_unavailable",
          "[unit][metadata]")
{
    const auto bytes = fixture("invalid-utf8-name.torrent");
    auto document = decode_torrent(bytes, MetadataReadMode::Lenient);

    REQUIRE(document);
    REQUIRE(document.value().info().name().empty());
    REQUIRE(document.value().info().files().front().path().segments().empty());
    REQUIRE(document.value().has_retained_extensions());
    REQUIRE(document.value().warnings().size() == 1);
    REQUIRE(document.value().warnings().front().field == "info.name");
    auto encoded = encode_top_level_patch(document.value());
    REQUIRE(encoded);
    REQUIRE(encoded.value().bytes == bytes);
}

TEST_CASE("given_invalid_utf8_info_name_when_read_strictly_then_invalid_torrent",
          "[unit][metadata]")
{
    auto document = decode_torrent(fixture("invalid-utf8-name.torrent"), MetadataReadMode::Strict);

    REQUIRE_FALSE(document);
    REQUIRE(document.error().code == ErrorCode::InvalidTorrent);
}

TEST_CASE("given_malformed_bencode_when_decoded_in_any_mode_then_invalid_bencode",
          "[unit][metadata][bencode]")
{
    const std::vector<std::string> malformed{"malformed-invalid-prefix.bencode",
                                             "malformed-truncated-dictionary.bencode",
                                             "malformed-leading-zero-integer.bencode",
                                             "malformed-negative-zero-integer.bencode",
                                             "malformed-positive-integer-overflow.bencode",
                                             "malformed-negative-integer-overflow.bencode",
                                             "malformed-leading-zero-string-length.bencode",
                                             "malformed-string-length-overflow.bencode",
                                             "malformed-trailing-data.bencode",
                                             "duplicate-top-level-key.bencode",
                                             "duplicate-info-key.bencode",
                                             "duplicate-nested-key.bencode"};
    for (const auto& name : malformed)
    {
        INFO("fixture: " << name);
        for (const auto mode : {MetadataReadMode::Lenient, MetadataReadMode::Strict})
        {
            auto document = decode_torrent(fixture(name), mode);
            REQUIRE_FALSE(document);
            REQUIRE(document.error().code == ErrorCode::InvalidBencode);
        }
    }
}

TEST_CASE("given_duplicate_non_utf8_dictionary_key_when_decoded_then_invalid_bencode",
          "[unit][metadata][bencode]")
{
    const auto bytes = fixture("duplicate-non-utf8-key.bencode");
    for (const auto mode : {MetadataReadMode::Lenient, MetadataReadMode::Strict})
    {
        auto document = decode_torrent(bytes, mode);
        REQUIRE_FALSE(document);
        REQUIRE(document.error().code == ErrorCode::InvalidBencode);
    }
}

TEST_CASE("given_bencode_resource_limit_exceeded_when_decoded_then_invalid_bencode",
          "[unit][metadata][bencode]")
{
    struct Case
    {
        std::string fixture_name;
        BencodeLimits limits;
    };
    std::vector<Case> cases;
    {
        BencodeLimits limits;
        limits.max_input_bytes = 1;
        cases.push_back({"limit-input.bencode", limits});
    }
    {
        BencodeLimits limits;
        limits.max_string_bytes = 1;
        cases.push_back({"limit-string.bencode", limits});
    }
    {
        BencodeLimits limits;
        limits.max_depth = 0;
        cases.push_back({"limit-depth.bencode", limits});
    }
    {
        BencodeLimits limits;
        limits.max_tokens = 1;
        cases.push_back({"limit-tokens.bencode", limits});
    }
    {
        BencodeLimits limits;
        limits.max_container_entries = 1;
        cases.push_back({"limit-list-entries.bencode", limits});
    }
    {
        BencodeLimits limits;
        limits.max_container_entries = 1;
        cases.push_back({"limit-dictionary-entries.bencode", limits});
    }
    {
        BencodeLimits limits;
        limits.max_integer_digits = 2;
        cases.push_back({"limit-integer-digits.bencode", limits});
    }

    for (const auto& test_case : cases)
    {
        INFO("fixture: " << test_case.fixture_name);
        for (const auto mode : {MetadataReadMode::Lenient, MetadataReadMode::Strict})
        {
            auto document = decode_torrent(fixture(test_case.fixture_name), mode, test_case.limits);
            REQUIRE_FALSE(document);
            REQUIRE(document.error().code == ErrorCode::InvalidBencode);
        }
    }
}

TEST_CASE("given_bencode_value_exactly_at_each_resource_limit_when_decoded_then_it_succeeds",
          "[unit][metadata][bencode]")
{
    struct Case
    {
        std::string fixture_name;
        BencodeLimits limits;
    };
    std::vector<Case> cases;
    {
        BencodeLimits limits;
        limits.max_input_bytes = fixture("limit-input.bencode").size();
        cases.push_back({"limit-input.bencode", limits});
    }
    {
        BencodeLimits limits;
        limits.max_string_bytes = 2;
        cases.push_back({"limit-string.bencode", limits});
    }
    {
        BencodeLimits limits;
        limits.max_depth = 1;
        cases.push_back({"limit-depth.bencode", limits});
    }
    {
        BencodeLimits limits;
        limits.max_tokens = 2;
        cases.push_back({"limit-tokens.bencode", limits});
    }
    {
        BencodeLimits limits;
        limits.max_container_entries = 2;
        cases.push_back({"limit-list-entries.bencode", limits});
    }
    {
        BencodeLimits limits;
        limits.max_container_entries = 2;
        cases.push_back({"limit-dictionary-entries.bencode", limits});
    }
    {
        BencodeLimits limits;
        limits.max_integer_digits = 3;
        cases.push_back({"limit-integer-digits.bencode", limits});
    }

    for (const auto& test_case : cases)
    {
        INFO("fixture: " << test_case.fixture_name);
        auto decoded = BencodeAdapter::decode(fixture(test_case.fixture_name), test_case.limits);
        REQUIRE(decoded);
    }
}

TEST_CASE("given_unsorted_unique_dictionary_when_bencode_decoded_then_input_order_is_retained",
          "[unit][metadata][bencode]")
{
    auto decoded = BencodeAdapter::decode(fixture("unsorted-dictionary.bencode"));

    REQUIRE(decoded);
    const auto& entries = std::get<BencodeDictionary>(decoded.value().root.children);
    REQUIRE(entries.size() == 2);
    const auto& bytes = *decoded.value().bytes;
    const auto first =
        std::string(reinterpret_cast<const char*>(bytes.data() + entries[0].first.offset),
                    entries[0].first.size);
    const auto second =
        std::string(reinterpret_cast<const char*>(bytes.data() + entries[1].first.offset),
                    entries[1].first.size);
    REQUIRE(first == "b");
    REQUIRE(second == "a");
}

TEST_CASE(
    "given_retained_unknown_fields_when_safe_patch_encoded_then_unknown_and_info_bytes_survive",
    "[unit][metadata][fidelity]")
{
    const auto original = fixture("unknown-extensions.torrent");
    auto document = decode_torrent(original, MetadataReadMode::Lenient);
    REQUIRE(document);
    auto candidate = document.value().with_metadata(metadata_with_comment("added"));
    REQUIRE(candidate);

    auto encoded = encode_top_level_patch(candidate.value());

    REQUIRE(encoded);
    REQUIRE(encoded.value().disposition == MetadataEncodeDisposition::Encoded);
    REQUIRE(raw_info(encoded.value().bytes) == raw_info(original));
    auto keys = top_level_keys(encoded.value().bytes);
    REQUIRE(std::is_sorted(keys.begin(), keys.end()));
    auto decoded = decode_torrent(encoded.value().bytes, MetadataReadMode::Lenient);
    REQUIRE(decoded);
    REQUIRE(decoded.value().has_retained_extensions());
    REQUIRE(decoded.value().metadata().comment() == "added");
}

TEST_CASE(
    "given_invalid_utf8_retained_field_when_unrelated_patch_encoded_then_invalid_bytes_survive",
    "[unit][metadata][fidelity]")
{
    const auto original = fixture("invalid-utf8-comment.torrent");
    auto document = decode_torrent(original, MetadataReadMode::Lenient);
    REQUIRE(document);
    TorrentMetadataInput input;
    input.creator = "editor";
    auto metadata = TorrentMetadata::create(std::move(input));
    REQUIRE(metadata);
    auto candidate = document.value().with_metadata(std::move(metadata).value());
    REQUIRE(candidate);

    auto encoded = encode_top_level_patch(candidate.value());

    REQUIRE(encoded);
    const std::vector<std::uint8_t> marker{'4', ':', 'b', 'a', 'd', 0xffU};
    REQUIRE(std::search(encoded.value().bytes.begin(), encoded.value().bytes.end(), marker.begin(),
                        marker.end()) != encoded.value().bytes.end());
    REQUIRE(raw_info(encoded.value().bytes) == raw_info(original));
    auto decoded = decode_torrent(encoded.value().bytes, MetadataReadMode::Lenient);
    REQUIRE(decoded);
    REQUIRE_FALSE(decoded.value().metadata().comment());
    REQUIRE(decoded.value().metadata().creator() == "editor");
}

TEST_CASE(
    "given_v2_or_hybrid_document_when_top_level_patch_encoded_then_raw_info_and_hashes_survive",
    "[unit][metadata][fidelity]")
{
    for (const auto& name : {std::string("valid-v2.torrent"), std::string("valid-hybrid.torrent")})
    {
        INFO(name);
        const auto original = fixture(name);
        auto document = decode_torrent(original, MetadataReadMode::Strict);
        REQUIRE(document);
        const auto v1 = document.value().info().info_hashes().v1();
        const auto v2 = document.value().info().info_hashes().v2();
        auto candidate = document.value().with_metadata(
            metadata_with_comment(document.value().metadata(), "patched"));
        REQUIRE(candidate);

        auto encoded = encode_top_level_patch(candidate.value());

        REQUIRE(encoded);
        REQUIRE(encoded.value().disposition == MetadataEncodeDisposition::Encoded);
        REQUIRE(raw_info(encoded.value().bytes) == raw_info(original));
        auto decoded = decode_torrent(encoded.value().bytes, MetadataReadMode::Strict);
        REQUIRE(decoded);
        REQUIRE(decoded.value().info().info_hashes().v1() == v1);
        REQUIRE(decoded.value().info().info_hashes().v2() == v2);
    }
}

TEST_CASE("given_top_level_source_when_source_changed_then_golden_patch_is_safe",
          "[unit][metadata][fidelity]")
{
    const auto original = fixture("top-level-source-v1.torrent");
    auto document = decode_torrent(original, MetadataReadMode::Strict);
    REQUIRE(document);
    TorrentMetadataInput input;
    input.source = "NEW";
    auto metadata = TorrentMetadata::create(std::move(input));
    REQUIRE(metadata);
    auto candidate = document.value().with_metadata(std::move(metadata).value());
    REQUIRE(candidate);

    auto encoded = encode_top_level_patch(candidate.value());

    REQUIRE(encoded);
    REQUIRE(encoded.value().disposition == MetadataEncodeDisposition::NeedRebuild);
    REQUIRE(encoded.value().bytes.empty());
}

TEST_CASE("given_recognized_torrent_field_with_wrong_type_when_decoded_then_invalid_torrent",
          "[unit][metadata]")
{
    const std::string info =
        "4:infod6:lengthi1e4:name1:x12:piece lengthi16384e6:pieces20:DDDDDDDDDDDDDDDDDDDDe";
    const std::vector<std::string> inputs{
        "d7:commenti1e" + info + "e", "d" + info + "8:announcei1ee",
        "d4:infod6:lengthi1e4:namei1e12:piece lengthi16384e6:pieces20:DDDDDDDDDDDDDDDDDDDDee"};
    for (const auto& input : inputs)
    {
        auto document = decode_torrent({input.begin(), input.end()}, MetadataReadMode::Lenient);
        REQUIRE_FALSE(document);
        REQUIRE(document.error().code == ErrorCode::InvalidTorrent);
    }
}

TEST_CASE("given_invalid_file_layout_when_decoded_in_any_mode_then_invalid_torrent",
          "[unit][metadata][layout]")
{
    for (const auto& name :
         {std::string("invalid-symlink-length.torrent"), std::string("invalid-path.torrent")})
    {
        INFO(name);
        for (const auto mode : {MetadataReadMode::Lenient, MetadataReadMode::Strict})
        {
            auto document = decode_torrent(fixture(name), mode);
            REQUIRE_FALSE(document);
            REQUIRE(document.error().code == ErrorCode::InvalidTorrent);
        }
    }
}

TEST_CASE("given_hybrid_with_invalid_padding_when_decoded_then_invalid_torrent", "[unit][metadata]")
{
    auto bytes = fixture("valid-hybrid.torrent");
    const std::string from = "i6384e";
    const std::string to = "i6383e";
    const auto position = std::search(bytes.begin(), bytes.end(), from.begin(), from.end());
    REQUIRE(position != bytes.end());
    std::copy(to.begin(), to.end(), position);

    auto document = decode_torrent(std::move(bytes), MetadataReadMode::Strict);

    REQUIRE_FALSE(document);
    REQUIRE(document.error().code == ErrorCode::InvalidTorrent);
}

TEST_CASE("given_bep47_hybrid_symlink_after_payload_when_read_then_zero_byte_link_needs_no_piece_"
          "alignment",
          "[unit][metadata][attributes]")
{
    const auto bytes = fixture("valid-bep47-hybrid.torrent");
    const auto expected_sha1 =
        Sha1Digest::from_hex("5353535353535353535353535353535353535353").value();

    for (const auto mode : {MetadataReadMode::Lenient, MetadataReadMode::Strict})
    {
        auto document = decode_torrent(bytes, mode);

        REQUIRE(document);
        REQUIRE(document.value().warnings().empty());
        const auto& info = document.value().info();
        REQUIRE(info.format() == TorrentFormat::Hybrid);
        REQUIRE(info.pieces().total_size() == 16484);
        REQUIRE(info.files().size() == 4);
        const auto& link = info.files().back();
        REQUIRE(link.path().to_string() == "link-to-a");
        REQUIRE(link.length() == 0);
        REQUIRE(link.attributes().hidden);
        REQUIRE(link.attributes().symlink);
        REQUIRE_FALSE(link.pieces_root().has_value());
        REQUIRE(link.sha1_hint() == expected_sha1);
        REQUIRE(link.symlink_target().has_value());
        REQUIRE(require_optional(link.symlink_target()).segments() ==
                std::vector<std::string>{"a.bin"});
    }
}

TEST_CASE("given_bep47_file_sha1_hint_without_attributes_when_read_then_both_modes_expose_it",
          "[unit][metadata][attributes]")
{
    const auto bytes = fixture("valid-file-sha1-hint.torrent");
    const auto expected = Sha1Digest::from_hex("5353535353535353535353535353535353535353").value();

    for (const auto mode : {MetadataReadMode::Lenient, MetadataReadMode::Strict})
    {
        auto document = decode_torrent(bytes, mode);

        REQUIRE(document);
        REQUIRE(document.value().warnings().empty());
        const auto& file = document.value().info().files().front();
        REQUIRE(file.sha1_hint() == expected);
        REQUIRE_FALSE(file.attributes().padding);
        REQUIRE_FALSE(file.attributes().executable);
        REQUIRE_FALSE(file.attributes().hidden);
        REQUIRE_FALSE(file.attributes().symlink);
    }
}

TEST_CASE("given_malformed_bep47_file_fields_when_read_in_any_mode_then_invalid_torrent",
          "[unit][metadata][attributes]")
{
    const std::vector<std::string> inputs{
        "invalid-bep47-sha1-length.torrent",
        "invalid-bep47-sha1-type.torrent",
        "invalid-bep47-regular-missing-length.torrent",
        "invalid-bep47-symlink-length-type.torrent",
        "invalid-bep47-missing-symlink-target.torrent",
        "invalid-bep47-symlink-target-type.torrent",
        "invalid-bep47-symlink-target-parent.torrent",
        "invalid-bep47-symlink-target-absolute.torrent",
        "invalid-bep47-symlink-target-backslash.torrent",
        "invalid-bep47-symlink-target-empty.torrent",
        "invalid-bep47-symlink-target-utf8.torrent",
        "invalid-bep47-attr-type.torrent",
        "invalid-bep47-target-without-link-attr.torrent",
        "invalid-symlink-length.torrent",
    };

    for (const auto& name : inputs)
    {
        INFO(name);
        for (const auto mode : {MetadataReadMode::Lenient, MetadataReadMode::Strict})
        {
            auto document = decode_torrent(fixture(name), mode);
            REQUIRE_FALSE(document);
            REQUIRE(document.error().code == ErrorCode::InvalidTorrent);
        }
    }
}

TEST_CASE("given_bep47_hybrid_link_metadata_mismatch_when_read_in_any_mode_then_invalid_torrent",
          "[unit][metadata][attributes]")
{
    for (const auto& name : {"invalid-bep47-hybrid-hidden-mismatch.torrent",
                             "invalid-bep47-hybrid-sha1-mismatch.torrent",
                             "invalid-bep47-hybrid-target-mismatch.torrent"})
    {
        INFO(name);
        for (const auto mode : {MetadataReadMode::Lenient, MetadataReadMode::Strict})
        {
            auto document = decode_torrent(fixture(name), mode);
            REQUIRE_FALSE(document);
            REQUIRE(document.error().code == ErrorCode::InvalidTorrent);
        }
    }
}

TEST_CASE("given_loaded_documents_when_metadata_fields_queried_then_categories_are_classified",
          "[unit][metadata]")
{
    auto standard_doc =
        decode_torrent(fixture("valid-v1-multifile.torrent"), MetadataReadMode::Strict);
    REQUIRE(standard_doc);
    const auto& standard_fields = standard_doc.value().metadata_fields();
    REQUIRE_FALSE(standard_fields.empty());
    REQUIRE(std::all_of(standard_fields.begin(), standard_fields.end(), [](const auto& field) {
        return field.category == MetadataFieldCategory::Standard;
    }));
    const auto has_standard = [&](const std::string_view key) {
        return std::any_of(standard_fields.begin(), standard_fields.end(), [&](const auto& field) {
            return field.key == key && field.category == MetadataFieldCategory::Standard;
        });
    };
    REQUIRE(has_standard("announce"));
    REQUIRE(has_standard("info"));
    REQUIRE(has_standard("name"));
    REQUIRE(has_standard("files"));
    REQUIRE(has_standard("path"));

    auto extension_doc =
        decode_torrent(fixture("similar-extension.torrent"), MetadataReadMode::Strict);
    REQUIRE(extension_doc);
    const auto& extension_fields = extension_doc.value().metadata_fields();
    const auto find_field = [&](const std::string_view key) -> const MetadataFieldInfo* {
        for (const auto& field : extension_fields)
        {
            if (field.key == key)
            {
                return &field;
            }
        }
        return nullptr;
    };
    const auto* similar = find_field("similar");
    REQUIRE(similar != nullptr);
    REQUIRE(similar->category == MetadataFieldCategory::Extension);
    REQUIRE(similar->scope == MetadataFieldScope::TopLevel);
    REQUIRE_FALSE(similar->info_hash);
    REQUIRE(similar->source == "BEP 38");
    const auto* update_url = find_field("update-url");
    REQUIRE(update_url != nullptr);
    REQUIRE(update_url->category == MetadataFieldCategory::Extension);
    REQUIRE(update_url->scope == MetadataFieldScope::Info);
    REQUIRE(update_url->info_hash);

    auto unknown_doc = decode_torrent(fixture("unknown-field.torrent"), MetadataReadMode::Lenient);
    REQUIRE(unknown_doc);
    const auto* zzz = find_any_field(unknown_doc.value(), "zzz-key");
    REQUIRE(zzz != nullptr);
    REQUIRE(zzz->category == MetadataFieldCategory::Unknown);
    REQUIRE(zzz->scope == MetadataFieldScope::TopLevel);
    const auto* qqq = find_any_field(unknown_doc.value(), "qqq-key");
    REQUIRE(qqq != nullptr);
    REQUIRE(qqq->category == MetadataFieldCategory::Unknown);
    REQUIRE(qqq->scope == MetadataFieldScope::Info);
}

TEST_CASE("given_direct_metadata_values_when_queried_then_bencode_values_are_rendered",
          "[unit][metadata][fields]")
{
    std::string escaped_value;
    escaped_value.push_back('\\');
    escaped_value.push_back('"');
    escaped_value.push_back('\n');
    escaped_value.push_back('\r');
    escaped_value.push_back('\t');
    escaped_value.push_back('\x01');

    std::string data = "d4:infod6:lengthi1e4:name1:x12:piece lengthi16384e6:pieces20:"
                       "123456789012345678906:x-infoi42ee8:x-binary2:";
    data.push_back(static_cast<char>(0xffU));
    data.push_back(static_cast<char>(0xfeU));
    data += "6:x-deeplllll1:xeeeee6:x-dictd1:ai1e1:b3:twoe9:x-escaped";
    data += std::to_string(escaped_value.size()) + ":" + escaped_value;
    data += "5:x-inti-7e6:x-listli1e3:twod1:k1:veee";

    auto document = decode_torrent({data.begin(), data.end()}, MetadataReadMode::Lenient);

    REQUIRE(document);
    const auto values = document.value().metadata_field_values();
    const auto require_value = [&](const std::string_view key, const MetadataFieldScope scope,
                                   const std::string_view expected) {
        const auto* value = find_field_value(values, key, scope);
        REQUIRE(value != nullptr);
        REQUIRE(value->value == expected);
    };
    require_value("x-binary", MetadataFieldScope::TopLevel, "<binary 2 bytes>");
    require_value("x-deep", MetadataFieldScope::TopLevel, "[[[[…]]]]");
    require_value("x-dict", MetadataFieldScope::TopLevel, R"({"a": 1, "b": "two"})");
    require_value("x-escaped", MetadataFieldScope::TopLevel, "\"\\\\\\\"\\n\\r\\t\\x01\"");
    require_value("x-int", MetadataFieldScope::TopLevel, "-7");
    require_value("x-list", MetadataFieldScope::TopLevel, R"([1, "two", {"k": "v"}])");
    require_value("x-info", MetadataFieldScope::Info, "42");
    const auto* info_value = find_field_value(values, "x-info", MetadataFieldScope::Info);
    REQUIRE(info_value != nullptr);
    REQUIRE_FALSE(info_value->type.empty());
}

TEST_CASE("given_torrent_file_structures_when_metadata_values_queried_then_structures_are_hidden",
          "[unit][metadata][fields]")
{
    for (const auto& name :
         {std::string("valid-v1-multifile.torrent"), std::string("valid-v2.torrent")})
    {
        INFO(name);
        auto document = decode_torrent(fixture(name), MetadataReadMode::Strict);
        REQUIRE(document);

        const auto values = document.value().metadata_field_values();

        REQUIRE(std::none_of(values.begin(), values.end(), [](const auto& value) {
            return value.scope == MetadataFieldScope::TopLevel && value.key == "info";
        }));
        REQUIRE(std::none_of(values.begin(), values.end(), [](const auto& value) {
            return value.scope == MetadataFieldScope::Info &&
                   (value.key == "files" || value.key == "file tree");
        }));
        REQUIRE(find_field_value(values, "name", MetadataFieldScope::Info) != nullptr);
    }
}

TEST_CASE("given_registry_field_locations_when_patchability_queried_then_bep_and_extension_"
          "boundaries_are_explicit",
          "[unit][metadata]")
{
    REQUIRE(info_patchability("private", FieldDictionary::InfoV1) ==
            InfoPatchability::StandardIdentity);
    REQUIRE(info_patchability("name", FieldDictionary::InfoV2) ==
            InfoPatchability::StandardIdentity);
    REQUIRE(info_patchability("source", FieldDictionary::InfoHybrid) ==
            InfoPatchability::ExplicitExtensionIdentity);
    REQUIRE(info_patchability("source", FieldDictionary::TopLevelV1) ==
            InfoPatchability::RetainedReadOnly);
    REQUIRE(info_patchability("name.utf-8", FieldDictionary::InfoV1) ==
            InfoPatchability::RetainedReadOnly);
    REQUIRE(info_patchability("symlink path", FieldDictionary::InfoV2FileTreeLeaf) ==
            InfoPatchability::RebuildRequired);
}

TEST_CASE("given_field_registry_when_classified_then_categories_are_stable", "[unit][metadata]")
{
    const auto standard = classify_field("name", FieldDictionary::InfoV1);
    REQUIRE(standard.category == MetadataFieldCategory::Standard);
    REQUIRE(standard.modeled);
    REQUIRE(standard.source == "BEP 3");

    const auto extension_similar = classify_field("similar", FieldDictionary::TopLevelV1);
    REQUIRE(extension_similar.category == MetadataFieldCategory::Extension);
    REQUIRE_FALSE(extension_similar.modeled);
    REQUIRE(extension_similar.source == "BEP 38");

    const auto extension_info_collections = classify_field("collections", FieldDictionary::InfoV1);
    REQUIRE(extension_info_collections.category == MetadataFieldCategory::Extension);
    REQUIRE_FALSE(extension_info_collections.modeled);

    const auto extension_update_url = classify_field("update-url", FieldDictionary::InfoHybrid);
    REQUIRE(extension_update_url.category == MetadataFieldCategory::Extension);
    REQUIRE(extension_update_url.source == "BEP 39");

    const auto extension_originator = classify_field("originator", FieldDictionary::InfoV2);
    REQUIRE(extension_originator.category == MetadataFieldCategory::Extension);

    const auto extension_x = classify_field("x-extra", FieldDictionary::TopLevelV1);
    REQUIRE(extension_x.category == MetadataFieldCategory::Extension);
    REQUIRE_FALSE(extension_x.modeled);
    REQUIRE(extension_x.source == "x-* convention");

    const auto unknown = classify_field("zzz-key", FieldDictionary::TopLevelV1);
    REQUIRE(unknown.category == MetadataFieldCategory::Unknown);
    REQUIRE_FALSE(unknown.modeled);

    const auto v1_key_in_v2 = classify_field("files", FieldDictionary::InfoV2);
    REQUIRE(v1_key_in_v2.category == MetadataFieldCategory::Unknown);

    const auto modeled_v2 = classify_field("file tree", FieldDictionary::InfoV2);
    REQUIRE(modeled_v2.category == MetadataFieldCategory::Standard);

    const auto modeled_piece_layers = classify_field("piece layers", FieldDictionary::TopLevelV2);
    REQUIRE(modeled_piece_layers.category == MetadataFieldCategory::Standard);

    REQUIRE(field_scope(FieldDictionary::InfoV1) == MetadataFieldScope::Info);
    REQUIRE(field_scope(FieldDictionary::InfoV1File) == MetadataFieldScope::InfoV1File);
    REQUIRE_FALSE(info_hash_bearing(FieldDictionary::TopLevelV1));
    REQUIRE(info_hash_bearing(FieldDictionary::InfoV1File));
}

TEST_CASE("given_field_registry_when_scanned_then_expected_entries_are_present", "[unit][metadata]")
{
    REQUIRE(registry_entry_count(FieldDictionary::TopLevelV1) == 12);
    REQUIRE(registry_entry_count(FieldDictionary::TopLevelV2) == 13);
    REQUIRE(registry_entry_count(FieldDictionary::InfoV1) == 14);
    REQUIRE(registry_entry_count(FieldDictionary::InfoV2) == 10);
    REQUIRE(registry_entry_count(FieldDictionary::InfoHybrid) == 15);
    REQUIRE(registry_entry_count(FieldDictionary::InfoV1File) == 5);
    REQUIRE(registry_entry_count(FieldDictionary::InfoV2FileTreeLeaf) == 5);
}
