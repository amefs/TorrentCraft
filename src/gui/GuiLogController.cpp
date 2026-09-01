#include "GuiLogController.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace {

using torrentutils::core::LogLevel;

[[nodiscard]] const char* level_name(const LogLevel level) noexcept
{
    switch (level)
    {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARNING";
    case LogLevel::Error:
        return "ERROR";
    }
    return "INFO";
}

[[nodiscard]] std::string redact_url_credentials(std::string_view value)
{
    std::string sanitized(value);
    std::size_t scheme = sanitized.find("://");
    while (scheme != std::string::npos)
    {
        const auto authority_start = scheme + 3U;
        const auto authority_end = sanitized.find_first_of("/?#", authority_start);
        const auto at = sanitized.find('@', authority_start);
        if (at != std::string::npos && (authority_end == std::string::npos || at < authority_end))
        {
            sanitized.replace(authority_start, at - authority_start, "<redacted>");
            scheme = sanitized.find("://", authority_start + 10U);
        }
        else
        {
            scheme = sanitized.find("://", authority_start);
        }
    }
    return sanitized;
}

[[nodiscard]] std::string escape_value(std::string_view value)
{
    const auto sanitized = redact_url_credentials(value);
    std::string escaped;
    escaped.reserve(sanitized.size() + 2U);
    escaped.push_back('"');
    for (const auto character : sanitized)
    {
        switch (character)
        {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(character);
            break;
        }
    }
    escaped.push_back('"');
    return escaped;
}

[[nodiscard]] std::string timestamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    ::gmtime_s(&utc, &time);
#else
    ::gmtime_r(&time, &utc);
#endif
    std::ostringstream stream;
    stream << (utc.tm_year + 1900) << '-' << std::setfill('0') << std::setw(2) << (utc.tm_mon + 1)
           << '-' << std::setw(2) << utc.tm_mday << 'T' << std::setw(2) << utc.tm_hour << ':'
           << std::setw(2) << utc.tm_min << ':' << std::setw(2) << utc.tm_sec << '.' << std::setw(3)
           << milliseconds.count() << 'Z';
    return stream.str();
}

[[nodiscard]] std::string path_text(const std::filesystem::path& path)
{
    return path.empty() ? std::string{} : path.u8string();
}

[[nodiscard]] LogLevel gui_log_level(const torrentutils::frontend::GuiLogLevel level) noexcept
{
    switch (level)
    {
    case torrentutils::frontend::GuiLogLevel::Debug:
        return LogLevel::Debug;
    case torrentutils::frontend::GuiLogLevel::Info:
        return LogLevel::Info;
    case torrentutils::frontend::GuiLogLevel::Warning:
        return LogLevel::Warning;
    case torrentutils::frontend::GuiLogLevel::Error:
        return LogLevel::Error;
    }
    return LogLevel::Info;
}

[[nodiscard]] bool is_diagnostic_level(const LogLevel level) noexcept
{
    return level == LogLevel::Warning || level == LogLevel::Error;
}

[[nodiscard]] bool is_key_info(std::string_view message) noexcept
{
    return message.find("event=\"start\"") != std::string_view::npos ||
           message.find("event=\"finish\"") != std::string_view::npos ||
           message.find("event=\"cancel\"") != std::string_view::npos ||
           message.find("event=\"config\"") != std::string_view::npos ||
           message.find("event=\"summary\"") != std::string_view::npos;
}

} // namespace

GuiLogController::~GuiLogController()
{
    close();
}

void GuiLogController::configure(const torrentutils::frontend::GuiPreferences& preferences,
                                 const std::filesystem::path& config_path)
{
    FailureCallback callback;
    std::string failure;
    {
        std::scoped_lock lock(mutex_);
        if (owned_output_)
        {
            owned_output_->flush();
            owned_output_->close();
        }
        owned_output_.reset();
        output_ = nullptr;
        file_size_ = 0;
        file_failure_reported_ = false;
        logging_enabled_ = preferences.logging_enabled;
        minimum_level_ = gui_log_level(preferences.log_level);
        log_path_ = preferences.log_path && !preferences.log_path->empty()
                        ? std::filesystem::u8path(*preferences.log_path)
                    : config_path.empty() ? std::filesystem::path{}
                                          : config_path.parent_path() / "torrentcraft.log";
        if (logging_enabled_ && !log_path_.empty() && !open_locked())
        {
            failure = failure_message_locked("could not open log file");
            callback = failure_callback_;
        }
    }
    if (!failure.empty() && callback)
    {
        callback(std::move(failure));
    }
}

void GuiLogController::set_failure_callback(FailureCallback callback)
{
    std::scoped_lock lock(mutex_);
    failure_callback_ = std::move(callback);
}

void GuiLogController::close()
{
    std::scoped_lock lock(mutex_);
    if (owned_output_)
    {
        owned_output_->flush();
        owned_output_->close();
    }
    owned_output_.reset();
    output_ = nullptr;
}

