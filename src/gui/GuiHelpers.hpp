#pragma once

// Helpers shared by the main window and its page controllers. Internal to the GUI.

#include <QString>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <torrentutils/core/application.hpp>
#include <vector>

class GuiTaskRunner;
class QLineEdit;
class QStandardItemModel;
class QTableView;
class QWidget;

namespace gui_util {

/** The error message followed by one "field: message" line per issue. */
[[nodiscard]] QString error_text(const torrentutils::core::Error& error);

[[nodiscard]] const char* error_code_name(torrentutils::core::ErrorCode code) noexcept;

[[nodiscard]] std::filesystem::path path_from_text(const QString& value);

[[nodiscard]] QString path_to_text(const std::filesystem::path& value);

/**
 * Updates an automatically generated path without overwriting an explicit user path.
 *
 * The generated value is tracked so a later default-directory change may refresh it, while
 * any different non-empty value is treated as an explicit user choice.
 */
void set_auto_path(QLineEdit* target, std::optional<std::filesystem::path>& generated,
                   const std::filesystem::path& candidate);

[[nodiscard]] QString format_iec(double raw_value, const QString& suffix);

[[nodiscard]] QString format_bytes_iec(std::uint64_t bytes);

[[nodiscard]] QString format_bytes_rate_iec(double bytes_per_second);

[[nodiscard]] std::optional<std::vector<torrentutils::core::TrackerTier>>
parse_tracker_tiers(const QString& text, QString* error);

// Tier/URL tracker tables shared by the Create, Modify, Inspect and Tracker pages.
void populate_tracker_values(QStandardItemModel& model,
                             const std::vector<std::vector<std::string>>& tiers);

[[nodiscard]] std::optional<torrentutils::core::TrackerList>
tracker_list_from_model(const QStandardItemModel& model, QString* error);

void configure_tracker_table(QTableView& table);

[[nodiscard]] int selected_tracker_row(const QTableView& view) noexcept;

void add_tracker_row(QWidget* parent, QTableView& view, QStandardItemModel& model);

void edit_tracker_row(QWidget* parent, QTableView& view, QStandardItemModel& model);

void remove_tracker_row(QWidget* parent, QTableView& view, QStandardItemModel& model);

void move_tracker_row(QTableView& view, QStandardItemModel& model, int direction);

/** Forwards Core progress to the task runner, which delivers it on the GUI thread. */
[[nodiscard]] torrentutils::core::ProgressCallback progress_callback(GuiTaskRunner* runner);

} // namespace gui_util
