#include "../src/inspection_capabilities.hpp"
#include "../src/metadata_engine.hpp"
#include "../src/torrent_engine_fault_injection.hpp"
#include "../src/verification_progress_publisher.hpp"

#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <torrentutils/core/torrent_engine.hpp>
#include <utility>
#include <vector>

namespace {

using namespace torrentutils::core;

class TempDirectory
{
  public:
    TempDirectory()
    {
        static std::atomic<std::uint64_t> sequence{};
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("torrentutils-phase5-" + std::to_string(tick) + "-" +
                 std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

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

std::vector<std::uint8_t> torrent_engine_fixture(const std::string& name)
{
    const auto path =
        std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" / "torrent-engine" / name;
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

template <typename Value>
[[nodiscard]] const Value& require_optional(const std::optional<Value>& value)
{
    if (!value.has_value())
    {
        throw std::logic_error("expected optional test value");
    }
    return value.value();
}

TorrentDocument v1_document(Sha1Digest piece_hash = sha1(2))
{
    auto path = LogicalPath::from_segments({"payload.bin"});
    REQUIRE(path);
    auto file = FileEntry::create(std::move(path).value(), 16);
    REQUIRE(file);
    auto hashes = InfoHashes::create(TorrentFormat::V1, sha1(1), std::nullopt);
    REQUIRE(hashes);
    auto pieces = PieceInfo::create(TorrentFormat::V1, 16, 16, {piece_hash});
    REQUIRE(pieces);
    auto info = TorrentInfo::create("payload.bin", TorrentFormat::V1, std::move(hashes).value(),
                                    std::move(pieces).value(), {std::move(file).value()});
    REQUIRE(info);
    auto metadata = TorrentMetadata::create();
    REQUIRE(metadata);
    auto trackers = TrackerList::create({});
    REQUIRE(trackers);
    auto document = TorrentDocument::create(std::move(info).value(), std::move(metadata).value(),
                                            std::move(trackers).value());
    REQUIRE(document);
    return std::move(document).value();
}

TorrentDocument unretained_v2_document()
{
    auto path = LogicalPath::from_segments({"payload.bin"});
    REQUIRE(path);
    auto file = FileEntry::create(std::move(path).value(), 16, {}, sha256(4));
    REQUIRE(file);
    auto hashes = InfoHashes::create(TorrentFormat::V2, std::nullopt, sha256(3));
    REQUIRE(hashes);
    auto pieces = PieceInfo::create(TorrentFormat::V2, std::uint64_t{16} * 1024U, 16);
    REQUIRE(pieces);
    auto info = TorrentInfo::create("payload.bin", TorrentFormat::V2, std::move(hashes).value(),
                                    std::move(pieces).value(), {std::move(file).value()});
    REQUIRE(info);
    auto metadata = TorrentMetadata::create();
    REQUIRE(metadata);
    auto trackers = TrackerList::create({});
    REQUIRE(trackers);
    auto document = TorrentDocument::create(std::move(info).value(), std::move(metadata).value(),
                                            std::move(trackers).value());
    REQUIRE(document);
    return std::move(document).value();
}

TorrentDocument capability_matrix_document()
{
    auto payload_path = LogicalPath::from_segments({"payload.bin"});
    REQUIRE(payload_path);
    FileAttributes payload_attributes;
    payload_attributes.executable = true;
    payload_attributes.hidden = true;
    auto payload =
        FileEntry::create(std::move(payload_path).value(), 1024, payload_attributes, sha256(4));
    REQUIRE(payload);
    auto link_path = LogicalPath::from_segments({"alias.bin"});
    REQUIRE(link_path);
    auto link_target = LogicalPath::from_segments({"payload.bin"});
    REQUIRE(link_target);
    FileAttributes link_attributes;
    link_attributes.symlink = true;
    auto link = FileEntry::create(std::move(link_path).value(), 0, link_attributes, std::nullopt,
                                  std::nullopt, std::move(link_target).value());
    REQUIRE(link);
    auto hashes = InfoHashes::create(TorrentFormat::V2, std::nullopt, sha256(3));
    REQUIRE(hashes);
    auto pieces = PieceInfo::create(TorrentFormat::V2, std::uint64_t{16} * 1024U, 1024);
    REQUIRE(pieces);
    std::vector<FileEntry> files;
    files.push_back(std::move(payload).value());
    files.push_back(std::move(link).value());
    auto info = TorrentInfo::create("payload", TorrentFormat::V2, std::move(hashes).value(),
                                    std::move(pieces).value(), std::move(files));
    REQUIRE(info);
    auto metadata = TorrentMetadata::create();
    REQUIRE(metadata);
    auto trackers = TrackerList::create({});
    REQUIRE(trackers);
    auto document = TorrentDocument::create(std::move(info).value(), std::move(metadata).value(),
                                            std::move(trackers).value());
    REQUIRE(document);
    return std::move(document).value();
}

TorrentDocument v1_three_piece_document()
{
    auto path = LogicalPath::from_segments({"payload.bin"});
    REQUIRE(path);
    auto file = FileEntry::create(std::move(path).value(), 48);
    REQUIRE(file);
    auto hashes = InfoHashes::create(TorrentFormat::V1, sha1(1), std::nullopt);
    REQUIRE(hashes);
    auto first = Sha1Digest::from_hex("fe5567e8d769550852182cdf69d74bb16dff8e29");
    REQUIRE(first);
    auto second = Sha1Digest::from_hex("160940a8ec96c161df37596ed212c6266f9bc5c9");
    REQUIRE(second);
    auto third = Sha1Digest::from_hex("a87bf8e93e3bd61c19f5f773aeb3454a9dff31ba");
    REQUIRE(third);
    std::vector<Sha1Digest> piece_hashes;
    piece_hashes.push_back(std::move(first).value());
    piece_hashes.push_back(std::move(second).value());
    piece_hashes.push_back(std::move(third).value());
    auto pieces = PieceInfo::create(TorrentFormat::V1, 16, 48, std::move(piece_hashes));
    REQUIRE(pieces);
    auto info = TorrentInfo::create("payload.bin", TorrentFormat::V1, std::move(hashes).value(),
                                    std::move(pieces).value(), {std::move(file).value()});
    REQUIRE(info);
    auto metadata = TorrentMetadata::create();
    REQUIRE(metadata);
    auto trackers = TrackerList::create({});
    REQUIRE(trackers);
    auto document = TorrentDocument::create(std::move(info).value(), std::move(metadata).value(),
                                            std::move(trackers).value());
    REQUIRE(document);
    return std::move(document).value();
}

TorrentDocument v1_two_file_document(Sha1Digest piece_hash)
{
    auto first_path = LogicalPath::from_segments({"a.bin"});
    REQUIRE(first_path);
    auto second_path = LogicalPath::from_segments({"b.bin"});
    REQUIRE(second_path);
    auto first_file = FileEntry::create(std::move(first_path).value(), 8);
    REQUIRE(first_file);
    auto second_file = FileEntry::create(std::move(second_path).value(), 8);
    REQUIRE(second_file);
    auto hashes = InfoHashes::create(TorrentFormat::V1, sha1(1), std::nullopt);
    REQUIRE(hashes);
    auto pieces = PieceInfo::create(TorrentFormat::V1, 16, 16, {piece_hash});
    REQUIRE(pieces);
    std::vector<FileEntry> files;
    files.push_back(std::move(first_file).value());
    files.push_back(std::move(second_file).value());
    auto info = TorrentInfo::create("payload", TorrentFormat::V1, std::move(hashes).value(),
                                    std::move(pieces).value(), std::move(files));
    REQUIRE(info);
    auto metadata = TorrentMetadata::create();
    REQUIRE(metadata);
    auto trackers = TrackerList::create({});
    REQUIRE(trackers);
    auto document = TorrentDocument::create(std::move(info).value(), std::move(metadata).value(),
                                            std::move(trackers).value());
    REQUIRE(document);
    return std::move(document).value();
}

TorrentDocument v1_bep47_document(Sha1Digest piece_hash)
{
    auto target_path = LogicalPath::from_segments({"target.bin"});
    REQUIRE(target_path);
    auto alias_path = LogicalPath::from_segments({"alias.bin"});
    REQUIRE(alias_path);
    auto declared_target = LogicalPath::from_segments({"target.bin"});
    REQUIRE(declared_target);
    auto target_file = FileEntry::create(std::move(target_path).value(), 16);
    REQUIRE(target_file);
    FileAttributes link_attributes;
    link_attributes.symlink = true;
    auto link_file =
        FileEntry::create(std::move(alias_path).value(), 0, link_attributes, std::nullopt,
                          std::nullopt, std::move(declared_target).value());
    REQUIRE(link_file);
    auto hashes = InfoHashes::create(TorrentFormat::V1, sha1(1), std::nullopt);
    REQUIRE(hashes);
    auto pieces = PieceInfo::create(TorrentFormat::V1, 16, 16, {piece_hash});
    REQUIRE(pieces);
    std::vector<FileEntry> files;
    files.push_back(std::move(target_file).value());
    files.push_back(std::move(link_file).value());
    auto info = TorrentInfo::create("payload", TorrentFormat::V1, std::move(hashes).value(),
                                    std::move(pieces).value(), std::move(files));
    REQUIRE(info);
    auto metadata = TorrentMetadata::create();
    REQUIRE(metadata);
    auto trackers = TrackerList::create({});
    REQUIRE(trackers);
    auto document = TorrentDocument::create(std::move(info).value(), std::move(metadata).value(),
                                            std::move(trackers).value());
    REQUIRE(document);
    return std::move(document).value();
}

std::size_t temporary_sibling_count(const std::filesystem::path& target)
{
    const auto prefix = target.filename().string() + ".torrentutils.tmp.";
    std::size_t count{};
    for (const auto& entry : std::filesystem::directory_iterator(target.parent_path()))
    {
        const auto filename = entry.path().filename().string();
        if (filename.compare(0, prefix.size(), prefix) == 0)
        {
            ++count;
        }
    }
    return count;
}

class RecordingLogger final : public Logger
{
  public:
    void log(const LogRecord& record) override
    {
        records.push_back(record);
    }

    std::vector<LogRecord> records;
};

} // namespace

TEST_CASE("given_operation_id_when_engine_logs_then_core_record_is_correlated")
{
    RecordingLogger logger;
    TaskContext context;
    context.logger = &logger;
    context.operation_id = "op-000042";

    const TorrentEngine engine;
    const auto result = engine.inspect(v1_document(), context);

    REQUIRE(result);
    REQUIRE_FALSE(logger.records.empty());
    REQUIRE(logger.records.front().message.find("operation_id=\"op-000042\"") != std::string::npos);
}

TEST_CASE("given_supported_document_when_inspected_then_capability_is_reported_without_payload_io",
          "[unit][torrent-engine][inspect]")
{
    const TorrentEngine engine;

    const auto result = engine.inspect(v1_document());

    REQUIRE(result);
    REQUIRE(result.value().verification_capability == VerificationCapability::Supported);
    REQUIRE(result.value().diagnostics.empty());
}

TEST_CASE("given_unretained_v2_document_when_inspected_then_missing_piece_layers_are_reported",
          "[unit][torrent-engine][inspect][v2]")
{
    const auto document = unretained_v2_document();
    const TorrentEngine engine;

    const auto inspection = engine.inspect(document);

    REQUIRE(inspection);
    REQUIRE(inspection.value().verification_capability == VerificationCapability::Unsupported);
    REQUIRE(inspection.value().diagnostics.size() == 1);
    REQUIRE(inspection.value().diagnostics.front().code ==
            VerificationCapabilityDiagnosticCode::UnsupportedPieceHashScheme);
    REQUIRE(inspection.value().diagnostics.front().message ==
            "v2 piece layer metadata is unavailable for verification");

    const auto verification = engine.verify({document, "payload.bin"});
    REQUIRE_FALSE(verification);
    REQUIRE(verification.error().code == ErrorCode::UnsupportedFeature);
}

TEST_CASE("given_document_with_independent_blockers_when_inspected_then_all_diagnostics_are_"
          "stable_and_ordered",
          "[unit][torrent-engine][inspect]")
{
    detail::VerificationBackendCapabilities capabilities;
    capabilities.verification = false;
    capabilities.v2_format = false;
    capabilities.file_attributes = false;
    capabilities.bep47_symlinks = false;
    capabilities.max_file_size = 512;
    capabilities.max_file_offset = 512;
    capabilities.max_piece_count = 0;

    const auto result =
        detail::inspect_verification_capability(capability_matrix_document(), [capabilities]() {
            return Result<detail::VerificationBackendCapabilities>::success(capabilities);
        });

    REQUIRE(result);
    REQUIRE(result.value().verification_capability == VerificationCapability::Unsupported);
    REQUIRE(result.value().diagnostics.size() == 6U);
    REQUIRE(result.value().diagnostics[0].code ==
            VerificationCapabilityDiagnosticCode::UnsupportedTorrentFormat);
    REQUIRE(result.value().diagnostics[1].code ==
            VerificationCapabilityDiagnosticCode::UnsupportedPieceHashScheme);
    REQUIRE(result.value().diagnostics[2].code ==
            VerificationCapabilityDiagnosticCode::UnsupportedFileLayout);
    REQUIRE(result.value().diagnostics[3].code ==
            VerificationCapabilityDiagnosticCode::UnsupportedFileAttribute);
    REQUIRE(result.value().diagnostics[4].code ==
            VerificationCapabilityDiagnosticCode::UnsupportedSymlinkSemantics);
    REQUIRE(result.value().diagnostics[5].code ==
            VerificationCapabilityDiagnosticCode::BackendFeatureUnavailable);
    REQUIRE(result.value().diagnostics[0].message ==
            "torrent format is unavailable for verification");
    REQUIRE(result.value().diagnostics[1].message ==
            "v2 piece layer metadata is unavailable for verification");
    REQUIRE(result.value().diagnostics[2].message ==
            "torrent file layout exceeds verification limits");
    REQUIRE(result.value().diagnostics[3].message ==
            "torrent file attributes are unavailable for verification");
    REQUIRE(result.value().diagnostics[4].message ==
            "BEP 47 symlink semantics are unavailable for verification");
    REQUIRE(result.value().diagnostics[5].message ==
            "required verification capability is unavailable");
    for (const auto& diagnostic : result.value().diagnostics)
    {
        REQUIRE_FALSE(diagnostic.message.empty());
        REQUIRE(diagnostic.message.find("libtorrent") == std::string::npos);
        REQUIRE(diagnostic.message.find("Boost") == std::string::npos);
    }
}

TEST_CASE("given_capability_provider_failure_when_inspected_then_result_error_is_preserved",
          "[unit][torrent-engine][inspect]")
{
    const auto result = detail::inspect_verification_capability(v1_document(), []() {
        return Result<detail::VerificationBackendCapabilities>::failure(
            {ErrorCode::Internal, "verification capability assessment failed", {}});
    });

    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == ErrorCode::Internal);
    REQUIRE(result.error().message == "verification capability assessment failed");
}

TEST_CASE("given_restricted_adapter_capabilities_when_inspected_then_public_diagnostics_and_"
          "direct_verify_are_consistent",
          "[unit][torrent-engine][inspect]")
{
    const auto document = capability_matrix_document();
    const TorrentEngine engine;
    detail::ScopedTorrentEngineFault fault{
        detail::TorrentEngineFault::InspectRestrictedCapabilities};

    const auto inspection = engine.inspect(document);

    REQUIRE(inspection);
    REQUIRE(inspection.value().verification_capability == VerificationCapability::Unsupported);
    REQUIRE(inspection.value().diagnostics.size() == 6U);
    REQUIRE(inspection.value().diagnostics[0].code ==
            VerificationCapabilityDiagnosticCode::UnsupportedTorrentFormat);
    REQUIRE(inspection.value().diagnostics[1].code ==
            VerificationCapabilityDiagnosticCode::UnsupportedPieceHashScheme);
    REQUIRE(inspection.value().diagnostics[2].code ==
            VerificationCapabilityDiagnosticCode::UnsupportedFileLayout);
    REQUIRE(inspection.value().diagnostics[3].code ==
            VerificationCapabilityDiagnosticCode::UnsupportedFileAttribute);
    REQUIRE(inspection.value().diagnostics[4].code ==
            VerificationCapabilityDiagnosticCode::UnsupportedSymlinkSemantics);
    REQUIRE(inspection.value().diagnostics[5].code ==
            VerificationCapabilityDiagnosticCode::BackendFeatureUnavailable);

    const auto verification = engine.verify({document, "unreadable-is-not-inspected"});

    REQUIRE_FALSE(verification);
    REQUIRE(verification.error().code == ErrorCode::UnsupportedFeature);
}

TEST_CASE("given_adapter_capability_initialization_failure_when_inspected_then_result_error_is_"
          "preserved",
          "[unit][torrent-engine][inspect]")
{
    const TorrentEngine engine;
    detail::ScopedTorrentEngineFault fault{
        detail::TorrentEngineFault::InspectBackendInitialization};

    const auto result = engine.inspect(v1_document());

    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == ErrorCode::Internal);
    REQUIRE(result.error().message == "verification capability assessment failed");
}

TEST_CASE("given_empty_create_paths_when_created_then_all_request_issues_are_reported",
          "[unit][torrent-engine][create]")
{
    auto options = CreateOptions::create();
    REQUIRE(options);
    const CreateRequest request{{}, {}, std::move(options).value()};
    const TorrentEngine engine;

    const auto result = engine.create(request);

    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == ErrorCode::ValidationFailed);
    REQUIRE(result.error().message == "create request validation failed");
    REQUIRE(result.error().issues.size() == 2);
    REQUIRE(result.error().issues[0].field == "create.content_root");
    REQUIRE(result.error().issues[1].field == "create.target_path");
}

TEST_CASE("given_piece_ranges_and_file_findings_when_consumed_then_public_values_are_composable",
          "[unit][torrent-engine][verification-contract]")
{
    const PieceRange range{2, 5, PieceVerificationState::Mismatched};
    auto findings = FileVerificationFinding::Missing | FileVerificationFinding::LengthMismatch;
    findings |= FileVerificationFinding::SharedPieceMismatch;

    REQUIRE(range.begin == 2);
    REQUIRE(range.end == 5);
    REQUIRE(range.state == PieceVerificationState::Mismatched);
    REQUIRE(has_finding(findings, FileVerificationFinding::Missing));
    REQUIRE(has_finding(findings, FileVerificationFinding::LengthMismatch));
    REQUIRE(has_finding(findings, FileVerificationFinding::SharedPieceMismatch));
    REQUIRE_FALSE(has_finding(findings, FileVerificationFinding::HashMismatch));
}

TEST_CASE("given_empty_verify_content_root_when_verified_then_request_is_rejected",
          "[unit][torrent-engine][verify]")
{
    const VerifyRequest request{v1_document(), {}};
    const TorrentEngine engine;

    const auto result = engine.verify(request);

    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == ErrorCode::ValidationFailed);
    REQUIRE(result.error().message == "verify request validation failed");
    REQUIRE(result.error().issues.size() == 1);
    REQUIRE(result.error().issues.front().field == "verify.content_root");
}

