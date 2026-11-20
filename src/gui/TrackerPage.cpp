#include "TrackerPage.hpp"

#include "GuiHelpers.hpp"
#include "GuiLogController.hpp"
#include "GuiTaskRunner.hpp"
#include "NotificationDialog.hpp"
#include "TrackerRewritePanel.hpp"
#include "ui_MainWindow.h"

#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScopeGuard>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableView>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <torrentutils/frontend/tracker_batch.hpp>
#include <utility>
#include <vector>

namespace {

// Tracker torrent list: the status column keeps its state so the text can be retranslated.
constexpr int kTrackerFileStateRole = Qt::UserRole + 1;
constexpr int kTrackerFileCountRole = Qt::UserRole + 2;
constexpr int kTrackerFileDetailRole = Qt::UserRole + 3;
// The batch summary dialog lists at most this many failed files.
constexpr std::size_t kMaximumListedFailures = 50;

using namespace torrentutils;
using namespace gui_util;
using core::Error;
using core::ErrorCode;

} // namespace

struct TrackerPage::TrackerBatchRun
{
    struct FileResult
    {
        std::filesystem::path path;
        frontend::TrackerBatchFileStatus status{};
        std::size_t changed_urls{};
        QString reason;
    };
    std::size_t file_count{};
    std::vector<FileResult> results;
    /** Why the run stopped early: the folder could not be listed, or the task was cancelled. */
    std::optional<Error> error;

    [[nodiscard]] std::size_t count(const frontend::TrackerBatchFileStatus status) const
    {
        return static_cast<std::size_t>(
            std::count_if(results.begin(), results.end(),
                          [status](const FileResult& result) { return result.status == status; }));
    }
};

TrackerPage::TrackerPage(Ui::TorrentCraftMainWindow& ui, TrackerPageHost& host, QWidget* window)
    : ui_(&ui), host_(host), window_(window), tracker_model_(std::make_unique<QStandardItemModel>())
{
    tracker_model_->setHorizontalHeaderLabels({tr("Tier"), tr("Tracker")});
    ui_->tblTrackerTiers->setModel(tracker_model_.get());
    configure_tracker_table(*ui_->tblTrackerTiers);
    connect_signals();
}

TrackerPage::~TrackerPage() = default;

void TrackerPage::connect_signals()
{
    connect(ui_->btnTrackerReset, &QPushButton::clicked, this, &TrackerPage::reset_tracker_page);
    connect(ui_->btnTrackerSelectFolder, &QPushButton::clicked, this, [this] {
        host_.choose_directory(ui_->editTrackerSourcePath, tr("Select torrent folder"));
        update_tracker_output_path();
        load_tracker_torrent();
    });
    connect(ui_->editTrackerSourcePath, &QLineEdit::editingFinished, this,
            &TrackerPage::load_tracker_torrent);
    connect(ui_->btnTrackerSaveTo, &QPushButton::clicked, this, [this] {
        host_.choose_directory(ui_->editTrackerOutputPath, tr("Select output folder"));
    });
    connect(ui_->btnTrackerReloadFolder, &QPushButton::clicked, this,
            &TrackerPage::load_tracker_torrent);
    connect(ui_->btnTrackerBatchConvert, &QPushButton::clicked, this,
            &TrackerPage::save_tracker_torrent);
    connect(ui_->btnTrackerCancel, &QPushButton::clicked, this,
            [this] { host_.cancel_operation(); });
    connect(ui_->tblTrackerTorrents, &QTableView::clicked, this, [this](const QModelIndex& index) {
        if (!index.isValid())
        {
            return;
        }
        const auto path = index.sibling(index.row(), 0).data(Qt::UserRole).toString();
        if (path.isEmpty())
        {
            return;
        }
        ui_->editTrackerSourcePath->setText(QDir::toNativeSeparators(path));
        load_tracker_torrent();
    });
    connect(ui_->btnTrackerAdd, &QPushButton::clicked, this,
            [this] { add_tracker_row(window_, *ui_->tblTrackerTiers, *tracker_model_); });
    connect(ui_->btnTrackerRemove, &QPushButton::clicked, this,
            [this] { remove_tracker_row(window_, *ui_->tblTrackerTiers, *tracker_model_); });
    connect(ui_->btnTrackerEdit, &QPushButton::clicked, this,
            [this] { edit_tracker_row(window_, *ui_->tblTrackerTiers, *tracker_model_); });
    connect(ui_->btnTrackerMoveUp, &QPushButton::clicked, this,
            [this] { move_tracker_row(*ui_->tblTrackerTiers, *tracker_model_, -1); });
    connect(ui_->btnTrackerMoveDown, &QPushButton::clicked, this,
            [this] { move_tracker_row(*ui_->tblTrackerTiers, *tracker_model_, 1); });
    tracker_rewrite_panel_ = new TrackerRewritePanel(ui_->pageTrackerRewrite);
    ui_->vboxTrackerRewritePage->addWidget(tracker_rewrite_panel_);
    // The tracker editor gets more room than the torrent list; users can drag the handle.
    ui_->splitTrackerFields->setStretchFactor(0, 2);
    ui_->splitTrackerFields->setStretchFactor(1, 1);
    for (auto* radio : {ui_->radTrackerReplace, ui_->radTrackerRewrite})
    {
        connect(radio, &QRadioButton::toggled, this, [this](const bool checked) {
            if (!checked)
            {
                return;
            }
            ui_->stackTrackerMode->setCurrentIndex(tracker_rewrite_mode() ? 1 : 0);
            update_tracker_actions();
        });
    }
    connect(tracker_rewrite_panel_, &TrackerRewritePanel::rulesChanged, this,
            [this](bool) { update_tracker_actions(); });
    connect(tracker_rewrite_panel_->preview_all_button(), &QPushButton::clicked, this,
            &TrackerPage::preview_tracker_rewrite_all);
    connect(ui_->chkTrackerRecursive, &QCheckBox::toggled, this, [this](bool) {
        if (tracker_source_directory_)
        {
            ui_->editTrackerSourcePath->setText(path_to_text(*tracker_source_directory_));
            load_tracker_torrent();
        }
    });
    update_tracker_actions();
}

