#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <torrentutils/core/cancellation.hpp>
#include <torrentutils/core/logging.hpp>

namespace torrentutils::core {

/** Progress within one operation stage. A total of zero means unknown work. */
struct ProgressInfo
{
    /** Optional byte counters for stages that process a measurable byte stream. */
    std::string stage;
    std::uint64_t completed{};
    std::uint64_t total{};
    std::uint64_t completed_bytes{};
    std::uint64_t total_bytes{};
};

/** Synchronous progress observer used only for the lifetime of a Core call. */
using ProgressCallback = std::function<void(const ProgressInfo&)>;

/** Non-owning execution controls and observers for a synchronous Core call. */
struct TaskContext
{
    CancellationToken cancellation;
    ProgressCallback on_progress;
    Logger* logger{};
    /** Optional GUI/session operation identifier used to correlate Core log records. */
    std::string operation_id;
};

} // namespace torrentutils::core