TEST_CASE("given_matching_v1_single_file_when_verified_then_complete_report_is_returned",
          "[integration][torrent-engine][verify]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    {
        std::ofstream output(content, std::ios::binary);
        output << "0123456789abcdef";
        REQUIRE(output);
    }
    auto piece_hash = Sha1Digest::from_hex("fe5567e8d769550852182cdf69d74bb16dff8e29");
    REQUIRE(piece_hash);
    const TorrentEngine engine;

    const auto result = engine.verify({v1_document(std::move(piece_hash).value()), content});

    REQUIRE(result);
    REQUIRE(result.value().outcome == VerificationOutcome::Verified);
    REQUIRE(result.value().expected_bytes == 16);
    REQUIRE(result.value().hashed_bytes == 16);
    REQUIRE(result.value().verified_bytes == 16);
    REQUIRE(result.value().mismatched_bytes == 0);
    REQUIRE(result.value().files.size() == 1);
    const auto& file = result.value().files.front();
    REQUIRE(file.path.segments() == std::vector<std::string>{"payload.bin"});
    REQUIRE(file.expected_bytes == 16);
    REQUIRE(file.hashed_bytes == 16);
    REQUIRE(file.verified_bytes == 16);
    REQUIRE(file.mismatched_bytes == 0);
    REQUIRE(file.findings == FileVerificationFinding::None);
}

TEST_CASE("given_matching_retained_v2_single_file_when_verified_then_complete_report_is_returned",
          "[integration][torrent-engine][verify][v2]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    const auto torrent = temp.path() / "payload.torrent";
    {
        std::ofstream output(content, std::ios::binary);
        output << std::string(20000, 'x');
        REQUIRE(output);
    }
    CreateOptionsInput input;
    input.format = TorrentFormat::V2;
    input.piece_length_strategy = PieceLengthStrategy::Fixed;
    input.fixed_piece_length = 16U * 1024U;
    auto options = CreateOptions::create(std::move(input));
    REQUIRE(options);
    const TorrentEngine engine;
    const auto created = engine.create({content, torrent, std::move(options).value(), false});
    REQUIRE(created);
    std::ifstream torrent_input(torrent, std::ios::binary);
    REQUIRE(torrent_input);
    std::vector<std::uint8_t> encoded{std::istreambuf_iterator<char>(torrent_input),
                                      std::istreambuf_iterator<char>()};
    auto document = detail::decode_torrent(std::move(encoded), detail::MetadataReadMode::Strict);
    REQUIRE(document);

    const auto result = engine.verify({std::move(document).value(), content});

    REQUIRE(result);
    REQUIRE(result.value().outcome == VerificationOutcome::Verified);
    REQUIRE(result.value().expected_bytes == 20000);
    REQUIRE(result.value().hashed_bytes == 20000);
    REQUIRE(result.value().verified_bytes == 20000);
    REQUIRE(result.value().mismatched_bytes == 0);
    REQUIRE(result.value().files.size() == 1);
    REQUIRE(result.value().files.front().path.segments() ==
            std::vector<std::string>{"payload.bin"});
    REQUIRE(result.value().files.front().findings == FileVerificationFinding::None);
}

TEST_CASE("given_mismatched_second_file_in_retained_v2_multi_file_when_verified_then_file_"
          "accounting_is_exact",
          "[integration][torrent-engine][verify][v2]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    const auto torrent = temp.path() / "payload.torrent";
    std::filesystem::create_directory(content);
    {
        std::ofstream output(content / "a.bin", std::ios::binary);
        output << std::string(10000, 'a');
        REQUIRE(output);
    }
    {
        std::ofstream output(content / "b.bin", std::ios::binary);
        output << std::string(100, 'b');
        REQUIRE(output);
    }
    CreateOptionsInput input;
    input.format = TorrentFormat::V2;
    input.piece_length_strategy = PieceLengthStrategy::Fixed;
    input.fixed_piece_length = 16U * 1024U;
    auto options = CreateOptions::create(std::move(input));
    REQUIRE(options);
    const TorrentEngine engine;
    const auto created = engine.create({content, torrent, std::move(options).value(), false});
    REQUIRE(created);
    std::ifstream torrent_input(torrent, std::ios::binary);
    REQUIRE(torrent_input);
    std::vector<std::uint8_t> encoded{std::istreambuf_iterator<char>(torrent_input),
                                      std::istreambuf_iterator<char>()};
    auto document = detail::decode_torrent(std::move(encoded), detail::MetadataReadMode::Strict);
    REQUIRE(document);
    {
        std::ofstream output(content / "b.bin", std::ios::binary | std::ios::trunc);
        output << std::string(100, 'x');
        REQUIRE(output);
    }

    std::vector<VerificationProgress> snapshots;
    VerifyRequest request{std::move(document).value(), content};
    request.on_progress = [&snapshots](const VerificationProgress& progress) {
        snapshots.push_back(progress);
    };

    const auto result = engine.verify(request);

    REQUIRE(result);
    REQUIRE(result.value().outcome == VerificationOutcome::Mismatched);
    REQUIRE(result.value().expected_bytes == 10100);
    REQUIRE(result.value().hashed_bytes == 10100);
    REQUIRE(result.value().verified_bytes == 10000);
    REQUIRE(result.value().mismatched_bytes == 100);
    REQUIRE(result.value().files.size() == 2);
    const auto& first = result.value().files[0];
    REQUIRE(first.path.segments() == std::vector<std::string>{"a.bin"});
    REQUIRE(first.expected_bytes == 10000);
    REQUIRE(first.hashed_bytes == 10000);
    REQUIRE(first.verified_bytes == 10000);
    REQUIRE(first.mismatched_bytes == 0);
    REQUIRE(first.findings == FileVerificationFinding::None);
    const auto& second = result.value().files[1];
    REQUIRE(second.path.segments() == std::vector<std::string>{"b.bin"});
    REQUIRE(second.expected_bytes == 100);
    REQUIRE(second.hashed_bytes == 100);
    REQUIRE(second.verified_bytes == 0);
    REQUIRE(second.mismatched_bytes == 100);
    REQUIRE(second.findings == FileVerificationFinding::HashMismatch);

    std::vector<int> piece_states(2, -1);
    std::uint64_t first_hashed{};
    std::uint64_t first_verified{};
    std::uint64_t first_mismatched{};
    std::uint64_t second_hashed{};
    std::uint64_t second_verified{};
    std::uint64_t second_mismatched{};
    for (const auto& snapshot : snapshots)
    {
        for (const auto& range : snapshot.piece_ranges)
        {
            REQUIRE(range.end <= piece_states.size());
            for (auto piece = range.begin; piece < range.end; ++piece)
            {
                piece_states[static_cast<std::size_t>(piece)] =
                    range.state == PieceVerificationState::Verified ? 1 : 0;
            }
        }
        for (const auto& file : snapshot.files)
        {
            if (file.path.segments() == std::vector<std::string>{"a.bin"})
            {
                first_hashed = file.hashed_bytes;
                first_verified = file.verified_bytes;
                first_mismatched = file.mismatched_bytes;
            }
            if (file.path.segments() == std::vector<std::string>{"b.bin"})
            {
                second_hashed = file.hashed_bytes;
                second_verified = file.verified_bytes;
                second_mismatched = file.mismatched_bytes;
            }
        }
    }
    REQUIRE(piece_states == std::vector<int>{1, 0});
    REQUIRE(first_hashed == 10000);
    REQUIRE(first_verified == 10000);
    REQUIRE(first_mismatched == 0);
    REQUIRE(second_hashed == 100);
    REQUIRE(second_verified == 0);
    REQUIRE(second_mismatched == 100);
}

TEST_CASE("given_versioned_retained_hybrid_fixture_when_verified_then_padding_is_excluded_from_"
          "report_and_progress",
          "[integration][torrent-engine][verify][hybrid][fixture]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    std::filesystem::create_directory(content);
    {
        std::ofstream output(content / "a.bin", std::ios::binary);
        output << std::string(10000, 'a');
        REQUIRE(output);
    }
    {
        std::ofstream output(content / "b.bin", std::ios::binary);
        output << std::string(100, 'b');
        REQUIRE(output);
    }
    auto document =
        detail::decode_torrent(torrent_engine_fixture("retained-hybrid-padding.torrent"),
                               detail::MetadataReadMode::Strict);
    REQUIRE(document);
    REQUIRE(document.value().info().format() == TorrentFormat::Hybrid);
    const auto& hashes = document.value().info().info_hashes();
    REQUIRE(require_optional(hashes.v1()).to_hex() == "527f44dac29d48ca711879b9c6f7e117e539c08c");
    REQUIRE(require_optional(hashes.v2()).to_hex() ==
            "99f1d3786e2e7b47c96eed3eeaf7b10264ef4df88d174bd1b5f6a67d255e7512");
    REQUIRE(document.value().info().files().size() == 4);
    REQUIRE(document.value().info().files()[1].attributes().padding);
    REQUIRE(document.value().info().files()[3].attributes().padding);

    std::vector<VerificationProgress> progress_events;
    VerifyRequest request{std::move(document).value(), content};
    request.on_progress = [&progress_events](const VerificationProgress& progress) {
        progress_events.push_back(progress);
    };
    const TorrentEngine engine;

    const auto result = engine.verify(request);

    REQUIRE(result);
    REQUIRE(result.value().outcome == VerificationOutcome::Verified);
    REQUIRE(result.value().expected_bytes == 10100);
    REQUIRE(result.value().hashed_bytes == 10100);
    REQUIRE(result.value().verified_bytes == 10100);
    REQUIRE(result.value().mismatched_bytes == 0);
    REQUIRE(result.value().files.size() == 2);
    REQUIRE(result.value().files[0].path.segments() == std::vector<std::string>{"a.bin"});
    REQUIRE(result.value().files[0].verified_bytes == 10000);
    REQUIRE(result.value().files[1].path.segments() == std::vector<std::string>{"b.bin"});
    REQUIRE(result.value().files[1].verified_bytes == 100);

    std::vector<int> piece_states(2, -1);
    std::uint64_t progress_bytes{};
    for (const auto& event : progress_events)
    {
        for (const auto& range : event.piece_ranges)
        {
            REQUIRE(range.end <= piece_states.size());
            for (auto piece = range.begin; piece < range.end; ++piece)
            {
                piece_states[static_cast<std::size_t>(piece)] =
                    range.state == PieceVerificationState::Verified ? 1 : 0;
            }
        }
        for (const auto& file : event.files)
        {
            REQUIRE_FALSE(file.path.segments().front() == ".pad");
            progress_bytes = (std::max)(progress_bytes, file.verified_bytes);
        }
    }
    REQUIRE(piece_states == std::vector<int>{1, 1});
    REQUIRE(progress_bytes == 10000);
}

