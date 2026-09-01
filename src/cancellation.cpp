#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <torrentutils/core/cancellation.hpp>
#include <utility>
#include <vector>

namespace torrentutils::core {
namespace detail {
struct CancellationState
{
    std::atomic<bool> cancelled{false};
    std::mutex mutex;

    struct CallbackEntry
    {
        std::function<void()> callback;
        bool active{true};
        bool executing{false};
        std::condition_variable completed;
    };

    std::vector<std::shared_ptr<CallbackEntry>> callbacks;
};

void invoke_callback(const std::function<void()>& callback) noexcept
{
    try
    {
        callback();
    }
    catch (...)
    {
        std::terminate();
    }
}
} // namespace detail

CancellationToken::CancellationToken(
    std::shared_ptr<const detail::CancellationState> state) noexcept
    : state_(std::move(state))
{
}

bool CancellationToken::is_cancelled() const noexcept
{
    return state_ != nullptr && state_->cancelled.load(std::memory_order_acquire);
}

CancellationRegistration::CancellationRegistration(std::shared_ptr<detail::CancellationState> state,
                                                   std::shared_ptr<void> entry) noexcept
    : state_(std::move(state)), entry_(std::move(entry))
{
}

CancellationRegistration::CancellationRegistration(CancellationRegistration&& other) noexcept
    : state_(std::move(other.state_)), entry_(std::move(other.entry_))
{
}

CancellationRegistration&
CancellationRegistration::operator=(CancellationRegistration&& other) noexcept
{
    if (this != &other)
    {
        reset();
        state_ = std::move(other.state_);
        entry_ = std::move(other.entry_);
    }
    return *this;
}

CancellationRegistration::~CancellationRegistration()
{
    reset();
}

void CancellationRegistration::reset() noexcept
{
    if (state_ == nullptr || entry_ == nullptr)
    {
        return;
    }

    const auto entry = std::static_pointer_cast<detail::CancellationState::CallbackEntry>(entry_);
    std::unique_lock<std::mutex> lock(state_->mutex);
    entry->active = false;
    entry->completed.wait(lock, [&entry] { return !entry->executing; });
    state_->callbacks.erase(std::remove(state_->callbacks.begin(), state_->callbacks.end(), entry),
                            state_->callbacks.end());
    entry_.reset();
    state_.reset();
}

CancellationRegistration CancellationToken::subscribe(std::function<void()> callback) const
{
    if (!callback || state_ == nullptr)
    {
        return {};
    }

    auto state = std::const_pointer_cast<detail::CancellationState>(state_);
    auto entry = std::make_shared<detail::CancellationState::CallbackEntry>();
    entry->callback = std::move(callback);
    bool invoke_now = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->cancelled.load(std::memory_order_acquire))
        {
            entry->active = false;
            entry->executing = true;
            invoke_now = true;
        }
        else
        {
            state->callbacks.push_back(entry);
        }
    }

    if (invoke_now)
    {
        detail::invoke_callback(entry->callback);
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            entry->executing = false;
        }
        entry->completed.notify_all();
        return {};
    }
    return CancellationRegistration(state, entry);
}

CancellationSource::CancellationSource() : state_(std::make_shared<detail::CancellationState>()) {}

void CancellationSource::cancel() noexcept
{
    if (state_ == nullptr)
    {
        return;
    }

    std::vector<std::shared_ptr<detail::CancellationState::CallbackEntry>> callbacks;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->cancelled.exchange(true, std::memory_order_acq_rel))
        {
            return;
        }
        for (const auto& entry : state_->callbacks)
        {
            if (entry->active)
            {
                entry->executing = true;
                callbacks.push_back(entry);
            }
        }
    }

    for (const auto& entry : callbacks)
    {
        detail::invoke_callback(entry->callback);
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            entry->executing = false;
        }
        entry->completed.notify_all();
    }
}

CancellationToken CancellationSource::token() const noexcept
{
    return CancellationToken(state_);
}
} // namespace torrentutils::core
