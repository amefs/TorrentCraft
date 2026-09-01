#pragma once

#include <string>

namespace torrentutils::core {

/** Severity of a diagnostic emitted by Core. */
enum class LogLevel
{
    Debug,
    Info,
    Warning,
    Error
};

/** Minimal public log record containing no backend-specific context. */
struct LogRecord
{
    LogLevel level{LogLevel::Info};
    std::string message;
};

/** Caller-provided synchronous diagnostic sink. */
class Logger
{
  public:
    virtual ~Logger() = default;

    /**
     * Receives one log record during a Core call.
     *
     * Exceptions thrown by an implementation propagate unchanged to the caller.
     */
    virtual void log(const LogRecord& record) = 0;
};

} // namespace torrentutils::core
