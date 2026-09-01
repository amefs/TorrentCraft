#include "libtorrent_adapter.hpp"

#include <cstdint>
#include <string_view>
#include <torrentutils/core/torrent_engine.hpp>
#include <utility>

namespace torrentutils::core {
namespace {

[[nodiscard]] bool is_valid_utf8(const std::string& value) noexcept
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
    std::size_t index = 0;
    while (index < value.size())
    {
        const auto first = bytes[index];
        if (first <= 0x7FU)
        {
            ++index;
            continue;
        }

        std::size_t count = 0;
        std::uint32_t code_point = 0;
        std::uint32_t minimum = 0;
        if ((first & 0xE0U) == 0xC0U)
        {
            count = 2;
            code_point = first & 0x1FU;
            minimum = 0x80;
        }
        else if ((first & 0xF0U) == 0xE0U)
        {
            count = 3;
            code_point = first & 0x0FU;
            minimum = 0x800;
        }
        else if ((first & 0xF8U) == 0xF0U)
        {
            count = 4;
            code_point = first & 0x07U;
            minimum = 0x10000;
        }
        else
        {
            return false;
        }

        if (index + count > value.size())
        {
            return false;
        }
        for (std::size_t offset = 1; offset < count; ++offset)
        {
            const auto next = bytes[index + offset];
            if ((next & 0xC0U) != 0x80U)
            {
                return false;
            }
            code_point = (code_point << 6U) | (next & 0x3FU);
        }
        if (code_point < minimum || code_point > 0x10FFFFU ||
            (code_point >= 0xD800U && code_point <= 0xDFFFU))
        {
            return false;
        }
        index += count;
    }
    return true;
}

void append_invalid_utf8_issue(const std::optional<std::string>& text, const char* field,
                               std::vector<FieldIssue>& issues)
{
    if (text && !is_valid_utf8(*text))
    {
        issues.push_back({field, "must contain valid UTF-8"});
    }
}

void log_operation(const TaskContext& context, const LogLevel level,
                   const std::string_view operation, const std::string_view event)
{
    if (context.logger == nullptr)
    {
        return;
    }
    auto message = "component=\"core\" operation=\"" + std::string(operation) + "\" event=\"" +
                   std::string(event) + "\"";
    if (!context.operation_id.empty())
    {
        message += " operation_id=\"" + context.operation_id + "\"";
    }
    context.logger->log({level, std::move(message)});
}

template <class T>
Result<T> log_result(Result<T> result, const TaskContext& context, const std::string_view operation)
{
    log_operation(context, result ? LogLevel::Info : LogLevel::Error, operation,
                  result ? "finish" : "failure");
    return result;
}
} // namespace

Result<InspectionReport> TorrentEngine::inspect(const TorrentDocument& document,
                                                const TaskContext& context) const
{
    log_operation(context, LogLevel::Info, "inspect", "start");
    if (context.cancellation.is_cancelled())
    {
        log_operation(context, LogLevel::Warning, "inspect", "cancel");
        return Result<InspectionReport>::failure(
            {ErrorCode::Cancelled, "torrent inspection was cancelled", {}});
    }

    return log_result(detail::LibtorrentAdapter::inspect(document), context, "inspect");
}

Result<CreateResult> TorrentEngine::create(const CreateRequest& request,
                                           const TaskContext& context) const
{
    log_operation(context, LogLevel::Info, "create", "start");
    std::vector<FieldIssue> issues;
    if (request.content_root.empty())
    {
        issues.push_back({"create.content_root", "must not be empty"});
    }
    if (request.target_path.empty())
    {
        issues.push_back({"create.target_path", "must not be empty"});
    }
    append_invalid_utf8_issue(request.creation_metadata.comment, "create.creation_metadata.comment",
                              issues);
    append_invalid_utf8_issue(request.creation_metadata.created_by,
                              "create.creation_metadata.created_by", issues);
    append_invalid_utf8_issue(request.create_info.source, "create.create_info.source", issues);
    if (request.creation_metadata.creation_time_unix_seconds &&
        *request.creation_metadata.creation_time_unix_seconds < 0)
    {
        issues.push_back({"create.creation_metadata.creation_time", "must not be negative"});
    }
    if (!issues.empty())
    {
        log_operation(context, LogLevel::Error, "create", "failure");
        return Result<CreateResult>::failure(
            {ErrorCode::ValidationFailed, "create request validation failed", std::move(issues)});
    }
    if (context.cancellation.is_cancelled())
    {
        log_operation(context, LogLevel::Warning, "create", "cancel");
        return Result<CreateResult>::failure(
            {ErrorCode::Cancelled, "torrent creation was cancelled", {}});
    }

    return log_result(detail::LibtorrentAdapter::create(request, context), context, "create");
}

Result<CreatePlan> TorrentEngine::plan_create(const CreatePlanRequest& request,
                                              const TaskContext& context) const
{
    log_operation(context, LogLevel::Info, "create_plan", "start");
    if (request.content_root.empty())
    {
        log_operation(context, LogLevel::Error, "create_plan", "failure");
        return Result<CreatePlan>::failure({ErrorCode::ValidationFailed,
                                            "create plan request validation failed",
                                            {{"create.content_root", "must not be empty"}}});
    }
    if (context.cancellation.is_cancelled())
    {
        log_operation(context, LogLevel::Warning, "create_plan", "cancel");
        return Result<CreatePlan>::failure(
            {ErrorCode::Cancelled, "create plan calculation was cancelled", {}});
    }

    return log_result(detail::LibtorrentAdapter::plan_create(request, context), context,
                      "create_plan");
}

Result<VerificationReport> TorrentEngine::verify(const VerifyRequest& request,
                                                 const TaskContext& context) const
{
    log_operation(context, LogLevel::Info, "verify", "start");
    if (request.content_root.empty())
    {
        log_operation(context, LogLevel::Error, "verify", "failure");
        return Result<VerificationReport>::failure(
            {ErrorCode::ValidationFailed,
             "verify request validation failed",
             {{"verify.content_root", "must not be empty"}}});
    }
    if (context.cancellation.is_cancelled())
    {
        log_operation(context, LogLevel::Warning, "verify", "cancel");
        return Result<VerificationReport>::failure(
            {ErrorCode::Cancelled, "torrent verification was cancelled", {}});
    }

    return log_result(detail::LibtorrentAdapter::verify(request, context), context, "verify");
}

} // namespace torrentutils::core