TEST_CASE("given_matching_retained_hybrid_with_padding_when_verified_then_only_logical_file_"
          "bytes_are_reported",
          "[integration][torrent-engine][verify][hybrid]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    const auto torrent = temp.path() / "payload.torrent";
    std::filesystem::create_directory(content);
    {
        std::ofstream output(content / "a.bin", std::ios::binary);
        output << std::string(10000, 'a');
        REQUIRE(output);
    }
    {
        std::ofstream output(content / "b.bin", std::ios::binary);
        output << std::string(100, 'b');
        REQUIRE(output);
    }
    CreateOptionsInput input;
    input.format = TorrentFormat::Hybrid;
    input.piece_length_strategy = PieceLengthStrategy::Fixed;
    input.fixed_piece_length = 16U * 1024U;
    auto options = CreateOptions::create(std::move(input));
    REQUIRE(options);
    const TorrentEngine engine;
    const auto created = engine.create({content, torrent, std::move(options).value(), false});
    REQUIRE(created);
    std::ifstream torrent_input(torrent, std::ios::binary);
    REQUIRE(torrent_input);
    std::vector<std::uint8_t> encoded{std::istreambuf_iterator<char>(torrent_input),
                                      std::istreambuf_iterator<char>()};
    auto document = detail::decode_torrent(std::move(encoded), detail::MetadataReadMode::Strict);
    REQUIRE(document);
    REQUIRE(document.value().info().format() == TorrentFormat::Hybrid);
    REQUIRE(document.value().info().files().size() == 4);
    REQUIRE(document.value().info().files()[1].attributes().padding);
    REQUIRE(document.value().info().files()[1].length() == 6384);
    REQUIRE(document.value().info().files()[3].attributes().padding);
    REQUIRE(document.value().info().files()[3].length() == 16284);

    std::vector<VerificationProgress> snapshots;
    VerifyRequest request{std::move(document).value(), content};
    request.on_progress = [&snapshots](const VerificationProgress& progress) {
        snapshots.push_back(progress);
    };

    const auto result = engine.verify(request);

    REQUIRE(result);
    REQUIRE(result.value().outcome == VerificationOutcome::Verified);
    REQUIRE(result.value().expected_bytes == 10100);
    REQUIRE(result.value().hashed_bytes == 10100);
    REQUIRE(result.value().verified_bytes == 10100);
    REQUIRE(result.value().mismatched_bytes == 0);
    REQUIRE(result.value().files.size() == 2);
    const auto& first = result.value().files[0];
    REQUIRE(first.path.segments() == std::vector<std::string>{"a.bin"});
    REQUIRE(first.expected_bytes == 10000);
    REQUIRE(first.hashed_bytes == 10000);
    REQUIRE(first.verified_bytes == 10000);
    REQUIRE(first.mismatched_bytes == 0);
    REQUIRE(first.findings == FileVerificationFinding::None);
    const auto& second = result.value().files[1];
    REQUIRE(second.path.segments() == std::vector<std::string>{"b.bin"});
    REQUIRE(second.expected_bytes == 100);
    REQUIRE(second.hashed_bytes == 100);
    REQUIRE(second.verified_bytes == 100);
    REQUIRE(second.mismatched_bytes == 0);
    REQUIRE(second.findings == FileVerificationFinding::None);

    std::vector<int> piece_states(2, -1);
    std::uint64_t first_hashed{};
    std::uint64_t first_verified{};
    std::uint64_t second_hashed{};
    std::uint64_t second_verified{};
    for (const auto& snapshot : snapshots)
    {
        for (const auto& range : snapshot.piece_ranges)
        {
            REQUIRE(range.end <= piece_states.size());
            for (auto piece = range.begin; piece < range.end; ++piece)
            {
                piece_states[static_cast<std::size_t>(piece)] =
                    range.state == PieceVerificationState::Verified ? 1 : 0;
            }
        }
        for (const auto& file : snapshot.files)
        {
            const auto segments = file.path.segments();
            REQUIRE((segments == std::vector<std::string>{"a.bin"} ||
                     segments == std::vector<std::string>{"b.bin"}));
            if (file.path.segments() == std::vector<std::string>{"a.bin"})
            {
                first_hashed = file.hashed_bytes;
                first_verified = file.verified_bytes;
            }
            if (file.path.segments() == std::vector<std::string>{"b.bin"})
            {
                second_hashed = file.hashed_bytes;
                second_verified = file.verified_bytes;
            }
        }
    }
    REQUIRE(piece_states == std::vector<int>{1, 1});
    REQUIRE(first_hashed == first.hashed_bytes);
    REQUIRE(first_verified == first.verified_bytes);
    REQUIRE(second_hashed == second.hashed_bytes);
    REQUIRE(second_verified == second.verified_bytes);
}

TEST_CASE("given_longer_file_in_retained_hybrid_when_verified_then_length_mismatch_is_"
          "reported_with_exact_logical_bytes",
          "[integration][torrent-engine][verify][hybrid]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    const auto torrent = temp.path() / "payload.torrent";
    std::filesystem::create_directory(content);
    {
        std::ofstream output(content / "a.bin", std::ios::binary);
        output << std::string(10000, 'a');
        REQUIRE(output);
    }
    {
        std::ofstream output(content / "b.bin", std::ios::binary);
        output << std::string(100, 'b');
        REQUIRE(output);
    }
    CreateOptionsInput input;
    input.format = TorrentFormat::Hybrid;
    input.piece_length_strategy = PieceLengthStrategy::Fixed;
    input.fixed_piece_length = 16U * 1024U;
    auto options = CreateOptions::create(std::move(input));
    REQUIRE(options);
    const TorrentEngine engine;
    const auto created = engine.create({content, torrent, std::move(options).value(), false});
    REQUIRE(created);
    std::ifstream torrent_input(torrent, std::ios::binary);
    REQUIRE(torrent_input);
    std::vector<std::uint8_t> encoded{std::istreambuf_iterator<char>(torrent_input),
                                      std::istreambuf_iterator<char>()};
    auto document = detail::decode_torrent(std::move(encoded), detail::MetadataReadMode::Strict);
    REQUIRE(document);
    {
        std::ofstream output(content / "a.bin", std::ios::binary | std::ios::app);
        output << 'x';
        REQUIRE(output);
    }

    const auto result = engine.verify({std::move(document).value(), content});

    REQUIRE(result);
    REQUIRE(result.value().outcome == VerificationOutcome::Incomplete);
    REQUIRE(result.value().expected_bytes == 10100);
    REQUIRE(result.value().hashed_bytes == 10100);
    REQUIRE(result.value().verified_bytes == 10100);
    REQUIRE(result.value().mismatched_bytes == 0);
    REQUIRE(result.value().files.size() == 2);
    const auto& first = result.value().files[0];
    REQUIRE(first.path.segments() == std::vector<std::string>{"a.bin"});
    REQUIRE(first.expected_bytes == 10000);
    REQUIRE(first.hashed_bytes == 10000);
    REQUIRE(first.verified_bytes == 10000);
    REQUIRE(first.mismatched_bytes == 0);
    REQUIRE(first.findings == FileVerificationFinding::LengthMismatch);
    const auto& second = result.value().files[1];
    REQUIRE(second.path.segments() == std::vector<std::string>{"b.bin"});
    REQUIRE(second.expected_bytes == 100);
    REQUIRE(second.hashed_bytes == 100);
    REQUIRE(second.verified_bytes == 100);
    REQUIRE(second.mismatched_bytes == 0);
    REQUIRE(second.findings == FileVerificationFinding::None);
}

TEST_CASE("given_longer_corrupted_file_in_retained_hybrid_when_verified_then_findings_are_"
          "combined_without_padding_bytes",
          "[integration][torrent-engine][verify][hybrid][finding]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    std::filesystem::create_directory(content);
    {
        std::ofstream output(content / "a.bin", std::ios::binary);
        output << 'x' << std::string(9999, 'a') << 'x';
        REQUIRE(output);
    }
    {
        std::ofstream output(content / "b.bin", std::ios::binary);
        output << std::string(100, 'b');
        REQUIRE(output);
    }
    auto document =
        detail::decode_torrent(torrent_engine_fixture("retained-hybrid-padding.torrent"),
                               detail::MetadataReadMode::Strict);
    REQUIRE(document);
    std::vector<VerificationProgress> progress_events;
    VerifyRequest request{std::move(document).value(), content};
    request.on_progress = [&progress_events](const VerificationProgress& progress) {
        progress_events.push_back(progress);
    };
    const TorrentEngine engine;

    const auto result = engine.verify(request);

    REQUIRE(result);
    REQUIRE(result.value().outcome == VerificationOutcome::Incomplete);
    REQUIRE(result.value().expected_bytes == 10100);
    REQUIRE(result.value().hashed_bytes == 10100);
    REQUIRE(result.value().verified_bytes == 100);
    REQUIRE(result.value().mismatched_bytes == 10000);
    REQUIRE(result.value().files.size() == 2);
    const auto& first = result.value().files[0];
    REQUIRE(first.hashed_bytes == 10000);
    REQUIRE(first.verified_bytes == 0);
    REQUIRE(first.mismatched_bytes == 10000);
    REQUIRE(has_finding(first.findings, FileVerificationFinding::LengthMismatch));
    REQUIRE(has_finding(first.findings, FileVerificationFinding::HashMismatch));
    const auto& second = result.value().files[1];
    REQUIRE(second.verified_bytes == 100);
    REQUIRE(second.mismatched_bytes == 0);
    REQUIRE(second.findings == FileVerificationFinding::None);

    std::vector<int> piece_states(2, -1);
    for (const auto& event : progress_events)
    {
        for (const auto& range : event.piece_ranges)
        {
            for (auto piece = range.begin; piece < range.end; ++piece)
            {
                piece_states[static_cast<std::size_t>(piece)] =
                    range.state == PieceVerificationState::Verified ? 1 : 0;
            }
        }
    }
    REQUIRE(piece_states == std::vector<int>{0, 1});
}

TEST_CASE("given_v1_verification_progress_callback_when_verified_then_piece_evidence_is_lossless",
          "[integration][torrent-engine][verify][progress]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    {
        std::ofstream output(content, std::ios::binary);
        output << "0123456789abcdefghijklmnopqrstuvfedcba9876543210";
        REQUIRE(output);
    }
    std::vector<VerificationProgress> snapshots;
    std::vector<std::thread::id> callback_threads;
    VerifyRequest request{v1_three_piece_document(), content};
    request.on_progress = [&snapshots, &callback_threads](const VerificationProgress& progress) {
        snapshots.push_back(progress);
        callback_threads.push_back(std::this_thread::get_id());
    };
    const auto invoking_thread = std::this_thread::get_id();
    const TorrentEngine engine;

    const auto result = engine.verify(request);

    REQUIRE(result);
    REQUIRE(result.value().outcome == VerificationOutcome::Mismatched);
    REQUIRE(result.value().hashed_bytes == 48);
    REQUIRE(result.value().verified_bytes == 32);
    REQUIRE(result.value().mismatched_bytes == 16);
    REQUIRE_FALSE(snapshots.empty());
    REQUIRE(callback_threads.size() == snapshots.size());
    for (const auto callback_thread : callback_threads)
    {
        REQUIRE(callback_thread == invoking_thread);
    }

    std::vector<int> piece_states(3, -1);
    std::uint64_t previous_sequence{};
    for (const auto& snapshot : snapshots)
    {
        REQUIRE(snapshot.sequence > previous_sequence);
        previous_sequence = snapshot.sequence;
        for (std::size_t index = 0; index < snapshot.piece_ranges.size(); ++index)
        {
            const auto& range = snapshot.piece_ranges[index];
            REQUIRE(is_valid(range));
            REQUIRE(range.end <= piece_states.size());
            if (index > 0)
            {
                const auto& previous = snapshot.piece_ranges[index - 1];
                REQUIRE(previous.end <= range.begin);
                REQUIRE((previous.end != range.begin || previous.state != range.state));
            }
            for (auto piece = range.begin; piece < range.end; ++piece)
            {
                REQUIRE(piece_states[piece] == -1);
                piece_states[piece] = range.state == PieceVerificationState::Verified ? 0 : 1;
            }
        }
    }
    REQUIRE(piece_states == std::vector<int>{0, 0, 1});

    const auto& final_snapshot = snapshots.back();
    REQUIRE(final_snapshot.files.size() == 1);
    REQUIRE(final_snapshot.files.front().path.segments() ==
            std::vector<std::string>{"payload.bin"});
    REQUIRE(final_snapshot.files.front().expected_bytes == 48);
    REQUIRE(final_snapshot.files.front().hashed_bytes == result.value().files.front().hashed_bytes);
    REQUIRE(final_snapshot.files.front().verified_bytes ==
            result.value().files.front().verified_bytes);
    REQUIRE(final_snapshot.files.front().mismatched_bytes ==
            result.value().files.front().mismatched_bytes);
}

TEST_CASE("given_one_piece_crossing_257_files_when_verified_then_progress_pages_both_payloads",
          "[integration][torrent-engine][verify][progress]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    const auto torrent = temp.path() / "payload.torrent";
    std::filesystem::create_directory(content);
    constexpr std::size_t file_count = 257U;
    for (std::size_t index = 0; index < file_count; ++index)
    {
        std::ofstream output(content / ("file-" + std::to_string(index) + ".bin"),
                             std::ios::binary);
        output << 'x';
        REQUIRE(output);
    }

    CreateOptionsInput input;
    input.format = TorrentFormat::V1;
    input.piece_length_strategy = PieceLengthStrategy::Fixed;
    input.fixed_piece_length = 16U * 1024U;
    auto options = CreateOptions::create(std::move(input));
    REQUIRE(options);
    const TorrentEngine engine;
    const auto created = engine.create({content, torrent, std::move(options).value(), false});
    REQUIRE(created);

    std::ifstream torrent_input(torrent, std::ios::binary);
    REQUIRE(torrent_input);
    std::vector<std::uint8_t> encoded{std::istreambuf_iterator<char>(torrent_input),
                                      std::istreambuf_iterator<char>()};
    auto document = detail::decode_torrent(std::move(encoded), detail::MetadataReadMode::Strict);
    REQUIRE(document);

    std::vector<VerificationProgress> snapshots;
    VerifyRequest request{std::move(document).value(), content};
    request.on_progress = [&snapshots](const VerificationProgress& progress) {
        snapshots.push_back(progress);
    };

    const auto result = engine.verify(request);

    REQUIRE(result);
    REQUIRE(result.value().outcome == VerificationOutcome::Verified);
    REQUIRE_FALSE(snapshots.empty());
    std::size_t delivered_files{};
    std::size_t delivered_pieces{};
    std::uint64_t previous_sequence{};
    for (const auto& snapshot : snapshots)
    {
        REQUIRE(snapshot.sequence > previous_sequence);
        previous_sequence = snapshot.sequence;
        REQUIRE(snapshot.files.size() <= 256U);
        REQUIRE(snapshot.piece_ranges.size() <= 256U);
        delivered_files += snapshot.files.size();
        for (const auto& range : snapshot.piece_ranges)
        {
            delivered_pieces += static_cast<std::size_t>(range.end - range.begin);
        }
    }
    REQUIRE(delivered_files == file_count);
    REQUIRE(delivered_pieces == 1U);
}

