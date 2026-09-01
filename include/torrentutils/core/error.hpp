#pragma once

#include <string>
#include <vector>

namespace torrentutils::core {

/** Stable categories for failures returned by the Core SDK. */
enum class ErrorCode
{
    FileNotFound,
    AccessDenied,
    InvalidBencode,
    InvalidTorrent,
    UnsupportedFeature,
    ValidationFailed,
    IoFailure,
    Cancelled,
    Conflict,
    ResourceLimitExceeded,
    Internal
};

/** A validation or parsing issue associated with one public field identifier. */
struct FieldIssue
{
    /** UTF-8 field identifier without domain-specific path syntax requirements. */
    std::string field;

    /** UTF-8 diagnostic intended for the caller. */
    std::string message;
};

/** A stable, caller-facing description of an expected Core failure. */
// Clang 18 misdiagnoses aggregate moves through Result's variant as uninitialized.
// NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Assign)
struct Error
{
    ErrorCode code{ErrorCode::Internal};
    std::string message;
    std::vector<FieldIssue> issues;
};

} // namespace torrentutils::core
