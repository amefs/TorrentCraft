#pragma once

#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <torrentutils/core/task.hpp>
#include <torrentutils/core/torrent_engine.hpp>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <indicators/progress_bar.hpp>

namespace torrentcraft::cli {

using Json = nlohmann::json;

enum class ProgressMode
{
    None,
    Json,
    Plain,
    Tty
};

[[nodiscard]] inline bool parse_progress_mode(const std::string_view value, ProgressMode& mode,
                                              std::string& error) noexcept
{
    if (value == "json")
    {
        mode = ProgressMode::Json;
        return true;
    }
    if (value == "plain")
    {
        mode = ProgressMode::Plain;
        return true;
    }
    if (value == "tty")
    {
        mode = ProgressMode::Tty;
        return true;
    }
    error = "--progress must be json, plain, or tty";
    return false;
}

[[nodiscard]] inline bool diagnostics_is_tty(const std::ostream& stream) noexcept
{
    if (&stream != &std::cerr)
    {
        return false;
    }
#ifdef _WIN32
    return ::_isatty(::_fileno(stderr)) != 0;
#else
    return ::isatty(::fileno(stderr)) != 0;
#endif
}

/**
 * Windows only: whether the console already decoded UTF-8 before the CLI forced
 * the output code page. When the CLI had to force CP_UTF8 (legacy GBK/CP936
 * console), some hosts still decode stderr bytes with the original code page,
 * so progress bar and tree glyphs fall back to ASCII there.
 */
inline bool& console_utf8_native() noexcept
{
    static bool value = true;
    return value;
}

[[nodiscard]] inline bool tty_supports_unicode() noexcept
{
    // Keep the library progress bar ASCII. indicators performs locale-dependent
    // Unicode width conversion and can overflow when the process locale is C.
    // Human-readable output remains explicitly UTF-8 elsewhere.
    return false;
}

inline unsigned int& console_output_cp() noexcept
{
    static unsigned int value = 65001U;
    return value;
}

#ifdef _WIN32
[[nodiscard]] inline std::string to_console_cp(const std::string& utf8, const unsigned int cp)
{
    if (utf8.empty() || cp == CP_UTF8)
    {
        return utf8;
    }
    const auto wide_length =
        ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (wide_length <= 0)
    {
        return utf8;
    }
    std::wstring wide(static_cast<std::size_t>(wide_length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(),
                          wide_length);
    const auto narrow_length =
        ::WideCharToMultiByte(cp, 0, wide.data(), wide_length, nullptr, 0, nullptr, nullptr);
    if (narrow_length <= 0)
    {
        return utf8;
    }
    std::string result(static_cast<std::size_t>(narrow_length), '\0');
    ::WideCharToMultiByte(cp, 0, wide.data(), wide_length, result.data(), narrow_length, nullptr,
                          nullptr);
    return result;
}
#endif

/**
 * Convert human-readable UTF-8 text to the console's native code page when the
 * target stream is a console. JSON output and redirected streams stay UTF-8.
 */
[[nodiscard]] inline std::string console_text(const std::string& utf8, const std::ostream& stream)
{
#ifdef _WIN32
    const auto is_console = (&stream == &std::cout && ::_isatty(::_fileno(stdout)) != 0) ||
                            (&stream == &std::cerr && ::_isatty(::_fileno(stderr)) != 0);
    if (!is_console || console_output_cp() == CP_UTF8)
    {
        return utf8;
    }
    return to_console_cp(utf8, console_output_cp());
#else
    (void)utf8;
    (void)stream;
    return utf8;
#endif
}

#ifdef _WIN32
[[nodiscard]] inline bool try_write_console_unicode(const std::string& utf8, std::ostream& stream)
{
    HANDLE handle = INVALID_HANDLE_VALUE;
    if (&stream == &std::cout)
    {
        handle = reinterpret_cast<HANDLE>(::_get_osfhandle(::_fileno(stdout)));
    }
    else if (&stream == &std::cerr)
    {
        handle = reinterpret_cast<HANDLE>(::_get_osfhandle(::_fileno(stderr)));
    }
    if (handle == INVALID_HANDLE_VALUE || handle == nullptr)
    {
        return false;
    }
    DWORD mode{};
    if (::GetConsoleMode(handle, &mode) == 0)
    {
        return false;
    }
    if (utf8.empty())
    {
        return true;
    }
    if (utf8.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    {
        return false;
    }
    const auto wide_length = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                                                   static_cast<int>(utf8.size()), nullptr, 0);
    if (wide_length <= 0)
    {
        return false;
    }
    std::wstring wide(static_cast<std::size_t>(wide_length), static_cast<wchar_t>(0));
    if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                              static_cast<int>(utf8.size()), wide.data(),
                              wide_length) != wide_length)
    {
        return false;
    }
    stream.flush();
    std::size_t offset = 0;
    while (offset < wide.size())
    {
        DWORD written{};
        const auto remaining = wide.size() - offset;
        const auto chunk = static_cast<DWORD>(remaining);
        if (::WriteConsoleW(handle, wide.data() + offset, chunk, &written, nullptr) == 0 ||
            written == 0)
        {
            return true;
        }
        offset += written;
    }
    return true;
}
#endif