TEST_CASE("given_out_of_order_completions_when_cadence_elapses_then_ranges_are_sorted_and_"
          "coalesced",
          "[unit][torrent-engine][verify][progress]")
{
    auto path = LogicalPath::from_segments({"payload.bin"});
    REQUIRE(path);
    std::vector<FileVerificationProgress> files;
    files.push_back({std::move(path).value(), 64, 0, 0, 0});
    detail::VerificationPieceLayouts layouts(4);
    for (auto& layout : layouts)
    {
        layout.overlaps.push_back({0, 16});
    }
    auto now = std::chrono::steady_clock::time_point{};
    std::vector<VerificationProgress> snapshots;
    detail::VerificationProgressPublisher publisher(
        std::move(files), layouts,
        [&snapshots](const VerificationProgress& progress) { snapshots.push_back(progress); }, {},
        [&now]() { return now; });

    publisher.record(2, PieceVerificationState::Verified);
    publisher.record(0, PieceVerificationState::Verified);
    publisher.record(1, PieceVerificationState::Verified);
    now += std::chrono::milliseconds{49};
    publisher.flush();
    REQUIRE(snapshots.empty());

    now += std::chrono::milliseconds{1};
    publisher.flush();

    REQUIRE(snapshots.size() == 1U);
    REQUIRE(snapshots.front().sequence == 1U);
    REQUIRE(snapshots.front().piece_ranges.size() == 1U);
    REQUIRE(snapshots.front().piece_ranges.front().begin == 0U);
    REQUIRE(snapshots.front().piece_ranges.front().end == 3U);
    REQUIRE(snapshots.front().files.size() == 1U);
    REQUIRE(snapshots.front().files.front().hashed_bytes == 48U);
    REQUIRE(snapshots.front().files.front().verified_bytes == 48U);

    publisher.record(3, PieceVerificationState::Mismatched);
    now += std::chrono::milliseconds{49};
    publisher.flush();
    REQUIRE(snapshots.size() == 1U);

    now += std::chrono::milliseconds{1};
    publisher.flush();

    REQUIRE(snapshots.size() == 2U);
    REQUIRE(snapshots.back().sequence == 2U);
    REQUIRE(snapshots.back().piece_ranges.size() == 1U);
    REQUIRE(snapshots.back().piece_ranges.front().begin == 3U);
    REQUIRE(snapshots.back().piece_ranges.front().end == 4U);
    REQUIRE(snapshots.back().files.size() == 1U);
    REQUIRE(snapshots.back().files.front().hashed_bytes == 64U);
    REQUIRE(snapshots.back().files.front().mismatched_bytes == 16U);
}

TEST_CASE("given_256_pending_ranges_when_recorded_then_progress_is_delivered_immediately",
          "[unit][torrent-engine][verify][progress]")
{
    auto path = LogicalPath::from_segments({"payload.bin"});
    REQUIRE(path);
    std::vector<FileVerificationProgress> files;
    files.push_back({std::move(path).value(), 256, 0, 0, 0});
    detail::VerificationPieceLayouts layouts(256);
    for (auto& layout : layouts)
    {
        layout.overlaps.push_back({0, 1});
    }
    const auto now = std::chrono::steady_clock::time_point{};
    std::vector<VerificationProgress> snapshots;
    detail::VerificationProgressPublisher publisher(
        std::move(files), layouts,
        [&snapshots](const VerificationProgress& progress) { snapshots.push_back(progress); }, {},
        [now]() { return now; });

    for (std::uint64_t piece = 0; piece < layouts.size(); ++piece)
    {
        const auto state = piece % 2U == 0U ? PieceVerificationState::Verified
                                            : PieceVerificationState::Mismatched;
        publisher.record(piece, state);
    }

    REQUIRE(snapshots.size() == 1U);
    REQUIRE(snapshots.front().piece_ranges.size() == 256U);
    REQUIRE(snapshots.front().files.size() == 1U);
}

TEST_CASE("given_256_pending_file_snapshots_when_recorded_then_progress_is_delivered_"
          "immediately",
          "[unit][torrent-engine][verify][progress]")
{
    constexpr std::size_t file_count = 256U;
    std::vector<FileVerificationProgress> files;
    files.reserve(file_count);
    for (std::size_t index = 0; index < file_count; ++index)
    {
        auto path = LogicalPath::from_segments({"file-" + std::to_string(index) + ".bin"});
        REQUIRE(path);
        files.push_back({std::move(path).value(), 1, 0, 0, 0});
    }
    detail::VerificationPieceLayouts layouts(1);
    for (std::size_t index = 0; index < file_count; ++index)
    {
        layouts.front().overlaps.push_back({index, 1});
    }
    const auto now = std::chrono::steady_clock::time_point{};
    std::vector<VerificationProgress> snapshots;
    detail::VerificationProgressPublisher publisher(
        std::move(files), layouts,
        [&snapshots](const VerificationProgress& progress) { snapshots.push_back(progress); }, {},
        [now]() { return now; });

    publisher.record(0, PieceVerificationState::Verified);

    REQUIRE(snapshots.size() == 1U);
    REQUIRE(snapshots.front().sequence == 1U);
    REQUIRE(snapshots.front().files.size() == file_count);
    REQUIRE(snapshots.front().piece_ranges.size() == 1U);
    REQUIRE(snapshots.front().piece_ranges.front().begin == 0U);
    REQUIRE(snapshots.front().piece_ranges.front().end == 1U);
}

TEST_CASE("given_progress_callback_cancellation_when_more_evidence_exists_then_no_final_drain_"
          "is_delivered",
          "[unit][torrent-engine][verify][progress][cancellation]")
{
    auto path = LogicalPath::from_segments({"payload.bin"});
    REQUIRE(path);
    std::vector<FileVerificationProgress> files;
    files.push_back({std::move(path).value(), 512, 0, 0, 0});
    detail::VerificationPieceLayouts layouts(512);
    for (auto& layout : layouts)
    {
        layout.overlaps.push_back({0, 1});
    }
    CancellationSource cancellation;
    std::size_t deliveries{};
    std::size_t delivered_ranges{};
    std::atomic<bool> callback_started{};
    std::atomic<bool> cancellation_finished{};
    detail::VerificationProgressPublisher publisher(
        std::move(files), layouts,
        [&callback_started, &cancellation_finished, &deliveries,
         &delivered_ranges](const VerificationProgress& progress) {
            ++deliveries;
            delivered_ranges += progress.piece_ranges.size();
            callback_started.store(true, std::memory_order_release);
            while (!cancellation_finished.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
        },
        cancellation.token(), []() { return std::chrono::steady_clock::time_point{}; });
    std::thread canceller([&callback_started, &cancellation, &cancellation_finished]() {
        while (!callback_started.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        cancellation.cancel();
        cancellation_finished.store(true, std::memory_order_release);
    });

    std::vector<PieceVerificationState> states;
    states.reserve(layouts.size());
    for (std::size_t piece = 0; piece < layouts.size(); ++piece)
    {
        states.push_back(piece % 2U == 0U ? PieceVerificationState::Verified
                                          : PieceVerificationState::Mismatched);
    }
    publisher.complete(states);
    canceller.join();

    REQUIRE(deliveries == 1U);
    REQUIRE(delivered_ranges == 256U);
}

TEST_CASE("given_no_progress_callback_when_verified_then_no_progress_publisher_is_constructed",
          "[integration][torrent-engine][verify][progress]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    {
        std::ofstream output(content, std::ios::binary);
        output << "0123456789abcdefghijklmnopqrstuvfedcba9876543210";
        REQUIRE(output);
    }
    const TorrentEngine engine;
    detail::ScopedTorrentEngineFault fault{
        detail::TorrentEngineFault::VerifyProgressPublisherConstruction};

    const auto without_callback = engine.verify({v1_three_piece_document(), content});

    REQUIRE(without_callback);
    VerifyRequest with_callback{v1_three_piece_document(), content};
    with_callback.on_progress = [](const VerificationProgress&) {};
    const auto with_callback_result = engine.verify(with_callback);
    REQUIRE_FALSE(with_callback_result);
    REQUIRE(with_callback_result.error().code == ErrorCode::Internal);
    REQUIRE(with_callback_result.error().message ==
            "could not initialize verification progress delivery: injected publisher construction "
            "failure");
}

TEST_CASE("given_mismatched_v1_single_file_when_verified_then_hash_mismatch_is_reported",
          "[integration][torrent-engine][verify]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    {
        std::ofstream output(content, std::ios::binary);
        output << "0123456789abcdef";
        REQUIRE(output);
    }
    const TorrentEngine engine;

    const auto result = engine.verify({v1_document(), content});

    REQUIRE(result);
    REQUIRE(result.value().outcome == VerificationOutcome::Mismatched);
    REQUIRE(result.value().expected_bytes == 16);
    REQUIRE(result.value().hashed_bytes == 16);
    REQUIRE(result.value().verified_bytes == 0);
    REQUIRE(result.value().mismatched_bytes == 16);
    REQUIRE(result.value().files.size() == 1);
    const auto& file = result.value().files.front();
    REQUIRE(file.expected_bytes == 16);
    REQUIRE(file.hashed_bytes == 16);
    REQUIRE(file.verified_bytes == 0);
    REQUIRE(file.mismatched_bytes == 16);
    REQUIRE(file.findings == FileVerificationFinding::HashMismatch);
}

TEST_CASE("given_fatal_backend_io_when_verified_then_result_is_io_failure_without_report",
          "[unit][torrent-engine][verify][io]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    {
        std::ofstream output(content, std::ios::binary);
        output << "0123456789abcdef";
        REQUIRE(output);
    }
    const TorrentEngine engine;
    detail::ScopedTorrentEngineFault fault{detail::TorrentEngineFault::VerifyBackendIo};

    const auto result = engine.verify({v1_document(), content});

    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == ErrorCode::IoFailure);
    REQUIRE(result.error().message ==
            "could not verify torrent content: injected backend I/O failure");
}

TEST_CASE("given_unreadable_regular_file_when_verified_then_access_denied_is_a_result_error",
          "[integration][torrent-engine][verify][io]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    {
        std::ofstream output(content, std::ios::binary);
        output << "0123456789abcdef";
        REQUIRE(output);
    }
    std::error_code permission_error;
    std::filesystem::permissions(content, std::filesystem::perms::none,
                                 std::filesystem::perm_options::replace, permission_error);
    REQUIRE_FALSE(permission_error);
    {
        std::ifstream probe(content, std::ios::binary);
        if (probe)
        {
            std::filesystem::permissions(
                content, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                std::filesystem::perm_options::replace, permission_error);
            SKIP("the test filesystem does not enforce unreadable file permissions");
        }
    }
    const TorrentEngine engine;

    const auto result = engine.verify({v1_document(), content});

    std::filesystem::permissions(
        content, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace, permission_error);
    REQUIRE_FALSE(permission_error);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == ErrorCode::AccessDenied);
}

TEST_CASE("given_missing_v1_single_file_when_verified_then_incomplete_report_is_returned",
          "[integration][torrent-engine][verify]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "missing.bin";
    const TorrentEngine engine;

    const auto result = engine.verify({v1_document(), content});

    REQUIRE(result);
    REQUIRE(result.value().outcome == VerificationOutcome::Incomplete);
    REQUIRE(result.value().expected_bytes == 16);
    REQUIRE(result.value().hashed_bytes == 0);
    REQUIRE(result.value().verified_bytes == 0);
    REQUIRE(result.value().mismatched_bytes == 0);
    REQUIRE(result.value().files.size() == 1);
    const auto& file = result.value().files.front();
    REQUIRE(file.path.segments() == std::vector<std::string>{"payload.bin"});
    REQUIRE(file.expected_bytes == 16);
    REQUIRE(file.hashed_bytes == 0);
    REQUIRE(file.verified_bytes == 0);
    REQUIRE(file.mismatched_bytes == 0);
    REQUIRE(file.findings == FileVerificationFinding::Missing);
}

TEST_CASE("given_short_v1_single_file_when_verified_then_length_mismatch_is_reported",
          "[integration][torrent-engine][verify]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    {
        std::ofstream output(content, std::ios::binary);
        output << "01234567";
        REQUIRE(output);
    }
    const TorrentEngine engine;

    const auto result = engine.verify({v1_document(), content});

    REQUIRE(result);
    REQUIRE(result.value().outcome == VerificationOutcome::Incomplete);
    REQUIRE(result.value().expected_bytes == 16);
    REQUIRE(result.value().hashed_bytes == 0);
    REQUIRE(result.value().verified_bytes == 0);
    REQUIRE(result.value().mismatched_bytes == 0);
    REQUIRE(result.value().files.size() == 1);
    const auto& file = result.value().files.front();
    REQUIRE(file.expected_bytes == 16);
    REQUIRE(file.hashed_bytes == 0);
    REQUIRE(file.verified_bytes == 0);
    REQUIRE(file.mismatched_bytes == 0);
    REQUIRE(file.findings == FileVerificationFinding::LengthMismatch);
}

TEST_CASE("given_directory_at_regular_file_path_when_verified_then_not_regular_file_is_reported",
          "[integration][torrent-engine][verify][finding]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    std::filesystem::create_directory(content);
    const TorrentEngine engine;

    const auto result = engine.verify({v1_document(), content});

    REQUIRE(result);
    REQUIRE(result.value().outcome == VerificationOutcome::Incomplete);
    REQUIRE(result.value().expected_bytes == 16);
    REQUIRE(result.value().hashed_bytes == 0);
    REQUIRE(result.value().verified_bytes == 0);
    REQUIRE(result.value().mismatched_bytes == 0);
    REQUIRE(result.value().files.size() == 1);
    const auto& file = result.value().files.front();
    REQUIRE(file.path.segments() == std::vector<std::string>{"payload.bin"});
    REQUIRE(file.expected_bytes == 16);
    REQUIRE(file.hashed_bytes == 0);
    REQUIRE(file.verified_bytes == 0);
    REQUIRE(file.mismatched_bytes == 0);
    REQUIRE(file.findings == FileVerificationFinding::NotRegularFile);
}

TEST_CASE("given_matching_v1_piece_spanning_two_files_when_verified_then_overlap_is_exact",
          "[integration][torrent-engine][verify]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    std::filesystem::create_directory(content);
    {
        std::ofstream output(content / "a.bin", std::ios::binary);
        output << "01234567";
        REQUIRE(output);
    }
    {
        std::ofstream output(content / "b.bin", std::ios::binary);
        output << "89abcdef";
        REQUIRE(output);
    }
    auto piece_hash = Sha1Digest::from_hex("fe5567e8d769550852182cdf69d74bb16dff8e29");
    REQUIRE(piece_hash);
    const TorrentEngine engine;

    const auto result =
        engine.verify({v1_two_file_document(std::move(piece_hash).value()), content});

    REQUIRE(result);
    REQUIRE(result.value().outcome == VerificationOutcome::Verified);
    REQUIRE(result.value().expected_bytes == 16);
    REQUIRE(result.value().hashed_bytes == 16);
    REQUIRE(result.value().verified_bytes == 16);
    REQUIRE(result.value().mismatched_bytes == 0);
    REQUIRE(result.value().files.size() == 2);
    for (const auto& file : result.value().files)
    {
        REQUIRE(file.expected_bytes == 8);
        REQUIRE(file.hashed_bytes == 8);
        REQUIRE(file.verified_bytes == 8);
        REQUIRE(file.mismatched_bytes == 0);
        REQUIRE(file.findings == FileVerificationFinding::None);
    }
}

