#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <torrentutils/core/foundation.hpp>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

class RecordingLogger final : public torrentutils::core::Logger
{
  public:
    void log(const torrentutils::core::LogRecord& record) override
    {
        records.push_back(record);
    }

    std::vector<torrentutils::core::LogRecord> records;
};

} // namespace

TEST_CASE("given_value_result_when_inspected_then_exposes_only_success",
          "[unit][foundation][result]")
{
    auto result = torrentutils::core::Result<std::string>::success("ready");

    REQUIRE(result.has_value());
    REQUIRE(static_cast<bool>(result));
    REQUIRE(result.value() == "ready");
}

TEST_CASE("given_error_result_when_inspected_then_preserves_structured_error",
          "[unit][foundation][result]")
{
    torrentutils::core::Error expected{
        torrentutils::core::ErrorCode::ValidationFailed,
        "request validation failed",
        {{"tracker", "must be an HTTP or UDP URL"}, {"name", "must not be empty"}}};

    auto result = torrentutils::core::Result<int>::failure(expected);

    REQUIRE_FALSE(result.has_value());
    REQUIRE_FALSE(static_cast<bool>(result));
    REQUIRE(result.error().code == torrentutils::core::ErrorCode::ValidationFailed);
    REQUIRE(result.error().message == "request validation failed");
    REQUIRE(result.error().issues.size() == 2);
    REQUIRE(result.error().issues.front().field == "tracker");
    REQUIRE(result.error().issues.front().message == "must be an HTTP or UDP URL");
}

TEST_CASE("given_void_results_when_inspected_then_success_and_failure_are_distinct",
          "[unit][foundation][result]")
{
    const auto success = torrentutils::core::Result<void>::success();
    const auto failure = torrentutils::core::Result<void>::failure(
        {torrentutils::core::ErrorCode::Cancelled, "cancelled", {}});

    REQUIRE(success.has_value());
    success.value();
    REQUIRE_FALSE(failure.has_value());
    REQUIRE(failure.error().code == torrentutils::core::ErrorCode::Cancelled);
}

TEST_CASE("given_copyable_result_when_copied_then_value_is_independent",
          "[unit][foundation][result]")
{
    auto original = torrentutils::core::Result<std::string>::success("original");
    auto copy = original;

    copy.value() = "copy";

    REQUIRE(original.value() == "original");
    REQUIRE(copy.value() == "copy");
}

TEST_CASE("given_move_only_value_when_moved_from_result_then_value_is_preserved",
          "[unit][foundation][result]")
{
    auto result =
        torrentutils::core::Result<std::unique_ptr<int>>::success(std::make_unique<int>(42));

    auto value = std::move(result).value();

    REQUIRE(value != nullptr);
    REQUIRE(*value == 42);
}

TEST_CASE("given_default_token_when_queried_then_it_is_not_cancelled",
          "[unit][foundation][cancellation]")
{
    const torrentutils::core::CancellationToken token;

    REQUIRE_FALSE(token.is_cancelled());
}

TEST_CASE("given_multiple_tokens_when_source_is_cancelled_then_all_observe_request",
          "[unit][foundation][cancellation]")
{
    torrentutils::core::CancellationSource source;
    const auto first = source.token();
    const auto second = source.token();

    source.cancel();
    source.cancel();

    REQUIRE(first.is_cancelled());
    REQUIRE(second.is_cancelled());
}

TEST_CASE("given_uncancelled_source_destruction_when_token_is_queried_then_it_remains_safe",
          "[unit][foundation][cancellation]")
{
    torrentutils::core::CancellationToken token;
    {
        torrentutils::core::CancellationSource source;
        token = source.token();
    }

    REQUIRE_FALSE(token.is_cancelled());
}

TEST_CASE("given_cancelled_source_destruction_when_token_is_queried_then_state_is_retained",
          "[unit][foundation][cancellation]")
{
    torrentutils::core::CancellationToken token;
    {
        torrentutils::core::CancellationSource source;
        token = source.token();
        source.cancel();
    }

    REQUIRE(token.is_cancelled());
}

TEST_CASE("given_token_on_one_thread_when_another_thread_cancels_then_request_is_visible",
          "[unit][foundation][cancellation]")
{
    torrentutils::core::CancellationSource source;
    const auto token = source.token();
    std::atomic<bool> thread_observed_uncancelled{false};

    std::thread worker([&source, &thread_observed_uncancelled]() {
        thread_observed_uncancelled.store(!source.token().is_cancelled(),
                                          std::memory_order_release);
        source.cancel();
    });
    worker.join();

    REQUIRE(thread_observed_uncancelled.load(std::memory_order_acquire));
    REQUIRE(token.is_cancelled());
}