void TrackerPage::set_busy(const bool busy)
{
    // Changing the source, recursion or mode mid-task would leave the list out of step with
    // what the batch processes.
    for (auto* control : std::initializer_list<QWidget*>{
             ui_->btnTrackerReloadFolder, ui_->btnTrackerBatchConvert, ui_->btnTrackerAdd,
             ui_->btnTrackerRemove, ui_->btnTrackerEdit, ui_->btnTrackerMoveUp,
             ui_->btnTrackerMoveDown, ui_->btnTrackerReset, ui_->editTrackerSourcePath,
             ui_->btnTrackerSelectFolder, ui_->chkTrackerRecursive, ui_->editTrackerOutputPath,
             ui_->btnTrackerSaveTo, ui_->radTrackerReplace, ui_->radTrackerRewrite})
    {
        control->setEnabled(!busy);
    }
    ui_->btnTrackerCancel->setEnabled(busy);
    update_tracker_actions();
}

void TrackerPage::retranslate()
{
    retranslate_tracker_files();
    tracker_model_->setHorizontalHeaderLabels({tr("Tier"), tr("Tracker")});
    configure_tracker_table(*ui_->tblTrackerTiers);
}

void TrackerPage::configuration_changed()
{
    update_tracker_output_path();
    if (tracker_source_directory_ && !tracker_loaded_)
    {
        populate_tracker_defaults();
    }
}

void TrackerPage::load_dropped_source(const QString& path)
{
    ui_->editTrackerSourcePath->setText(path);
    update_tracker_output_path();
    load_tracker_torrent();
}

void TrackerPage::update_tracker_output_path()
{
    set_auto_path(ui_->editTrackerOutputPath, tracker_auto_output_path_,
                  host_.default_save_directory());
}

void TrackerPage::populate_tracker_defaults()
{
    const auto& configuration = host_.configuration();
    if (!configuration)
    {
        tracker_model_->clear();
        tracker_model_->setHorizontalHeaderLabels({tr("Tier"), tr("Tracker")});
        return;
    }
    const auto& parsed = configuration->parsed();
    auto settings = parsed.defaults;
    const auto default_preset = parsed.default_preset;
    if (default_preset)
    {
        const auto iterator = parsed.presets.find(*default_preset);
        if (iterator != parsed.presets.end())
        {
            settings = frontend::overlay_settings(settings, iterator->second);
        }
    }
    populate_tracker_values(
        *tracker_model_, settings.tracker_tiers.value_or(std::vector<std::vector<std::string>>{}));
}

