#pragma once

#include <chrono>
#include <cstddef>
#include <torrentutils/core/application.hpp>

namespace torrentutils::core::detail {

/** Test-only synchronization aid; this header is not part of the installed SDK. */
class VerificationAdmissionControllerTestProbe
{
  public:
    [[nodiscard]] static bool
    wait_for_waiter_count(const VerificationAdmissionController& controller,
                          std::size_t waiter_count, std::chrono::milliseconds timeout);
};

} // namespace torrentutils::core::detail
