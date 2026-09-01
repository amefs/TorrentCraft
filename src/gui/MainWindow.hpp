#pragma once

#include <QMainWindow>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <torrentutils/core/application.hpp>
#include <torrentutils/frontend/config.hpp>
#include <utility>
#include <vector>

namespace Ui {
class TorrentCraftMainWindow;
}
class QAction;
class QActionGroup;
class QCloseEvent;
class QEvent;
class QLabel;
class QTranslator;
class FileTreeModel;
class GuiTaskRunner;
class GuiLogController;
class QStandardItemModel;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
  public:
    explicit MainWindow(QWidget* parent = nullptr);
    explicit MainWindow(GuiLogController* logger, QWidget* parent = nullptr);
    ~MainWindow() override;

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    void show_about();
    void choose_file(class QLineEdit* target, const QString& caption, const QString& filter = {});
    void choose_output_file(class QLineEdit* target, const QString& caption);
    void choose_directory(class QLineEdit* target, const QString& caption);
    void update_create_output_path();
    void update_modify_output_path(const std::filesystem::path& source);
    void update_tracker_output_path();
    void populate_tracker_defaults();
    [[nodiscard]] std::filesystem::path default_save_directory() const;
    void remember_save_directory(const std::filesystem::path& directory);
    void calculate_create_plan();
    void create_torrent();
    void cancel_operation();
    void load_inspect_torrent();
    void validate_inspect_torrent();
    void load_modify_torrent();
    void preview_modify();
    void save_modify(bool preview_only = false);
    void invalidate_verify_torrent();
    void load_verify_torrent();
    void start_verify();
    void load_tracker_torrent();
    void save_tracker_torrent();
    void apply_language(QAction* action);
    void apply_memory_working_set_limit();
    void reload_configuration();
    void initialize_configuration();
    void show_configuration();
    void populate_advanced_configuration();
    void apply_creation_settings(const torrentutils::frontend::CreationSettingsPatch& settings);
    void apply_advanced_configuration();
    void reset_advanced_configuration();
    void open_log_file();
    void copy_diagnostic_context();
    void configure_logger();
    void import_preset();
    void save_preset();
    void remove_preset();
    void clear_current_form();
    void mark_preset_modified();
    void set_active_preset(const QString& name);
    using LogFields = std::vector<std::pair<std::string, std::string>>;
    [[nodiscard]] std::string begin_gui_operation(std::string_view component,
                                                  std::string_view operation,
                                                  const LogFields& fields = {});
    void finish_gui_operation(const std::string& operation_id, torrentutils::core::LogLevel level,
                              std::string_view component, std::string_view operation,
                              std::string_view event, const LogFields& fields = {});

    void update_window_title();
    void load_inline_preset(const QString& name);
    void refresh_preset_menu();
    void set_busy(bool busy);
    void set_status(const QString& text);
    void reset_progress_state();
    /**
     * Updates the status-bar progress text and estimates Piece and hashed-byte rates from
     * consecutive progress callbacks. The estimates are informational and reset when a task
     * stage restarts.
     */
    void update_progress_status(const QString& stage, qulonglong completed, qulonglong total,
                                qulonglong completed_bytes, qulonglong total_bytes);
    void show_error(const torrentutils::core::Error& error);
    void show_task_error(const QString& message);
    void populate_modify_fields(const torrentutils::core::LoadedTorrent& loaded);
    void populate_inspect_fields(const torrentutils::core::LoadedTorrent& loaded);
    [[nodiscard]] torrentutils::core::Result<torrentutils::frontend::CreationSettingsPatch>
    creation_patch_from_create_form() const;
    [[nodiscard]] std::optional<torrentutils::core::CreateOptions> create_options_from_ui();
    [[nodiscard]] bool eventFilter(QObject* watched, QEvent* event) override;

    Ui::TorrentCraftMainWindow* ui_;
    std::unique_ptr<GuiLogController> owned_logger_;
    GuiLogController* logger_{};
    torrentutils::core::FileTorrentRepository repository_;
    torrentutils::core::SystemClock clock_;
    torrentutils::core::TorrentService service_;
    std::unique_ptr<GuiTaskRunner> task_runner_;
    std::unique_ptr<FileTreeModel> inspect_file_model_;
    std::unique_ptr<QStandardItemModel> inspect_tracker_model_;
    std::unique_ptr<FileTreeModel> verify_file_model_;
    std::unique_ptr<QStandardItemModel> create_tracker_model_;
    std::unique_ptr<QStandardItemModel> modify_tracker_model_;
    std::unique_ptr<QStandardItemModel> tracker_model_;
    std::optional<torrentutils::core::LoadedTorrent> inspect_loaded_;
    std::optional<torrentutils::core::LoadedTorrent> modify_loaded_;
    std::optional<torrentutils::core::LoadedTorrent> verify_loaded_;
    std::optional<torrentutils::core::LoadedTorrent> tracker_loaded_;
    std::optional<std::filesystem::path> tracker_source_directory_;
    std::optional<std::filesystem::path> create_auto_output_path_;
    std::optional<std::filesystem::path> modify_auto_output_path_;
    std::optional<std::filesystem::path> tracker_auto_output_path_;
    std::optional<torrentutils::frontend::ConfigFile> config_;
    QActionGroup* language_group_{};
    QTranslator* translator_{};
    bool verification_running_{};
    bool applying_creation_settings_{};
    bool preset_modified_{};
    QString active_preset_name_;
    struct ActiveOperation
    {
        std::string id;
        std::string component;
        std::string operation;
    };
    std::vector<ActiveOperation> active_operations_;
    std::optional<std::chrono::steady_clock::time_point> last_progress_time_;
    QString last_progress_stage_;
    qulonglong last_progress_completed_{};
    double progress_speed_units_per_second_{};
    std::optional<qulonglong> last_progress_completed_bytes_;
    double progress_speed_bytes_per_second_{};
    qulonglong verify_completed_pieces_{};
    qulonglong verify_total_pieces_{};
};
