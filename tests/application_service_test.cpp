#include "../src/torrent_engine_fault_injection.hpp"
#include "../src/verification_admission_controller_test_probe.hpp"

#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <torrentutils/core/application.hpp>
#include <utility>

namespace {
using namespace torrentutils::core;

template <typename Value>
[[nodiscard]] const Value& require_optional(const std::optional<Value>& value)
{
    if (!value.has_value())
    {
        throw std::logic_error("expected optional test value");
    }
    return value.value();
}

class FixedClock final : public Clock
{
  public:
    [[nodiscard]] std::int64_t now_unix_seconds() const noexcept override
    {
        return 1'786'489'200;
    }
};

struct VerificationServiceSet
{
    FileTorrentRepository first_repository;
    FixedClock first_clock;
    TorrentService first_service{first_repository, first_clock};
    FileTorrentRepository second_repository;
    FixedClock second_clock;
    TorrentService second_service{second_repository, second_clock};
    FileTorrentRepository third_repository;
    FixedClock third_clock;
    TorrentService third_service{third_repository, third_clock};
};

class HeldVerification
{
  public:
    HeldVerification(TorrentService& service, VerificationAdmissionController& controller,
                     VerifyRequest request)
        : verifier_(service, controller), request_(std::move(request))
    {
        request_.on_progress = [this](const VerificationProgress&) {
            std::unique_lock<std::mutex> lock(gate_mutex_);
            if (!entered_)
            {
                entered_ = true;
                gate_changed_.notify_all();
                gate_changed_.wait(lock, [this] { return release_; });
            }
        };
        thread_ = std::thread([this] { result_ = verifier_.verify(request_); });
    }

    ~HeldVerification()
    {
        release();
        join();
    }

    HeldVerification(const HeldVerification&) = delete;
    HeldVerification& operator=(const HeldVerification&) = delete;

    void wait_until_entered()
    {
        std::unique_lock<std::mutex> lock(gate_mutex_);
        gate_changed_.wait(lock, [this] { return entered_; });
    }

    void release()
    {
        {
            std::lock_guard<std::mutex> lock(gate_mutex_);
            release_ = true;
        }
        gate_changed_.notify_all();
    }

    void join()
    {
        if (thread_.joinable())
        {
            thread_.join();
        }
    }

    [[nodiscard]] const std::optional<Result<VerificationReport>>& result() const noexcept
    {
        return result_;
    }

  private:
    AdmissionControlledVerifier verifier_;
    VerifyRequest request_;
    std::optional<Result<VerificationReport>> result_;
    std::mutex gate_mutex_;
    std::condition_variable gate_changed_;
    bool entered_{};
    bool release_{};
    std::thread thread_;
};

[[nodiscard]] std::filesystem::path metadata_fixture(const char* name)
{
    return std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" / "metadata" / name;
}
class TempDirectory
{
  public:
    TempDirectory()
    {
        static std::atomic<std::uint64_t> sequence{};
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("torrentutils-phase6-" + std::to_string(tick) + "-" +
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
[[nodiscard]] std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::filesystem::path copy_fixture(TempDirectory& temporary, const char* name)
{
    const auto target = temporary.path() / name;
    std::filesystem::copy_file(metadata_fixture(name), target);
    return target;
}

[[nodiscard]] VerifyRequest make_verification_request(TempDirectory& temporary,
                                                      TorrentService& service)
{
    const auto content = temporary.path() / "payload.bin";
    {
        std::ofstream output(content, std::ios::binary);
        output << std::string(std::size_t{128} * 1024U, 'x');
        if (!output)
        {
            throw std::logic_error("could not create verification test payload");
        }
    }

    CreateOptionsInput input;
    input.format = TorrentFormat::V1;
    input.piece_length_strategy = PieceLengthStrategy::Fixed;
    input.fixed_piece_length = 16U * 1024U;
    auto options = CreateOptions::create(std::move(input));
    if (!options)
    {
        throw std::logic_error("could not create verification test options");
    }
    const auto torrent = temporary.path() / "payload.torrent";
    const auto created = service.create({content, torrent, std::move(options).value(), false});
    if (!created)
    {
        throw std::logic_error("could not create verification test torrent");
    }
    const auto loaded = service.load(torrent);
    if (!loaded)
    {
        throw std::logic_error("could not load verification test torrent");
    }
    return {loaded.value().document(), content};
}
} // namespace

TEST_CASE("given_regular_torrent_when_loaded_then_service_returns_source_bound_document")
{
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);

    const auto source = metadata_fixture("valid-v1.torrent");
    const auto result = service.load(source);

    REQUIRE(result);
    CHECK(result.value().source_path() == std::filesystem::absolute(source).lexically_normal());
    CHECK(result.value().source_state() == LoadedTorrentSourceState::RegularFile);
    CHECK(result.value().diagnostics().empty());
    CHECK(result.value().document().info().name() == "test.bin");
}

TEST_CASE("given_system_clock_when_queried_then_it_returns_current_unix_time")
{
    const auto before = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const auto actual = SystemClock{}.now_unix_seconds();
    const auto after = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    CHECK(actual >= before);
    CHECK(actual <= after);
}

TEST_CASE("given_loaded_torrents_when_assigned_then_the_source_bound_snapshot_is_replaced")
{
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    auto copy_assigned = service.load(metadata_fixture("valid-v1.torrent")).value();
    const auto replacement = service.load(metadata_fixture("top-level-source-v1.torrent")).value();

    copy_assigned = replacement;
    CHECK(copy_assigned.source_path() == replacement.source_path());

    auto move_assigned = service.load(metadata_fixture("valid-v1.torrent")).value();
    move_assigned = std::move(copy_assigned);
    CHECK(move_assigned.source_path() == replacement.source_path());
}

TEST_CASE("given_unknown_fields_when_loaded_leniently_then_diagnostics_are_stable")
{
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);

