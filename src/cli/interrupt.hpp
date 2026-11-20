#pragma once

#include <condition_variable>
#include <iosfwd>
#include <mutex>
#include <thread>
#include <torrentutils/core/cancellation.hpp>

namespace torrentcraft::cli::detail {

/**
 * Records that the process writes to a console whose shell is not waiting for it. That shell
 * keeps reading keys, so Ctrl+C reaches the shell instead of this process.
 */
void set_console_shared_with_shell(bool value) noexcept;

/** Tells the user how to cancel when Ctrl+C cannot reach this process; otherwise prints nothing. */
void print_cancel_hint(std::ostream& diagnostics);

/**
 * Turns Ctrl+C (SIGINT and SIGTERM on POSIX, Ctrl+C and Ctrl+Break on Windows) into cancellation
 * of the running operation while the scope is alive, so the operation stops at its next
 * cancellation point, removes its temporary output and exits with code 7. A second interrupt
 * ends the process immediately. The previous handlers are restored when the scope ends.
 *
 * Only one scope may be alive at a time.
 */
class InterruptScope
{
  public:
    InterruptScope();
    ~InterruptScope();
    InterruptScope(const InterruptScope&) = delete;
    InterruptScope& operator=(const InterruptScope&) = delete;
    InterruptScope(InterruptScope&&) = delete;
    InterruptScope& operator=(InterruptScope&&) = delete;

    [[nodiscard]] torrentutils::core::CancellationToken token() const noexcept;

  private:
    void watch();

    torrentutils::core::CancellationSource source_;
    std::mutex mutex_;
    std::condition_variable stop_requested_;
    bool stop_{};
    std::thread watcher_;
};

} // namespace torrentcraft::cli::detail