TEST_CASE("given_mismatched_v1_piece_spanning_two_files_when_verified_then_attribution_is_shared",
          "[integration][torrent-engine][verify]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    std::filesystem::create_directory(content);
    {
        std::ofstream output(content / "a.bin", std::ios::binary);
        output << "01234567";
        REQUIRE(output);
    }
    {
        std::ofstream output(content / "b.bin", std::ios::binary);
        output << "89abcdef";
        REQUIRE(output);
    }
    const TorrentEngine engine;

    const auto result = engine.verify({v1_two_file_document(sha1(2)), content});

    REQUIRE(result);
    REQUIRE(result.value().outcome == VerificationOutcome::Mismatched);
    REQUIRE(result.value().expected_bytes == 16);
    REQUIRE(result.value().hashed_bytes == 16);
    REQUIRE(result.value().verified_bytes == 0);
    REQUIRE(result.value().mismatched_bytes == 16);
    REQUIRE(result.value().files.size() == 2);
    for (const auto& file : result.value().files)
    {
        REQUIRE(file.expected_bytes == 8);
        REQUIRE(file.hashed_bytes == 8);
        REQUIRE(file.verified_bytes == 0);
        REQUIRE(file.mismatched_bytes == 8);
        REQUIRE(has_finding(file.findings, FileVerificationFinding::SharedPieceMismatch));
        REQUIRE_FALSE(has_finding(file.findings, FileVerificationFinding::HashMismatch));
    }
}

TEST_CASE("given_missing_file_in_v1_spanning_piece_when_verified_then_piece_is_not_mismatched",
          "[integration][torrent-engine][verify]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    std::filesystem::create_directory(content);
    {
        std::ofstream output(content / "a.bin", std::ios::binary);
        output << "01234567";
        REQUIRE(output);
    }
    auto piece_hash = Sha1Digest::from_hex("fe5567e8d769550852182cdf69d74bb16dff8e29");
    REQUIRE(piece_hash);
    const TorrentEngine engine;

    const auto result =
        engine.verify({v1_two_file_document(std::move(piece_hash).value()), content});

    REQUIRE(result);
    REQUIRE(result.value().outcome == VerificationOutcome::Incomplete);
    REQUIRE(result.value().expected_bytes == 16);
    REQUIRE(result.value().hashed_bytes == 0);
    REQUIRE(result.value().verified_bytes == 0);
    REQUIRE(result.value().mismatched_bytes == 0);
    REQUIRE(result.value().files.size() == 2);
    const auto& present = result.value().files[0];
    REQUIRE(present.hashed_bytes == 0);
    REQUIRE(present.verified_bytes == 0);
    REQUIRE(present.mismatched_bytes == 0);
    REQUIRE(present.findings == FileVerificationFinding::None);
    const auto& missing = result.value().files[1];
    REQUIRE(missing.hashed_bytes == 0);
    REQUIRE(missing.verified_bytes == 0);
    REQUIRE(missing.mismatched_bytes == 0);
    REQUIRE(missing.findings == FileVerificationFinding::Missing);
}

TEST_CASE("given_bep47_read_symlink_access_denied_when_verified_then_result_is_access_denied",
          "[unit][torrent-engine][verify][bep47][io]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    std::filesystem::create_directory(content);
    {
        std::ofstream output(content / "target.bin", std::ios::binary);
        output << "0123456789abcdef";
        REQUIRE(output);
    }
    std::error_code symlink_error;
    std::filesystem::create_symlink("target.bin", content / "alias.bin", symlink_error);
    if (symlink_error)
    {
        SKIP("filesystem symlink creation is unavailable: " + symlink_error.message());
    }
    auto piece_hash = Sha1Digest::from_hex("fe5567e8d769550852182cdf69d74bb16dff8e29");
    REQUIRE(piece_hash);
    const TorrentEngine engine;
    detail::ScopedTorrentEngineFault fault{detail::TorrentEngineFault::ReadSymlinkAccessDenied};

    const auto result = engine.verify({v1_bep47_document(std::move(piece_hash).value()), content});

    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == ErrorCode::AccessDenied);
    REQUIRE(result.error().message == "could not read BEP 47 symlink target");
}

TEST_CASE("given_bep47_link_with_different_target_when_verified_then_identity_mismatch_is_reported",
          "[integration][torrent-engine][verify][bep47]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    std::filesystem::create_directory(content);
    {
        std::ofstream output(content / "target.bin", std::ios::binary);
        output << "0123456789abcdef";
        REQUIRE(output);
    }
    {
        std::ofstream output(content / "other.bin", std::ios::binary);
        output << "fedcba9876543210";
        REQUIRE(output);
    }
    std::error_code symlink_error;
    std::filesystem::create_symlink("other.bin", content / "alias.bin", symlink_error);
    if (symlink_error)
    {
        SKIP("filesystem symlink creation is unavailable: " + symlink_error.message());
    }
    auto piece_hash = Sha1Digest::from_hex("fe5567e8d769550852182cdf69d74bb16dff8e29");
    REQUIRE(piece_hash);
    const TorrentEngine engine;

    const auto result = engine.verify({v1_bep47_document(std::move(piece_hash).value()), content});

    REQUIRE(result);
    REQUIRE(result.value().outcome == VerificationOutcome::Mismatched);
    REQUIRE(result.value().expected_bytes == 16);
    REQUIRE(result.value().hashed_bytes == 16);
    REQUIRE(result.value().verified_bytes == 16);
    REQUIRE(result.value().mismatched_bytes == 0);
    REQUIRE(result.value().files.size() == 2);
    REQUIRE(result.value().files[0].findings == FileVerificationFinding::None);
    REQUIRE(result.value().files[1].expected_bytes == 0);
    REQUIRE(result.value().files[1].hashed_bytes == 0);
    REQUIRE(result.value().files[1].verified_bytes == 0);
    REQUIRE(result.value().files[1].mismatched_bytes == 0);
    REQUIRE(result.value().files[1].findings == FileVerificationFinding::SymlinkTargetMismatch);
}

TEST_CASE("given_missing_bep47_link_when_verified_then_symlink_missing_is_reported",
          "[integration][torrent-engine][verify][bep47]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    std::filesystem::create_directory(content);
    {
        std::ofstream output(content / "target.bin", std::ios::binary);
        output << "0123456789abcdef";
        REQUIRE(output);
    }
    auto piece_hash = Sha1Digest::from_hex("fe5567e8d769550852182cdf69d74bb16dff8e29");
    REQUIRE(piece_hash);
    const TorrentEngine engine;

    const auto result = engine.verify({v1_bep47_document(std::move(piece_hash).value()), content});

    REQUIRE(result);
    REQUIRE(result.value().outcome == VerificationOutcome::Incomplete);
    REQUIRE(result.value().expected_bytes == 16);
    REQUIRE(result.value().hashed_bytes == 16);
    REQUIRE(result.value().verified_bytes == 16);
    REQUIRE(result.value().mismatched_bytes == 0);
    REQUIRE(result.value().files.size() == 2);
    REQUIRE(result.value().files[0].findings == FileVerificationFinding::None);
    REQUIRE(result.value().files[1].expected_bytes == 0);
    REQUIRE(result.value().files[1].hashed_bytes == 0);
    REQUIRE(result.value().files[1].verified_bytes == 0);
    REQUIRE(result.value().files[1].mismatched_bytes == 0);
    REQUIRE(result.value().files[1].findings == FileVerificationFinding::SymlinkMissing);
}

TEST_CASE("given_lexically_equivalent_bep47_link_target_when_verified_then_identity_matches",
          "[integration][torrent-engine][verify][bep47]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    std::filesystem::create_directories(content / "nested");
    {
        std::ofstream output(content / "target.bin", std::ios::binary);
        output << "0123456789abcdef";
        REQUIRE(output);
    }
    std::error_code symlink_error;
    std::filesystem::create_symlink("nested/../target.bin", content / "alias.bin", symlink_error);
    if (symlink_error)
    {
        SKIP("filesystem symlink creation is unavailable: " + symlink_error.message());
    }
    auto piece_hash = Sha1Digest::from_hex("fe5567e8d769550852182cdf69d74bb16dff8e29");
    REQUIRE(piece_hash);
    const TorrentEngine engine;

    const auto result = engine.verify({v1_bep47_document(std::move(piece_hash).value()), content});

    REQUIRE(result);
    REQUIRE(result.value().outcome == VerificationOutcome::Verified);
    REQUIRE(result.value().expected_bytes == 16);
    REQUIRE(result.value().hashed_bytes == 16);
    REQUIRE(result.value().verified_bytes == 16);
    REQUIRE(result.value().mismatched_bytes == 0);
    REQUIRE(result.value().files.size() == 2);
    REQUIRE(result.value().files[0].findings == FileVerificationFinding::None);
    REQUIRE(result.value().files[1].findings == FileVerificationFinding::None);
}

TEST_CASE("given_bep47_link_path_with_regular_file_when_verified_then_symlink_missing_is_"
          "reported",
          "[integration][torrent-engine][verify][bep47]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    std::filesystem::create_directory(content);
    {
        std::ofstream output(content / "target.bin", std::ios::binary);
        output << "0123456789abcdef";
        REQUIRE(output);
    }
    {
        std::ofstream output(content / "alias.bin", std::ios::binary);
        output << "not a symlink";
        REQUIRE(output);
    }
    auto piece_hash = Sha1Digest::from_hex("fe5567e8d769550852182cdf69d74bb16dff8e29");
    REQUIRE(piece_hash);
    const TorrentEngine engine;

    const auto result = engine.verify({v1_bep47_document(std::move(piece_hash).value()), content});

    REQUIRE(result);
    REQUIRE(result.value().outcome == VerificationOutcome::Incomplete);
    REQUIRE(result.value().expected_bytes == 16);
    REQUIRE(result.value().hashed_bytes == 16);
    REQUIRE(result.value().verified_bytes == 16);
    REQUIRE(result.value().mismatched_bytes == 0);
    REQUIRE(result.value().files.size() == 2);
    REQUIRE(result.value().files[0].findings == FileVerificationFinding::None);
    REQUIRE(result.value().files[1].expected_bytes == 0);
    REQUIRE(result.value().files[1].hashed_bytes == 0);
    REQUIRE(result.value().files[1].verified_bytes == 0);
    REQUIRE(result.value().files[1].mismatched_bytes == 0);
    REQUIRE(result.value().files[1].findings == FileVerificationFinding::SymlinkMissing);
}

TEST_CASE("given_bep47_link_path_with_directory_when_verified_then_symlink_missing_is_reported",
          "[integration][torrent-engine][verify][bep47]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    std::filesystem::create_directories(content / "alias.bin");
    {
        std::ofstream output(content / "target.bin", std::ios::binary);
        output << "0123456789abcdef";
        REQUIRE(output);
    }
    auto piece_hash = Sha1Digest::from_hex("fe5567e8d769550852182cdf69d74bb16dff8e29");
    REQUIRE(piece_hash);
    const TorrentEngine engine;

    const auto result = engine.verify({v1_bep47_document(std::move(piece_hash).value()), content});

    REQUIRE(result);
    REQUIRE(result.value().outcome == VerificationOutcome::Incomplete);
    REQUIRE(result.value().expected_bytes == 16);
    REQUIRE(result.value().hashed_bytes == 16);
    REQUIRE(result.value().verified_bytes == 16);
    REQUIRE(result.value().mismatched_bytes == 0);
    REQUIRE(result.value().files.size() == 2);
    REQUIRE(result.value().files[0].findings == FileVerificationFinding::None);
    REQUIRE(result.value().files[1].expected_bytes == 0);
    REQUIRE(result.value().files[1].hashed_bytes == 0);
    REQUIRE(result.value().files[1].verified_bytes == 0);
    REQUIRE(result.value().files[1].mismatched_bytes == 0);
    REQUIRE(result.value().files[1].findings == FileVerificationFinding::SymlinkMissing);
}

TEST_CASE("given_piece_range_bounds_when_checked_then_only_non_empty_half_open_ranges_are_valid",
          "[unit][torrent-engine][verification-contract]")
{
    REQUIRE(is_valid(PieceRange{0, 1, PieceVerificationState::Verified}));
    REQUIRE(is_valid(PieceRange{4, 9, PieceVerificationState::Mismatched}));
    REQUIRE_FALSE(is_valid(PieceRange{3, 3, PieceVerificationState::Verified}));
    REQUIRE_FALSE(is_valid(PieceRange{8, 7, PieceVerificationState::Verified}));
}

TEST_CASE("given_missing_content_root_when_created_then_file_not_found_is_reported",
          "[unit][torrent-engine][create]")
{
    auto options = CreateOptions::create();
    REQUIRE(options);
    const auto missing = std::filesystem::temp_directory_path() /
                         "torrentutils-phase5-create-content-root-does-not-exist";
    const auto target =
        std::filesystem::temp_directory_path() / "torrentutils-phase5-create-target.torrent";
    const CreateRequest request{missing, target, std::move(options).value()};
    const TorrentEngine engine;

    const auto result = engine.create(request);

    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == ErrorCode::FileNotFound);
    REQUIRE(result.error().message == "create content root does not exist");
}

TEST_CASE("given_regular_file_when_created_as_v1_then_committed_torrent_matches_result",
          "[integration][torrent-engine][create]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    const auto target = temp.path() / "payload.torrent";
    {
        std::ofstream output(content, std::ios::binary);
        output << "0123456789abcdef";
        REQUIRE(output);
    }

    CreateOptionsInput input;
    input.format = TorrentFormat::V1;
    input.piece_length_strategy = PieceLengthStrategy::Fixed;
    input.fixed_piece_length = 16U * 1024U;
    auto options = CreateOptions::create(std::move(input));
    REQUIRE(options);
    const CreateRequest request{content, target, std::move(options).value()};
    const TorrentEngine engine;

    const auto result = engine.create(request);

    REQUIRE(result);
    REQUIRE(result.value().target_path == target);
    REQUIRE(result.value().format == TorrentFormat::V1);
    REQUIRE(result.value().payload_bytes == 16);
    REQUIRE(result.value().piece_length == 16U * 1024U);
    REQUIRE(result.value().info_hashes.v1().has_value());
    REQUIRE_FALSE(result.value().info_hashes.v2().has_value());
    REQUIRE(std::filesystem::is_regular_file(target));
}

TEST_CASE("given_creation_metadata_when_created_then_it_is_top_level_and_preserves_info_hash",
          "[integration][torrent-engine][create]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    const auto plain_target = temp.path() / "plain.torrent";
    const auto metadata_target = temp.path() / "metadata.torrent";
    {
        std::ofstream output(content, std::ios::binary);
        output << "0123456789abcdef";
        REQUIRE(output);
    }

    CreateOptionsInput input;
    input.format = TorrentFormat::V1;
    input.piece_length_strategy = PieceLengthStrategy::Fixed;
    input.fixed_piece_length = 16U * 1024U;
    auto options = CreateOptions::create(std::move(input));
    REQUIRE(options);
    const TorrentEngine engine;
    const auto plain = engine.create({content, plain_target, options.value()});
    const auto metadata = engine.create({content,
                                         metadata_target,
                                         options.value(),
                                         false,
                                         {"release comment", "TorrentCraft", 1234567890}});

    REQUIRE(plain);
    REQUIRE(metadata);
    REQUIRE(plain.value().info_hashes.v1() == metadata.value().info_hashes.v1());

    std::ifstream input_file(metadata_target, std::ios::binary);
    REQUIRE(input_file);
    std::vector<std::uint8_t> encoded{std::istreambuf_iterator<char>(input_file),
                                      std::istreambuf_iterator<char>()};
    auto document = detail::decode_torrent(std::move(encoded), detail::MetadataReadMode::Strict);
    REQUIRE(document);
    REQUIRE(document.value().metadata().comment() == "release comment");
    REQUIRE(document.value().metadata().creator() == "TorrentCraft");
    REQUIRE(document.value().metadata().creation_time_unix_seconds() == 1234567890);
    REQUIRE_FALSE(detail::source_is_in_info(document.value()));
}