    const auto result = service.load(metadata_fixture("unknown-extensions.torrent"));

    REQUIRE(result);
    REQUIRE(result.value().diagnostics().size() == 2);
    CHECK(result.value().diagnostics()[0].scope == LoadDiagnosticScope::TopLevel);
    CHECK(result.value().diagnostics()[0].key ==
          DiagnosticKeyBytes{'x', '-', 'e', 'x', 't', 'r', 'a'});
    CHECK(result.value().diagnostics()[1].scope == LoadDiagnosticScope::Info);
    CHECK(result.value().diagnostics()[1].key == DiagnosticKeyBytes{'x', '-', 'i', 'n', 'f', 'o'});
}

TEST_CASE("given_unknown_fields_when_loaded_strictly_then_load_is_rejected")
{
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);

    const auto result = service.load(metadata_fixture("unknown-field.torrent"), {LoadMode::Strict});

    REQUIRE_FALSE(result);
    CHECK(result.error().code == ErrorCode::UnsupportedFeature);
}

TEST_CASE("given_x_extension_fields_when_loaded_strictly_then_load_succeeds_with_diagnostics")
{
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);

    const auto result =
        service.load(metadata_fixture("unknown-extensions.torrent"), {LoadMode::Strict});

    REQUIRE(result);
    REQUIRE(result.value().diagnostics().size() == 2);
    CHECK(result.value().diagnostics()[0].scope == LoadDiagnosticScope::TopLevel);
    CHECK(result.value().diagnostics()[0].key ==
          DiagnosticKeyBytes{'x', '-', 'e', 'x', 't', 'r', 'a'});
}

TEST_CASE("given_final_source_symlink_when_loaded_then_target_bytes_are_read_and_state_is_frozen")
{
    TempDirectory temporary;
    const auto link = temporary.path() / "linked.torrent";
    std::filesystem::create_symlink(metadata_fixture("valid-v1.torrent"), link);

    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto result = service.load(link);

    REQUIRE(result);
    CHECK(result.value().source_path() == std::filesystem::absolute(link).lexically_normal());
    CHECK(result.value().source_state() == LoadedTorrentSourceState::SymlinkFollowed);
    CHECK(result.value().document().info().name() == "test.bin");
}

TEST_CASE("given_invalid_final_source_entry_when_loaded_then_validation_fails")
{
    TempDirectory temporary;
    const auto dangling = temporary.path() / "dangling.torrent";
    std::filesystem::create_symlink(temporary.path() / "missing.torrent", dangling);

    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);

    const auto dangling_result = service.load(dangling);
    const auto directory_result = service.load(temporary.path());

    REQUIRE_FALSE(dangling_result);
    CHECK(dangling_result.error().code == ErrorCode::ValidationFailed);
    REQUIRE_FALSE(directory_result);
    CHECK(directory_result.error().code == ErrorCode::ValidationFailed);
}
TEST_CASE("given_missing_or_oversized_source_when_loaded_then_service_rejects_it")
{
    TempDirectory temporary;
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);

    const auto missing = service.load(temporary.path() / "missing.torrent");

    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == ErrorCode::FileNotFound);

    const auto empty_source = temporary.path() / "empty.torrent";
    std::ofstream(empty_source, std::ios::binary).close();
    const auto empty = service.load(empty_source);

    REQUIRE_FALSE(empty);
    CHECK(empty.error().code == ErrorCode::InvalidBencode);

    const auto oversized_source = temporary.path() / "oversized.torrent";
    {
        std::ofstream output(oversized_source, std::ios::binary);
        output.seekp(static_cast<std::streamoff>(128U) * 1024U * 1024U);
        output.put("\0"[0]);
        REQUIRE(output);
    }

    const auto oversized = service.load(oversized_source);

    REQUIRE_FALSE(oversized);
    CHECK(oversized.error().code == ErrorCode::InvalidBencode);
}

TEST_CASE("given_invalid_engine_requests_when_called_through_service_then_errors_are_preserved")
{
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto document = service.load(metadata_fixture("valid-v1.torrent")).value().document();
    const auto options = CreateOptions::create();
    REQUIRE(options);

    const auto create_result = service.create({{}, {}, options.value()});
    const auto verify_result = service.verify({document, {}});

    REQUIRE_FALSE(create_result);
    CHECK(create_result.error().code == ErrorCode::ValidationFailed);
    REQUIRE_FALSE(verify_result);
    CHECK(verify_result.error().code == ErrorCode::ValidationFailed);
}

