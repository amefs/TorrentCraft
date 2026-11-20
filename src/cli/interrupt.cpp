#include "interrupt.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <csignal>
#include <ostream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace torrentcraft::cli::detail {
namespace {

// Signal handlers may only touch lock-free atomics; the watcher thread turns the count into a
// cancellation request, which takes locks and runs callbacks.
std::atomic<int> interrupt_count{0};
static_assert(std::atomic<int>::is_always_lock_free);

std::atomic<bool> scope_active{false};

std::atomic<bool> console_shared_with_shell{false};

#ifdef _WIN32

BOOL WINAPI on_console_control(const DWORD event)
{
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT)
    {
        return FALSE;
    }
    // Returning FALSE for a second interrupt lets the default handler end the process.
    return interrupt_count.fetch_add(1) == 0 ? TRUE : FALSE;
}

void install_handlers()
{
    // A process started in a new process group (for example by `start`, some terminals and
    // launchers) inherits "ignore Ctrl+C", and then no handler sees CTRL_C_EVENT. Turn Ctrl+C
    // processing back on so the operation can be cancelled; Ctrl+Break is never ignored.
    ::SetConsoleCtrlHandler(nullptr, FALSE);
    ::SetConsoleCtrlHandler(on_console_control, TRUE);
}

void restore_handlers()
{
    ::SetConsoleCtrlHandler(on_console_control, FALSE);
}

#else

using SignalAction = struct sigaction;

SignalAction previous_interrupt{};
SignalAction previous_terminate{};

void on_signal(const int signal_number)
{
    if (interrupt_count.fetch_add(1) != 0)
    {
        // A second interrupt ends the process the way it would have without this scope.
        std::signal(signal_number, SIG_DFL);
        std::raise(signal_number);
    }
}

void install_handlers()
{
    SignalAction action{};
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    sigaction(SIGINT, &action, &previous_interrupt);
    sigaction(SIGTERM, &action, &previous_terminate);
}

void restore_handlers()
{
    sigaction(SIGINT, &previous_interrupt, nullptr);
    sigaction(SIGTERM, &previous_terminate, nullptr);
}

#endif

} // namespace

void set_console_shared_with_shell(const bool value) noexcept
{
    console_shared_with_shell.store(value);
}

void print_cancel_hint(std::ostream& diagnostics)
{
    if (console_shared_with_shell.load())
    {
        diagnostics << "note: the shell does not wait for torrentcraft-gui.exe and keeps Ctrl+C; "
                       "press Ctrl+Break to cancel, or use torrentcraft.exe instead\n";
    }
}

InterruptScope::InterruptScope()
{
    [[maybe_unused]] const bool was_active = scope_active.exchange(true);
    assert(!was_active && "only one InterruptScope may be alive at a time");
    interrupt_count.store(0);
    install_handlers();
    watcher_ = std::thread([this] { watch(); });
}

InterruptScope::~InterruptScope()
{
    {
        const std::lock_guard lock(mutex_);
        stop_ = true;
    }
    stop_requested_.notify_one();
    watcher_.join();
    restore_handlers();
    interrupt_count.store(0);
    scope_active.store(false);
}

torrentutils::core::CancellationToken InterruptScope::token() const noexcept
{
    return source_.token();
}

void InterruptScope::watch()
{
    constexpr auto poll_interval = std::chrono::milliseconds(20);
    std::unique_lock lock(mutex_);
    while (!stop_requested_.wait_for(lock, poll_interval, [this] { return stop_; }))
    {
        if (interrupt_count.load() != 0)
        {
            source_.cancel();
        }
    }
}

} // namespace torrentcraft::cli::detail
