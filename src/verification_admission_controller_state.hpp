#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <torrentutils/core/application.hpp>

namespace torrentutils::core {
namespace detail {
struct AdmissionTicket
{
};
} // namespace detail

struct VerificationAdmissionController::State
{
    explicit State(const std::size_t limit) : capacity(limit), available(limit) {}

    const std::size_t capacity;
    std::size_t available;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::shared_ptr<detail::AdmissionTicket>> queue;
};
} // namespace torrentutils::core