TEST_CASE("given_non_default_file_order_policy_when_created_through_service_then_it_is_"
          "forwarded_unchanged")
{
    TempDirectory temporary;
    const auto content = temporary.path() / "payload";
    std::filesystem::create_directories(content / "nested");
    {
        std::ofstream first(content / "root10.bin", std::ios::binary);
        first << std::string(9000, 'a');
        REQUIRE(first);
        std::ofstream second(content / "root2.bin", std::ios::binary);
        second << std::string(9000, 'b');
        REQUIRE(second);
        std::ofstream nested(content / "nested" / "child.bin", std::ios::binary);
        nested << std::string(9000, 'c');
        REQUIRE(nested);
    }

    std::error_code symlink_error;
    std::filesystem::create_symlink("root2.bin", content / "alias.bin", symlink_error);
    if (symlink_error)
    {
        SKIP("filesystem symlink creation is unavailable: " + symlink_error.message());
    }

    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    for (const auto format : {TorrentFormat::V1, TorrentFormat::V2, TorrentFormat::Hybrid})
    {
        for (const auto policy :
             {FileOrderPolicy::Lexicographical, FileOrderPolicy::CanonicalAlignment,
              FileOrderPolicy::Natural, FileOrderPolicy::BreadthFirst})
        {
            CAPTURE(static_cast<int>(format), static_cast<int>(policy));
            CreateOptionsInput service_input;
            service_input.format = format;
            service_input.piece_length_strategy = PieceLengthStrategy::Fixed;
            service_input.fixed_piece_length = 16U * 1024U;
            service_input.file_order_policy = policy;
            auto service_options = CreateOptions::create(std::move(service_input));
            REQUIRE(service_options);

            const auto suffix = std::to_string(static_cast<int>(format)) + "-" +
                                std::to_string(static_cast<int>(policy));
            const auto service_target = temporary.path() / ("service-" + suffix + ".torrent");
            const auto service_result = service.create(
                {content, service_target, std::move(service_options).value(), false});
            REQUIRE(service_result);

            CreateOptionsInput direct_input;
            direct_input.format = format;
            direct_input.piece_length_strategy = PieceLengthStrategy::Fixed;
            direct_input.fixed_piece_length = 16U * 1024U;
            direct_input.file_order_policy = policy;
            auto direct_options = CreateOptions::create(std::move(direct_input));
            REQUIRE(direct_options);
            const auto direct_target = temporary.path() / ("direct-" + suffix + ".torrent");
            const auto direct_result = TorrentEngine{}.create(
                {content, direct_target, std::move(direct_options).value(), false});
            REQUIRE(direct_result);

            REQUIRE(service_result.value().info_hashes.v1() ==
                    direct_result.value().info_hashes.v1());
            REQUIRE(service_result.value().info_hashes.v2() ==
                    direct_result.value().info_hashes.v2());
            REQUIRE(read_bytes(service_target) == read_bytes(direct_target));

            const auto loaded = service.load(service_target);
            REQUIRE(loaded);
            const auto symlink = std::find_if(
                loaded.value().document().info().files().begin(),
                loaded.value().document().info().files().end(),
                [](const FileEntry& file) { return file.path().to_string() == "alias.bin"; });
            REQUIRE(symlink != loaded.value().document().info().files().end());
            CHECK(symlink->attributes().symlink);
            CHECK(symlink->length() == 0);
            REQUIRE(symlink->symlink_target().has_value());
            CHECK(require_optional(symlink->symlink_target()).to_string() == "root2.bin");
        }
    }
}

TEST_CASE("given_empty_edit_batch_when_edited_then_original_loaded_torrent_is_unchanged")
{
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(metadata_fixture("valid-v1.torrent")).value();

    const auto result = service.edit(loaded, {});

    REQUIRE(result);
    CHECK(result.value().disposition == EditDisposition::NoChange);
    CHECK(result.value().loaded.document().metadata().comment() == "original comment");
}

TEST_CASE("given_safe_top_level_edits_when_edited_then_changes_are_applied_in_memory")
{
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(metadata_fixture("valid-v1.torrent")).value();

    const auto result =
        service.edit(loaded, {SetComment{""}, SetCreationTimeNow{}, ClearInfoSource{}});

    REQUIRE(result);
    CHECK(result.value().disposition == EditDisposition::Applied);
    CHECK(result.value().loaded.document().metadata().comment() == "");
    CHECK(result.value().loaded.document().metadata().creation_time_unix_seconds() ==
          clock.now_unix_seconds());
    CHECK_FALSE(result.value().loaded.document().metadata().source());
}

TEST_CASE("given_private_edit_when_batch_is_valid_then_edit_and_safe_metadata_are_applied")
{
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(metadata_fixture("valid-v1.torrent")).value();

    const auto result = service.edit(loaded, {SetComment{"discarded"}, SetPrivate{false}});

    REQUIRE(result);
    CHECK(result.value().disposition == EditDisposition::Applied);
    CHECK(result.value().loaded.document().metadata().comment() == "discarded");
    CHECK_FALSE(result.value().loaded.document().info().is_private());
}
TEST_CASE("given_applied_edit_when_saved_then_source_is_atomically_replaced_and_rebased")
{
    TempDirectory temporary;
    const auto source = copy_fixture(temporary, "top-level-source-v1.torrent");
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(source).value();
    const auto edited = service.edit(loaded, {SetComment{"saved"}}).value().loaded;

    const auto saved = service.save(edited);

    REQUIRE(saved);
    CHECK(saved.value().disposition == SaveDisposition::Saved);
    const auto reloaded = service.load(source);
    REQUIRE(reloaded);
    CHECK(reloaded.value().document().metadata().comment() == "saved");
    const auto second_save = service.save(saved.value().loaded);
    REQUIRE(second_save);
    CHECK(second_save.value().disposition == SaveDisposition::NoChange);
}

TEST_CASE(
    "given_explicit_info_edit_when_batch_is_valid_then_edit_requests_rebuild_without_mutation")
{
    TempDirectory temporary;
    const auto source = copy_fixture(temporary, "top-level-source-v1.torrent");
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(source).value();
    const auto original_bytes = read_bytes(source);

    const auto edited = service.edit(loaded, {SetComment{"batch comment"}, SetName{"renamed.bin"}});

    REQUIRE(edited);
    CHECK(edited.value().disposition == EditDisposition::Applied);
    CHECK(edited.value().loaded.document().metadata().comment() == "batch comment");
    CHECK(edited.value().loaded.document().info().name() == "renamed.bin");
    CHECK(read_bytes(source) == original_bytes);
}

