#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <torrentutils/core/application.hpp>
#include <torrentutils/frontend/config.hpp>
#include <torrentutils/frontend/tracker_batch.hpp>
#include <utility>
#include <vector>

namespace Ui {
class TorrentCraftMainWindow;
}
class GuiLogController;
class GuiTaskRunner;
class QLineEdit;
class QStandardItemModel;
class QWidget;
class TrackerRewritePanel;

/** What the Tracker page needs from the window that hosts it. */
class TrackerPageHost
{
  public:
    using LogFields = std::vector<std::pair<std::string, std::string>>;

    TrackerPageHost() = default;
    TrackerPageHost(const TrackerPageHost&) = delete;
    TrackerPageHost& operator=(const TrackerPageHost&) = delete;
    TrackerPageHost(TrackerPageHost&&) = delete;
    TrackerPageHost& operator=(TrackerPageHost&&) = delete;
    virtual ~TrackerPageHost() = default;

    [[nodiscard]] virtual const torrentutils::core::TorrentService& torrent_service() const = 0;
    [[nodiscard]] virtual GuiTaskRunner& task_runner() = 0;
    [[nodiscard]] virtual bool task_running() const = 0;
    [[nodiscard]] virtual GuiLogController* logger() const = 0;
    [[nodiscard]] virtual const std::optional<torrentutils::frontend::ConfigFile>&
    configuration() const = 0;
    [[nodiscard]] virtual std::string begin_gui_operation(std::string_view component,
                                                          std::string_view operation,
                                                          const LogFields& fields = {}) = 0;
    virtual void finish_gui_operation(const std::string& operation_id,
                                      torrentutils::core::LogLevel level,
                                      std::string_view component, std::string_view operation,
                                      std::string_view event, const LogFields& fields = {}) = 0;
    virtual void set_busy(bool busy) = 0;
    virtual void set_status(const QString& text) = 0;
    virtual void show_error(const torrentutils::core::Error& error) = 0;
    virtual void show_task_error(const QString& message) = 0;
    virtual void cancel_operation() = 0;
    virtual void choose_directory(QLineEdit* target, const QString& caption) = 0;
    [[nodiscard]] virtual std::filesystem::path default_save_directory() const = 0;
    virtual void remember_save_directory(const std::filesystem::path& directory) = 0;
};

/**
 * The Tracker page: loads a torrent or a folder of torrents, then replaces tracker tiers or
 * rewrites tracker URLs, previewing and converting the files as background tasks.
 */
class TrackerPage final : public QObject
{
    Q_OBJECT
  public:
    TrackerPage(Ui::TorrentCraftMainWindow& ui, TrackerPageHost& host, QWidget* window);
    ~TrackerPage() override;
    TrackerPage(const TrackerPage&) = delete;
    TrackerPage& operator=(const TrackerPage&) = delete;
    TrackerPage(TrackerPage&&) = delete;
    TrackerPage& operator=(TrackerPage&&) = delete;

    /** Enables or disables the page's controls while a task runs. */
    void set_busy(bool busy);
    /** Re-applies translated texts after a language change. */
    void retranslate();
    /** Follows a configuration change: default save folder and tracker defaults. */
    void configuration_changed();
    /** Loads a file or folder dropped on the source field. */
    void load_dropped_source(const QString& path);

  private:
    using LogFields = TrackerPageHost::LogFields;

    /** What the torrent list shows for one file; text is derived so it can retranslate. */
    enum class TrackerFileState
    {
        Ready,
        Unavailable,
        WillChange,
        Updated,
        NoChange,
        Failed,
        WillFail,
    };

    void connect_signals();
    void update_tracker_output_path();
    void populate_tracker_defaults();
    void load_tracker_torrent();
    void save_tracker_torrent();
    void preview_tracker_rewrite_all();
    void reset_tracker_page();
    void update_tracker_actions();
    [[nodiscard]] bool tracker_rewrite_mode() const;
    void set_tracker_files_model(QStandardItemModel* model);
    void set_tracker_file_status(const std::filesystem::path& path, TrackerFileState state,
                                 qulonglong changed_urls = 0, const QString& detail = {});
    [[nodiscard]] QString tracker_file_state_text(TrackerFileState state,
                                                  qulonglong changed_urls) const;
    void retranslate_tracker_files();
    struct TrackerBatchRun;
    /**
     * Lists the torrents under root and applies edit to each, collecting per-file results.
     * Runs on the worker thread; used by both Convert and Preview all.
     */
    void run_tracker_batch(TrackerBatchRun& run, const std::filesystem::path& root, bool recursive,
                           const torrentutils::frontend::TrackerBatchEdit& edit,
                           const torrentutils::frontend::TrackerBatchOptions& options,
                           const char* stage, const std::string& operation_id,
                           const torrentutils::core::CancellationToken& token) const;

    Ui::TorrentCraftMainWindow* ui_;
    TrackerPageHost& host_;
    QWidget* window_;
    std::unique_ptr<QStandardItemModel> tracker_model_;
    std::optional<torrentutils::core::LoadedTorrent> tracker_loaded_;
    std::optional<std::filesystem::path> tracker_source_directory_;
    TrackerRewritePanel* tracker_rewrite_panel_{};
    QStandardItemModel* tracker_files_model_{};
    QHash<QString, int> tracker_file_rows_;
    std::optional<std::filesystem::path> tracker_auto_output_path_;
};