void TrackerPage::load_tracker_torrent()
{
    if (host_.task_running() || ui_->editTrackerSourcePath->text().trimmed().isEmpty())
    {
        return;
    }
    const auto source = path_from_text(ui_->editTrackerSourcePath->text());
    const auto operation_id = host_.begin_gui_operation(
        "gui", "tracker_load",
        {{"path", ui_->editTrackerSourcePath->text().trimmed().toUtf8().toStdString()}});
    update_tracker_output_path();
    std::error_code error;
    if (std::filesystem::is_directory(source, error))
    {
        if (error)
        {
            host_.finish_gui_operation(
                operation_id, core::LogLevel::Error, "gui", "tracker_load", "failure",
                {{"reason", "directory_enumeration"}, {"message", error.message()}});
            host_.show_error({ErrorCode::IoFailure,
                              "failed to enumerate tracker source directory: " + error.message(),
                              {}});
            return;
        }

        struct DirectoryOutcome
        {
            std::vector<frontend::TorrentBatchFile> files;
            std::optional<Error> error;
        };
        auto outcome = std::make_shared<DirectoryOutcome>();
        tracker_loaded_.reset();
        tracker_source_directory_.reset();
        tracker_rewrite_panel_->set_preview_source({}, std::nullopt);
        set_tracker_files_model(nullptr);
        host_.set_busy(true);
        host_.set_status(tr("Loading torrent"));
        const auto recursive = ui_->chkTrackerRecursive->isChecked();
        host_.task_runner().start(
            [source, outcome, recursive](const core::CancellationToken& token) {
                auto listed = frontend::enumerate_torrent_files(source, recursive, token);
                if (!listed)
                {
                    outcome->error = std::move(listed).error();
                    return;
                }
                outcome->files = std::move(listed).value();
            },
            [this, source, outcome, operation_id] {
                if (outcome->error)
                {
                    const auto& error = *outcome->error;
                    host_.finish_gui_operation(
                        operation_id,
                        error.code == ErrorCode::Cancelled ? core::LogLevel::Warning
                                                           : core::LogLevel::Error,
                        "gui", "tracker_load",
                        error.code == ErrorCode::Cancelled ? "cancel" : "failure",
                        {{"code", error_code_name(error.code)}, {"message", error.message}});
                    host_.show_error(error);
                    return;
                }
                tracker_source_directory_ = source.lexically_normal();
                auto* files = new QStandardItemModel(this);
                for (const auto& file : outcome->files)
                {
                    auto* name_item = new QStandardItem(
                        QString::fromUtf8(file.relative.generic_u8string().c_str()));
                    name_item->setData(path_to_text(file.path), Qt::UserRole);
                    auto* size_item = file.size ? new QStandardItem(format_bytes_iec(*file.size))
                                                : new QStandardItem(QString());
                    auto* status_item = new QStandardItem;
                    status_item->setData(static_cast<int>(file.size
                                                              ? TrackerFileState::Ready
                                                              : TrackerFileState::Unavailable),
                                         kTrackerFileStateRole);
                    files->appendRow({name_item, size_item, status_item});
                }
                set_tracker_files_model(files);
                tracker_model_.reset(new QStandardItemModel(this));
                tracker_model_->setHorizontalHeaderLabels({tr("Tier"), tr("Tracker")});
                ui_->tblTrackerTiers->setModel(tracker_model_.get());
                populate_tracker_defaults();
                configure_tracker_table(*ui_->tblTrackerTiers);
                host_.finish_gui_operation(operation_id, core::LogLevel::Info, "gui",
                                           "tracker_load", "finish",
                                           {{"file_count", std::to_string(outcome->files.size())},
                                            {"source_kind", "directory"}});
                host_.set_status(tr("Torrent folder loaded; select one file for tracker editing."));
            });
        return;
    }
    if (tracker_source_directory_)
    {
        // A file picked from the folder list keeps the folder batch, including subfolders.
        const auto relative = source.lexically_normal().lexically_relative(
            tracker_source_directory_->lexically_normal());
        if (relative.empty() || *relative.begin() == "..")
        {
            tracker_source_directory_.reset();
        }
    }
    auto result = std::make_shared<std::optional<core::Result<core::LoadedTorrent>>>();
    host_.set_busy(true);
    host_.set_status(tr("Loading tracker list"));
    host_.task_runner().start(
        [this, source, result, operation_id](const core::CancellationToken& token) {
            core::TaskContext context{token, {}, host_.logger(), operation_id};
            *result = host_.torrent_service().load(source, {}, context);
        },
        [this, result, operation_id] {
            if (!*result)
            {
                host_.finish_gui_operation(operation_id, core::LogLevel::Error, "gui",
                                           "tracker_load", "failure",
                                           {{"reason", "missing_result"}});
                host_.show_task_error(tr("The tracker task did not return a result."));
            }
            else if (!result->value())
            {
                const auto& error = result->value().error();
                host_.finish_gui_operation(
                    operation_id,
                    error.code == ErrorCode::Cancelled ? core::LogLevel::Warning
                                                       : core::LogLevel::Error,
                    "gui", "tracker_load",
                    error.code == ErrorCode::Cancelled ? "cancel" : "failure",
                    {{"code", error_code_name(error.code)}, {"message", error.message}});
                host_.show_error(error);
            }
            else
            {
                tracker_loaded_ = result->value().value();
                tracker_model_->clear();
                tracker_model_->setHorizontalHeaderLabels({tr("Tier"), tr("Tracker")});
                const auto& tiers = tracker_loaded_->document().trackers().tiers();
                for (std::size_t tier_index = 0; tier_index < tiers.size(); ++tier_index)
                {
                    for (const auto& tracker : tiers[tier_index].trackers())
                    {
                        tracker_model_->appendRow(
                            {new QStandardItem(QString::number(tier_index + 1U)),
                             new QStandardItem(QString::fromStdString(tracker.value()))});
                    }
                }
                configure_tracker_table(*ui_->tblTrackerTiers);
                tracker_rewrite_panel_->set_preview_source(
                    QString::fromUtf8(tracker_loaded_->source_path().filename().u8string().c_str()),
                    tracker_loaded_->document().trackers());
                host_.finish_gui_operation(
                    operation_id, core::LogLevel::Info, "gui", "tracker_load", "finish",
                    {{"tier_count", std::to_string(tiers.size())}, {"source_kind", "file"}});
                host_.set_status(tr("Tracker list loaded"));
            }
        });
}

