#pragma once

#include <functional>
#include <memory>

namespace torrentutils::core {

namespace detail {
struct CancellationState;
}

class CancellationSource;

/**
 * Move-only cancellation callback subscription.
 *
 * Destruction or move assignment unregisters the callback and waits for an already-started
 * callback to complete. A callback must not destroy or move-assign its own registration.
 */
class CancellationRegistration
{
  public:
    CancellationRegistration() noexcept = default;
    CancellationRegistration(const CancellationRegistration&) = delete;
    CancellationRegistration& operator=(const CancellationRegistration&) = delete;
    CancellationRegistration(CancellationRegistration&& other) noexcept;
    CancellationRegistration& operator=(CancellationRegistration&& other) noexcept;
    ~CancellationRegistration();

  private:
    explicit CancellationRegistration(std::shared_ptr<detail::CancellationState> state,
                                      std::shared_ptr<void> entry) noexcept;
    void reset() noexcept;

    std::shared_ptr<detail::CancellationState> state_;
    std::shared_ptr<void> entry_;

    friend class CancellationToken;
};
/** A copyable, read-only view of a cross-thread cancellation request. */

class CancellationToken
{
  public:
    /** Creates a token that is never cancelled. */
    CancellationToken() noexcept = default;

    /** Returns whether the associated source has requested cancellation. */
    [[nodiscard]] bool is_cancelled() const noexcept;

    /**
     * Registers a synchronous one-shot callback; callbacks must not throw.
     *
     * If cancellation already occurred, the callback runs before this function returns.
     */
    [[nodiscard]] CancellationRegistration subscribe(std::function<void()> callback) const;

  private:
    std::shared_ptr<const detail::CancellationState> state_;

    explicit CancellationToken(std::shared_ptr<const detail::CancellationState> state) noexcept;
    friend class CancellationSource;
};

/** The move-only authority that can request cancellation for its tokens. */
class CancellationSource
{
  public:
    /** Creates a new independent cancellation state. */
    CancellationSource();

    CancellationSource(const CancellationSource&) = delete;
    CancellationSource& operator=(const CancellationSource&) = delete;
    CancellationSource(CancellationSource&&) noexcept = default;
    CancellationSource& operator=(CancellationSource&&) noexcept = default;
    ~CancellationSource() = default;

    /** Requests cancellation. This operation is idempotent and thread-safe. */
    void cancel() noexcept;

    /** Returns a copyable token observing this source. */
    [[nodiscard]] CancellationToken token() const noexcept;

  private:
    std::shared_ptr<detail::CancellationState> state_;
};

} // namespace torrentutils::core
