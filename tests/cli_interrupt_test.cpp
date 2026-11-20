// Ctrl+C handling is tested by raising SIGINT in-process, which Windows does not route to the
// console control handler, so these tests are POSIX-only.
#ifndef _WIN32

#include "cli.hpp"
#include "interrupt.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <ostream>
#include <sstream>
#include <streambuf>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

volatile std::sig_atomic_t fallback_interrupts = 0;

void count_fallback_interrupt(int /*signal_number*/)
{
    fallback_interrupts = fallback_interrupts + 1;
}

/** Replaces SIGINT's default action, so an interrupt outside InterruptScope cannot end the test. */
class FallbackInterruptHandler
{
  public:
    FallbackInterruptHandler() : previous_(std::signal(SIGINT, count_fallback_interrupt))
    {
        fallback_interrupts = 0;
    }

    ~FallbackInterruptHandler()
    {
        std::signal(SIGINT, previous_);
    }

    FallbackInterruptHandler(const FallbackInterruptHandler&) = delete;
    FallbackInterruptHandler& operator=(const FallbackInterruptHandler&) = delete;
    FallbackInterruptHandler(FallbackInterruptHandler&&) = delete;
    FallbackInterruptHandler& operator=(FallbackInterruptHandler&&) = delete;

  private:
    void (*previous_)(int);
};

/** A stream buffer that runs an action the first time anything is written to it. */
class FirstWriteBuffer final : public std::stringbuf
{
  public:
    explicit FirstWriteBuffer(std::function<void()> action) : action_(std::move(action)) {}

  protected:
    std::streamsize xsputn(const char* text, std::streamsize count) override
    {
        trigger();
        return std::stringbuf::xsputn(text, count);
    }

    int_type overflow(int_type character) override
    {
        trigger();
        return std::stringbuf::overflow(character);
    }

  private:
    void trigger()
    {
        if (action_)
        {
            std::exchange(action_, {})();
        }
    }

    std::function<void()> action_;
};

void interrupt_and_wait()
{
    std::raise(SIGINT);
    // Give the scope's watcher time to turn the interrupt into cancellation.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

int run_cli(const std::vector<std::string>& arguments, std::ostream& output,
            std::ostream& diagnostics)
{
    std::vector<const char*> argv;
    argv.reserve(arguments.size());
    for (const auto& argument : arguments)
    {
        argv.push_back(argument.c_str());
    }
    return torrentcraft::cli::run(static_cast<int>(argv.size()), argv.data(), output, diagnostics);
}

class TempDirectory
{
  public:
    TempDirectory()
        : path_(std::filesystem::temp_directory_path() /
                ("torrentcraft-interrupt-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
    {
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;
    TempDirectory(TempDirectory&&) = delete;
    TempDirectory& operator=(TempDirectory&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

/** Large enough that hashing outlasts the watcher's poll interval; sparse, so cheap to make. */
void write_sparse_payload(const std::filesystem::path& path)
{
    constexpr std::uintmax_t payload_bytes = 512ULL * 1024U * 1024U;
    {
        const std::ofstream create(path, std::ios::binary);
        REQUIRE(create);
    }
    std::filesystem::resize_file(path, payload_bytes);
}

[[nodiscard]] std::vector<std::filesystem::path>
directory_entries(const std::filesystem::path& path)
{
    std::vector<std::filesystem::path> entries;
    for (const auto& entry : std::filesystem::directory_iterator(path))
    {
        entries.push_back(entry.path().filename());
    }
    return entries;
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("given_interrupt_scope_when_sigint_raised_then_token_is_cancelled_and_handler_restored",
          "[unit][cli][interrupt]")
{
    const FallbackInterruptHandler fallback;
    {
        const torrentcraft::cli::detail::InterruptScope scope;
        const auto token = scope.token();
        CHECK_FALSE(token.is_cancelled());
        std::raise(SIGINT);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!token.is_cancelled() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(token.is_cancelled());
        CHECK(fallback_interrupts == 0);
    }
    std::raise(SIGINT);
    CHECK(fallback_interrupts == 1);
}

TEST_CASE("given_ctrl_c_during_create_when_hashing_then_exit_7_and_no_output_is_left",
          "[unit][cli][interrupt]")
{
    const FallbackInterruptHandler fallback;
    const TempDirectory temp;
    const auto payload = temp.path() / "payload.bin";
    write_sparse_payload(payload);
    const auto target = temp.path() / "payload.torrent";

    std::ostringstream output;
    FirstWriteBuffer progress_buffer(interrupt_and_wait);
    std::ostream diagnostics(&progress_buffer);
    CHECK(run_cli({"torrentcraft", "create", payload.u8string(), "-o", target.u8string(),
                   "--piece-size", "1024", "--progress=json"},
                  output, diagnostics) == 7);
    CHECK(progress_buffer.str().find("cancel") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(target));
    CHECK(directory_entries(temp.path()) ==
          std::vector<std::filesystem::path>{std::filesystem::path("payload.bin")});
    CHECK(fallback_interrupts == 0);
}

TEST_CASE("given_ctrl_c_during_verify_when_hashing_then_exit_7", "[unit][cli][interrupt]")
{
    const FallbackInterruptHandler fallback;
    const TempDirectory temp;
    const auto payload = temp.path() / "payload.bin";
    write_sparse_payload(payload);
    const auto torrent = temp.path() / "payload.torrent";
    std::ostringstream ignored;
    REQUIRE(run_cli({"torrentcraft", "create", payload.u8string(), "-o", torrent.u8string(),
                     "--piece-size", "1024", "--quiet"},
                    ignored, ignored) == 0);

    std::ostringstream output;
    FirstWriteBuffer progress_buffer(interrupt_and_wait);
    std::ostream diagnostics(&progress_buffer);
    CHECK(run_cli(
              {"torrentcraft", "verify", torrent.u8string(), payload.u8string(), "--progress=json"},
              output, diagnostics) == 7);
    CHECK(progress_buffer.str().find("cancel") != std::string::npos);
    CHECK(fallback_interrupts == 0);
}

TEST_CASE("given_ctrl_c_during_tracker_rewrite_when_first_file_done_then_rest_are_untouched",
          "[unit][cli][interrupt][tracker]")
{
    const FallbackInterruptHandler fallback;
    const TempDirectory temp;
    const auto fixture = std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" /
                         "metadata" / "valid-v1.torrent";
    const auto original = read_file(fixture);
    for (const char* name : {"a.torrent", "b.torrent", "c.torrent"})
    {
        std::filesystem::copy_file(fixture, temp.path() / name);
    }

    FirstWriteBuffer output_buffer(interrupt_and_wait);
    std::ostream output(&output_buffer);
    std::ostringstream diagnostics;
    CHECK(run_cli({"torrentcraft", "tracker", "rewrite", temp.path().u8string(), "-e",
                   "s#tracker\\.example#tracker.new.example#"},
                  output, diagnostics) == 7);
    CHECK(diagnostics.str().find("cancelled") != std::string::npos);
    int untouched = 0;
    for (const char* name : {"a.torrent", "b.torrent", "c.torrent"})
    {
        untouched += read_file(temp.path() / name) == original ? 1 : 0;
    }
    CHECK(untouched == 2);
    CHECK(fallback_interrupts == 0);
}

#endif