void TrackerPage::save_tracker_torrent()
{
    if (host_.task_running())
    {
        return;
    }

    const auto rewrite = tracker_rewrite_mode();
    std::optional<frontend::TrackerBatchEdit> batch_edit;
    std::size_t rule_count = 0;
    std::optional<core::TrackerList> trackers;
    if (rewrite)
    {
        // Apply edits still waiting for the preview debounce, so Convert uses the rules on screen.
        tracker_rewrite_panel_->refresh();
        const auto& rules = tracker_rewrite_panel_->rules();
        if (!rules)
        {
            NotificationDialog::show_error(window_, tr("Invalid rewrite rules"),
                                           tr("Enter at least one valid rewrite rule."));
            return;
        }
        batch_edit = frontend::TrackerBatchEdit{*rules};
        rule_count = rules->size();
    }
    else
    {
        QString tracker_error;
        trackers = tracker_list_from_model(*tracker_model_, &tracker_error);
        if (!trackers)
        {
            NotificationDialog::show_error(window_, tr("Invalid tracker list"), tracker_error);
            return;
        }
        batch_edit = frontend::TrackerBatchEdit{*trackers};
    }
    const auto destination_dir = path_from_text(ui_->editTrackerOutputPath->text());
    const auto dry_run = ui_->chkTrackerDryRun->isChecked();
    // A rewrite always runs through the batch flow; a single loaded file is a batch of one.
    std::optional<std::filesystem::path> batch_root = tracker_source_directory_;
    if (!batch_root && rewrite && tracker_loaded_)
    {
        batch_root = tracker_loaded_->source_path();
    }
    if (batch_root)
    {
        const auto source_root = *batch_root;
        const auto source_directory =
            tracker_source_directory_ ? *tracker_source_directory_ : source_root.parent_path();
        auto outcome = std::make_shared<TrackerBatchRun>();
        frontend::TrackerBatchOptions options;
        options.output_dir = destination_dir;
        options.dry_run = dry_run;
        options.overwrite = ui_->chkTrackerOverwrite->isChecked();
        options.backup = ui_->chkTrackerBackup->isChecked();
        const auto recursive = ui_->chkTrackerRecursive->isChecked();
        LogFields fields{{"source", source_root.u8string()},
                         {"destination", destination_dir.u8string()},
                         {"mode", rewrite ? "rewrite" : "replace"},
                         {"recursive", recursive ? "true" : "false"},
                         {"dry_run", dry_run ? "true" : "false"},
                         {"overwrite", options.overwrite ? "true" : "false"},
                         {"backup", options.backup ? "true" : "false"}};
        if (rewrite)
        {
            fields.emplace_back("rule_count", std::to_string(rule_count));
        }
        const auto operation_id = host_.begin_gui_operation("gui", "tracker_batch", fields);
        host_.set_busy(true);
        host_.set_status(tr("Converting tracker files"));
        host_.task_runner().start(
            [this, source_root, edit = std::move(*batch_edit), options, recursive, outcome,
             operation_id](const core::CancellationToken& token) {
                run_tracker_batch(*outcome, source_root, recursive, edit, options, "Tracker batch",
                                  operation_id, token);
            },
            [this, outcome, dry_run, destination_dir, source_directory, operation_id] {
                for (const auto& result : outcome->results)
                {
                    switch (result.status)
                    {
                    case frontend::TrackerBatchFileStatus::Changed:
                        set_tracker_file_status(result.path,
                                                dry_run ? TrackerFileState::WillChange
                                                        : TrackerFileState::Updated,
                                                static_cast<qulonglong>(result.changed_urls));
                        break;
                    case frontend::TrackerBatchFileStatus::Unchanged:
                        set_tracker_file_status(result.path, TrackerFileState::NoChange);
                        break;
                    case frontend::TrackerBatchFileStatus::Failed:
                        set_tracker_file_status(result.path, TrackerFileState::Failed, 0,
                                                result.reason);
                        break;
                    }
                }
                if (outcome->error)
                {
                    const auto& error = *outcome->error;
                    const auto cancelled = error.code == ErrorCode::Cancelled;
                    host_.finish_gui_operation(
                        operation_id, cancelled ? core::LogLevel::Warning : core::LogLevel::Error,
                        "gui", "tracker_batch", cancelled ? "cancel" : "failure",
                        {{"code", error_code_name(error.code)}, {"message", error.message}});
                    host_.show_error(error);
                    return;
                }
                if (outcome->file_count == 0U)
                {
                    host_.finish_gui_operation(operation_id, core::LogLevel::Warning, "gui",
                                               "tracker_batch", "skip",
                                               {{"reason", "no_torrent_files"}});
                    NotificationDialog::show_warning(
                        window_, tr("No torrent files"),
                        tr("The selected folder contains no torrent files."));
                    return;
                }
                const auto changed = outcome->count(frontend::TrackerBatchFileStatus::Changed);
                const auto processed =
                    changed + outcome->count(frontend::TrackerBatchFileStatus::Unchanged);
                std::size_t failure_count = 0;
                std::size_t logged_failures = 0;
                std::size_t suppressed_failures = 0;
                QString failure_lines;
                for (const auto& result : outcome->results)
                {
                    if (result.status != frontend::TrackerBatchFileStatus::Failed)
                    {
                        continue;
                    }
                    ++failure_count;
                    if (failure_count <= kMaximumListedFailures)
                    {
                        failure_lines += QStringLiteral("\n- %1: %2")
                                             .arg(QString::fromUtf8(reinterpret_cast<const char*>(
                                                      result.path.u8string().c_str())),
                                                  result.reason);
                    }
                    if (logged_failures >= 32U)
                    {
                        ++suppressed_failures;
                        continue;
                    }
                    host_.logger()->log_event(core::LogLevel::Warning, "gui", "tracker_batch_file",
                                              "failure",
                                              {{"operation_id", operation_id},
                                               {"path", result.path.u8string()},
                                               {"reason", result.reason.toUtf8().toStdString()}});
                    ++logged_failures;
                }
                host_.finish_gui_operation(
                    operation_id,
                    failure_count == 0U ? core::LogLevel::Info : core::LogLevel::Warning, "gui",
                    "tracker_batch", "finish",
                    {{"processed_count", std::to_string(processed)},
                     {"changed_count", std::to_string(changed)},
                     {"file_count", std::to_string(outcome->file_count)},
                     {"failure_count", std::to_string(failure_count)},
                     {"suppressed_count", std::to_string(suppressed_failures)},
                     {"dry_run", dry_run ? "true" : "false"}});
                QString summary = tr("Processed %1 torrent file(s); %2 changed.%3")
                                      .arg(static_cast<qulonglong>(processed))
                                      .arg(static_cast<qulonglong>(changed))
                                      .arg(dry_run ? tr(" No files were written.") : QString());
                if (failure_count != 0U)
                {
                    summary +=
                        tr("\n\nFailed %1 file(s):").arg(static_cast<qulonglong>(failure_count));
                    summary += failure_lines;
                    if (failure_count > kMaximumListedFailures)
                    {
                        summary += tr("\n- …and %1 more")
                                       .arg(static_cast<qulonglong>(failure_count -
                                                                    kMaximumListedFailures));
                    }
                    NotificationDialog::show_warning(
                        window_, tr("Tracker batch completed with errors"), summary);
                }
                else
                {
                    NotificationDialog::show_info(window_, tr("Tracker batch complete"), summary);
                }
                if (!dry_run)
                {
                    host_.remember_save_directory(destination_dir.empty() ? source_directory
                                                                          : destination_dir);
                }
                host_.set_status(tr("Ready"));
            });
        return;
    }

    if (!tracker_loaded_)
    {
        NotificationDialog::show_warning(window_, tr("No torrent loaded"),
                                         tr("Load a torrent file before converting trackers."));
        return;
    }
    const auto target = destination_dir.empty()
                            ? tracker_loaded_->source_path()
                            : destination_dir / tracker_loaded_->source_path().filename();
    core::SaveRequest save_request;
    if (!destination_dir.empty())
    {
        save_request.mode = core::SaveTargetMode::NewPath;
        save_request.destination = target;
        save_request.allow_overwrite = ui_->chkTrackerOverwrite->isChecked();
    }
    save_request.backup = ui_->chkTrackerBackup->isChecked();
    const auto edited = std::make_shared<std::optional<core::Result<core::EditResult>>>();
    auto outcome = std::make_shared<std::optional<core::Result<core::SaveResult>>>();
    const auto operation_id = host_.begin_gui_operation(
        "gui", "tracker_save",
        {{"source", tracker_loaded_->source_path().u8string()},
         {"target", target.u8string()},
         {"dry_run", dry_run ? "true" : "false"},
         {"overwrite", ui_->chkTrackerOverwrite->isChecked() ? "true" : "false"},
         {"backup", ui_->chkTrackerBackup->isChecked() ? "true" : "false"}});
    host_.set_busy(true);
    host_.task_runner().start(
        [this, trackers = std::move(trackers).value(), edited, outcome, save_request, dry_run,
         operation_id](const core::CancellationToken& token) {
            *edited =
                host_.torrent_service().edit(*tracker_loaded_, {core::ReplaceTrackers{trackers}});
            if (token.is_cancelled())
            {
                *edited = core::Result<core::EditResult>::failure(
                    {ErrorCode::Cancelled, "tracker modification was cancelled", {}});
                return;
            }
            if (!edited->value() || dry_run ||
                edited->value().value().disposition != core::EditDisposition::Applied)
            {
                return;
            }
            core::TaskContext context{token, progress_callback(&host_.task_runner()),
                                      host_.logger(), operation_id};
            *outcome =
                host_.torrent_service().save(edited->value().value().loaded, save_request, context);
        },
        [this, edited, outcome, dry_run, operation_id, target] {
            if (!*edited)
            {
                host_.finish_gui_operation(operation_id, core::LogLevel::Error, "gui",
                                           "tracker_save", "failure",
                                           {{"reason", "missing_edit_result"}});
                host_.show_task_error(tr("No tracker edit result was produced."));
            }
            else if (!edited->value())
            {
                const auto& error = edited->value().error();
                host_.finish_gui_operation(
                    operation_id,
                    error.code == ErrorCode::Cancelled ? core::LogLevel::Warning
                                                       : core::LogLevel::Error,
                    "gui", "tracker_save",
                    error.code == ErrorCode::Cancelled ? "cancel" : "failure",
                    {{"code", error_code_name(error.code)}, {"message", error.message}});
                host_.show_error(error);
            }
            else if (edited->value().value().disposition == core::EditDisposition::NeedRebuild)
            {
                host_.finish_gui_operation(operation_id, core::LogLevel::Warning, "gui",
                                           "tracker_save", "rebuild_required",
                                           {{"reason", "identity_change"}});
                NotificationDialog::show_warning(
                    window_, tr("Rebuild required"),
                    tr("Tracker changes require a rebuild. No file was written."));
            }
            else if (dry_run)
            {
                host_.finish_gui_operation(operation_id, core::LogLevel::Info, "gui",
                                           "tracker_save", "finish", {{"preview", "true"}});
                NotificationDialog::show_info(
                    window_, tr("Preview"),
                    tr("Tracker changes are valid; dry run wrote nothing."));
            }
            else if (!*outcome)
            {
                host_.finish_gui_operation(operation_id, core::LogLevel::Error, "gui",
                                           "tracker_save", "failure",
                                           {{"reason", "missing_save_result"}});
                host_.show_task_error(tr("No tracker save result was produced."));
            }
            else if (!outcome->value())
            {
                const auto& error = outcome->value().error();
                host_.finish_gui_operation(
                    operation_id,
                    error.code == ErrorCode::Cancelled ? core::LogLevel::Warning
                                                       : core::LogLevel::Error,
                    "gui", "tracker_save",
                    error.code == ErrorCode::Cancelled ? "cancel" : "failure",
                    {{"code", error_code_name(error.code)}, {"message", error.message}});
                host_.show_error(error);
            }
            else
            {
                host_.finish_gui_operation(operation_id, core::LogLevel::Info, "gui",
                                           "tracker_save", "finish",
                                           {{"target", target.u8string()}});
                tracker_loaded_ = outcome->value().value().loaded;
                host_.set_status(tr("Tracker torrent saved"));
                host_.remember_save_directory(tracker_loaded_->source_path().parent_path());
            }
        });
}