TEST_CASE("given_edited_torrent_when_saved_to_new_path_then_original_and_target_are_distinct")
{
    TempDirectory temporary;
    const auto source = copy_fixture(temporary, "top-level-source-v1.torrent");
    const auto target = temporary.path() / "copy.torrent";
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(source).value();
    const auto edited = service.edit(loaded, {SetComment{"new target"}}).value().loaded;

    SaveRequest request;
    request.mode = SaveTargetMode::NewPath;
    request.destination = target;
    const auto saved = service.save(edited, request);

    REQUIRE(saved);
    CHECK(saved.value().disposition == SaveDisposition::Saved);
    CHECK(saved.value().loaded.source_path() == std::filesystem::absolute(target));
    CHECK(std::filesystem::exists(source));
    CHECK(std::filesystem::exists(target));
    CHECK(service.load(target).value().document().metadata().comment() == "new target");
}

TEST_CASE("given_identity_edits_when_saved_then_reloaded_document_is_rebased_and_valid")
{
    TempDirectory temporary;
    const auto source = copy_fixture(temporary, "top-level-source-v1.torrent");
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(source).value();
    const auto original_hash = loaded.document().info().info_hashes().v1();

    const auto edited = service.edit(
        loaded, {SetPrivate{true}, SetName{"renamed.bin"}, SetInfoSource{"client-extension"}});

    REQUIRE(edited);
    REQUIRE(edited.value().disposition == EditDisposition::Applied);
    const auto saved = service.save(edited.value().loaded);
    REQUIRE(saved);
    CHECK(saved.value().disposition == SaveDisposition::Saved);

    const auto reloaded = service.load(source);
    REQUIRE(reloaded);
    CHECK(reloaded.value().document().info().is_private());
    CHECK(reloaded.value().document().info().name() == "renamed.bin");
    CHECK(reloaded.value().document().info().files().front().path().to_string() == "renamed.bin");
    CHECK(reloaded.value().document().metadata().source() == "client-extension");
    CHECK(reloaded.value().document().info().info_hashes().v1() != original_hash);
    const auto source_bytes = read_bytes(source);
    const std::string wire(source_bytes.begin(), source_bytes.end());
    CHECK(wire.find("3:OLD") != std::string::npos);

    const auto second_save = service.save(saved.value().loaded);
    REQUIRE(second_save);
    CHECK(second_save.value().disposition == SaveDisposition::NoChange);
}

TEST_CASE("given_source_bytes_changed_after_load_when_saved_then_conflict_preserves_external_bytes")
{
    TempDirectory temporary;
    const auto source = copy_fixture(temporary, "top-level-source-v1.torrent");
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(source).value();
    const auto edited = service.edit(loaded, {SetComment{"mine"}}).value().loaded;
    std::filesystem::copy_file(metadata_fixture("valid-v1.torrent"), source,
                               std::filesystem::copy_options::overwrite_existing);
    const auto external = read_bytes(source);

    const auto saved = service.save(edited);

    REQUIRE_FALSE(saved);
    CHECK(saved.error().code == ErrorCode::Conflict);
    CHECK(read_bytes(source) == external);
}

TEST_CASE("given_regular_unchanged_loaded_torrent_when_source_disappears_then_save_is_no_change")
{
    TempDirectory temporary;
    const auto source = copy_fixture(temporary, "valid-v1.torrent");
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(source).value();
    std::filesystem::remove(source);

    const auto saved = service.save(loaded);

    REQUIRE(saved);
    CHECK(saved.value().disposition == SaveDisposition::NoChange);
}

TEST_CASE("given_edited_source_that_disappears_before_save_then_commit_reports_conflict")
{
    TempDirectory temporary;
    const auto source = copy_fixture(temporary, "top-level-source-v1.torrent");
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(source).value();
    const auto edited = service.edit(loaded, {SetComment{"pending"}}).value().loaded;
    std::filesystem::remove(source);

    const auto saved = service.save(edited);

    REQUIRE_FALSE(saved);
    CHECK(saved.error().code == ErrorCode::Conflict);
}

TEST_CASE("given_edited_source_parent_that_disappears_before_save_then_commit_reports_io_failure")
{
    TempDirectory temporary;
    const auto source = copy_fixture(temporary, "top-level-source-v1.torrent");
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(source).value();
    const auto edited = service.edit(loaded, {SetComment{"pending"}}).value().loaded;
    std::filesystem::remove_all(temporary.path());

    const auto saved = service.save(edited);

    REQUIRE_FALSE(saved);
    CHECK(saved.error().code == ErrorCode::IoFailure);
}

TEST_CASE("given_edited_source_that_grows_beyond_limit_then_commit_reports_conflict")
{
    TempDirectory temporary;
    const auto source = copy_fixture(temporary, "top-level-source-v1.torrent");
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(source).value();
    const auto edited = service.edit(loaded, {SetComment{"pending"}}).value().loaded;
    {
        std::ofstream output(source, std::ios::binary | std::ios::trunc);
        output.seekp(static_cast<std::streamoff>(128U) * 1024U * 1024U);
        output.put("\0"[0]);
        REQUIRE(output);
    }

    const auto saved = service.save(edited);

    REQUIRE_FALSE(saved);
    CHECK(saved.error().code == ErrorCode::Conflict);
}

