#include "verification_admission_controller_state.hpp"

#include <algorithm>
#include <mutex>
#include <torrentutils/core/application.hpp>

namespace torrentutils::core {
class VerificationAdmissionController::Permit
{
  public:
    explicit Permit(std::shared_ptr<State> state) : state_(std::move(state)) {}

    ~Permit()
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        ++state_->available;
        state_->changed.notify_all();
    }

    Permit(const Permit&) = delete;
    Permit& operator=(const Permit&) = delete;

  private:
    std::shared_ptr<State> state_;
};

namespace {
[[nodiscard]] Error cancelled_verification()
{
    return {ErrorCode::Cancelled, "verification cancelled before admission", {}};
}
} // namespace

VerificationAdmissionController::VerificationAdmissionController(
    std::shared_ptr<State> state) noexcept
    : state_(std::move(state))
{
}

Result<VerificationAdmissionController>
VerificationAdmissionController::create(const std::size_t capacity)
{
    if (capacity == 0)
    {
        return Result<VerificationAdmissionController>::failure(
            {ErrorCode::ValidationFailed,
             "verification admission capacity must be positive",
             {{"capacity", "must be greater than zero"}}});
    }
    return Result<VerificationAdmissionController>::success(
        VerificationAdmissionController(std::make_shared<State>(capacity)));
}

std::size_t VerificationAdmissionController::capacity() const noexcept
{
    return state_->capacity;
}

AdmissionControlledVerifier::AdmissionControlledVerifier(
    const TorrentService& service, VerificationAdmissionController& controller) noexcept
    : service_(service), controller_(controller)
{
}

Result<VerificationReport> AdmissionControlledVerifier::verify(const VerifyRequest& request,
                                                               const TaskContext& context) const
{
    if (context.cancellation.is_cancelled())
    {
        return Result<VerificationReport>::failure(cancelled_verification());
    }

    const auto state = controller_.state_;
    const auto ticket = std::make_shared<detail::AdmissionTicket>();
    const auto registration = context.cancellation.subscribe(
        [weak_state = std::weak_ptr<VerificationAdmissionController::State>(state)] {
            if (const auto locked = weak_state.lock())
            {
                locked->changed.notify_all();
            }
        });

    std::unique_lock<std::mutex> lock(state->mutex);
    state->queue.push_back(ticket);
    state->changed.notify_all();
    while (true)
    {
        if (context.cancellation.is_cancelled())
        {
            state->queue.erase(std::find(state->queue.begin(), state->queue.end(), ticket),
                               state->queue.end());
            state->changed.notify_all();
            return Result<VerificationReport>::failure(cancelled_verification());
        }
        if (state->available != 0 && state->queue.front() == ticket)
        {
            state->queue.pop_front();
            --state->available;
            break;
        }
        state->changed.wait(lock);
    }
    lock.unlock();

    const VerificationAdmissionController::Permit permit(state);
    return service_.verify(request, context);
}
} // namespace torrentutils::core