void TrackerPage::reset_tracker_page()
{
    if (host_.task_running())
    {
        // A worker may still read this page's loaded torrent.
        return;
    }
    tracker_loaded_.reset();
    tracker_source_directory_.reset();
    ui_->editTrackerSourcePath->clear();
    ui_->editTrackerOutputPath->clear();
    tracker_auto_output_path_.reset();
    set_tracker_files_model(nullptr);
    tracker_model_->clear();
    tracker_model_->setHorizontalHeaderLabels({tr("Tier"), tr("Tracker")});
    ui_->chkTrackerOverwrite->setChecked(false);
    ui_->chkTrackerDryRun->setChecked(false);
    ui_->chkTrackerBackup->setChecked(false);
    {
        const QSignalBlocker block_recursive(ui_->chkTrackerRecursive);
        ui_->chkTrackerRecursive->setChecked(false);
    }
    ui_->radTrackerReplace->setChecked(true);
    tracker_rewrite_panel_->clear();
    ui_->progTrackerBatch->setValue(0);
    ui_->lblTrackerProgressFiles->clear();
    host_.set_status(tr("Ready"));
}

bool TrackerPage::tracker_rewrite_mode() const
{
    return ui_->radTrackerRewrite->isChecked();
}

void TrackerPage::update_tracker_actions()
{
    if (tracker_rewrite_panel_ == nullptr)
    {
        return;
    }
    const auto busy = host_.task_running();
    const auto rules_ready = tracker_rewrite_panel_->rules().has_value();
    ui_->btnTrackerBatchConvert->setEnabled(!busy && (!tracker_rewrite_mode() || rules_ready));
    tracker_rewrite_panel_->preview_all_button()->setEnabled(!busy && rules_ready &&
                                                             tracker_source_directory_);
}