inline void write_human_text(std::ostream& output, const std::string& utf8)
{
#ifdef _WIN32
    if (try_write_console_unicode(utf8, output))
    {
        return;
    }
#endif
    output << console_text(utf8, output);
}

[[nodiscard]] inline std::string format_bytes(const double bytes)
{
    constexpr const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = bytes;
    int unit = 0;
    while (value >= 1024.0 && unit < 4)
    {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream output;
    output << std::fixed << std::setprecision(2) << value << ' ' << units[unit];
    return output.str();
}

[[nodiscard]] inline std::string format_eta(const double seconds)
{
    if (seconds < 0.0 || seconds != seconds || seconds > 360000.0)
    {
        return "--:--";
    }
    const auto total = static_cast<long long>(seconds);
    const auto hours = total / 3600;
    const auto minutes = (total % 3600) / 60;
    const auto secs = total % 60;
    std::ostringstream output;
    if (hours > 0)
    {
        output << hours << ':' << std::setw(2) << std::setfill('0') << minutes << ':'
               << std::setw(2) << secs;
    }
    else
    {
        output << std::setw(2) << minutes << ':' << std::setw(2) << secs;
    }
    return output.str();
}

/**
 * Time-throttled synchronous progress renderer.
 *
 * Core progress callbacks may fire very frequently. The writer only touches its
 * output stream when the configured interval has elapsed or the operation is
 * terminal, so a slow sink (file, pipe) cannot stall the Core operation.
 */
class ProgressWriter
{
  public:
    ProgressWriter(const ProgressMode mode, const bool stderr_is_tty, const bool quiet,
                   std::ostream& out,
                   std::chrono::milliseconds interval = std::chrono::milliseconds{100})
        : mode_(mode), stderr_is_tty_(stderr_is_tty), quiet_(quiet), out_(out), interval_(interval)
    {
        if (mode_ == ProgressMode::Tty && stderr_is_tty_ && !quiet_)
        {
            const bool unicode = tty_supports_unicode();
            tty_bar_ = std::make_unique<indicators::ProgressBar>(
                indicators::option::BarWidth{40}, indicators::option::Start{"["},
                indicators::option::Fill{unicode ? "█" : "#"},
                indicators::option::Lead{unicode ? "█" : "#"},
                indicators::option::Remainder{unicode ? "░" : "-"}, indicators::option::End{"]"},
                indicators::option::ShowPercentage{true},
                indicators::option::ShowElapsedTime{false},
                indicators::option::ShowRemainingTime{false}, indicators::option::Stream{out_});
        }
    }

    ProgressWriter(const ProgressWriter&) = delete;
    ProgressWriter& operator=(const ProgressWriter&) = delete;

    void create_start(const std::uint64_t total_bytes) noexcept
    {
        create_mode_ = true;
        create_total_bytes_ = total_bytes;
    }

    void create_event(const torrentutils::core::ProgressInfo& progress)
    {
        if (mode_ == ProgressMode::None || quiet_)
        {
            return;
        }
        create_stage_ = progress.stage;
        create_completed_ = progress.completed;
        create_total_ = progress.total;
        emitted_any_ = true;
        const auto terminal = progress.total > 0U && progress.completed >= progress.total;
        if (!should_emit(terminal))
        {
            return;
        }
        if (terminal)
        {
            rendered_terminal_ = true;
        }
        emit_create();
    }

    void verify_start(const std::uint32_t piece_length, const std::uint64_t total_bytes,
                      const std::size_t total_files) noexcept
    {
        create_mode_ = false;
        piece_length_ = piece_length;
        verify_total_bytes_ = total_bytes;
        verify_total_files_ = total_files;
    }

    void verify_event(const torrentutils::core::VerificationProgress& progress)
    {
        if (mode_ == ProgressMode::None || quiet_)
        {
            return;
        }
        for (const auto& file : progress.files)
        {
            const auto path = file.path.to_string();
            auto& state = verify_files_[path];
            state.expected = file.expected_bytes;
            state.hashed = file.hashed_bytes;
            state.mismatched = file.mismatched_bytes;
        }
        verify_hashed_ = 0U;
        verify_finished_files_ = 0U;
        verify_error_files_ = 0U;
        for (const auto& entry : verify_files_)
        {
            verify_hashed_ += entry.second.hashed;
            if (entry.second.expected > 0U && entry.second.hashed == entry.second.expected)
            {
                ++verify_finished_files_;
            }
            if (entry.second.mismatched > 0U)
            {
                ++verify_error_files_;
            }
        }
        emitted_any_ = true;
        const auto terminal = verify_total_bytes_ > 0U && verify_hashed_ >= verify_total_bytes_;
        if (!should_emit(terminal))
        {
            return;
        }
        if (terminal)
        {
            rendered_terminal_ = true;
        }
        emit_verify(progress);
    }

    void finish() noexcept
    {
        if (finished_)
        {
            return;
        }
        finished_ = true;
        if (quiet_ || mode_ == ProgressMode::None || !emitted_any_)
        {
            return;
        }
        if (mode_ == ProgressMode::Tty && stderr_is_tty_ && tty_bar_)
        {
            if (!rendered_terminal_)
            {
                tty_bar_->set_progress(100U);
            }
            return;
        }
        if (rendered_terminal_)
        {
            return;
        }
        if (mode_ == ProgressMode::Json)
        {
            if (!last_json_line_.empty())
            {
                out_ << last_json_line_ << '\n';
            }
            else if (create_mode_)
            {
                out_ << Json{{"stage", create_stage_},
                             {"completed", create_total_},
                             {"total", create_total_}}
                            .dump()
                     << '\n';
            }
            return;
        }
        out_ << (create_mode_ ? "[HASH] 100%" : "[VERIFY] 100%") << '\n';
    }

  private:
    struct VerifyFileState
    {
        std::uint64_t expected{};
        std::uint64_t hashed{};
        std::uint64_t mismatched{};
    };

    [[nodiscard]] bool should_emit(const bool terminal) noexcept
    {
        if (terminal)
        {
            return true;
        }
        const auto now = std::chrono::steady_clock::now();
        if (last_emit_.time_since_epoch().count() == 0)
        {
            last_emit_ = now;
            return true;
        }
        if (now - last_emit_ >= interval_)
        {
            last_emit_ = now;
            return true;
        }
        return false;
    }

    [[nodiscard]] std::string stage_label() const
    {
        if (create_stage_ == "hashing")
        {
            return "HASH";
        }
        std::string label = create_stage_;
        for (auto& character : label)
        {
            character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
        }
        return label.empty() ? "HASH" : label;
    }

    void update_speed(const std::uint64_t done_bytes) noexcept
    {
        const auto now = std::chrono::steady_clock::now();
        if (last_speed_time_.time_since_epoch().count() != 0)
        {
            const auto elapsed = std::chrono::duration<double>(now - last_speed_time_).count();
            if (elapsed > 0.0 && done_bytes >= last_bytes_)
            {
                speed_bps_ = static_cast<double>(done_bytes - last_bytes_) / elapsed;
            }
        }
        last_speed_time_ = now;
        last_bytes_ = done_bytes;
    }

    void emit_create()
    {
        const auto percent =
            create_total_ > 0U
                ? static_cast<std::size_t>(static_cast<long double>(create_completed_) * 100.0L /
                                           static_cast<long double>(create_total_))
                : 0U;
        if (mode_ == ProgressMode::Json)
        {
            last_json_line_ = Json{
                {"stage", create_stage_},
                {"completed", create_completed_},
                {"total",
                 create_total_}}.dump();
            out_ << last_json_line_ << '\n';
            return;
        }
        if (mode_ == ProgressMode::Plain || !stderr_is_tty_)
        {
            out_ << '[' << stage_label() << "] " << percent << "%\n";
            return;
        }
        const auto done_bytes = create_total_ > 0U ? static_cast<double>(create_completed_) /
                                                         static_cast<double>(create_total_) *
                                                         static_cast<double>(create_total_bytes_)
                                                   : 0.0;
        update_speed(static_cast<std::uint64_t>(done_bytes));
        const auto remaining = done_bytes < static_cast<double>(create_total_bytes_)
                                   ? static_cast<double>(create_total_bytes_) - done_bytes
                                   : 0.0;
        std::ostringstream postfix;
        postfix << format_bytes(done_bytes) << " / "
                << format_bytes(static_cast<double>(create_total_bytes_))
                << " | Speed: " << format_bytes(speed_bps_)
                << "/s | ETA: " << format_eta(speed_bps_ > 0.0 ? remaining / speed_bps_ : -1.0);
        tty_bar_->set_option(indicators::option::PrefixText{"Creating torrent "});
        tty_bar_->set_option(indicators::option::PostfixText{console_text(postfix.str(), out_)});
        tty_bar_->set_progress(percent);
    }

    void emit_verify(const torrentutils::core::VerificationProgress& progress)
    {
        const auto percent =
            verify_total_bytes_ > 0U
                ? static_cast<std::size_t>(static_cast<long double>(verify_hashed_) * 100.0L /
                                           static_cast<long double>(verify_total_bytes_))
                : 0U;
        if (mode_ == ProgressMode::Json)
        {
            Json files = Json::array();
            for (const auto& file : progress.files)
            {
                files.push_back({{"path", file.path.to_string()},
                                 {"expected_bytes", file.expected_bytes},
                                 {"hashed_bytes", file.hashed_bytes},
                                 {"verified_bytes", file.verified_bytes},
                                 {"mismatched_bytes", file.mismatched_bytes}});
            }
            last_json_line_ =
                Json{{"sequence", progress.sequence}, {"files", std::move(files)}}.dump();
            out_ << last_json_line_ << '\n';
            return;
        }
        if (mode_ == ProgressMode::Plain || !stderr_is_tty_)
        {
            out_ << "[VERIFY] " << percent << "%\n";
            return;
        }
        const auto total_pieces =
            piece_length_ > 0U ? (verify_total_bytes_ + piece_length_ - 1U) / piece_length_ : 0U;
        const auto done_pieces = piece_length_ > 0U ? verify_hashed_ / piece_length_ : 0U;
        update_speed(verify_hashed_);
        const auto remaining = verify_hashed_ < verify_total_bytes_
                                   ? static_cast<double>(verify_total_bytes_ - verify_hashed_)
                                   : 0.0;
        std::ostringstream postfix;
        postfix << " | Files: " << verify_finished_files_ << " / " << verify_total_files_
                << " | Pieces: " << done_pieces << " / " << total_pieces
                << " | Speed: " << format_bytes(speed_bps_) << "/s"
                << " | Errors: " << verify_error_files_
                << " | ETA: " << format_eta(speed_bps_ > 0.0 ? remaining / speed_bps_ : -1.0);
        tty_bar_->set_option(indicators::option::PrefixText{"Verifying torrent "});
        tty_bar_->set_option(indicators::option::PostfixText{console_text(postfix.str(), out_)});
        tty_bar_->set_progress(percent);
    }

    ProgressMode mode_;
    bool stderr_is_tty_;
    bool quiet_;
    std::ostream& out_;
    std::chrono::milliseconds interval_;
    std::chrono::steady_clock::time_point last_emit_{};
    bool finished_{};
    bool emitted_any_{};
    bool rendered_terminal_{};
    bool create_mode_{};
    std::string last_json_line_;

    std::string create_stage_;
    std::uint64_t create_total_bytes_{};
    std::uint64_t create_completed_{};
    std::uint64_t create_total_{};

    std::uint32_t piece_length_{};
    std::uint64_t verify_total_bytes_{};
    std::uint64_t verify_hashed_{};
    std::size_t verify_total_files_{};
    std::size_t verify_finished_files_{};
    std::size_t verify_error_files_{};
    std::map<std::string, VerifyFileState> verify_files_;
    std::chrono::steady_clock::time_point last_speed_time_{};
    std::uint64_t last_bytes_{};
    double speed_bps_{};

    std::unique_ptr<indicators::ProgressBar> tty_bar_;
};

} // namespace torrentcraft::cli
