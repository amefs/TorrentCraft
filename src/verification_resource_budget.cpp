#include <limits>
#include <torrentutils/core/torrent_engine.hpp>

namespace torrentutils::core {

VerificationResourceBudget::VerificationResourceBudget(
    VerificationResourceBudgetInput input) noexcept
    : input_(input)
{
}

Result<VerificationResourceBudget>
VerificationResourceBudget::create(VerificationResourceBudgetInput input)
{
    const auto invalid = [](const char* field, const char* message) {
        return Result<VerificationResourceBudget>::failure(
            {ErrorCode::ValidationFailed,
             "verification resource budget validation failed",
             {{field, message}}});
    };
    if (input.hashing_workers == 0U)
        return invalid("verify.resource_budget.hashing_workers", "must be greater than zero");
    if (input.checking_memory_bytes == 0U)
        return invalid("verify.resource_budget.checking_memory_bytes", "must be greater than zero");
    if (input.checking_memory_bytes > (std::numeric_limits<std::uint64_t>::max)() - 16383U)
        return invalid("verify.resource_budget.checking_memory_bytes",
                       "must support 16 KiB rounding without overflow");
    if (input.max_logical_files == 0U)
        return invalid("verify.resource_budget.max_logical_files", "must be greater than zero");
    if (input.max_pieces == 0U)
        return invalid("verify.resource_budget.max_pieces", "must be greater than zero");
    return Result<VerificationResourceBudget>::success(VerificationResourceBudget(input));
}

std::uint32_t VerificationResourceBudget::hashing_workers() const noexcept
{
    return input_.hashing_workers;
}

std::uint64_t VerificationResourceBudget::checking_memory_bytes() const noexcept
{
    return input_.checking_memory_bytes;
}

std::uint64_t VerificationResourceBudget::max_logical_files() const noexcept
{
    return input_.max_logical_files;
}

std::uint64_t VerificationResourceBudget::max_pieces() const noexcept
{
    return input_.max_pieces;
}

} // namespace torrentutils::core