void TrackerPage::set_tracker_files_model(QStandardItemModel* model)
{
    auto* previous = tracker_files_model_;
    tracker_files_model_ = model;
    ui_->tblTrackerTorrents->setModel(model);
    if (previous != nullptr && previous != model)
    {
        previous->deleteLater();
    }
    tracker_file_rows_.clear();
    if (model == nullptr)
    {
        return;
    }
    for (int row = 0; row < model->rowCount(); ++row)
    {
        tracker_file_rows_.insert(model->item(row, 0)->data(Qt::UserRole).toString(), row);
    }
    retranslate_tracker_files();
    auto* file_header = ui_->tblTrackerTorrents->horizontalHeader();
    file_header->setStretchLastSection(false);
    file_header->setSectionResizeMode(0, QHeaderView::Stretch);
    file_header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    file_header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
}

QString TrackerPage::tracker_file_state_text(const TrackerFileState state,
                                             const qulonglong changed_urls) const
{
    switch (state)
    {
    case TrackerFileState::Ready:
        return tr("Ready");
    case TrackerFileState::Unavailable:
        return tr("Unavailable");
    case TrackerFileState::WillChange:
        return tr("Will change (%1)").arg(changed_urls);
    case TrackerFileState::Updated:
        return tr("Updated");
    case TrackerFileState::NoChange:
        return tr("No change");
    case TrackerFileState::Failed:
        return tr("Failed");
    case TrackerFileState::WillFail:
        return tr("Will fail");
    }
    return {};
}