TEST_CASE("given_create_info_source_when_created_then_it_is_in_info_and_changes_info_hash",
          "[integration][torrent-engine][create]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    const auto plain_target = temp.path() / "plain.torrent";
    const auto sourced_target = temp.path() / "sourced.torrent";
    {
        std::ofstream output(content, std::ios::binary);
        output << "0123456789abcdef";
        REQUIRE(output);
    }

    CreateOptionsInput input;
    input.format = TorrentFormat::V1;
    input.piece_length_strategy = PieceLengthStrategy::Fixed;
    input.fixed_piece_length = 16U * 1024U;
    auto options = CreateOptions::create(std::move(input));
    REQUIRE(options);
    const TorrentEngine engine;
    const auto plain = engine.create({content, plain_target, options.value()});
    const auto sourced =
        engine.create({content, sourced_target, options.value(), false, {}, {"private-tracker"}});

    REQUIRE(plain);
    REQUIRE(sourced);
    REQUIRE(plain.value().info_hashes.v1() != sourced.value().info_hashes.v1());

    std::ifstream input_file(sourced_target, std::ios::binary);
    REQUIRE(input_file);
    std::vector<std::uint8_t> encoded{std::istreambuf_iterator<char>(input_file),
                                      std::istreambuf_iterator<char>()};
    auto document = detail::decode_torrent(std::move(encoded), detail::MetadataReadMode::Strict);
    REQUIRE(document);
    REQUIRE(document.value().metadata().source() == "private-tracker");
    REQUIRE(detail::source_is_in_info(document.value()));
}

TEST_CASE("given_invalid_creation_text_when_created_then_request_is_rejected",
          "[unit][torrent-engine][create]")
{
    auto options = CreateOptions::create();
    REQUIRE(options);
    const std::string invalid_utf8{"\xC3\x28", 2};
    const CreateRequest request{"content",
                                "target.torrent",
                                std::move(options).value(),
                                false,
                                {invalid_utf8, invalid_utf8, std::nullopt},
                                {invalid_utf8}};
    const TorrentEngine engine;

    const auto result = engine.create(request);

    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == ErrorCode::ValidationFailed);
    REQUIRE(result.error().issues.size() == 3U);
    REQUIRE(result.error().issues[0].field == "create.creation_metadata.comment");
    REQUIRE(result.error().issues[1].field == "create.creation_metadata.created_by");
    REQUIRE(result.error().issues[2].field == "create.create_info.source");
}

TEST_CASE("given_utf8_boundary_sequences_when_creation_is_validated_then_each_is_classified",
          "[unit][torrent-engine][create]")
{
    struct Utf8Case
    {
        std::string value;
        bool valid;
    };
    const std::vector<Utf8Case> cases{
        {std::string{"\xC2\xA2", 2}, true},
        {std::string{"\xE2\x82\xAC", 3}, true},
        {std::string{"\xF0\x9F\x92\xA9", 4}, true},
        {std::string{"\xF4\x8F\xBF\xBF", 4}, true},
        {std::string{"\x80", 1}, false},
        {std::string{"\xC2", 1}, false},
        {std::string{"\xE2\x82", 2}, false},
        {std::string{"\xF0\x9F\x92", 3}, false},
        {std::string{"\xC3\x28", 2}, false},
        {std::string{"\xC0\x80", 2}, false},
        {std::string{"\xED\xA0\x80", 3}, false},
        {std::string{"\xF4\x90\x80\x80", 4}, false},
    };
    const TorrentEngine engine;

    for (const auto& test_case : cases)
    {
        DYNAMIC_SECTION("valid=" << test_case.valid << " size=" << test_case.value.size())
        {
            auto options = CreateOptions::create();
            REQUIRE(options);
            const CreateRequest request{{},
                                        {},
                                        std::move(options).value(),
                                        false,
                                        {test_case.value, std::nullopt, std::nullopt}};

            const auto result = engine.create(request);

            REQUIRE_FALSE(result);
            REQUIRE(result.error().code == ErrorCode::ValidationFailed);
            const auto issue =
                std::find_if(result.error().issues.begin(), result.error().issues.end(),
                             [](const FieldIssue& value) {
                                 return value.field == "create.creation_metadata.comment";
                             });
            CHECK((issue == result.error().issues.end()) == test_case.valid);
        }
    }
}

TEST_CASE(
    "given_existing_target_and_overwrite_enabled_when_created_then_target_is_atomically_replaced",
    "[integration][torrent-engine][create]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    const auto target = temp.path() / "payload.torrent";
    {
        std::ofstream output(content, std::ios::binary);
        output << "0123456789abcdef";
        REQUIRE(output);
    }
    {
        std::ofstream output(target, std::ios::binary);
        output << "existing creation target";
        REQUIRE(output);
    }

    CreateOptionsInput input;
    input.format = TorrentFormat::V1;
    input.piece_length_strategy = PieceLengthStrategy::Fixed;
    input.fixed_piece_length = 16U * 1024U;
    auto options = CreateOptions::create(std::move(input));
    REQUIRE(options);
    const TorrentEngine engine;

    const auto result = engine.create({content, target, std::move(options).value(), true});

    REQUIRE(result);
    REQUIRE(result.value().target_path == target);
    std::ifstream input_file(target, std::ios::binary);
    REQUIRE(input_file);
    const std::string encoded{std::istreambuf_iterator<char>(input_file),
                              std::istreambuf_iterator<char>()};
    REQUIRE(encoded != "existing creation target");
    REQUIRE(encoded.find("4:info") != std::string::npos);
    REQUIRE(temporary_sibling_count(target) == 0);
}

TEST_CASE("given_existing_target_and_hashing_cancellation_when_overwrite_enabled_then_old_target_"
          "is_unchanged",
          "[integration][torrent-engine][create]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    const auto target = temp.path() / "payload.torrent";
    {
        std::ofstream output(content, std::ios::binary);
        output << "0123456789abcdef";
        REQUIRE(output);
    }
    {
        std::ofstream output(target, std::ios::binary);
        output << "existing creation target";
        REQUIRE(output);
    }

    CreateOptionsInput input;
    input.format = TorrentFormat::V1;
    input.piece_length_strategy = PieceLengthStrategy::Fixed;
    input.fixed_piece_length = 16U * 1024U;
    auto options = CreateOptions::create(std::move(input));
    REQUIRE(options);
    CancellationSource cancellation;
    TaskContext context;
    context.cancellation = cancellation.token();
    context.on_progress = [&cancellation](const ProgressInfo&) { cancellation.cancel(); };
    const TorrentEngine engine;

    const auto result = engine.create({content, target, std::move(options).value(), true}, context);

    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == ErrorCode::Cancelled);
    std::ifstream input_file(target, std::ios::binary);
    REQUIRE(input_file);
    const std::string contents{std::istreambuf_iterator<char>(input_file),
                               std::istreambuf_iterator<char>()};
    REQUIRE(contents == "existing creation target");
    REQUIRE(temporary_sibling_count(target) == 0);
}

TEST_CASE("given_metadata_encoding_failure_when_created_then_no_target_or_temporary_sibling_"
          "is_left",
          "[unit][torrent-engine][create][io]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    const auto target = temp.path() / "payload.torrent";
    {
        std::ofstream output(content, std::ios::binary);
        output << "0123456789abcdef";
        REQUIRE(output);
    }

    CreateOptionsInput input;
    input.format = TorrentFormat::V1;
    input.piece_length_strategy = PieceLengthStrategy::Fixed;
    input.fixed_piece_length = 16U * 1024U;
    auto options = CreateOptions::create(std::move(input));
    REQUIRE(options);
    const TorrentEngine engine;
    detail::ScopedTorrentEngineFault fault{detail::TorrentEngineFault::CreateMetadataEncoding};

    const auto result = engine.create({content, target, std::move(options).value(), false});

    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == ErrorCode::Internal);
    REQUIRE(result.error().message ==
            "could not generate torrent metadata: injected metadata encoding failure");
    REQUIRE_FALSE(std::filesystem::exists(target));
    REQUIRE(temporary_sibling_count(target) == 0U);
}

TEST_CASE("given_unreplaceable_target_when_created_then_temporary_sibling_is_removed",
          "[integration][torrent-engine][create]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    const auto target = temp.path() / "payload.torrent";
    {
        std::ofstream output(content, std::ios::binary);
        output << "0123456789abcdef";
        REQUIRE(output);
    }
    std::filesystem::create_directory(target);

    CreateOptionsInput input;
    input.format = TorrentFormat::V1;
    input.piece_length_strategy = PieceLengthStrategy::Fixed;
    input.fixed_piece_length = 16U * 1024U;
    auto options = CreateOptions::create(std::move(input));
    REQUIRE(options);
    const TorrentEngine engine;

    const auto result = engine.create({content, target, std::move(options).value(), true});

    REQUIRE_FALSE(result);
    REQUIRE((result.error().code == ErrorCode::IoFailure ||
             result.error().code == ErrorCode::AccessDenied));
    REQUIRE(std::filesystem::is_directory(target));
    REQUIRE(temporary_sibling_count(target) == 0);
}

TEST_CASE("given_unreadable_create_payload_when_hashed_then_access_denied_leaves_no_output",
          "[integration][torrent-engine][create][io]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    const auto target = temp.path() / "payload.torrent";
    {
        std::ofstream output(content, std::ios::binary);
        output << std::string(std::size_t{32} * 1024U, 'x');
        REQUIRE(output);
    }
    std::error_code permission_error;
    std::filesystem::permissions(content, std::filesystem::perms::none,
                                 std::filesystem::perm_options::replace, permission_error);
    REQUIRE_FALSE(permission_error);
    {
        std::ifstream probe(content, std::ios::binary);
        if (probe)
        {
            std::filesystem::permissions(
                content, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                std::filesystem::perm_options::replace, permission_error);
            SKIP("the test filesystem does not enforce unreadable file permissions");
        }
    }
    CreateOptionsInput input;
    input.format = TorrentFormat::V1;
    input.piece_length_strategy = PieceLengthStrategy::Fixed;
    input.fixed_piece_length = 16U * 1024U;
    auto options = CreateOptions::create(std::move(input));
    REQUIRE(options);
    const TorrentEngine engine;

    const auto result = engine.create({content, target, std::move(options).value(), false});

    std::filesystem::permissions(
        content, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace, permission_error);
    REQUIRE_FALSE(permission_error);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == ErrorCode::AccessDenied);
    REQUIRE_FALSE(std::filesystem::exists(target));
    REQUIRE(temporary_sibling_count(target) == 0);
}

TEST_CASE("given_unwritable_target_directory_when_created_then_write_failure_leaves_no_output",
          "[integration][torrent-engine][create][io]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    const auto output_directory = temp.path() / "output";
    const auto target = output_directory / "payload.torrent";
    {
        std::ofstream output(content, std::ios::binary);
        output << "0123456789abcdef";
        REQUIRE(output);
    }
    std::filesystem::create_directory(output_directory);
    std::error_code permission_error;
    std::filesystem::permissions(
        output_directory, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
        std::filesystem::perm_options::replace, permission_error);
    REQUIRE_FALSE(permission_error);
    {
        std::ofstream probe(output_directory / "permission-probe");
        if (probe)
        {
            probe.close();
            std::filesystem::permissions(output_directory, std::filesystem::perms::owner_all,
                                         std::filesystem::perm_options::replace, permission_error);
            std::filesystem::remove(output_directory / "permission-probe");
            SKIP("the test filesystem does not enforce unwritable directory permissions");
        }
    }
    CreateOptionsInput input;
    input.format = TorrentFormat::V1;
    input.piece_length_strategy = PieceLengthStrategy::Fixed;
    input.fixed_piece_length = 16U * 1024U;
    auto options = CreateOptions::create(std::move(input));
    REQUIRE(options);
    const TorrentEngine engine;

    const auto result = engine.create({content, target, std::move(options).value(), false});

    std::filesystem::permissions(output_directory, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, permission_error);
    REQUIRE_FALSE(permission_error);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == ErrorCode::IoFailure);
    REQUIRE(result.error().message == "could not create temporary torrent file");
    REQUIRE_FALSE(std::filesystem::exists(target));
    REQUIRE(temporary_sibling_count(target) == 0);
}

TEST_CASE("given_regular_file_when_created_as_v2_then_only_v2_info_hash_is_committed",
          "[integration][torrent-engine][create]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    const auto target = temp.path() / "payload.torrent";
    {
        std::ofstream output(content, std::ios::binary);
        output << "0123456789abcdef";
        REQUIRE(output);
    }

    CreateOptionsInput input;
    input.format = TorrentFormat::V2;
    input.piece_length_strategy = PieceLengthStrategy::Fixed;
    input.fixed_piece_length = 16U * 1024U;
    auto options = CreateOptions::create(std::move(input));
    REQUIRE(options);
    const TorrentEngine engine;

    const auto result = engine.create({content, target, std::move(options).value(), false});

    REQUIRE(result);
    REQUIRE(result.value().format == TorrentFormat::V2);
    REQUIRE(result.value().payload_bytes == 16);
    REQUIRE_FALSE(result.value().info_hashes.v1().has_value());
    REQUIRE(result.value().info_hashes.v2().has_value());
    std::ifstream input_file(target, std::ios::binary);
    REQUIRE(input_file);
    const std::string encoded{std::istreambuf_iterator<char>(input_file),
                              std::istreambuf_iterator<char>()};
    REQUIRE(encoded.find("12:meta versioni2e") != std::string::npos);
}

TEST_CASE("given_private_trackers_and_web_seed_when_created_then_options_are_encoded",
          "[integration][torrent-engine][create]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    const auto target = temp.path() / "payload.torrent";
    {
        std::ofstream output(content, std::ios::binary);
        output << "payload";
        REQUIRE(output);
    }

    auto first_tracker = TrackerUrl::parse("https://one.example/announce");
    REQUIRE(first_tracker);
    auto second_tracker = TrackerUrl::parse("https://two.example/announce");
    REQUIRE(second_tracker);
    auto first_tier = TrackerTier::create({std::move(first_tracker).value()});
    REQUIRE(first_tier);
    auto second_tier = TrackerTier::create({std::move(second_tracker).value()});
    REQUIRE(second_tier);
    auto web_seed = WebSeedUrl::parse("https://seed.example/payload.bin");
    REQUIRE(web_seed);

    CreateOptionsInput input;
    input.format = TorrentFormat::V1;
    input.piece_length_strategy = PieceLengthStrategy::Fixed;
    input.fixed_piece_length = 16U * 1024U;
    input.is_private = true;
    input.tracker_tiers.push_back(std::move(first_tier).value());
    input.tracker_tiers.push_back(std::move(second_tier).value());
    input.web_seeds.push_back(std::move(web_seed).value());
    auto options = CreateOptions::create(std::move(input));
    REQUIRE(options);
    const TorrentEngine engine;

    const auto result = engine.create({content, target, std::move(options).value(), false});

    REQUIRE(result);
    std::ifstream input_file(target, std::ios::binary);
    REQUIRE(input_file);
    const std::string encoded{std::istreambuf_iterator<char>(input_file),
                              std::istreambuf_iterator<char>()};
    REQUIRE(encoded.find("8:announce") != std::string::npos);
    REQUIRE(encoded.find("13:announce-list") != std::string::npos);
    REQUIRE(encoded.find("https://one.example/announce") != std::string::npos);
    REQUIRE(encoded.find("https://two.example/announce") != std::string::npos);
    REQUIRE(encoded.find("8:url-list") != std::string::npos);
    REQUIRE(encoded.find("https://seed.example/payload.bin") != std::string::npos);
    REQUIRE(encoded.find("7:privatei1e") != std::string::npos);
}