TEST_CASE("given_default_task_context_when_inspected_then_observers_are_empty",
          "[unit][foundation][task]")
{
    const torrentutils::core::TaskContext context;

    REQUIRE_FALSE(context.cancellation.is_cancelled());
    REQUIRE_FALSE(static_cast<bool>(context.on_progress));
    REQUIRE(context.logger == nullptr);
    REQUIRE(context.operation_id.empty());
}

TEST_CASE("given_progress_values_when_total_is_zero_or_known_then_representation_is_preserved",
          "[unit][foundation][task]")
{
    const torrentutils::core::ProgressInfo unknown{"hashing", 7, 0};
    const torrentutils::core::ProgressInfo known{"writing", 3, 10};

    REQUIRE(unknown.stage == "hashing");
    REQUIRE(unknown.completed == 7);
    REQUIRE(unknown.total == 0);
    REQUIRE(known.stage == "writing");
    REQUIRE(known.completed == 3);
    REQUIRE(known.total == 10);
}

TEST_CASE("given_logger_in_task_context_when_called_then_minimal_record_is_received",
          "[unit][foundation][logging]")
{
    RecordingLogger logger;
    torrentutils::core::TaskContext context;
    context.logger = &logger;

    context.logger->log({torrentutils::core::LogLevel::Warning, "retrying read"});

    REQUIRE(logger.records.size() == 1);
    REQUIRE(logger.records.front().level == torrentutils::core::LogLevel::Warning);
    REQUIRE(logger.records.front().message == "retrying read");
}

TEST_CASE("given_cancellation_subscription_when_cancelled_then_callback_runs_once",
          "[unit][foundation][cancellation]")
{
    torrentutils::core::CancellationSource source;
    std::atomic<unsigned int> calls{0};
    const auto registration =
        source.token().subscribe([&calls] { calls.fetch_add(1, std::memory_order_relaxed); });

    source.cancel();
    source.cancel();

    REQUIRE(calls.load(std::memory_order_relaxed) == 1);
}

TEST_CASE("given_destroyed_cancellation_subscription_when_cancelled_then_callback_is_not_run",
          "[unit][foundation][cancellation]")
{
    torrentutils::core::CancellationSource source;
    std::atomic<unsigned int> calls{0};
    {
        const auto registration =
            source.token().subscribe([&calls] { calls.fetch_add(1, std::memory_order_relaxed); });
        static_cast<void>(registration);
    }

    source.cancel();

    REQUIRE(calls.load(std::memory_order_relaxed) == 0);
}

TEST_CASE("given_callback_in_flight_when_registration_is_destroyed_then_destruction_waits",
          "[unit][foundation][cancellation]")
{
    torrentutils::core::CancellationSource source;
    std::mutex mutex;
    std::condition_variable changed;
    bool callback_started{};
    bool release_callback{};
    std::atomic<bool> reset_started{};
    std::atomic<bool> reset_finished{};
    auto registration = source.token().subscribe([&] {
        std::unique_lock<std::mutex> lock(mutex);
        callback_started = true;
        changed.notify_all();
        changed.wait(lock, [&release_callback] { return release_callback; });
    });

    std::thread canceller([&source] { source.cancel(); });
    {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock, [&callback_started] { return callback_started; });
    }
    std::thread resetter(
        [registration = std::move(registration), &reset_started, &reset_finished]() mutable {
            reset_started.store(true, std::memory_order_release);
            registration = {};
            reset_finished.store(true, std::memory_order_release);
        });
    while (!reset_started.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(reset_finished.load(std::memory_order_acquire) == false);

    {
        std::lock_guard<std::mutex> lock(mutex);
        release_callback = true;
    }
    changed.notify_all();
    canceller.join();
    resetter.join();

    CHECK(reset_finished.load(std::memory_order_acquire));
}

TEST_CASE("given_cancelled_token_when_subscribed_then_callback_runs_immediately",
          "[unit][foundation][cancellation]")
{
    torrentutils::core::CancellationSource source;
    source.cancel();
    bool called = false;

    const auto registration = source.token().subscribe([&called] { called = true; });

    REQUIRE(called);
    static_cast<void>(registration);
}
static_assert(std::is_copy_constructible_v<torrentutils::core::CancellationToken>);
static_assert(std::is_copy_assignable_v<torrentutils::core::CancellationToken>);
static_assert(!std::is_copy_constructible_v<torrentutils::core::CancellationSource>);
static_assert(!std::is_copy_assignable_v<torrentutils::core::CancellationSource>);
static_assert(std::is_move_constructible_v<torrentutils::core::CancellationSource>);
static_assert(std::is_move_assignable_v<torrentutils::core::CancellationSource>);
static_assert(std::has_virtual_destructor_v<torrentutils::core::Logger>);
