#include "progress.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

namespace {

using torrentcraft::cli::ProgressMode;
using torrentcraft::cli::ProgressWriter;

constexpr const char* unicode_console_sample_name =
    u8"[EAC][201209][UZCL-2200][田渕夏海・中村巴奈重・櫻井美希]["
    u8"TVアニメ「安達としまむら」オリジナル・サウンドトラック]";

[[nodiscard]] torrentutils::core::ProgressInfo create_event(const std::uint64_t completed,
                                                            const std::uint64_t total)
{
    return {"hashing", completed, total};
}

[[nodiscard]] torrentutils::core::VerificationProgress verify_event(const std::uint64_t sequence,
                                                                    const std::uint64_t hashed)
{
    auto path = torrentutils::core::LogicalPath::from_segments({"a.bin"}).value();
    torrentutils::core::FileVerificationProgress file{path, 100U, hashed, 0U, 0U};
    torrentutils::core::VerificationProgress progress;
    progress.sequence = sequence;
    progress.files.push_back(file);
    return progress;
}

TEST_CASE("given_mixed_unicode_label_when_tty_progress_then_label_is_not_rendered",
          "[unit][progress]")
{
    std::ostringstream out;
    ProgressWriter writer(ProgressMode::Tty, true, false, out, std::chrono::milliseconds{0});
    writer.create_start(100);
    writer.create_event(create_event(100, 100));

    REQUIRE(out.str().find(unicode_console_sample_name) == std::string::npos);
    REQUIRE(out.str().find("Speed:") != std::string::npos);
}

TEST_CASE("given_zero_interval_when_create_events_then_every_event_is_emitted", "[unit][progress]")
{
    std::ostringstream out;
    ProgressWriter writer(ProgressMode::Plain, false, false, out, std::chrono::milliseconds{0});
    writer.create_start(100);
    writer.create_event(create_event(10, 100));
    writer.create_event(create_event(50, 100));
    writer.create_event(create_event(100, 100));
    writer.finish();

    REQUIRE(out.str().find("[HASH] 10%") != std::string::npos);
    REQUIRE(out.str().find("[HASH] 50%") != std::string::npos);
    REQUIRE(out.str().find("[HASH] 100%") != std::string::npos);
}

TEST_CASE("given_huge_interval_when_create_events_then_intermediate_events_are_suppressed",
          "[unit][progress]")
{
    std::ostringstream out;
    ProgressWriter writer(ProgressMode::Plain, false, false, out, std::chrono::hours{1});
    writer.create_start(100);
    writer.create_event(create_event(10, 100));
    writer.create_event(create_event(50, 100));
    writer.create_event(create_event(100, 100));
    writer.finish();

    REQUIRE(out.str().find("[HASH] 10%") != std::string::npos);
    REQUIRE(out.str().find("[HASH] 50%") == std::string::npos);
    REQUIRE(out.str().find("[HASH] 100%") != std::string::npos);
}

TEST_CASE("given_huge_interval_when_finish_then_terminal_state_is_guaranteed", "[unit][progress]")
{
    std::ostringstream out;
    ProgressWriter writer(ProgressMode::Plain, false, false, out, std::chrono::hours{1});
    writer.create_start(100);
    writer.create_event(create_event(10, 100));
    writer.create_event(create_event(50, 100));
    writer.finish();

    REQUIRE(out.str().find("[HASH] 100%") != std::string::npos);
}

TEST_CASE("given_json_mode_when_create_events_then_lines_are_valid_json", "[unit][progress]")
{
    std::ostringstream out;
    ProgressWriter writer(ProgressMode::Json, false, false, out, std::chrono::milliseconds{0});
    writer.create_start(100);
    writer.create_event(create_event(50, 100));
    writer.finish();

    const auto line = out.str();
    const auto first_line = line.substr(0U, line.find('\n'));
    const auto parsed = nlohmann::json::parse(first_line, nullptr, false);
    REQUIRE_FALSE(parsed.is_discarded());
    REQUIRE(parsed["stage"] == "hashing");
    REQUIRE(parsed["completed"] == 50);
    REQUIRE(parsed["total"] == 100);
}

TEST_CASE("given_verify_events_when_plain_then_verify_lines_are_printed", "[unit][progress]")
{
    std::ostringstream out;
    ProgressWriter writer(ProgressMode::Plain, false, false, out, std::chrono::milliseconds{0});
    writer.verify_start(16384, 100, 1);
    writer.verify_event(verify_event(1, 50));
    writer.finish();

    REQUIRE(out.str().find("[VERIFY] 50%") != std::string::npos);
}

TEST_CASE("given_tty_mode_without_terminal_when_events_then_plain_lines_are_emitted",
          "[unit][progress]")
{
    std::ostringstream out;
    ProgressWriter writer(ProgressMode::Tty, false, false, out, std::chrono::milliseconds{0});
    writer.verify_start(16384, 100, 1);
    writer.verify_event(verify_event(1, 50));
    writer.finish();

    REQUIRE(out.str().find("[VERIFY] 50%") != std::string::npos);
}

TEST_CASE("given_quiet_when_events_then_nothing_is_emitted", "[unit][progress]")
{
    std::ostringstream out;
    ProgressWriter writer(ProgressMode::Plain, false, true, out, std::chrono::milliseconds{0});
    writer.create_start(100);
    writer.create_event(create_event(100, 100));
    writer.finish();

    REQUIRE(out.str().empty());
}

TEST_CASE("given_parse_progress_mode_when_valid_and_invalid_values_then_results_are_stable",
          "[unit][progress]")
{
    ProgressMode mode = ProgressMode::None;
    std::string error;
    REQUIRE(torrentcraft::cli::parse_progress_mode("json", mode, error));
    REQUIRE(mode == ProgressMode::Json);
    REQUIRE(torrentcraft::cli::parse_progress_mode("plain", mode, error));
    REQUIRE(mode == ProgressMode::Plain);
    REQUIRE(torrentcraft::cli::parse_progress_mode("tty", mode, error));
    REQUIRE(mode == ProgressMode::Tty);
    REQUIRE_FALSE(torrentcraft::cli::parse_progress_mode("xml", mode, error));
    REQUIRE(error.find("json, plain, or tty") != std::string::npos);
}

TEST_CASE("given_verify_pages_when_new_file_is_discovered_then_progress_does_not_regress",
          "[unit][progress]")
{
    std::ostringstream out;
    ProgressWriter writer(ProgressMode::Plain, false, false, out, std::chrono::milliseconds{0});
    writer.verify_start(16384, 200, 2);

    const auto path_a = torrentutils::core::LogicalPath::from_segments({"a.bin"}).value();
    const auto path_b = torrentutils::core::LogicalPath::from_segments({"b.bin"}).value();

    torrentutils::core::VerificationProgress first;
    first.sequence = 1;
    first.files.push_back({path_a, 100, 100, 100, 0});
    writer.verify_event(first);

    torrentutils::core::VerificationProgress second;
    second.sequence = 2;
    second.files.push_back({path_a, 100, 100, 100, 0});
    second.files.push_back({path_b, 100, 0, 0, 0});
    writer.verify_event(second);

    writer.finish();

    const auto text = out.str();
    const auto half = text.find("[VERIFY] 50%");
    const auto full = text.find("[VERIFY] 100%");
    REQUIRE(half != std::string::npos);
    REQUIRE(full != std::string::npos);
    REQUIRE(half < full);
}

} // namespace