void GuiLogController::log(const torrentutils::core::LogRecord& record)
{
    const auto line =
        timestamp() + " level=" + level_name(record.level) + ' ' + record.message + '\n';
    FailureCallback callback;
    std::string failure;
    {
        std::scoped_lock lock(mutex_);
        append_diagnostic_locked(line, record.level);
        if (!logging_enabled_ || output_ == nullptr ||
            static_cast<int>(record.level) < static_cast<int>(minimum_level_))
        {
            return;
        }
        if (file_size_ + line.size() > max_file_size && !rotate_locked())
        {
            failure = failure_message_locked("could not rotate log file");
            callback = failure_callback_;
        }
        else
        {
            output_->write(line.data(), static_cast<std::streamsize>(line.size()));
            output_->flush();
            if (!*output_)
            {
                failure = failure_message_locked("could not write log file");
                callback = failure_callback_;
            }
            else
            {
                file_size_ += line.size();
            }
        }
    }
    if (!failure.empty() && callback)
    {
        callback(std::move(failure));
    }
}

void GuiLogController::log_event(const torrentutils::core::LogLevel level,
                                 const std::string_view component, const std::string_view operation,
                                 const std::string_view event, const Fields& fields)
{
    std::string message = "component=" + escape_value(component) +
                          " operation=" + escape_value(operation) + " event=" + escape_value(event);
    for (const auto& [key, value] : fields)
    {
        message += ' ' + key + '=' + escape_value(value);
    }
    log({level, std::move(message)});
}

GuiLogController::OperationId GuiLogController::begin_operation(const std::string_view component,
                                                                const std::string_view operation,
                                                                const Fields& fields)
{
    OperationId operation_id;
    {
        std::scoped_lock lock(mutex_);
        std::ostringstream id;
        id << "op-" << std::setfill('0') << std::setw(6) << next_operation_id_++;
        operation_id = id.str();
        operations_.emplace(operation_id, std::chrono::steady_clock::now());
    }

    auto operation_fields = fields;
    operation_fields.emplace_back("operation_id", operation_id);
    log_event(LogLevel::Info, component, operation, "start", operation_fields);
    return operation_id;
}

void GuiLogController::finish_operation(const OperationId& operation_id, const LogLevel level,
                                        const std::string_view component,
                                        const std::string_view operation,
                                        const std::string_view event, const Fields& fields)
{
    std::int64_t duration_ms = -1;
    {
        std::scoped_lock lock(mutex_);
        const auto iterator = operations_.find(operation_id);
        if (iterator != operations_.end())
        {
            duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - iterator->second)
                              .count();
            operations_.erase(iterator);
        }
    }

    auto operation_fields = fields;
    operation_fields.emplace_back("operation_id", operation_id);
    if (duration_ms >= 0)
    {
        operation_fields.emplace_back("duration_ms", std::to_string(duration_ms));
    }
    log_event(level, component, operation, event, operation_fields);
}

std::filesystem::path GuiLogController::active_log_path() const
{
    std::scoped_lock lock(mutex_);
    return log_path_;
}

bool GuiLogController::file_logging_active() const
{
    std::scoped_lock lock(mutex_);
    return logging_enabled_ && output_ != nullptr;
}

std::string GuiLogController::diagnostic_context() const
{
    std::scoped_lock lock(mutex_);
    std::ostringstream result;
    result << "TorrentCraft diagnostic context\n"
           << "log_path=" << path_text(log_path_) << "\n"
           << "file_logging_active=" << (logging_enabled_ && output_ != nullptr ? "true" : "false")
           << "\nrecords=" << diagnostic_records_.size() << "\n\n";
    for (const auto& record : diagnostic_records_)
    {
        result << record;
    }
    return result.str();
}

std::string GuiLogController::failure_message_locked(std::string message)
{
    if (file_failure_reported_)
    {
        return {};
    }
    file_failure_reported_ = true;
    logging_enabled_ = false;
    if (owned_output_)
    {
        owned_output_->close();
    }
    owned_output_.reset();
    output_ = nullptr;
    return message;
}

bool GuiLogController::rotate_locked()
{
    if (!output_ || log_path_.empty())
    {
        return false;
    }
    output_->flush();
    output_->close();
    for (unsigned int index = max_rotated_files; index > 1U; --index)
    {
        const auto source = log_path_.string() + '.' + std::to_string(index - 1U);
        const auto target = log_path_.string() + '.' + std::to_string(index);
        std::error_code error;
        std::filesystem::remove(target, error);
        error.clear();
        if (std::filesystem::exists(source, error))
        {
            std::filesystem::rename(source, target, error);
            if (error)
            {
                return false;
            }
        }
    }
    std::error_code error;
    std::filesystem::remove(log_path_.string() + ".1", error);
    error.clear();
    std::filesystem::rename(log_path_, log_path_.string() + ".1", error);
    if (error)
    {
        return false;
    }
    file_size_ = 0;
    return open_locked();
}

bool GuiLogController::open_locked()
{
    if (log_path_.empty())
    {
        return false;
    }
    std::error_code error;
    if (!log_path_.parent_path().empty())
    {
        std::filesystem::create_directories(log_path_.parent_path(), error);
        if (error)
        {
            return false;
        }
    }
    owned_output_ = std::make_unique<std::ofstream>(log_path_, std::ios::binary | std::ios::app);
    if (!*owned_output_)
    {
        owned_output_.reset();
        return false;
    }
    output_ = owned_output_.get();
    const auto size = std::filesystem::file_size(log_path_, error);
    file_size_ = error ? 0 : size;
    return true;
}

void GuiLogController::append_diagnostic_locked(const std::string& line, const LogLevel level)
{
    if (!is_diagnostic_level(level) && !(level == LogLevel::Info && is_key_info(line)))
    {
        return;
    }
    diagnostic_records_.push_back(line);
    while (diagnostic_records_.size() > max_diagnostic_records)
    {
        diagnostic_records_.pop_front();
    }
}
