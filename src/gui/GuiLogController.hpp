#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <torrentutils/core/logging.hpp>
#include <torrentutils/frontend/settings.hpp>
#include <unordered_map>
#include <utility>
#include <vector>

class GuiLogController final : public torrentutils::core::Logger
{
  public:
    using Fields = std::vector<std::pair<std::string, std::string>>;
    using FailureCallback = std::function<void(std::string)>;
    using OperationId = std::string;

    GuiLogController() = default;
    ~GuiLogController() override;

    void configure(const torrentutils::frontend::GuiPreferences& preferences,
                   const std::filesystem::path& config_path);
    void set_failure_callback(FailureCallback callback);
    void close();

    void log(const torrentutils::core::LogRecord& record) override;
    void log_event(torrentutils::core::LogLevel level, std::string_view component,
                   std::string_view operation, std::string_view event, const Fields& fields = {});
    [[nodiscard]] OperationId begin_operation(std::string_view component,
                                              std::string_view operation,
                                              const Fields& fields = {});
    void finish_operation(const OperationId& operation_id, torrentutils::core::LogLevel level,
                          std::string_view component, std::string_view operation,
                          std::string_view event, const Fields& fields = {});

    [[nodiscard]] std::filesystem::path active_log_path() const;
    [[nodiscard]] bool file_logging_active() const;
    [[nodiscard]] std::string diagnostic_context() const;

  private:
    static constexpr std::uintmax_t max_file_size = 16U * 1024U * 1024U;
    static constexpr unsigned int max_rotated_files = 5U;
    static constexpr std::size_t max_diagnostic_records = 256U;

    [[nodiscard]] std::string failure_message_locked(std::string message);
    [[nodiscard]] bool rotate_locked();
    [[nodiscard]] bool open_locked();
    void append_diagnostic_locked(const std::string& line, torrentutils::core::LogLevel level);

    mutable std::mutex mutex_;
    std::ofstream* output_{nullptr};
    std::unique_ptr<std::ofstream> owned_output_;
    std::filesystem::path log_path_;
    torrentutils::core::LogLevel minimum_level_{torrentutils::core::LogLevel::Info};
    bool logging_enabled_{};
    bool file_failure_reported_{};
    std::uintmax_t file_size_{};
    std::deque<std::string> diagnostic_records_;
    FailureCallback failure_callback_;
    std::uint64_t next_operation_id_{1};
    std::unordered_map<OperationId, std::chrono::steady_clock::time_point> operations_;
};