void TrackerPage::retranslate_tracker_files()
{
    if (tracker_files_model_ == nullptr)
    {
        return;
    }
    tracker_files_model_->setHorizontalHeaderLabels({tr("Filename"), tr("Size"), tr("Status")});
    for (int row = 0; row < tracker_files_model_->rowCount(); ++row)
    {
        auto* item = tracker_files_model_->item(row, 2);
        if (item == nullptr)
        {
            continue;
        }
        const auto text = tracker_file_state_text(
            static_cast<TrackerFileState>(item->data(kTrackerFileStateRole).toInt()),
            item->data(kTrackerFileCountRole).toULongLong());
        const auto had_detail = item->data(kTrackerFileDetailRole).toString();
        item->setText(text);
        item->setToolTip(had_detail.isEmpty() ? text : had_detail);
    }
}

void TrackerPage::set_tracker_file_status(const std::filesystem::path& path,
                                          const TrackerFileState state,
                                          const qulonglong changed_urls, const QString& detail)
{
    if (tracker_files_model_ == nullptr)
    {
        return;
    }
    const auto row = tracker_file_rows_.find(path_to_text(path));
    if (row == tracker_file_rows_.end())
    {
        return;
    }
    if (auto* item = tracker_files_model_->item(*row, 2))
    {
        item->setData(static_cast<int>(state), kTrackerFileStateRole);
        item->setData(changed_urls, kTrackerFileCountRole);
        item->setData(detail, kTrackerFileDetailRole);
        const auto text = tracker_file_state_text(state, changed_urls);
        item->setText(text);
        item->setToolTip(detail.isEmpty() ? text : detail);
    }
}