TEST_CASE("given_regular_directory_when_created_as_v1_then_all_payload_bytes_are_committed",
          "[integration][torrent-engine][create]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    const auto target = temp.path() / "payload.torrent";
    std::filesystem::create_directories(content / "nested");
    {
        std::ofstream first(content / "a.bin", std::ios::binary);
        first << "abc";
        REQUIRE(first);
        std::ofstream second(content / "nested" / "b.bin", std::ios::binary);
        second << "12345";
        REQUIRE(second);
    }

    CreateOptionsInput input;
    input.format = TorrentFormat::V1;
    input.piece_length_strategy = PieceLengthStrategy::Fixed;
    input.fixed_piece_length = 16U * 1024U;
    auto options = CreateOptions::create(std::move(input));
    REQUIRE(options);
    const TorrentEngine engine;

    const auto result = engine.create({content, target, std::move(options).value(), false});

    REQUIRE(result);
    REQUIRE(result.value().target_path == target);
    REQUIRE(result.value().format == TorrentFormat::V1);
    REQUIRE(result.value().payload_bytes == 8);
    REQUIRE(result.value().piece_length == 16U * 1024U);
    REQUIRE(result.value().info_hashes.v1().has_value());
    REQUIRE_FALSE(result.value().info_hashes.v2().has_value());
    REQUIRE(std::filesystem::file_size(target) > 0);
}

TEST_CASE("given_equivalent_directory_trees_with_different_creation_order_when_created_then_"
          "metadata_is_identical",
          "[integration][torrent-engine][create][deterministic]")
{
    const TempDirectory temp;
    const auto first_content = temp.path() / "first" / "payload";
    const auto second_content = temp.path() / "second" / "payload";
    const auto first_target = temp.path() / "first.torrent";
    const auto second_target = temp.path() / "second.torrent";

    std::filesystem::create_directories(first_content / "nested");
    {
        std::ofstream first(first_content / "a.bin", std::ios::binary);
        first << std::string(10000, 'a');
        REQUIRE(first);
        std::ofstream second(first_content / "nested" / "b.bin", std::ios::binary);
        second << std::string(100, 'b');
        REQUIRE(second);
    }

    std::filesystem::create_directories(second_content);
    {
        std::ofstream first(second_content / "a.bin", std::ios::binary);
        first << std::string(10000, 'a');
        REQUIRE(first);
    }
    std::filesystem::create_directories(second_content / "nested");
    {
        std::ofstream second(second_content / "nested" / "b.bin", std::ios::binary);
        second << std::string(100, 'b');
        REQUIRE(second);
    }

    CreateOptionsInput input;
    input.format = TorrentFormat::V1;
    input.piece_length_strategy = PieceLengthStrategy::Fixed;
    input.fixed_piece_length = 16U * 1024U;
    auto first_options = CreateOptions::create(input);
    REQUIRE(first_options);
    auto second_options = CreateOptions::create(std::move(input));
    REQUIRE(second_options);
    const TorrentEngine engine;

    const auto first_result =
        engine.create({first_content, first_target, std::move(first_options).value(), false});
    const auto second_result =
        engine.create({second_content, second_target, std::move(second_options).value(), false});

    REQUIRE(first_result);
    REQUIRE(second_result);
    REQUIRE(first_result.value().info_hashes.v1() == second_result.value().info_hashes.v1());
    std::ifstream first_input(first_target, std::ios::binary);
    std::ifstream second_input(second_target, std::ios::binary);
    REQUIRE(first_input);
    REQUIRE(second_input);
    const std::vector<std::uint8_t> first_bytes{std::istreambuf_iterator<char>(first_input),
                                                std::istreambuf_iterator<char>()};
    const std::vector<std::uint8_t> second_bytes{std::istreambuf_iterator<char>(second_input),
                                                 std::istreambuf_iterator<char>()};
    REQUIRE(first_bytes == second_bytes);
}

TEST_CASE("given_versioned_create_matrix_when_created_then_v1_v2_and_hybrid_match_libtorrent_"
          "fixtures_and_recheck",
          "[integration][torrent-engine][create][fixture]")
{
    struct CreateMatrixCase
    {
        TorrentFormat format;
        std::string fixture;
        std::size_t document_file_count;
        std::string expected_v1;
        std::string expected_v2;
    };
    const std::vector<CreateMatrixCase> cases{
        {TorrentFormat::V1, "create-matrix-v1.torrent", 2,
         "cf932f2cdf1fb5bf357dbd4c1c97c95fbc96b07d", ""},
        {TorrentFormat::V2, "create-matrix-v2.torrent", 2, "",
         "974c6e1d173260e47ae5f979ba8705562ef41b49e0062fc93c2613be15dccf68"},
        {TorrentFormat::Hybrid, "create-matrix-hybrid.torrent", 4,
         "b3646b48e0ecbf9034dc1bfc2955e8122902799b",
         "5d31bb65bbbcaf2fdd841b4937bc1e0c6fcd5dfa90087e233cb4f5c167075310"},
    };

    for (const auto& test_case : cases)
    {
        DYNAMIC_SECTION(test_case.fixture)
        {
            const TempDirectory temp;
            const auto content = temp.path() / "payload";
            const auto target = temp.path() / test_case.fixture;
            std::filesystem::create_directories(content / "nested");
            {
                std::ofstream first(content / "a.bin", std::ios::binary);
                first << std::string(10000, 'a');
                REQUIRE(first);
                std::ofstream second(content / "nested" / "b.bin", std::ios::binary);
                second << std::string(100, 'b');
                REQUIRE(second);
            }

            auto first_tracker = TrackerUrl::parse("https://one.example/announce");
            REQUIRE(first_tracker);
            auto second_tracker = TrackerUrl::parse("https://two.example/announce");
            REQUIRE(second_tracker);
            auto first_tier = TrackerTier::create({std::move(first_tracker).value()});
            REQUIRE(first_tier);
            auto second_tier = TrackerTier::create({std::move(second_tracker).value()});
            REQUIRE(second_tier);
            auto web_seed = WebSeedUrl::parse("https://seed.example/payload/");
            REQUIRE(web_seed);

            CreateOptionsInput input;
            input.format = test_case.format;
            input.piece_length_strategy = PieceLengthStrategy::Fixed;
            input.fixed_piece_length = 16U * 1024U;
            input.is_private = true;
            input.tracker_tiers.push_back(std::move(first_tier).value());
            input.tracker_tiers.push_back(std::move(second_tier).value());
            input.web_seeds.push_back(std::move(web_seed).value());
            auto options = CreateOptions::create(std::move(input));
            REQUIRE(options);
            const TorrentEngine engine;

            const auto result = engine.create({content, target, std::move(options).value(), false});

            REQUIRE(result);
            REQUIRE(result.value().format == test_case.format);
            REQUIRE(result.value().payload_bytes == 10100);
            REQUIRE(result.value().piece_length == 16U * 1024U);
            REQUIRE(
                (result.value().info_hashes.v1().has_value() == !test_case.expected_v1.empty()));
            REQUIRE(
                (result.value().info_hashes.v2().has_value() == !test_case.expected_v2.empty()));
            if (!test_case.expected_v1.empty())
                REQUIRE(require_optional(result.value().info_hashes.v1()).to_hex() ==
                        test_case.expected_v1);
            if (!test_case.expected_v2.empty())
                REQUIRE(require_optional(result.value().info_hashes.v2()).to_hex() ==
                        test_case.expected_v2);
            std::ifstream torrent_input(target, std::ios::binary);
            REQUIRE(torrent_input);
            const std::vector<std::uint8_t> created_bytes{
                std::istreambuf_iterator<char>(torrent_input), std::istreambuf_iterator<char>()};
            REQUIRE(created_bytes == torrent_engine_fixture(test_case.fixture));

            auto document = detail::decode_torrent(created_bytes, detail::MetadataReadMode::Strict);
            REQUIRE(document);
            REQUIRE(document.value().info().format() == test_case.format);
            REQUIRE(document.value().info().is_private());
            REQUIRE(document.value().info().files().size() == test_case.document_file_count);
            REQUIRE(document.value().trackers().tiers().size() == 2);
            REQUIRE(document.value().trackers().tiers()[0].trackers()[0].value() ==
                    "https://one.example/announce");
            REQUIRE(document.value().trackers().tiers()[1].trackers()[0].value() ==
                    "https://two.example/announce");
            REQUIRE(document.value().metadata().web_seeds().size() == 1);
            REQUIRE(document.value().metadata().web_seeds()[0].value() ==
                    "https://seed.example/payload/");

            const auto verification = engine.verify({std::move(document).value(), content});
            REQUIRE(verification);
            REQUIRE(verification.value().outcome == VerificationOutcome::Verified);
            REQUIRE(verification.value().expected_bytes == 10100);
            REQUIRE(verification.value().verified_bytes == 10100);
        }
    }
}

TEST_CASE("given_versioned_file_order_policy_matrix_when_created_then_golden_layouts_and_hashes_"
          "match",
          "[integration][torrent-engine][create][file-order][fixture]")
{
    struct FileOrderCase
    {
        FileOrderPolicy policy;
        TorrentFormat format;
        std::string fixture;
    };
    const std::vector<FileOrderCase> cases{
        {FileOrderPolicy::Lexicographical, TorrentFormat::V1,
         "file-order-policy/file-order-lexicographical-v1.torrent"},
        {FileOrderPolicy::Lexicographical, TorrentFormat::V2,
         "file-order-policy/file-order-lexicographical-v2.torrent"},
        {FileOrderPolicy::Lexicographical, TorrentFormat::Hybrid,
         "file-order-policy/file-order-lexicographical-hybrid.torrent"},
        {FileOrderPolicy::CanonicalAlignment, TorrentFormat::V1,
         "file-order-policy/file-order-canonical-alignment-v1.torrent"},
        {FileOrderPolicy::CanonicalAlignment, TorrentFormat::V2,
         "file-order-policy/file-order-canonical-alignment-v2.torrent"},
        {FileOrderPolicy::CanonicalAlignment, TorrentFormat::Hybrid,
         "file-order-policy/file-order-canonical-alignment-hybrid.torrent"},
        {FileOrderPolicy::Natural, TorrentFormat::V1,
         "file-order-policy/file-order-natural-v1.torrent"},
        {FileOrderPolicy::Natural, TorrentFormat::V2,
         "file-order-policy/file-order-natural-v2.torrent"},
        {FileOrderPolicy::Natural, TorrentFormat::Hybrid,
         "file-order-policy/file-order-natural-hybrid.torrent"},
        {FileOrderPolicy::BreadthFirst, TorrentFormat::V1,
         "file-order-policy/file-order-breadth-first-v1.torrent"},
        {FileOrderPolicy::BreadthFirst, TorrentFormat::V2,
         "file-order-policy/file-order-breadth-first-v2.torrent"},
        {FileOrderPolicy::BreadthFirst, TorrentFormat::Hybrid,
         "file-order-policy/file-order-breadth-first-hybrid.torrent"},
    };
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    std::filesystem::create_directories(content / "nested");
    {
        std::ofstream first(content / "root10.bin", std::ios::binary);
        first << std::string(9000, 'a');
        REQUIRE(first);
        std::ofstream second(content / "root2.bin", std::ios::binary);
        second << std::string(9000, 'b');
        REQUIRE(second);
        std::ofstream third(content / "nested" / "child.bin", std::ios::binary);
        third << std::string(9000, 'c');
        REQUIRE(third);
        std::ofstream(content / "empty.bin", std::ios::binary).close();
    }

    const TorrentEngine engine;
    for (const auto& test_case : cases)
    {
        DYNAMIC_SECTION(test_case.fixture)
        {
            CreateOptionsInput input;
            input.format = test_case.format;
            input.piece_length_strategy = PieceLengthStrategy::Fixed;
            input.fixed_piece_length = 16U * 1024U;
            input.file_order_policy = test_case.policy;
            auto options = CreateOptions::create(std::move(input));
            REQUIRE(options);
            const auto target = temp.path() / test_case.fixture;
            std::filesystem::create_directories(target.parent_path());
            const auto result = engine.create({content, target, std::move(options).value(), false});
            REQUIRE(result);
            REQUIRE(result.value().format == test_case.format);
            REQUIRE(result.value().payload_bytes == 27000);
            REQUIRE(result.value().piece_length == 16U * 1024U);
            std::ifstream input_file(target, std::ios::binary);
            REQUIRE(input_file);
            const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input_file),
                                                  std::istreambuf_iterator<char>()};
            REQUIRE(bytes == torrent_engine_fixture(test_case.fixture));
            REQUIRE(detail::decode_torrent(bytes, detail::MetadataReadMode::Strict));
        }
    }
}

TEST_CASE("given_file_order_policies_with_bep47_symlink_when_created_then_link_identity_is_"
          "preserved",
          "[integration][torrent-engine][create][file-order][bep47]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    std::filesystem::create_directories(content / "nested");
    {
        std::ofstream root(content / "root2.bin", std::ios::binary);
        root << std::string(9000, 'b');
        REQUIRE(root);
        std::ofstream nested(content / "nested" / "root10.bin", std::ios::binary);
        nested << std::string(9000, 'a');
        REQUIRE(nested);
        std::ofstream(content / "empty.bin", std::ios::binary).close();
    }
    std::error_code symlink_error;
    std::filesystem::create_symlink("root2.bin", content / "alias.bin", symlink_error);
    if (symlink_error)
    {
        SKIP("filesystem symlink creation is unavailable: " + symlink_error.message());
    }

    const TorrentEngine engine;
    for (const auto format : {TorrentFormat::V1, TorrentFormat::V2, TorrentFormat::Hybrid})
    {
        for (const auto policy :
             {FileOrderPolicy::Lexicographical, FileOrderPolicy::CanonicalAlignment,
              FileOrderPolicy::Natural, FileOrderPolicy::BreadthFirst})
        {
            CreateOptionsInput input;
            input.format = format;
            input.piece_length_strategy = PieceLengthStrategy::Fixed;
            input.fixed_piece_length = 16U * 1024U;
            input.file_order_policy = policy;
            auto options = CreateOptions::create(std::move(input));
            REQUIRE(options);
            const auto target =
                temp.path() / ("symlink-" + std::to_string(static_cast<int>(format)) + "-" +
                               std::to_string(static_cast<int>(policy)) + ".torrent");
            const auto result = engine.create({content, target, std::move(options).value(), false});
            REQUIRE(result);
            REQUIRE(result.value().payload_bytes == 18000);

            std::ifstream torrent_input(target, std::ios::binary);
            REQUIRE(torrent_input);
            const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(torrent_input),
                                                  std::istreambuf_iterator<char>()};
            auto document = detail::decode_torrent(bytes, detail::MetadataReadMode::Strict);
            REQUIRE(document);
            const auto symlink = std::find_if(
                document.value().info().files().begin(), document.value().info().files().end(),
                [](const FileEntry& file) { return file.path().to_string() == "alias.bin"; });
            REQUIRE(symlink != document.value().info().files().end());
            REQUIRE(symlink->attributes().symlink);
            REQUIRE(symlink->length() == 0);
            REQUIRE(symlink->symlink_target().has_value());
            REQUIRE(require_optional(symlink->symlink_target()).to_string() == "root2.bin");
        }
    }
}