TEST_CASE("given_edited_source_replaced_by_non_regular_entry_then_commit_reports_conflict")
{
    TempDirectory temporary;
    const auto source = copy_fixture(temporary, "top-level-source-v1.torrent");
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(source).value();
    const auto edited = service.edit(loaded, {SetComment{"pending"}}).value().loaded;
    std::filesystem::remove(source);
    std::filesystem::create_directory(source);

    const auto saved = service.save(edited);

    REQUIRE_FALSE(saved);
    CHECK(saved.error().code == ErrorCode::Conflict);
}

TEST_CASE("given_edited_source_replaced_by_symlink_then_commit_reports_conflict")
{
    TempDirectory temporary;
    const auto source = copy_fixture(temporary, "top-level-source-v1.torrent");
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(source).value();
    const auto edited = service.edit(loaded, {SetComment{"pending"}}).value().loaded;
    std::filesystem::remove(source);
    std::filesystem::create_symlink(metadata_fixture("valid-v1.torrent"), source);

    const auto saved = service.save(edited);

    REQUIRE_FALSE(saved);
    CHECK(saved.error().code == ErrorCode::Conflict);
}

TEST_CASE("given_safe_metadata_actions_when_edited_then_each_is_applied_in_memory")
{
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(metadata_fixture("valid-v1.torrent")).value();

    const auto result =
        service.edit(loaded, {SetCreator{"creator"}, SetInfoSource{"source"}, SetCreationTime{42}});

    REQUIRE(result);
    REQUIRE(result.value().disposition == EditDisposition::Applied);
    CHECK(result.value().loaded.document().metadata().creator() == "creator");
    CHECK(result.value().loaded.document().metadata().source() == "source");
    CHECK(result.value().loaded.document().metadata().creation_time_unix_seconds() == 42);
}

TEST_CASE("given_symlink_followed_loaded_torrent_when_saved_then_it_is_always_rejected")
{
    TempDirectory temporary;
    const auto link = temporary.path() / "linked.torrent";
    std::filesystem::create_symlink(metadata_fixture("valid-v1.torrent"), link);
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(link).value();
    const auto edited = service.edit(loaded, {SetComment{"allowed in memory"}});
    REQUIRE(edited);

    const auto saved = service.save(edited.value().loaded);

    REQUIRE_FALSE(saved);
    CHECK(saved.error().code == ErrorCode::ValidationFailed);
}

TEST_CASE("given_pre_cancelled_save_when_bytes_are_unchanged_then_cancelled_has_priority")
{
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(metadata_fixture("valid-v1.torrent")).value();
    CancellationSource cancellation;
    cancellation.cancel();

    const auto saved = service.save(loaded, {cancellation.token(), {}, nullptr, {}});

    REQUIRE_FALSE(saved);
    CHECK(saved.error().code == ErrorCode::Cancelled);
}

TEST_CASE("given_invalid_action_and_info_edit_when_edited_then_validation_failure_has_priority")
{
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(metadata_fixture("valid-v1.torrent")).value();

    const auto result = service.edit(
        loaded, {SetComment{std::string(1, static_cast<char>(0xff))}, SetPrivate{false}});

    REQUIRE_FALSE(result);
    CHECK(result.error().code == ErrorCode::ValidationFailed);
}
TEST_CASE(
    "given_loaded_document_when_inspected_through_service_then_torrent_engine_result_is_reused")
{
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(metadata_fixture("valid-v1.torrent")).value();

    const auto inspected = service.inspect(loaded.document());

    REQUIRE(inspected);
    CHECK(inspected.value().verification_capability == VerificationCapability::Supported);
}
TEST_CASE("given_info_source_action_matching_existing_value_when_edited_then_no_change")
{
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(metadata_fixture("valid-v1.torrent")).value();

    const auto result = service.edit(loaded, {SetInfoSource{"SRC"}});

    REQUIRE(result);
    CHECK(result.value().disposition == EditDisposition::NoChange);
    CHECK(result.value().loaded.document().metadata().source() == "SRC");
}

TEST_CASE("given_set_empty_then_clear_comment_when_edited_then_only_clear_removes_the_field")
{
    TempDirectory temporary;
    const auto source = copy_fixture(temporary, "top-level-source-v1.torrent");
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(source).value();

    const auto set_empty = service.edit(loaded, {SetComment{""}});
    REQUIRE(set_empty);
    CHECK(set_empty.value().disposition == EditDisposition::Applied);
    const auto comment = set_empty.value().loaded.document().metadata().comment();
    REQUIRE(comment);
    if (comment)
    {
        CHECK(comment->empty());
    }
    const auto saved_empty = service.save(set_empty.value().loaded);
    REQUIRE(saved_empty);

    const auto cleared = service.edit(saved_empty.value().loaded, {ClearComment{}});
    REQUIRE(cleared);
    CHECK(cleared.value().disposition == EditDisposition::Applied);
    CHECK_FALSE(cleared.value().loaded.document().metadata().comment());
}

TEST_CASE("given_empty_replacements_when_edited_then_only_controlled_top_level_lists_are_cleared")
{
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(metadata_fixture("valid-v1.torrent")).value();
    const auto empty_trackers = TrackerList::create({}).value();

    const auto result = service.edit(
        loaded, {ReplaceWebSeeds{{}}, ReplaceDhtNodes{{}}, ReplaceTrackers{empty_trackers}});

    REQUIRE(result);
    CHECK(result.value().disposition == EditDisposition::Applied);
    CHECK(result.value().loaded.document().metadata().web_seeds().empty());
    CHECK(result.value().loaded.document().metadata().dht_nodes().empty());
    CHECK(result.value().loaded.document().trackers().tiers().empty());
    CHECK(result.value().loaded.document().metadata().collections() ==
          loaded.document().metadata().collections());
}

