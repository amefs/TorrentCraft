#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <torrentutils/frontend/tracker_batch.hpp>
#include <utility>
#include <vector>

namespace {

using namespace torrentutils::core;
using namespace torrentutils::frontend;

class TempDirectory
{
  public:
    TempDirectory()
    {
        static std::atomic<std::uint64_t> sequence{};
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ =
            std::filesystem::temp_directory_path() / ("torrentcraft-batch-" + std::to_string(tick) +
                                                      "-" + std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(path_);
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;
    TempDirectory(TempDirectory&&) = delete;
    TempDirectory& operator=(TempDirectory&&) = delete;

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

[[nodiscard]] std::filesystem::path fixture(const std::string& name)
{
    return std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" / "metadata" / name;
}

void copy_fixture(const std::string& name, const std::filesystem::path& target)
{
    std::filesystem::create_directories(target.parent_path());
    std::filesystem::copy_file(fixture(name), target);
}

void write_text(const std::filesystem::path& target, const std::string& text)
{
    std::filesystem::create_directories(target.parent_path());
    std::ofstream output(target, std::ios::binary);
    output << text;
}

[[nodiscard]] std::vector<std::string> relatives(const std::vector<TorrentBatchFile>& files)
{
    std::vector<std::string> result;
    result.reserve(files.size());
    for (const auto& file : files)
    {
        result.push_back(file.relative.generic_string());
    }
    return result;
}

[[nodiscard]] TrackerRewriteRules rules(const std::vector<std::string>& expressions)
{
    auto parsed = TrackerRewriteRules::parse(expressions);
    if (!parsed)
    {
        throw std::runtime_error("rules failed to parse: " + parsed.error().message);
    }
    return std::move(parsed).value();
}

[[nodiscard]] TorrentBatchFile single(const std::filesystem::path& path)
{
    auto files = enumerate_torrent_files(path, false);
    if (!files || files.value().size() != 1U)
    {
        throw std::runtime_error("expected exactly one torrent file");
    }
    return files.value().front();
}

[[nodiscard]] std::vector<std::string> tracker_values(const TorrentService& service,
                                                      const std::filesystem::path& path)
{
    std::vector<std::string> result;
    const auto loaded = service.load(path);
    if (!loaded)
    {
        throw std::runtime_error("expected the torrent to load");
    }
    for (const auto& tier : loaded.value().document().trackers().tiers())
    {
        for (const auto& tracker : tier.trackers())
        {
            result.push_back(tracker.value());
        }
    }
    return result;
}

[[nodiscard]] ErrorCode error_code(const TrackerBatchFileResult& result)
{
    if (!result.error)
    {
        throw std::logic_error("expected a failed batch result");
    }
    return result.error->code;
}

[[nodiscard]] std::size_t preview_count(const TrackerBatchFileResult& result,
                                        const TrackerRewriteStatus status)
{
    if (!result.preview)
    {
        throw std::logic_error("expected a rewrite preview");
    }
    return result.preview->count(status);
}

const std::string kRewrite = R"(s#tracker\.example#tracker.new.example#g)";

TEST_CASE("given_folder_layout_when_enumerated_then_only_torrents_are_listed_in_order",
          "[unit][frontend][tracker][batch]")
{
    const TempDirectory temp;
    const auto root = temp.path() / "in";
    copy_fixture("valid-v1.torrent", root / "b.torrent");
    copy_fixture("valid-v1.torrent", root / "a.torrent");
    copy_fixture("valid-v1.torrent", root / "sub" / "deep" / "c.torrent");
    copy_fixture("valid-v1.torrent", root / "sub" / "d.torrent");
    write_text(root / "notes.txt", "not a torrent");
    write_text(root / "upper.TORRENT", "different extension case");
    std::filesystem::create_directories(root / "folder.torrent");

    const auto flat = enumerate_torrent_files(root, false);
    REQUIRE(flat);
    CHECK(relatives(flat.value()) == std::vector<std::string>{"a.torrent", "b.torrent"});
    CHECK(flat.value().front().path == root / "a.torrent");
    CHECK(flat.value().front().size == std::filesystem::file_size(root / "a.torrent"));

    const auto deep = enumerate_torrent_files(root, true);
    REQUIRE(deep);
    CHECK(relatives(deep.value()) == std::vector<std::string>{"a.torrent", "b.torrent",
                                                              "sub/d.torrent",
                                                              "sub/deep/c.torrent"});
    CHECK(batch_output_path(temp.path() / "out", deep.value().back()) ==
          temp.path() / "out" / "sub" / "deep" / "c.torrent");

    const auto empty_root = temp.path() / "empty";
    std::filesystem::create_directories(empty_root);
    const auto empty = enumerate_torrent_files(empty_root, true);
    REQUIRE(empty);
    CHECK(empty.value().empty());
}

TEST_CASE("given_file_or_missing_root_when_enumerated_then_single_file_or_error_is_returned",
          "[unit][frontend][tracker][batch]")
{
    const TempDirectory temp;
    const auto torrent = temp.path() / "nested" / "one.torrent";
    copy_fixture("valid-v1.torrent", torrent);
    write_text(temp.path() / "notes.txt", "text");

    const auto file = enumerate_torrent_files(torrent, true);
    REQUIRE(file);
    REQUIRE(file.value().size() == 1U);
    CHECK(file.value().front().path == torrent);
    CHECK(file.value().front().relative == std::filesystem::path("one.torrent"));
    CHECK(batch_output_path(temp.path() / "out", file.value().front()) ==
          temp.path() / "out" / "one.torrent");

    const auto text = enumerate_torrent_files(temp.path() / "notes.txt", false);
    REQUIRE_FALSE(text);
    CHECK(text.error().code == ErrorCode::ValidationFailed);

    const auto missing = enumerate_torrent_files(temp.path() / "missing", false);
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == ErrorCode::FileNotFound);
}

TEST_CASE("given_directory_symlink_when_enumerated_recursively_then_it_is_not_followed",
          "[unit][frontend][tracker][batch]")
{
    const TempDirectory temp;
    const auto root = temp.path() / "in";
    const auto outside = temp.path() / "outside";
    copy_fixture("valid-v1.torrent", root / "a.torrent");
    copy_fixture("valid-v1.torrent", outside / "hidden.torrent");
    std::error_code error;
    std::filesystem::create_directory_symlink(outside, root / "link", error);
    if (error)
    {
        SKIP("filesystem directory symlink creation is unavailable: " + error.message());
    }

    const auto files = enumerate_torrent_files(root, true);
    REQUIRE(files);
    CHECK(relatives(files.value()) == std::vector<std::string>{"a.torrent"});
}

TEST_CASE("given_unreadable_folder_when_enumerated_then_an_io_failure_is_reported",
          "[unit][frontend][tracker][batch]")
{
    const TempDirectory temp;
    const auto root = temp.path() / "in";
    copy_fixture("valid-v1.torrent", root / "a.torrent");
    copy_fixture("valid-v1.torrent", root / "locked" / "b.torrent");
    using std::filesystem::perms;
    std::filesystem::permissions(root / "locked", perms::none);
    const bool readable = static_cast<bool>(std::ifstream(root / "locked" / "b.torrent"));
    if (readable)
    {
        std::filesystem::permissions(root / "locked", perms::owner_all);
        SKIP("the test filesystem does not enforce unreadable directory permissions");
    }

    const auto nested = enumerate_torrent_files(root, true);
    const auto locked = enumerate_torrent_files(root / "locked", false);
    const auto locked_recursive = enumerate_torrent_files(root / "locked", true);
    std::filesystem::permissions(root / "locked", perms::owner_all);

    REQUIRE_FALSE(nested);
    CHECK(nested.error().code == ErrorCode::IoFailure);
    REQUIRE_FALSE(locked);
    CHECK(locked.error().code == ErrorCode::IoFailure);
    REQUIRE_FALSE(locked_recursive);
    CHECK(locked_recursive.error().code == ErrorCode::IoFailure);
}

TEST_CASE("given_cancelled_token_when_enumerated_then_cancellation_is_reported",
          "[unit][frontend][tracker][batch]")
{
    const TempDirectory temp;
    copy_fixture("valid-v1.torrent", temp.path() / "a.torrent");
    CancellationSource source;
    source.cancel();

    for (const bool recursive : {false, true})
    {
        const auto files = enumerate_torrent_files(temp.path(), recursive, source.token());
        REQUIRE_FALSE(files);
        CHECK(files.error().code == ErrorCode::Cancelled);
    }
}

TEST_CASE("given_rewrite_rules_when_batch_file_is_applied_then_outcome_and_target_are_reported",
          "[unit][frontend][tracker][batch]")
{
    const TempDirectory temp;
    const auto root = temp.path() / "in";
    copy_fixture("valid-v1.torrent", root / "sub" / "a.torrent");
    const auto files = enumerate_torrent_files(root, true).value();
    REQUIRE(files.size() == 1U);
    const auto& file = files.front();
    const auto original_size = std::filesystem::file_size(file.path);
    FileTorrentRepository repository;
    SystemClock clock;
    const TorrentService service(repository, clock);
    const auto out = temp.path() / "out";

    TrackerBatchOptions dry_run;
    dry_run.output_dir = out;
    dry_run.dry_run = true;
    const auto preview = apply_tracker_batch_file(service, file, rules({kRewrite}), dry_run);
    CHECK(preview.status == TrackerBatchFileStatus::Changed);
    CHECK_FALSE(preview.error);
    CHECK(preview.target == out / "sub" / "a.torrent");
    REQUIRE(preview.preview);
    CHECK(preview_count(preview, TrackerRewriteStatus::Changed) == 2U);
    CHECK_FALSE(std::filesystem::exists(out));

    TrackerBatchOptions to_folder;
    to_folder.output_dir = out;
    const auto written = apply_tracker_batch_file(service, file, rules({kRewrite}), to_folder);
    REQUIRE(written.status == TrackerBatchFileStatus::Changed);
    CHECK(tracker_values(service, out / "sub" / "a.torrent") ==
          std::vector<std::string>{"https://tracker.new.example/announce",
                                   "udp://tracker.new.example:6969/announce",
                                   "https://backup.example/announce"});
    CHECK(std::filesystem::file_size(file.path) == original_size);

    const auto conflict = apply_tracker_batch_file(service, file, rules({kRewrite}), to_folder);
    CHECK(conflict.status == TrackerBatchFileStatus::Failed);
    REQUIRE(conflict.error);
    CHECK(error_code(conflict) == ErrorCode::Conflict);
    CHECK(conflict.preview);
    CHECK_FALSE(conflict.cancelled());

    to_folder.overwrite = true;
    CHECK(apply_tracker_batch_file(service, file, rules({kRewrite}), to_folder).status ==
          TrackerBatchFileStatus::Changed);

    const auto unchanged = apply_tracker_batch_file(service, file, rules({"s/nomatch/x/"}), {});
    CHECK(unchanged.status == TrackerBatchFileStatus::Unchanged);
    CHECK(unchanged.target == file.path);
    CHECK(std::filesystem::file_size(file.path) == original_size);

    const auto invalid = apply_tracker_batch_file(service, file, rules({"s#^udp:#ftp:#"}), {});
    CHECK(invalid.status == TrackerBatchFileStatus::Failed);
    REQUIRE(invalid.error);
    CHECK(error_code(invalid) == ErrorCode::ValidationFailed);
    REQUIRE(invalid.preview);
    CHECK(preview_count(invalid, TrackerRewriteStatus::Invalid) == 1U);

    TrackerBatchOptions in_place;
    in_place.backup = true;
    REQUIRE(apply_tracker_batch_file(service, file, rules({kRewrite}), in_place).status ==
            TrackerBatchFileStatus::Changed);
    CHECK(tracker_values(service, file.path).front() == "https://tracker.new.example/announce");
    std::size_t backups = 0;
    for (const auto& entry : std::filesystem::directory_iterator(file.path.parent_path()))
    {
        backups += entry.path().filename().string().rfind("a.torrent.bak", 0) == 0U ? 1U : 0U;
    }
    CHECK(backups == 1U);
}

TEST_CASE("given_symlinked_torrent_when_applied_in_place_then_dry_run_and_write_both_fail",
          "[unit][frontend][tracker][batch]")
{
    const TempDirectory temp;
    const auto root = temp.path() / "in";
    copy_fixture("valid-v1.torrent", temp.path() / "real.torrent");
    std::filesystem::create_directories(root);
    std::error_code error;
    std::filesystem::create_symlink(temp.path() / "real.torrent", root / "link.torrent", error);
    if (error)
    {
        SKIP("filesystem symlink creation is unavailable: " + error.message());
    }
    const auto files = enumerate_torrent_files(root, false).value();
    REQUIRE(files.size() == 1U);
    FileTorrentRepository repository;
    SystemClock clock;
    const TorrentService service(repository, clock);

    // Saving in place refuses a symlinked source, so the dry run must report that too.
    TrackerBatchOptions dry_run;
    dry_run.dry_run = true;
    const auto preview =
        apply_tracker_batch_file(service, files.front(), rules({kRewrite}), dry_run);
    CHECK(preview.status == TrackerBatchFileStatus::Failed);
    const auto written = apply_tracker_batch_file(service, files.front(), rules({kRewrite}), {});
    CHECK(written.status == TrackerBatchFileStatus::Failed);

    // Writing to an output folder does not touch the link, so it still works.
    TrackerBatchOptions to_folder;
    to_folder.output_dir = temp.path() / "out";
    CHECK(apply_tracker_batch_file(service, files.front(), rules({kRewrite}), to_folder).status ==
          TrackerBatchFileStatus::Changed);
}

TEST_CASE("given_tracker_list_when_batch_file_is_applied_then_list_replaces_without_preview",
          "[unit][frontend][tracker][batch]")
{
    const TempDirectory temp;
    const auto torrent = temp.path() / "a.torrent";
    copy_fixture("valid-v1.torrent", torrent);
    const auto file = single(torrent);
    FileTorrentRepository repository;
    SystemClock clock;
    const TorrentService service(repository, clock);
    const auto replacement =
        TrackerList::create(
            {TrackerTier::create({TrackerUrl::parse("https://only.example/announce").value()})
                 .value()})
            .value();

    const auto replaced = apply_tracker_batch_file(service, file, replacement, {});
    REQUIRE(replaced.status == TrackerBatchFileStatus::Changed);
    CHECK_FALSE(replaced.preview);
    CHECK(tracker_values(service, torrent) ==
          std::vector<std::string>{"https://only.example/announce"});

    const auto again = apply_tracker_batch_file(service, file, replacement, {});
    CHECK(again.status == TrackerBatchFileStatus::Unchanged);
}

TEST_CASE("given_unusable_input_when_batch_file_is_applied_then_the_file_fails_alone",
          "[unit][frontend][tracker][batch]")
{
    const TempDirectory temp;
    FileTorrentRepository repository;
    SystemClock clock;
    const TorrentService service(repository, clock);

    write_text(temp.path() / "broken.torrent", "not bencode");
    const auto broken = apply_tracker_batch_file(service, single(temp.path() / "broken.torrent"),
                                                 rules({kRewrite}), {});
    CHECK(broken.status == TrackerBatchFileStatus::Failed);
    REQUIRE(broken.error);
    CHECK_FALSE(broken.preview);
    CHECK_FALSE(broken.cancelled());

    const auto torrent = temp.path() / "a.torrent";
    copy_fixture("valid-v1.torrent", torrent);
    const auto file = single(torrent);

    CancellationSource source;
    source.cancel();
    TaskContext cancelled;
    cancelled.cancellation = source.token();
    const auto stopped = apply_tracker_batch_file(service, file, rules({kRewrite}), {}, cancelled);
    CHECK(stopped.status == TrackerBatchFileStatus::Failed);
    CHECK(stopped.cancelled());

    // The output folder cannot be created below a regular file.
    write_text(temp.path() / "blocker", "file");
    TrackerBatchOptions blocked;
    blocked.output_dir = temp.path() / "blocker" / "out";
    const auto unwritable = apply_tracker_batch_file(service, file, rules({kRewrite}), blocked);
    CHECK(unwritable.status == TrackerBatchFileStatus::Failed);
    REQUIRE(unwritable.error);
    CHECK(error_code(unwritable) == ErrorCode::IoFailure);
    CHECK(unwritable.preview);
}

} // namespace