TEST_CASE("given_versioned_qbittorrent_followed_symlink_matrix_when_verified_then_v1_v2_and_"
          "hybrid_match_recheck_evidence",
          "[integration][torrent-engine][verify][fixture][qbittorrent][symlink]")
{
    struct QbittorrentCase
    {
        TorrentFormat format;
        std::string fixture;
    };
    const std::vector<QbittorrentCase> cases{
        {TorrentFormat::V1,
         "qbittorrent-followed-symlink/qbittorrent-4.6.3-v1-followed-symlink.torrent"},
        {TorrentFormat::V2,
         "qbittorrent-followed-symlink/qbittorrent-4.6.3-v2-followed-symlink.torrent"},
        {TorrentFormat::Hybrid,
         "qbittorrent-followed-symlink/qbittorrent-4.6.3-hybrid-followed-symlink.torrent"},
    };
    const auto fixture_content = std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" /
                                 "torrent-engine" / "qbittorrent-followed-symlink" / "payload" /
                                 "sample-content";
    const TempDirectory temp;
    const auto content = temp.path() / "sample-content";
    std::filesystem::create_directories(content / "data");
    std::filesystem::create_directories(content / "docs");
    std::filesystem::create_directories(content / "links");
    std::filesystem::copy_file(fixture_content / "data" / "second.bin",
                               content / "data" / "second.bin");
    std::filesystem::copy_file(fixture_content / "data" / "target.bin",
                               content / "data" / "target.bin");
    std::ifstream readme_input(fixture_content / "docs" / "README.txt", std::ios::binary);
    REQUIRE(readme_input.good());
    const std::string readme((std::istreambuf_iterator<char>(readme_input)),
                             std::istreambuf_iterator<char>());
    std::ofstream readme_output(content / "docs" / "README.txt", std::ios::binary);
    REQUIRE(readme_output.good());
    for (const char character : readme)
    {
        if (character != '\r')
        {
            readme_output.put(character);
        }
    }
    readme_output.close();
    REQUIRE(readme_output.good());
    std::filesystem::copy_file(fixture_content / "data" / "target.bin",
                               content / "links" / "alias.bin");
    const TorrentEngine engine;

    for (const auto& test_case : cases)
    {
        DYNAMIC_SECTION(test_case.fixture)
        {
            auto document = detail::decode_torrent(torrent_engine_fixture(test_case.fixture),
                                                   detail::MetadataReadMode::Strict);

            REQUIRE(document);
            REQUIRE(document.value().info().name() == "DevBase");
            REQUIRE(document.value().info().format() == test_case.format);
            REQUIRE(document.value().info().pieces().piece_length() == std::uint64_t{32} * 1024U);
            REQUIRE(!document.value().info().files().empty());
            for (const auto& file : document.value().info().files())
            {
                REQUIRE_FALSE(file.attributes().symlink);
                REQUIRE_FALSE(file.symlink_target().has_value());
            }

            const auto verification = engine.verify({std::move(document).value(), content});
            REQUIRE(verification);
            for (const auto& file : verification.value().files)
            {
                CAPTURE(file.path.to_string());
                REQUIRE(file.findings == FileVerificationFinding::None);
            }
            REQUIRE(verification.value().outcome == VerificationOutcome::Verified);
            REQUIRE(verification.value().expected_bytes == 135211U);
            REQUIRE(verification.value().verified_bytes == 135211U);
        }
    }
}

TEST_CASE("given_contained_regular_file_symlink_when_created_then_bep47_link_is_zero_length",
          "[integration][torrent-engine][create][bep47]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    const auto target = temp.path() / "payload.torrent";
    std::filesystem::create_directories(content);
    {
        std::ofstream output(content / "target.bin", std::ios::binary);
        output << "abc";
        REQUIRE(output);
    }
    std::error_code symlink_error;
    std::filesystem::create_symlink("target.bin", content / "alias.bin", symlink_error);
    if (symlink_error)
    {
        SKIP("filesystem symlink creation is unavailable: " + symlink_error.message());
    }

    CreateOptionsInput input;
    input.format = TorrentFormat::V1;
    input.piece_length_strategy = PieceLengthStrategy::Fixed;
    input.fixed_piece_length = 16U * 1024U;
    auto options = CreateOptions::create(std::move(input));
    REQUIRE(options);
    const TorrentEngine engine;

    const auto result = engine.create({content, target, std::move(options).value(), false});

    REQUIRE(result);
    REQUIRE(result.value().payload_bytes == 3);
    std::ifstream input_file(target, std::ios::binary);
    REQUIRE(input_file);
    const std::string encoded{std::istreambuf_iterator<char>(input_file),
                              std::istreambuf_iterator<char>()};
    const auto attr_key = encoded.find("4:attr");
    REQUIRE(attr_key != std::string::npos);
    const auto attr_length_begin = attr_key + std::string{"4:attr"}.size();
    const auto attr_length_end = encoded.find(':', attr_length_begin);
    REQUIRE(attr_length_end != std::string::npos);
    const auto attr_length =
        std::stoull(encoded.substr(attr_length_begin, attr_length_end - attr_length_begin));
    const auto attributes = encoded.substr(attr_length_end + 1, attr_length);
    REQUIRE(attributes.find('l') != std::string::npos);
    REQUIRE(encoded.find("6:lengthi0e4:pathl9:alias.bine") != std::string::npos);
    REQUIRE(encoded.find("12:symlink pathl10:target.bine") != std::string::npos);
}

TEST_CASE("given_invalid_local_symlink_targets_when_created_then_validation_fails_without_output",
          "[integration][torrent-engine][create][bep47]")
{
    const auto create_options = []() {
        CreateOptionsInput input;
        input.format = TorrentFormat::V1;
        input.piece_length_strategy = PieceLengthStrategy::Fixed;
        input.fixed_piece_length = 16U * 1024U;
        auto options = CreateOptions::create(std::move(input));
        REQUIRE(options);
        return std::move(options).value();
    };
    const auto require_failure = [](const Result<CreateResult>& result,
                                    const std::filesystem::path& target, const std::string& issue) {
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code == ErrorCode::ValidationFailed);
        REQUIRE(result.error().message == "create request validation failed");
        REQUIRE(result.error().issues.size() == 1);
        REQUIRE(result.error().issues.front().field == "create.content_root");
        REQUIRE(result.error().issues.front().message == issue);
        REQUIRE_FALSE(std::filesystem::exists(target));
        REQUIRE(temporary_sibling_count(target) == 0);
    };
    const TorrentEngine engine;

    SECTION("directory target")
    {
        const TempDirectory temp;
        const auto content = temp.path() / "payload";
        const auto target = temp.path() / "payload.torrent";
        std::filesystem::create_directories(content / "directory");
        {
            std::ofstream payload(content / "included.bin", std::ios::binary);
            payload << "included";
            REQUIRE(payload);
        }
        std::error_code symlink_error;
        std::filesystem::create_directory_symlink("directory", content / "alias", symlink_error);
        if (symlink_error)
        {
            SKIP("filesystem directory symlink creation is unavailable: " +
                 symlink_error.message());
        }

        const auto result = engine.create({content, target, create_options(), false});

        require_failure(result, target, "contains a symlink whose target is not a regular file");
    }

    SECTION("dangling target")
    {
        const TempDirectory temp;
        const auto content = temp.path() / "payload";
        const auto target = temp.path() / "payload.torrent";
        std::filesystem::create_directories(content);
        {
            std::ofstream payload(content / "included.bin", std::ios::binary);
            payload << "included";
            REQUIRE(payload);
        }
        std::error_code symlink_error;
        std::filesystem::create_symlink("missing.bin", content / "alias.bin", symlink_error);
        if (symlink_error)
        {
            SKIP("filesystem symlink creation is unavailable: " + symlink_error.message());
        }

        const auto result = engine.create({content, target, create_options(), false});

        require_failure(result, target, "contains a dangling symlink");
    }

    SECTION("cyclic target")
    {
        const TempDirectory temp;
        const auto content = temp.path() / "payload";
        const auto target = temp.path() / "payload.torrent";
        std::filesystem::create_directories(content);
        {
            std::ofstream payload(content / "included.bin", std::ios::binary);
            payload << "included";
            REQUIRE(payload);
        }
        std::error_code symlink_error;
        std::filesystem::create_symlink("second.bin", content / "first.bin", symlink_error);
        if (!symlink_error)
        {
            std::filesystem::create_symlink("first.bin", content / "second.bin", symlink_error);
        }
        if (symlink_error)
        {
            SKIP("filesystem cyclic symlink creation is unavailable: " + symlink_error.message());
        }

        const auto result = engine.create({content, target, create_options(), false});

        require_failure(result, target, "contains a symlink whose target is another symlink");
    }
}

TEST_CASE("given_external_symlink_when_created_then_validation_fails_before_commit",
          "[integration][torrent-engine][create][bep47]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    const auto target = temp.path() / "payload.torrent";
    std::filesystem::create_directories(content);
    {
        std::ofstream payload(content / "included.bin", std::ios::binary);
        payload << "included";
        REQUIRE(payload);
        std::ofstream external(temp.path() / "external.bin", std::ios::binary);
        external << "external";
        REQUIRE(external);
    }
    std::error_code symlink_error;
    std::filesystem::create_symlink("../external.bin", content / "alias.bin", symlink_error);
    if (symlink_error)
    {
        SKIP("filesystem symlink creation is unavailable: " + symlink_error.message());
    }

    CreateOptionsInput input;
    input.format = TorrentFormat::V1;
    input.piece_length_strategy = PieceLengthStrategy::Fixed;
    input.fixed_piece_length = 16U * 1024U;
    auto options = CreateOptions::create(std::move(input));
    REQUIRE(options);
    const TorrentEngine engine;

    const auto result = engine.create({content, target, std::move(options).value(), false});

    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == ErrorCode::ValidationFailed);
    REQUIRE(result.error().message == "create request validation failed");
    REQUIRE(result.error().issues.size() == 1);
    REQUIRE(result.error().issues.front().field == "create.content_root");
    REQUIRE(result.error().issues.front().message == "contains a symlink with an external target");
    REQUIRE_FALSE(std::filesystem::exists(target));
    REQUIRE(temporary_sibling_count(target) == 0);
}

TEST_CASE("given_nested_contained_symlink_when_created_then_target_is_torrent_root_relative",
          "[integration][torrent-engine][create][bep47]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload";
    const auto target = temp.path() / "payload.torrent";
    std::filesystem::create_directories(content / "nested");
    {
        std::ofstream output(content / "target.bin", std::ios::binary);
        output << "abc";
        REQUIRE(output);
    }
    std::error_code symlink_error;
    std::filesystem::create_symlink("../target.bin", content / "nested" / "alias.bin",
                                    symlink_error);
    if (symlink_error)
    {
        SKIP("filesystem symlink creation is unavailable: " + symlink_error.message());
    }

    CreateOptionsInput input;
    input.format = TorrentFormat::V1;
    input.piece_length_strategy = PieceLengthStrategy::Fixed;
    input.fixed_piece_length = 16U * 1024U;
    auto options = CreateOptions::create(std::move(input));
    REQUIRE(options);
    const TorrentEngine engine;

    const auto result = engine.create({content, target, std::move(options).value(), false});

    REQUIRE(result);
    REQUIRE(result.value().payload_bytes == 3);
    std::ifstream input_file(target, std::ios::binary);
    REQUIRE(input_file);
    const std::string encoded{std::istreambuf_iterator<char>(input_file),
                              std::istreambuf_iterator<char>()};
    REQUIRE(encoded.find("12:symlink pathl10:target.bine") != std::string::npos);
    REQUIRE(encoded.find("12:symlink pathl2:..10:target.bine") == std::string::npos);
}

TEST_CASE("given_verification_resource_budget_when_created_then_it_validates_each_operation_limit",
          "[unit][torrent-engine][verification-resource-budget]")
{
    VerificationResourceBudgetInput input;
    input.hashing_workers = 1;
    input.checking_memory_bytes = 16ULL * 1024ULL + 1U;
    input.max_logical_files = 1;
    input.max_pieces = 1;
    const auto budget = VerificationResourceBudget::create(input);
    REQUIRE(budget);
    REQUIRE(budget.value().hashing_workers() == 1);
    REQUIRE(budget.value().checking_memory_bytes() == 16ULL * 1024ULL + 1U);
    REQUIRE(budget.value().max_logical_files() == 1);
    REQUIRE(budget.value().max_pieces() == 1);

    input.hashing_workers = 0;
    const auto invalid = VerificationResourceBudget::create(input);
    REQUIRE_FALSE(invalid);
    REQUIRE(invalid.error().code == ErrorCode::ValidationFailed);
    REQUIRE(invalid.error().issues.front().field == "verify.resource_budget.hashing_workers");
}

TEST_CASE("given_budget_below_torrent_piece_count_when_verified_then_backend_work_is_not_started",
          "[unit][torrent-engine][verification-resource-budget]")
{
    VerificationResourceBudgetInput input;
    input.hashing_workers = 1;
    input.checking_memory_bytes = 16ULL * 1024ULL;
    input.max_logical_files = 1;
    input.max_pieces = 1;
    const auto budget = VerificationResourceBudget::create(input);
    REQUIRE(budget);

    const TorrentEngine engine;
    VerifyRequest request{v1_three_piece_document(), "unreadable-is-not-inspected"};
    request.resource_budget = budget.value();
    const auto result = engine.verify(request);

    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == ErrorCode::ResourceLimitExceeded);
}

TEST_CASE("given_explicit_disk_io_mode_when_created_and_verified_then_mmap_backend_is_accepted",
          "[integration][torrent-engine][create][verify]")
{
    const TempDirectory temp;
    const auto content = temp.path() / "payload.bin";
    const auto target = temp.path() / "payload.torrent";
    {
        std::ofstream output(content, std::ios::binary);
        output << "0123456789abcdef";
        REQUIRE(output);
    }

    CreateOptionsInput input;
    input.format = TorrentFormat::V1;
    input.piece_length_strategy = PieceLengthStrategy::Fixed;
    input.fixed_piece_length = 16U * 1024U;
    auto options = CreateOptions::create(std::move(input));
    REQUIRE(options);
    const TorrentEngine engine;

    const auto created =
        engine.create({content, target, options.value(), false, {}, {}, DiskIoMode::Mmap});
    REQUIRE(created);
    REQUIRE(std::filesystem::is_regular_file(target));

    std::ifstream input_stream(target, std::ios::binary);
    std::vector<std::uint8_t> encoded{std::istreambuf_iterator<char>(input_stream),
                                      std::istreambuf_iterator<char>()};
    auto document = detail::decode_torrent(std::move(encoded), detail::MetadataReadMode::Strict);
    REQUIRE(document);

    const auto result = engine.verify(
        VerifyRequest{std::move(document).value(), content, {}, {}, DiskIoMode::Mmap});
    REQUIRE(result);
    REQUIRE(result.value().outcome == VerificationOutcome::Verified);
}