TEST_CASE("given_tracker_action_batch_when_edited_then_each_action_is_applied")
{
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(metadata_fixture("valid-v1.torrent")).value();
    const auto empty_trackers = TrackerList::create({}).value();
    const auto first = TrackerUrl::parse("https://first.example/announce").value();
    const auto second = TrackerUrl::parse("https://second.example/announce").value();
    const auto third = TrackerUrl::parse("https://third.example/announce").value();
    const auto first_tier = TrackerTier::create({first}).value();
    const auto second_tier = TrackerTier::create({second}).value();

    const auto result =
        service.edit(loaded, {ReplaceTrackers{empty_trackers}, AddTrackerTier{first_tier},
                              AddTrackerTier{second_tier}, AddTrackerToTier{0, third},
                              MoveTrackerWithinTier{0, 1, 0}, MoveTracker{0, 0, 1},
                              MoveTrackerTier{1, 0}, RemoveTracker{0, 0}});

    REQUIRE(result);
    REQUIRE(result.value().disposition == EditDisposition::Applied);
    const auto& tiers = result.value().loaded.document().trackers().tiers();
    REQUIRE(tiers.size() == 2);
    CHECK(tiers[0].trackers() == std::vector<TrackerUrl>{third});
    CHECK(tiers[1].trackers() == std::vector<TrackerUrl>{first});
}

TEST_CASE("given_load_diagnostics_when_edited_and_saved_then_they_persist_until_reload")
{
    TempDirectory temporary;
    const auto source = copy_fixture(temporary, "unknown-extensions.torrent");
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(source).value();
    const auto edited = service.edit(loaded, {SetComment{"preserve diagnostics"}}).value().loaded;

    const auto saved = service.save(edited);

    REQUIRE(saved);
    REQUIRE(saved.value().loaded.diagnostics().size() == 2);
    CHECK(saved.value().loaded.diagnostics()[0].key == loaded.diagnostics()[0].key);
    CHECK(saved.value().loaded.diagnostics()[1].key == loaded.diagnostics()[1].key);
    const auto reloaded = service.load(source);
    REQUIRE(reloaded);
    CHECK(reloaded.value().diagnostics().size() == 2);
}

TEST_CASE("given_invalid_tracker_action_and_info_edit_then_validation_failure_has_priority")
{
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(metadata_fixture("valid-v1.torrent")).value();

    const auto result = service.edit(loaded, {RemoveTracker{99, 0}, SetPrivate{false}});

    REQUIRE_FALSE(result);
    CHECK(result.error().code == ErrorCode::ValidationFailed);
}
TEST_CASE("given_invalid_admission_capacity_when_created_then_validation_fails")
{
    const auto controller = VerificationAdmissionController::create(0);

    REQUIRE_FALSE(controller);
    CHECK(controller.error().code == ErrorCode::ValidationFailed);
}

TEST_CASE("given_pre_cancelled_verify_when_admission_is_requested_then_backend_is_not_entered")
{
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    auto controller = VerificationAdmissionController::create(1);
    const auto document = service.load(metadata_fixture("valid-v1.torrent")).value().document();
    const VerifyRequest request{document, {}};

    REQUIRE(controller);
    AdmissionControlledVerifier verifier(service, controller.value());
    CancellationSource cancellation;
    cancellation.cancel();

    const auto result = verifier.verify(request, {cancellation.token(), {}, nullptr, {}});

    REQUIRE_FALSE(result);
    CHECK(result.error().code == ErrorCode::Cancelled);
}