void TrackerPage::preview_tracker_rewrite_all()
{
    if (host_.task_running() || !tracker_source_directory_)
    {
        return;
    }
    tracker_rewrite_panel_->refresh();
    auto outcome = std::make_shared<TrackerBatchRun>();
    const auto root = *tracker_source_directory_;
    const auto recursive = ui_->chkTrackerRecursive->isChecked();
    const auto& rules = tracker_rewrite_panel_->rules();
    if (!rules)
    {
        return;
    }
    const frontend::TrackerBatchEdit edit = *rules;
    const auto operation_id =
        host_.begin_gui_operation("gui", "tracker_rewrite_preview",
                                  {{"source", root.u8string()},
                                   {"recursive", recursive ? "true" : "false"},
                                   {"rule_count", std::to_string(rules->size())}});
    host_.set_busy(true);
    host_.set_status(tr("Previewing tracker rewrite"));
    host_.task_runner().start(
        [this, root, recursive, edit, outcome, operation_id](const core::CancellationToken& token) {
            frontend::TrackerBatchOptions options;
            options.dry_run = true;
            run_tracker_batch(*outcome, root, recursive, edit, options, "Tracker preview",
                              operation_id, token);
        },
        [this, outcome, operation_id] {
            std::size_t changed = 0;
            std::size_t failed = 0;
            for (const auto& result : outcome->results)
            {
                switch (result.status)
                {
                case frontend::TrackerBatchFileStatus::Changed:
                    ++changed;
                    set_tracker_file_status(result.path, TrackerFileState::WillChange,
                                            static_cast<qulonglong>(result.changed_urls));
                    break;
                case frontend::TrackerBatchFileStatus::Unchanged:
                    set_tracker_file_status(result.path, TrackerFileState::NoChange);
                    break;
                case frontend::TrackerBatchFileStatus::Failed:
                    ++failed;
                    set_tracker_file_status(result.path, TrackerFileState::WillFail, 0,
                                            result.reason);
                    break;
                }
            }
            if (outcome->error)
            {
                const auto& error = *outcome->error;
                host_.finish_gui_operation(
                    operation_id,
                    error.code == ErrorCode::Cancelled ? core::LogLevel::Warning
                                                       : core::LogLevel::Error,
                    "gui", "tracker_rewrite_preview",
                    error.code == ErrorCode::Cancelled ? "cancel" : "failure",
                    {{"code", error_code_name(error.code)}, {"message", error.message}});
                host_.show_error(error);
                return;
            }
            host_.finish_gui_operation(operation_id, core::LogLevel::Info, "gui",
                                       "tracker_rewrite_preview", "finish",
                                       {{"file_count", std::to_string(outcome->results.size())},
                                        {"changed_count", std::to_string(changed)},
                                        {"failure_count", std::to_string(failed)}});
            host_.set_status(tr("Rewrite preview: %1 of %2 torrent(s) would change, %3 would fail.")
                                 .arg(static_cast<qulonglong>(changed))
                                 .arg(static_cast<qulonglong>(outcome->results.size()))
                                 .arg(static_cast<qulonglong>(failed)));
        });
}

void TrackerPage::run_tracker_batch(TrackerBatchRun& run, const std::filesystem::path& root,
                                    const bool recursive, const frontend::TrackerBatchEdit& edit,
                                    const frontend::TrackerBatchOptions& options,
                                    const char* const stage, const std::string& operation_id,
                                    const core::CancellationToken& token) const
{
    auto listed = frontend::enumerate_torrent_files(root, recursive, token);
    if (!listed)
    {
        run.error = listed.error();
        return;
    }
    const auto& files = listed.value();
    run.file_count = files.size();
    for (std::size_t index = 0; index < files.size(); ++index)
    {
        host_.task_runner().report({stage, index, files.size()});
        const core::TaskContext context{token, {}, host_.logger(), operation_id};
        const auto result = frontend::apply_tracker_batch_file(
            host_.torrent_service(), files[index], edit, options, context);
        if (result.error && result.cancelled())
        {
            run.error = *result.error;
            return;
        }
        TrackerBatchRun::FileResult entry{files[index].path, result.status, 0U, {}};
        if (result.preview)
        {
            entry.changed_urls = result.preview->count(core::TrackerRewriteStatus::Changed) +
                                 result.preview->count(core::TrackerRewriteStatus::Merged);
        }
        if (result.error)
        {
            entry.reason = error_text(*result.error);
        }
        run.results.push_back(std::move(entry));
    }
    host_.task_runner().report({stage, files.size(), files.size()});
}
