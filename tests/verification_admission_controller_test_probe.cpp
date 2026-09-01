#include "../src/verification_admission_controller_test_probe.hpp"

#include "../src/verification_admission_controller_state.hpp"

#include <mutex>

namespace torrentutils::core::detail {
bool VerificationAdmissionControllerTestProbe::wait_for_waiter_count(
    const VerificationAdmissionController& controller, const std::size_t waiter_count,
    const std::chrono::milliseconds timeout)
{
    const auto state = controller.state_;
    std::unique_lock<std::mutex> lock(state->mutex);
    return state->changed.wait_for(
        lock, timeout, [&state, waiter_count] { return state->queue.size() >= waiter_count; });
}
} // namespace torrentutils::core::detail