TEST_CASE("given_capacity_one_when_waiter_is_cancelled_then_backend_is_not_entered_and_capacity_"
          "recovers",
          "[integration][application][verification-admission]")
{
    TempDirectory temporary;
    VerificationServiceSet services;
    const auto base_request = make_verification_request(temporary, services.first_service);
    auto controller = VerificationAdmissionController::create(1);
    REQUIRE(controller);
    AdmissionControlledVerifier waiter_verifier(services.second_service, controller.value());
    AdmissionControlledVerifier recovery_verifier(services.third_service, controller.value());

    HeldVerification first(services.first_service, controller.value(), base_request);
    first.wait_until_entered();

    CancellationSource cancellation;
    std::atomic<bool> waiter_progress{};
    std::mutex waiter_mutex;
    std::condition_variable waiter_changed;
    bool waiter_finished{};
    std::optional<Result<VerificationReport>> waiter_result;
    VerifyRequest waiter_request = base_request;
    waiter_request.on_progress = [&waiter_progress](const VerificationProgress&) {
        waiter_progress.store(true, std::memory_order_release);
    };
    std::thread waiter([&] {
        waiter_result =
            waiter_verifier.verify(waiter_request, {cancellation.token(), {}, nullptr, {}});
        {
            std::lock_guard<std::mutex> lock(waiter_mutex);
            waiter_finished = true;
        }
        waiter_changed.notify_all();
    });
    const bool waiter_queued =
        detail::VerificationAdmissionControllerTestProbe::wait_for_waiter_count(
            controller.value(), 1U, std::chrono::milliseconds(2'000));
    cancellation.cancel();
    bool cancelled_while_first_held{};
    {
        std::unique_lock<std::mutex> lock(waiter_mutex);
        cancelled_while_first_held = waiter_changed.wait_for(
            lock, std::chrono::seconds(2), [&waiter_finished] { return waiter_finished; });
    }

    first.release();
    waiter.join();
    first.join();

    REQUIRE(waiter_queued);
    REQUIRE(cancelled_while_first_held);
    REQUIRE(waiter_result.has_value());
    const auto& completed_waiter = require_optional(waiter_result);
    REQUIRE_FALSE(completed_waiter);
    CHECK(completed_waiter.error().code == ErrorCode::Cancelled);
    CHECK(waiter_progress.load(std::memory_order_acquire) == false);
    REQUIRE(first.result().has_value());
    const auto& completed_first = require_optional(first.result());
    REQUIRE(completed_first);

    const auto recovered = recovery_verifier.verify(base_request);
    REQUIRE(recovered);
}

TEST_CASE("given_capacity_one_when_two_waiters_queue_then_they_enter_backend_in_fifo_order",
          "[integration][application][verification-admission]")
{
    TempDirectory temporary;
    VerificationServiceSet services;
    const auto base_request = make_verification_request(temporary, services.first_service);
    auto controller = VerificationAdmissionController::create(1);
    REQUIRE(controller);
    AdmissionControlledVerifier second_verifier(services.second_service, controller.value());
    AdmissionControlledVerifier third_verifier(services.third_service, controller.value());

    HeldVerification first(services.first_service, controller.value(), base_request);
    first.wait_until_entered();

    std::mutex order_mutex;
    std::vector<int> backend_order;
    std::atomic<bool> second_recorded{};
    std::atomic<bool> third_recorded{};
    VerifyRequest second_request = base_request;
    second_request.on_progress = [&](const VerificationProgress&) {
        if (!second_recorded.exchange(true, std::memory_order_acq_rel))
        {
            std::lock_guard<std::mutex> lock(order_mutex);
            backend_order.push_back(2);
        }
    };
    VerifyRequest third_request = base_request;
    third_request.on_progress = [&](const VerificationProgress&) {
        if (!third_recorded.exchange(true, std::memory_order_acq_rel))
        {
            std::lock_guard<std::mutex> lock(order_mutex);
            backend_order.push_back(3);
        }
    };
    std::optional<Result<VerificationReport>> second_result;
    std::optional<Result<VerificationReport>> third_result;
    std::thread second([&] { second_result = second_verifier.verify(second_request); });
    const bool second_queued =
        detail::VerificationAdmissionControllerTestProbe::wait_for_waiter_count(
            controller.value(), 1U, std::chrono::milliseconds(2'000));
    std::thread third;
    bool third_queued{};
    if (second_queued)
    {
        third = std::thread([&] { third_result = third_verifier.verify(third_request); });
        third_queued = detail::VerificationAdmissionControllerTestProbe::wait_for_waiter_count(
            controller.value(), 2U, std::chrono::milliseconds(2'000));
    }

    first.release();
    first.join();
    second.join();
    if (third.joinable())
    {
        third.join();
    }

    REQUIRE(second_queued);
    REQUIRE(third_queued);
    REQUIRE(first.result().has_value());
    const auto& completed_first = require_optional(first.result());
    REQUIRE(completed_first);
    REQUIRE(second_result.has_value());
    const auto& completed_second = require_optional(second_result);
    REQUIRE(completed_second);
    REQUIRE(third_result.has_value());
    const auto& completed_third = require_optional(third_result);
    REQUIRE(completed_third);
    CHECK(backend_order == std::vector<int>{2, 3});
}

TEST_CASE("given_acquired_permit_when_verify_errors_cancels_or_throws_then_capacity_is_released",
          "[integration][application][verification-admission]")
{
    TempDirectory temporary;
    VerificationServiceSet services;
    const auto base_request = make_verification_request(temporary, services.first_service);
    REQUIRE(services.first_service.verify(base_request));

    auto controller = VerificationAdmissionController::create(1);
    REQUIRE(controller);
    AdmissionControlledVerifier verifier(services.first_service, controller.value());

    {
        detail::ScopedTorrentEngineFault fault{detail::TorrentEngineFault::VerifyBackendIo};
        const auto failed = verifier.verify(base_request);
        REQUIRE_FALSE(failed);
        CHECK(failed.error().code == ErrorCode::IoFailure);
    }
    REQUIRE(verifier.verify(base_request));

    CancellationSource cancellation;
    VerifyRequest cancelling_request = base_request;
    cancelling_request.on_progress = [&cancellation](const VerificationProgress&) {
        cancellation.cancel();
    };
    const auto cancelled =
        verifier.verify(cancelling_request, {cancellation.token(), {}, nullptr, {}});
    REQUIRE_FALSE(cancelled);
    CHECK(cancelled.error().code == ErrorCode::Cancelled);
    REQUIRE(verifier.verify(base_request));

    VerifyRequest throwing_request = base_request;
    throwing_request.on_progress = [](const VerificationProgress&) {
        throw std::runtime_error("verification progress failure");
    };
    CHECK_THROWS_AS(verifier.verify(throwing_request), std::runtime_error);
    REQUIRE(verifier.verify(base_request));
}
TEST_CASE("given_save_request_edges_then_repository_enforces_target_policy")
{
    TempDirectory temporary;
    const auto source = copy_fixture(temporary, "top-level-source-v1.torrent");
    FileTorrentRepository repository;
    FixedClock clock;
    TorrentService service(repository, clock);
    const auto loaded = service.load(source).value();

    {
        const auto edited =
            service.edit(loaded, {SetComment{"missing destination"}}).value().loaded;
        SaveRequest request;
        request.mode = SaveTargetMode::NewPath;
        const auto saved = service.save(edited, request);
        REQUIRE_FALSE(saved);
        CHECK(saved.error().code == ErrorCode::ValidationFailed);
    }

    {
        const auto target = temporary.path() / "directory-target.torrent";
        std::filesystem::create_directory(target);
        const auto edited = service.edit(loaded, {SetComment{"directory target"}}).value().loaded;
        SaveRequest request;
        request.mode = SaveTargetMode::NewPath;
        request.destination = target;
        const auto saved = service.save(edited, request);
        REQUIRE_FALSE(saved);
        CHECK(saved.error().code == ErrorCode::Conflict);
    }

    {
        const auto target = temporary.path() / "symlink-target.torrent";
        std::error_code symlink_error;
        std::filesystem::create_symlink(source, target, symlink_error);
        if (!symlink_error)
        {
            const auto edited = service.edit(loaded, {SetComment{"symlink target"}}).value().loaded;
            SaveRequest request;
            request.mode = SaveTargetMode::NewPath;
            request.destination = target;
            const auto saved = service.save(edited, request);
            REQUIRE_FALSE(saved);
            CHECK(saved.error().code == ErrorCode::Conflict);
        }
    }

    {
        const auto target = temporary.path() / "existing-target.torrent";
        std::filesystem::copy_file(source, target);
        const auto edited = service.edit(loaded, {SetComment{"existing target"}}).value().loaded;
        SaveRequest request;
        request.mode = SaveTargetMode::NewPath;
        request.destination = target;
        const auto saved = service.save(edited, request);
        REQUIRE_FALSE(saved);
        CHECK(saved.error().code == ErrorCode::Conflict);
    }

    {
        const auto edited = service.edit(loaded, {SetComment{"same source"}}).value().loaded;
        SaveRequest request;
        request.mode = SaveTargetMode::NewPath;
        request.destination = source;
        const auto saved = service.save(edited, request);
        REQUIRE_FALSE(saved);
        CHECK(saved.error().code == ErrorCode::Conflict);
    }

    {
        const auto target = temporary.path() / "backup-target.torrent";
        std::filesystem::copy_file(source, target);
        const auto original_target = read_bytes(target);
        const auto edited = service.edit(loaded, {SetComment{"overwrite target"}}).value().loaded;
        SaveRequest request;
        request.mode = SaveTargetMode::NewPath;
        request.destination = target;
        request.allow_overwrite = true;
        request.backup = true;
        const auto saved = service.save(edited, request);
        REQUIRE(saved);
        CHECK(saved.value().disposition == SaveDisposition::Saved);
        CHECK(read_bytes(target) != original_target);

        std::filesystem::path backup;
        for (const auto& entry : std::filesystem::directory_iterator(temporary.path()))
        {
            const auto name = entry.path().filename().string();
            if (name.rfind("backup-target.torrent.bak-", 0) == 0)
            {
                backup = entry.path();
            }
        }
        REQUIRE_FALSE(backup.empty());
        CHECK(read_bytes(backup) == original_target);
    }

    {
        const auto target = temporary.path() / "missing-parent" / "target.torrent";
        const auto edited = service.edit(loaded, {SetComment{"missing parent"}}).value().loaded;
        SaveRequest request;
        request.mode = SaveTargetMode::NewPath;
        request.destination = target;
        const auto saved = service.save(edited, request);
        REQUIRE_FALSE(saved);
        CHECK(saved.error().code == ErrorCode::IoFailure);
    }

    {
        const auto target = temporary.path() / std::string(4096, 'x');
        const auto edited = service.edit(loaded, {SetComment{"long target"}}).value().loaded;
        SaveRequest request;
        request.mode = SaveTargetMode::NewPath;
        request.destination = target;
        const auto saved = service.save(edited, request);
        REQUIRE_FALSE(saved);
        CHECK(saved.error().code == ErrorCode::IoFailure);
    }

    {
        const auto invalid_name = std::string("invalid\0name", 12);
        const auto edited = service.edit(loaded, {SetComment{"invalid target"}}).value().loaded;
        SaveRequest request;
        request.mode = SaveTargetMode::NewPath;
        request.destination = temporary.path() / invalid_name;
        const auto saved = service.save(edited, request);
        REQUIRE(saved);
        CHECK(saved.value().disposition == SaveDisposition::Saved);
    }

    {
        class SourceOnlyRepository final : public TorrentRepository
        {
          public:
            using TorrentRepository::commit;

            Result<LoadedTorrent> load(const std::filesystem::path&, LoadOptions) override
            {
                return Result<LoadedTorrent>::failure(
                    {ErrorCode::UnsupportedFeature, "test repository does not load", {}});
            }

            Result<LoadedTorrent> commit(const LoadedTorrent&, std::vector<std::uint8_t>,
                                         const CancellationToken&) override
            {
                return Result<LoadedTorrent>::failure(
                    {ErrorCode::UnsupportedFeature, "test repository is source-only", {}});
            }
        } source_only_repository;
        SaveRequest request;
        request.mode = SaveTargetMode::NewPath;
        const auto committed = source_only_repository.commit(loaded, {}, request, {});
        REQUIRE_FALSE(committed);
        CHECK(committed.error().code == ErrorCode::UnsupportedFeature);
    }

    {
        CancellationSource cancellation;
        cancellation.cancel();
        const auto committed = repository.commit(loaded, read_bytes(source), cancellation.token());
        REQUIRE_FALSE(committed);
        CHECK(committed.error().code == ErrorCode::Cancelled);
    }
}
