#pragma once

#include <cassert>
#include <torrentutils/core/error.hpp>
#include <utility>
#include <variant>

namespace torrentutils::core {

/**
 * Contains exactly one successful value or one expected Core error.
 *
 * Calling value() on an error or error() on a value violates a programming
 * precondition and is asserted in debug builds.
 */
template <class T> class [[nodiscard]] Result
{
  public:
    /** Creates a successful result from a value. */
    static Result success(T value)
    {
        return Result(ValueTag{}, std::move(value));
    }

    /** Creates a failed result from an Error. */
    static Result failure(Error error)
    {
        return Result(ErrorTag{}, std::move(error));
    }

    /** Returns true when this result contains a value. */
    [[nodiscard]] bool has_value() const noexcept
    {
        return storage_.index() == 0;
    }

    /** Returns true when this result contains a value. */
    explicit operator bool() const noexcept
    {
        return has_value();
    }

    /** Returns the successful value. Requires has_value(). */
    [[nodiscard]] T& value() &
    {
        assert(has_value());
        return std::get<0>(storage_);
    }

    /** Returns the successful value. Requires has_value(). */
    [[nodiscard]] const T& value() const&
    {
        assert(has_value());
        return std::get<0>(storage_);
    }

    /** Moves out the successful value. Requires has_value(). */
    [[nodiscard]] T&& value() &&
    {
        assert(has_value());
        return std::get<0>(std::move(storage_));
    }

    /** Returns the failure. Requires !has_value(). */
    [[nodiscard]] Error& error() &
    {
        assert(!has_value());
        return std::get<1>(storage_);
    }

    /** Returns the failure. Requires !has_value(). */
    [[nodiscard]] const Error& error() const&
    {
        assert(!has_value());
        return std::get<1>(storage_);
    }

    /** Moves out the failure. Requires !has_value(). */
    [[nodiscard]] Error&& error() &&
    {
        assert(!has_value());
        return std::get<1>(std::move(storage_));
    }

  private:
    struct ValueTag
    {
    };
    struct ErrorTag
    {
    };

    Result(ValueTag, T value) : storage_(std::in_place_index<0>, std::move(value)) {}

    Result(ErrorTag, Error error) : storage_(std::in_place_index<1>, std::move(error)) {}

    std::variant<T, Error> storage_;
};

/** Result specialization for successful operations that return no value. */
template <> class [[nodiscard]] Result<void>
{
  public:
    /** Creates a successful result. */
    static Result success()
    {
        return Result(ValueTag{});
    }

    /** Creates a failed result from an Error. */
    static Result failure(Error error)
    {
        return Result(ErrorTag{}, std::move(error));
    }

    /** Returns true when this result represents success. */
    [[nodiscard]] bool has_value() const noexcept
    {
        return storage_.index() == 0;
    }

    /** Returns true when this result represents success. */
    explicit operator bool() const noexcept
    {
        return has_value();
    }

    /** Observes success. Requires has_value(). */
    void value() const
    {
        assert(has_value());
    }

    /** Returns the failure. Requires !has_value(). */
    [[nodiscard]] Error& error() &
    {
        assert(!has_value());
        return std::get<1>(storage_);
    }

    /** Returns the failure. Requires !has_value(). */
    [[nodiscard]] const Error& error() const&
    {
        assert(!has_value());
        return std::get<1>(storage_);
    }

    /** Moves out the failure. Requires !has_value(). */
    [[nodiscard]] Error&& error() &&
    {
        assert(!has_value());
        return std::get<1>(std::move(storage_));
    }

  private:
    struct ValueTag
    {
    };
    struct ErrorTag
    {
    };

    explicit Result(ValueTag) : storage_(std::in_place_index<0>) {}

    Result(ErrorTag, Error error) : storage_(std::in_place_index<1>, std::move(error)) {}

    std::variant<std::monostate, Error> storage_;
};

} // namespace torrentutils::core
