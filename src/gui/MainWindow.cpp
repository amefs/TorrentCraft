#include "MainWindow.hpp"

#include "AboutDialog.hpp"
#include "FileTreeModel.hpp"
#include "GuiLogController.hpp"
#include "GuiTaskRunner.hpp"
#include "Logo.hpp"
#include "NotificationDialog.hpp"
#include "OperationCloseDialog.hpp"
#include "PresetPreviewDialog.hpp"
#include "TrackerEditDialog.hpp"
#include "ui_MainWindow.h"

#include <QAbstractItemView>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QStandardItemModel>
#include <QStyleFactory>
#include <QSysInfo>
#include <QTableView>
#include <QTranslator>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QWidget>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace torrentutils;
using core::CreateOptions;
using core::CreateOptionsInput;
using core::Error;
using core::ErrorCode;
using core::FileOrderPolicy;
using core::PieceLengthStrategy;
using core::TorrentFormat;

constexpr std::string_view default_created_by = "TorrentCraft";

QString error_text(const Error& error)
{
    QString text = QString::fromStdString(error.message);
    for (const auto& issue : error.issues)
    {
        text += QStringLiteral("\n") + QString::fromStdString(issue.field) + QStringLiteral(": ") +
                QString::fromStdString(issue.message);
    }
    return text;
}

QString format_name(const TorrentFormat format)
{
    switch (format)
    {
    case TorrentFormat::V1:
        return QStringLiteral("V1");
    case TorrentFormat::V2:
        return QStringLiteral("V2");
    case TorrentFormat::Hybrid:
        return QStringLiteral("Hybrid");
    }
    return QStringLiteral("Unknown");
}

const char* error_code_name(const ErrorCode code) noexcept
{
    switch (code)
    {
    case ErrorCode::FileNotFound:
        return "file_not_found";
    case ErrorCode::AccessDenied:
        return "access_denied";
    case ErrorCode::InvalidBencode:
        return "invalid_bencode";
    case ErrorCode::InvalidTorrent:
        return "invalid_torrent";
    case ErrorCode::UnsupportedFeature:
        return "unsupported_feature";
    case ErrorCode::ValidationFailed:
        return "validation_failed";
    case ErrorCode::IoFailure:
        return "io_failure";
    case ErrorCode::Cancelled:
        return "cancelled";
    case ErrorCode::Conflict:
        return "conflict";
    case ErrorCode::ResourceLimitExceeded:
        return "resource_limit_exceeded";
    case ErrorCode::Internal:
        return "internal";
    }
    return "unknown";
}

const char* verification_outcome_name(const core::VerificationOutcome outcome) noexcept
{
    switch (outcome)
    {
    case core::VerificationOutcome::Verified:
        return "verified";
    case core::VerificationOutcome::Mismatched:
        return "mismatched";
    case core::VerificationOutcome::Incomplete:
        return "incomplete";
    }
    return "unknown";
}

std::string finding_names(const core::FileVerificationFinding findings)
{
    if (findings == core::FileVerificationFinding::None)
    {
        return "none";
    }
    std::string result;
    const auto append = [&result, findings](const core::FileVerificationFinding finding,
                                            const char* name) {
        if (core::has_finding(findings, finding))
        {
            if (!result.empty())
                result += ",";
            result += name;
        }
    };
    append(core::FileVerificationFinding::Missing, "missing");
    append(core::FileVerificationFinding::NotRegularFile, "not_regular_file");
    append(core::FileVerificationFinding::LengthMismatch, "length_mismatch");
    append(core::FileVerificationFinding::HashMismatch, "hash_mismatch");
    append(core::FileVerificationFinding::SharedPieceMismatch, "shared_piece_mismatch");
    append(core::FileVerificationFinding::SymlinkMissing, "symlink_missing");
    append(core::FileVerificationFinding::SymlinkTargetMismatch, "symlink_target_mismatch");
    return result;
}

int memory_bytes_to_mib(const std::uint64_t bytes)
{
    constexpr auto bytes_per_mib = std::uint64_t{1024U} * 1024U;
    const auto mib = bytes / bytes_per_mib + (bytes % bytes_per_mib != 0U ? 1U : 0U);
    return static_cast<int>(
        (std::min)(mib, static_cast<std::uint64_t>((std::numeric_limits<int>::max)())));
}

QString preset_preview(const frontend::CreationSettingsPatch& settings,
                       const frontend::CreationSettingsPatch* defaults = nullptr)
{
    const auto effective =
        defaults != nullptr ? frontend::overlay_settings(*defaults, settings) : settings;
    QStringList lines;
    if (effective.format)
        lines << QStringLiteral("format: ") + format_name(*effective.format);
    if (effective.file_order)
        lines << QStringLiteral("file_order: ") +
                     QString::number(static_cast<int>(*effective.file_order));
    if (effective.piece_size)
        lines << QStringLiteral("piece_size: ") +
                     (effective.piece_size->fixed_kib
                          ? QString::number(*effective.piece_size->fixed_kib) +
                                QStringLiteral(" KiB")
                          : QStringLiteral("auto"));
    if (effective.is_private)
        lines << QStringLiteral("private: ") +
                     (*effective.is_private ? QStringLiteral("true") : QStringLiteral("false"));
    if (effective.tracker_tiers)
        lines << QStringLiteral("tracker_tiers: ") +
                     QString::number(static_cast<qulonglong>(effective.tracker_tiers->size()));
    if (effective.web_seeds)
        lines << QStringLiteral("web_seeds: ") +
                     QString::number(static_cast<qulonglong>(effective.web_seeds->size()));
    if (effective.comment)
        lines << QStringLiteral("comment: ") + QString::fromStdString(*effective.comment);
    if (effective.created_by)
        lines << QStringLiteral("created_by: ") + QString::fromStdString(*effective.created_by);
    if (effective.info_source)
        lines << QStringLiteral("source: ") + QString::fromStdString(*effective.info_source);
    return lines.join(QLatin1Char('\n'));
}

int piece_size_combo_index(const std::uint32_t kib) noexcept
{
    if (kib < 16U || (kib & (kib - 1U)) != 0U)
    {
        return 0;
    }
    int index = 1;
    auto value = kib;
    while (value > 16U)
    {
        value >>= 1U;
        ++index;
    }
    return index;
}

std::filesystem::path path_from_text(const QString& value)
{
    return std::filesystem::u8path(value.toUtf8().toStdString());
}

QString path_to_text(const std::filesystem::path& value)
{
    const auto text = QString::fromUtf8(value.u8string().c_str());
    return QDir::toNativeSeparators(QDir::fromNativeSeparators(text));
}

enum class PathDropTarget : std::uint8_t
{
    None,
    Create,
    Inspect,
    Modify,
    Tracker,
    VerifyTorrent,
    VerifyContent
};

PathDropTarget path_drop_target(const QObject* watched, const Ui::TorrentCraftMainWindow& ui)
{
    if (watched == ui.grpCreateInput || watched == ui.editCreateInputPath)
        return PathDropTarget::Create;
    if (watched == ui.grpInspectInput || watched == ui.editInspectTorrentPath)
        return PathDropTarget::Inspect;
    if (watched == ui.grpModifyInput || watched == ui.editModifyTorrentPath)
        return PathDropTarget::Modify;
    if (watched == ui.grpTrackerInput || watched == ui.editTrackerSourcePath)
        return PathDropTarget::Tracker;
    if (watched == ui.grpVerifyTorrent || watched == ui.editVerifyTorrentPath)
        return PathDropTarget::VerifyTorrent;
    if (watched == ui.grpVerifyContent || watched == ui.editVerifyContentPath)
        return PathDropTarget::VerifyContent;
    return PathDropTarget::None;
}

std::optional<QString> local_path_from_drop(const QMimeData& mime_data)
{
    const auto urls = mime_data.urls();
    if (urls.isEmpty())
        return std::nullopt;

    auto url = urls.front();
    if (!url.isValid() || !url.isLocalFile())
        return std::nullopt;

    const auto host = url.host();
    if (!host.isEmpty() && host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) != 0)
        return std::nullopt;
    if (!host.isEmpty())
        url.setHost({});

    const auto path = url.toLocalFile();
    if (path.isEmpty())
        return std::nullopt;
    return QDir::toNativeSeparators(path);
}

/**
 * Updates an automatically generated path without overwriting an explicit user path.
 *
 * The generated value is tracked so a later default-directory change may refresh it, while
 * any different non-empty value is treated as an explicit user choice.
 */
void set_auto_path(QLineEdit* target, std::optional<std::filesystem::path>& generated,
                   const std::filesystem::path& candidate)
{
    const auto current_text = target->text().trimmed();
    if (!current_text.isEmpty())
    {
        const auto current = path_from_text(current_text).lexically_normal();
        if (!generated || current != generated->lexically_normal())
        {
            return;
        }
    }
    target->setText(path_to_text(candidate));
    generated = candidate;
}

std::optional<std::string> optional_text(const QString& value)
{
    const auto text = value.trimmed();
    if (text.isEmpty())
    {
        return std::nullopt;
    }
    return text.toUtf8().toStdString();
}

QString initial_style_name()
{
    static const QString name = QApplication::style()->objectName();
    return name;
}

void apply_gui_display_preferences(const frontend::GuiPreferences& preferences)
{
    static const QFont default_font = QApplication::font();
    QFont font = default_font;
    if (preferences.font_family)
    {
        const auto families = QFontDatabase::families();
        if (families.contains(QString::fromUtf8(preferences.font_family->c_str())))
        {
            font.setFamily(QString::fromUtf8(preferences.font_family->c_str()));
        }
    }
    QApplication::setFont(font);

    const auto requested_style =
        preferences.style ? QString::fromUtf8(preferences.style->c_str()) : QString{};
    const auto default_style = initial_style_name();
    const auto target_style = requested_style.isEmpty() ? default_style : requested_style;
    const auto apply_style = [](const QString& name) {
        for (const auto& available : QStyleFactory::keys())
        {
            if (available.compare(name, Qt::CaseInsensitive) == 0)
            {
                QApplication::setStyle(available);
                return true;
            }
        }
        return false;
    };
    if (!apply_style(target_style) && target_style != default_style)
    {
        static_cast<void>(apply_style(default_style));
    }
}

bool is_custom_created_by(const std::optional<std::string>& value)
{
    return value.has_value() && value.value() != default_created_by;
}

QString optional_string(const std::optional<std::string>& value, const QString& fallback = {})
{
    if (!value.has_value())
    {
        return fallback;
    }
    return QString::fromStdString(value.value());
}

QString format_iec(const double raw_value, const QString& suffix)
{
    constexpr const char* kByteUnits[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = std::max(0.0, raw_value);
    int unit = 0;
    while (value >= 1024.0 && unit < 4)
    {
        value /= 1024.0;
        ++unit;
    }
    const int precision = unit == 0 ? 0 : value < 10.0 ? 2 : value < 100.0 ? 1 : 0;
    auto text = QString::number(value, 'f', precision);
    while (text.contains(QLatin1Char('.')) && text.endsWith(QLatin1Char('0')))
    {
        text.chop(1);
    }
    if (text.endsWith(QLatin1Char('.')))
    {
        text.chop(1);
    }
    return text + QLatin1Char(' ') + QString::fromLatin1(kByteUnits[unit]) + suffix;
}

QString format_bytes_iec(const std::uint64_t bytes)
{
    return format_iec(static_cast<double>(bytes), QString());
}

QString format_bytes_rate_iec(const double bytes_per_second)
{
    return format_iec(bytes_per_second, QStringLiteral("/s"));
}

QString format_piece_rate(const double pieces_per_second, const QString& unit)
{
    const auto value = std::max(0.0, pieces_per_second);
    const int precision = value < 10.0 ? 2 : 1;
    return QString::number(value, 'f', precision) + QLatin1Char(' ') + unit;
}

template <typename Digest>
QString optional_digest(const std::optional<Digest>& value, const QString& fallback)
{
    if (!value.has_value())
    {
        return fallback;
    }
    return QString::fromStdString(value.value().to_hex());
}

std::vector<std::string> nonempty_lines(const QString& value)
{
    std::vector<std::string> result;
    for (const auto& line :
         value.split(QRegularExpression(QStringLiteral("[\\r\\n]")), Qt::SkipEmptyParts))
    {
        const auto trimmed = line.trimmed();
        if (!trimmed.isEmpty())
        {
            result.push_back(trimmed.toUtf8().toStdString());
        }
    }
    return result;
}

std::optional<std::vector<core::TrackerTier>> parse_tracker_tiers(const QString& text,
                                                                  QString* error)
{
    std::vector<core::TrackerTier> tiers;
    const auto groups =
        text.split(QRegularExpression(QStringLiteral("\\r?\\n\\s*\\r?\\n")), Qt::SkipEmptyParts);
    for (const auto& group : groups)
    {
        std::vector<core::TrackerUrl> urls;
        for (const auto& line :
             group.split(QRegularExpression(QStringLiteral("[\\r\\n]")), Qt::SkipEmptyParts))
        {
            auto parsed = core::TrackerUrl::parse(line.trimmed().toUtf8().toStdString());
            if (!parsed)
            {
                if (error != nullptr)
                {
                    *error = error_text(parsed.error());
                }
                return std::nullopt;
            }
            urls.push_back(std::move(parsed).value());
        }
        if (!urls.empty())
        {
            auto tier = core::TrackerTier::create(std::move(urls));
            if (!tier)
            {
                if (error != nullptr)
                {
                    *error = error_text(tier.error());
                }
                return std::nullopt;
            }
            tiers.push_back(std::move(tier).value());
        }
    }
    return tiers;
}

std::optional<std::vector<core::WebSeedUrl>> parse_web_seeds(const QString& text, QString* error)
{
    std::vector<core::WebSeedUrl> seeds;
    for (const auto& line : nonempty_lines(text))
    {
        auto seed = core::WebSeedUrl::parse(line);
        if (!seed)
        {
            if (error != nullptr)
            {
                *error = error_text(seed.error());
            }
            return std::nullopt;
        }
        seeds.push_back(std::move(seed).value());
    }
    return seeds;
}

std::optional<std::vector<core::DhtNode>> parse_dht_nodes(const QString& text, QString* error)
{
    std::vector<core::DhtNode> nodes;
    for (const auto& line : nonempty_lines(text))
    {
        const auto separator = line.find_last_of(':');
        if (separator == std::string::npos || separator == 0U || separator + 1U == line.size())
        {
            if (error != nullptr)
            {
                *error = QCoreApplication::translate("MainWindow", "DHT nodes must use HOST:PORT.");
            }
            return std::nullopt;
        }
        bool valid_port = false;
        const auto port = QString::fromStdString(line.substr(separator + 1U)).toUInt(&valid_port);
        if (!valid_port || port == 0U || port > 65535U)
        {
            if (error != nullptr)
            {
                *error = QCoreApplication::translate("MainWindow",
                                                     "DHT node port must be between 1 and 65535.");
            }
            return std::nullopt;
        }
        auto node = core::DhtNode::create(line.substr(0U, separator), port);
        if (!node)
        {
            if (error != nullptr)
            {
                *error = error_text(node.error());
            }
            return std::nullopt;
        }
        nodes.push_back(std::move(node).value());
    }
    return nodes;
}

void populate_tracker_values(QStandardItemModel& model,
                             const std::vector<std::vector<std::string>>& tiers)
{
    model.clear();
    model.setHorizontalHeaderLabels({QObject::tr("Tier"), QObject::tr("Tracker")});
    for (std::size_t tier_index = 0; tier_index < tiers.size(); ++tier_index)
    {
        for (const auto& tracker : tiers[tier_index])
        {
            model.appendRow({new QStandardItem(QString::number(tier_index + 1U)),
                             new QStandardItem(QString::fromStdString(tracker))});
        }
    }
}

void populate_tracker_model(QStandardItemModel& model, const core::TrackerList& trackers)
{
    std::vector<std::vector<std::string>> tiers;
    tiers.reserve(trackers.tiers().size());
    for (const auto& tier : trackers.tiers())
    {
        std::vector<std::string> values;
        values.reserve(tier.trackers().size());
        for (const auto& tracker : tier.trackers())
        {
            values.push_back(tracker.value());
        }
        tiers.push_back(std::move(values));
    }
    populate_tracker_values(model, tiers);
}

std::optional<core::TrackerList> tracker_list_from_model(const QStandardItemModel& model,
                                                         QString* error)
{
    std::vector<std::vector<core::TrackerUrl>> grouped;
    for (int row = 0; row < model.rowCount(); ++row)
    {
        const auto tier_text = model.index(row, 0).data().toString();
        const auto tracker_text = model.index(row, 1).data().toString().trimmed();
        bool valid = false;
        const auto tier_index = tier_text.toInt(&valid);
        if (!valid || tier_index <= 0)
        {
            if (error != nullptr)
            {
                *error = QObject::tr("Tracker row %1 has an invalid tier.").arg(row + 1);
            }
            return std::nullopt;
        }
        if (grouped.size() < static_cast<std::size_t>(tier_index))
        {
            grouped.resize(static_cast<std::size_t>(tier_index));
        }
        auto tracker = core::TrackerUrl::parse(tracker_text.toUtf8().toStdString());
        if (!tracker)
        {
            if (error != nullptr)
            {
                *error = error_text(tracker.error());
            }
            return std::nullopt;
        }
        grouped[static_cast<std::size_t>(tier_index - 1)].push_back(std::move(tracker).value());
    }

    std::vector<core::TrackerTier> tiers;
    bool empty_tier_seen = false;
    for (auto& group : grouped)
    {
        if (group.empty())
        {
            empty_tier_seen = true;
            continue;
        }
        if (empty_tier_seen)
        {
            if (error != nullptr)
            {
                *error = QObject::tr("Tracker tiers must be numbered consecutively.");
            }
            return std::nullopt;
        }
        auto tier = core::TrackerTier::create(std::move(group));
        if (!tier)
        {
            if (error != nullptr)
            {
                *error = error_text(tier.error());
            }
            return std::nullopt;
        }
        tiers.push_back(std::move(tier).value());
    }
    auto result = core::TrackerList::create(std::move(tiers));
    if (!result)
    {
        if (error != nullptr)
        {
            *error = error_text(result.error());
        }
        return std::nullopt;
    }
    return std::move(result).value();
}

void configure_tracker_table(QTableView& table)
{
    auto* header = table.horizontalHeader();
    table.setAlternatingRowColors(true);
    header->setStretchLastSection(true);
    header->setSectionResizeMode(0, QHeaderView::Fixed);
    header->setMinimumSectionSize(48);
    header->resizeSection(0, 48);
    header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table.setTextElideMode(Qt::ElideNone);
    table.setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
}

int selected_tracker_row(const QTableView& view) noexcept
{
    const auto index = view.currentIndex();
    return index.isValid() ? index.row() : -1;
}

void add_tracker_row(QWidget* parent, QTableView& view, QStandardItemModel& model)
{
    TrackerEditDialog dialog(1, {}, parent->tr("Add tracker"), parent, true);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }
    QString parse_error;
    const auto trackers = parse_tracker_tiers(dialog.tracker(), &parse_error);
    if (!trackers)
    {
        NotificationDialog::show_error(parent, parent->tr("Invalid tracker list"), parse_error);
        return;
    }

    int next_tier = 1;
    for (int row = 0; row < model.rowCount(); ++row)
    {
        next_tier = (std::max)(next_tier, model.index(row, 0).data().toInt() + 1);
    }
    int last_row = -1;
    for (const auto& tier : *trackers)
    {
        for (const auto& tracker : tier.trackers())
        {
            last_row = model.rowCount();
            model.appendRow({new QStandardItem(QString::number(next_tier)),
                             new QStandardItem(QString::fromStdString(tracker.value()))});
        }
        ++next_tier;
    }
    if (last_row >= 0)
    {
        view.selectRow(last_row);
    }
}

void edit_tracker_row(QWidget* parent, QTableView& view, QStandardItemModel& model)
{
    const auto row = selected_tracker_row(view);
    if (row < 0)
    {
        NotificationDialog::show_warning(parent, parent->tr("No tracker selected"),
                                         parent->tr("Select a tracker row first."));
        return;
    }
    TrackerEditDialog dialog(model.index(row, 0).data().toInt(),
                             model.index(row, 1).data().toString(), parent->tr("Edit tracker"),
                             parent);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }
    model.setData(model.index(row, 0), dialog.tier());
    model.setData(model.index(row, 1), dialog.tracker());
    view.selectRow(row);
}

void remove_tracker_row(QWidget* parent, QTableView& view, QStandardItemModel& model)
{
    const auto row = selected_tracker_row(view);
    if (row < 0)
    {
        NotificationDialog::show_warning(parent, parent->tr("No tracker selected"),
                                         parent->tr("Select a tracker row first."));
        return;
    }
    model.removeRow(row);
    if (model.rowCount() > 0)
    {
        view.selectRow(std::min(row, model.rowCount() - 1));
    }
}

void move_tracker_row(QTableView& view, QStandardItemModel& model, const int direction)
{
    const auto row = selected_tracker_row(view);
    const auto target = row + direction;
    if (row < 0 || target < 0 || target >= model.rowCount())
    {
        return;
    }
    auto values = model.takeRow(row);
    model.insertRow(target, values);
    view.selectRow(target);
}

core::ProgressCallback progress_callback(GuiTaskRunner* runner)
{
    return [runner](const core::ProgressInfo& progress) { runner->report(progress); };
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : MainWindow(nullptr, parent) {}

MainWindow::MainWindow(GuiLogController* logger, QWidget* parent)
    : QMainWindow(parent), ui_(new Ui::TorrentCraftMainWindow),
      owned_logger_(logger == nullptr ? std::make_unique<GuiLogController>() : nullptr),
      logger_(logger != nullptr ? logger : owned_logger_.get()), service_(repository_, clock_),
      task_runner_(std::make_unique<GuiTaskRunner>(this)),
      inspect_file_model_(std::make_unique<FileTreeModel>(this, FileTreeModel::Mode::Inspect)),
      inspect_tracker_model_(std::make_unique<QStandardItemModel>(this)),
      verify_file_model_(std::make_unique<FileTreeModel>(this, FileTreeModel::Mode::Verify)),
      create_tracker_model_(std::make_unique<QStandardItemModel>(this)),
      modify_tracker_model_(std::make_unique<QStandardItemModel>(this)),
      tracker_model_(std::make_unique<QStandardItemModel>(this)), translator_(new QTranslator(this))
{
    ui_->setupUi(this);
    ui_->cmbAdvancedStyle->addItem(tr("Default"), QString());
    logger_->set_failure_callback([this](std::string message) {
        const auto text = QString::fromUtf8(message.data(), static_cast<int>(message.size()));
        QMetaObject::invokeMethod(
            this,
            [this, text] {
                NotificationDialog::show_warning(this, tr("Logging unavailable"), text);
            },
            Qt::QueuedConnection);
    });
    auto styles = QStyleFactory::keys();
    std::sort(styles.begin(), styles.end(), [](const auto& left, const auto& right) {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    for (const auto& style : styles)
    {
        ui_->cmbAdvancedStyle->addItem(style, style);
    }
    ui_->cmbAdvancedFont->addItem(tr("Default"), QString());
    auto fonts = QFontDatabase::families();
    std::sort(fonts.begin(), fonts.end(), [](const auto& left, const auto& right) {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    for (const auto& font : fonts)
    {
        ui_->cmbAdvancedFont->addItem(font, font);
    }
    ui_->lblCreatePieces->clear();
    ui_->lblAdvancedVerifyMemory->setText(tr("Verification memory (MiB):"));
    ui_->grpAdvancedDefaultSave->setMinimumHeight(0);
    ui_->grpAdvancedDefaultSave->layout()->setContentsMargins(6, 6, 6, 6);

    setWindowIcon(torrentcraft::gui::application_icon());
    ui_->treeInspectFileTree->setModel(inspect_file_model_.get());
    ui_->treeVerifyFileList->setModel(verify_file_model_.get());
    for (auto* tree : {ui_->treeInspectFileTree, ui_->treeVerifyFileList})
    {
        tree->setSortingEnabled(false);
        tree->setUniformRowHeights(true);
        tree->setAnimated(false);
        tree->setAlternatingRowColors(true);
        tree->header()->setStretchLastSection(false);
        tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    }
    ui_->treeVerifyFileList->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    create_tracker_model_->setHorizontalHeaderLabels({tr("Tier"), tr("Tracker")});
    ui_->tblCreateTrackers->setModel(create_tracker_model_.get());
    modify_tracker_model_->setHorizontalHeaderLabels({tr("Tier"), tr("Tracker")});
    ui_->tblModifyTrackers->setModel(modify_tracker_model_.get());
    tracker_model_->setHorizontalHeaderLabels({tr("Tier"), tr("Tracker")});
    ui_->tblTrackerTiers->setModel(tracker_model_.get());
    inspect_tracker_model_->setHorizontalHeaderLabels({tr("Tier"), tr("Tracker")});
    ui_->tblInspectTrackers->setModel(inspect_tracker_model_.get());
    for (auto* table : {ui_->tblCreateTrackers, ui_->tblModifyTrackers, ui_->tblTrackerTiers,
                        ui_->tblInspectTrackers})
    {
        configure_tracker_table(*table);
    }
    ui_->treeInspectMetadataFields->setColumnCount(4);
    ui_->treeInspectMetadataFields->setHeaderLabels(
        {tr("Key"), tr("Value"), tr("Scope"), tr("Type")});
    ui_->treeInspectMetadataFields->setRootIsDecorated(false);
    ui_->treeInspectMetadataFields->setAlternatingRowColors(true);
    ui_->treeInspectMetadataFields->header()->setSectionResizeMode(0,
                                                                   QHeaderView::ResizeToContents);
    ui_->treeInspectMetadataFields->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui_->treeInspectMetadataFields->header()->setSectionResizeMode(2,
                                                                   QHeaderView::ResizeToContents);
    ui_->treeInspectMetadataFields->header()->setSectionResizeMode(3,
                                                                   QHeaderView::ResizeToContents);
    ui_->editModifySource->setReadOnly(false);
    ui_->chkModifyClearSource->setEnabled(true);
    ui_->chkModifyClearSource->setChecked(false);
    ui_->btnCreateCancel->setEnabled(false);
    ui_->btnVerifyCancel->setEnabled(false);
    ui_->btnTrackerCancel->setEnabled(false);

    language_group_ = new QActionGroup(this);
    language_group_->setExclusive(true);
    language_group_->addAction(ui_->actionLanguageEnglish);
    language_group_->addAction(ui_->actionLanguageChinese);
    connect(ui_->actionAbout, &QAction::triggered, this, &MainWindow::show_about);
    connect(ui_->actionLanguageEnglish, &QAction::triggered, this,
            [this] { apply_language(ui_->actionLanguageEnglish); });
    connect(ui_->actionLanguageChinese, &QAction::triggered, this,
            [this] { apply_language(ui_->actionLanguageChinese); });
    connect(ui_->actionImportFromFile, &QAction::triggered, this, &MainWindow::import_preset);
    connect(ui_->actionSavePreset, &QAction::triggered, this, &MainWindow::save_preset);
    connect(ui_->actionRemovePreset, &QAction::triggered, this, &MainWindow::remove_preset);
    connect(ui_->actionClearAll, &QAction::triggered, this, &MainWindow::clear_current_form);

    connect(ui_->btnCreateSelectFile, &QPushButton::clicked, this, [this] {
        choose_file(ui_->editCreateInputPath, tr("Select content file"));
        update_create_output_path();
    });
    connect(ui_->btnCreateSelectFolder, &QPushButton::clicked, this, [this] {
        choose_directory(ui_->editCreateInputPath, tr("Select content folder"));
        update_create_output_path();
    });
    connect(ui_->editCreateInputPath, &QLineEdit::textChanged, this,
            [this] { update_create_output_path(); });
    connect(ui_->btnCreateSaveTo, &QPushButton::clicked, this,
            [this] { choose_output_file(ui_->editCreateOutputPath, tr("Select torrent output")); });
    connect(ui_->btnCreateCalcPieces, &QPushButton::clicked, this,
            &MainWindow::calculate_create_plan);
    connect(ui_->btnCreateTorrent, &QPushButton::clicked, this, &MainWindow::create_torrent);
    connect(ui_->btnCreateCancel, &QPushButton::clicked, this, &MainWindow::cancel_operation);
    connect(ui_->btnCreateTrackerAdd, &QPushButton::clicked, this, [this] {
        const auto rows = create_tracker_model_->rowCount();
        add_tracker_row(this, *ui_->tblCreateTrackers, *create_tracker_model_);
        if (create_tracker_model_->rowCount() != rows)
            mark_preset_modified();
    });
    connect(ui_->btnCreateTrackerRemove, &QPushButton::clicked, this, [this] {
        const auto rows = create_tracker_model_->rowCount();
        remove_tracker_row(this, *ui_->tblCreateTrackers, *create_tracker_model_);
        if (create_tracker_model_->rowCount() != rows)
            mark_preset_modified();
    });
    connect(ui_->btnCreateTrackerEdit, &QPushButton::clicked, this,
            [this] { edit_tracker_row(this, *ui_->tblCreateTrackers, *create_tracker_model_); });
    connect(ui_->btnCreateTrackerUp, &QPushButton::clicked, this, [this] {
        const auto row = selected_tracker_row(*ui_->tblCreateTrackers);
        if (row > 0)
        {
            move_tracker_row(*ui_->tblCreateTrackers, *create_tracker_model_, -1);
            mark_preset_modified();
        }
    });
    connect(ui_->btnCreateTrackerDown, &QPushButton::clicked, this, [this] {
        const auto row = selected_tracker_row(*ui_->tblCreateTrackers);
        if (row >= 0 && row + 1 < create_tracker_model_->rowCount())
        {
            move_tracker_row(*ui_->tblCreateTrackers, *create_tracker_model_, 1);
            mark_preset_modified();
        }
    });
    connect(ui_->cmbCreateFormat, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](const int) { mark_preset_modified(); });
    connect(ui_->cmbCreateFileOrder, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](const int) { mark_preset_modified(); });
    connect(ui_->cmbCreatePieceSize, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](const int) { mark_preset_modified(); });
    connect(ui_->chkCreatePrivate, &QCheckBox::toggled, this,
            [this](const bool) { mark_preset_modified(); });
    connect(ui_->editCreateComment, &QPlainTextEdit::textChanged, this,
            [this] { mark_preset_modified(); });
    connect(ui_->editCreateWebSeeds, &QPlainTextEdit::textChanged, this,
            [this] { mark_preset_modified(); });
    connect(ui_->editCreateSource, &QLineEdit::textChanged, this,
            [this](const QString&) { mark_preset_modified(); });
    connect(ui_->chkCreateCreator, &QCheckBox::toggled, this,
            [this](const bool) { mark_preset_modified(); });
    connect(ui_->editCreateCreator, &QLineEdit::textChanged, this,
            [this](const QString&) { mark_preset_modified(); });
    connect(create_tracker_model_.get(), &QStandardItemModel::itemChanged, this,
            [this](QStandardItem*) { mark_preset_modified(); });
    connect(ui_->chkCreateCreator, &QCheckBox::toggled, ui_->editCreateCreator,
            &QWidget::setEnabled);
    connect(ui_->editCreateCreator, &QLineEdit::textChanged, this, [this] {
        if (ui_->editCreateCreator->text().trimmed() == QStringLiteral("TorrentCraft"))
        {
            ui_->chkCreateCreator->setChecked(false);
        }
    });
    connect(ui_->chkCreateDate, &QCheckBox::toggled, ui_->dateCreateCreation, &QWidget::setEnabled);

    connect(ui_->btnInspectSelectFile, &QPushButton::clicked, this, [this] {
        choose_file(ui_->editInspectTorrentPath, tr("Select torrent"),
                    tr("Torrent files (*.torrent)"));
        load_inspect_torrent();
    });
    connect(ui_->editInspectTorrentPath, &QLineEdit::editingFinished, this,
            &MainWindow::load_inspect_torrent);
    connect(ui_->btnInspectLoad, &QPushButton::clicked, this, &MainWindow::load_inspect_torrent);
    connect(ui_->btnInspectValidate, &QPushButton::clicked, this,
            &MainWindow::validate_inspect_torrent);

    connect(ui_->btnModifySelectFile, &QPushButton::clicked, this, [this] {
        choose_file(ui_->editModifyTorrentPath, tr("Select torrent"),
                    tr("Torrent files (*.torrent)"));
        load_modify_torrent();
    });
    connect(ui_->btnModifySaveTo, &QPushButton::clicked, this, [this] {
        choose_output_file(ui_->editModifyOutputPath, tr("Select modified torrent output"));
    });
    connect(ui_->editModifyTorrentPath, &QLineEdit::textChanged, this, [this] {
        const auto input_text = ui_->editModifyTorrentPath->text().trimmed();
        if (input_text.isEmpty())
        {
            if (modify_auto_output_path_ &&
                path_from_text(ui_->editModifyOutputPath->text()).lexically_normal() ==
                    modify_auto_output_path_->lexically_normal())
            {
                ui_->editModifyOutputPath->clear();
            }
            modify_auto_output_path_.reset();
            return;
        }
        update_modify_output_path(path_from_text(input_text));
    });
    connect(ui_->editModifyTorrentPath, &QLineEdit::editingFinished, this,
            &MainWindow::load_modify_torrent);
    connect(ui_->chkModifyCreator, &QCheckBox::toggled, ui_->editModifyCreator,
            &QWidget::setEnabled);
    connect(ui_->editModifyCreator, &QLineEdit::textChanged, this, [this] {
        if (ui_->editModifyCreator->text().trimmed() == QStringLiteral("TorrentCraft"))
        {
            ui_->chkModifyCreator->setChecked(false);
        }
    });
    connect(ui_->chkModifyDate, &QCheckBox::toggled, ui_->dateModifyCreation, &QWidget::setEnabled);
    connect(ui_->btnModifyTrackerAdd, &QPushButton::clicked, this,
            [this] { add_tracker_row(this, *ui_->tblModifyTrackers, *modify_tracker_model_); });
    connect(ui_->btnModifyTrackerRemove, &QPushButton::clicked, this,
            [this] { remove_tracker_row(this, *ui_->tblModifyTrackers, *modify_tracker_model_); });
    connect(ui_->btnModifyTrackerEdit, &QPushButton::clicked, this,
            [this] { edit_tracker_row(this, *ui_->tblModifyTrackers, *modify_tracker_model_); });
    connect(ui_->btnModifyTrackerUp, &QPushButton::clicked, this,
            [this] { move_tracker_row(*ui_->tblModifyTrackers, *modify_tracker_model_, -1); });
    connect(ui_->btnModifyTrackerDown, &QPushButton::clicked, this,
            [this] { move_tracker_row(*ui_->tblModifyTrackers, *modify_tracker_model_, 1); });
    connect(ui_->btnModifyPreview, &QPushButton::clicked, this, &MainWindow::preview_modify);
    connect(ui_->btnModifySave, &QPushButton::clicked, this, &MainWindow::save_modify);

    connect(ui_->btnVerifySelectFile, &QPushButton::clicked, this, [this] {
        choose_file(ui_->editVerifyTorrentPath, tr("Select torrent"),
                    tr("Torrent files (*.torrent)"));
        load_verify_torrent();
    });
    connect(ui_->btnVerifySelectFolder, &QPushButton::clicked, this,
            [this] { choose_directory(ui_->editVerifyContentPath, tr("Select content folder")); });
    connect(ui_->editVerifyTorrentPath, &QLineEdit::textChanged, this,
            [this] { invalidate_verify_torrent(); });
    connect(ui_->editVerifyTorrentPath, &QLineEdit::editingFinished, this,
            &MainWindow::load_verify_torrent);
    connect(ui_->btnVerifyStart, &QPushButton::clicked, this, &MainWindow::start_verify);
    connect(ui_->btnVerifyCancel, &QPushButton::clicked, this, &MainWindow::cancel_operation);

    connect(ui_->btnTrackerSelectFolder, &QPushButton::clicked, this, [this] {
        choose_directory(ui_->editTrackerSourcePath, tr("Select torrent folder"));
        update_tracker_output_path();
        load_tracker_torrent();
    });
    connect(ui_->editTrackerSourcePath, &QLineEdit::editingFinished, this,
            &MainWindow::load_tracker_torrent);
    connect(ui_->btnTrackerSaveTo, &QPushButton::clicked, this,
            [this] { choose_directory(ui_->editTrackerOutputPath, tr("Select output folder")); });
    connect(ui_->btnTrackerReloadFolder, &QPushButton::clicked, this,
            &MainWindow::load_tracker_torrent);
    connect(ui_->btnTrackerBatchConvert, &QPushButton::clicked, this,
            &MainWindow::save_tracker_torrent);
    connect(ui_->btnTrackerCancel, &QPushButton::clicked, this, &MainWindow::cancel_operation);
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
            [this] { add_tracker_row(this, *ui_->tblTrackerTiers, *tracker_model_); });
    connect(ui_->btnTrackerRemove, &QPushButton::clicked, this,
            [this] { remove_tracker_row(this, *ui_->tblTrackerTiers, *tracker_model_); });
    connect(ui_->btnTrackerEdit, &QPushButton::clicked, this,
            [this] { edit_tracker_row(this, *ui_->tblTrackerTiers, *tracker_model_); });
    connect(ui_->btnTrackerMoveUp, &QPushButton::clicked, this,
            [this] { move_tracker_row(*ui_->tblTrackerTiers, *tracker_model_, -1); });
    connect(ui_->btnTrackerMoveDown, &QPushButton::clicked, this,
            [this] { move_tracker_row(*ui_->tblTrackerTiers, *tracker_model_, 1); });

    connect(ui_->btnAdvancedBrowseConfig, &QPushButton::clicked, this,
            [this] { choose_file(ui_->editAdvancedConfigPath, tr("Select configuration file")); });
    connect(ui_->btnAdvancedBrowseSavePath, &QPushButton::clicked, this, [this] {
        choose_directory(ui_->editAdvancedSavePath, tr("Select default save folder"));
    });
    connect(ui_->btnAdvancedBrowseLogPath, &QPushButton::clicked, this,
            [this] { choose_file(ui_->editAdvancedLogPath, tr("Select log file")); });
    connect(ui_->cmbAdvancedSaveMode, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](const int index) { ui_->editAdvancedSavePath->setEnabled(index == 2); });
    connect(ui_->btnAdvancedReloadConfig, &QPushButton::clicked, this,
            &MainWindow::reload_configuration);
    connect(ui_->btnAdvancedInitConfig, &QPushButton::clicked, this,
            &MainWindow::initialize_configuration);
    connect(ui_->btnAdvancedOpenLog, &QPushButton::clicked, this, &MainWindow::open_log_file);
    connect(ui_->btnAdvancedCopyDiagnostics, &QPushButton::clicked, this,
            &MainWindow::copy_diagnostic_context);
    connect(ui_->btnAdvancedShowConfig, &QPushButton::clicked, this,
            &MainWindow::show_configuration);
    connect(ui_->btnAdvancedApply, &QPushButton::clicked, this,
            &MainWindow::apply_advanced_configuration);
    connect(ui_->btnAdvancedReset, &QPushButton::clicked, this,
            &MainWindow::reset_advanced_configuration);

    connect(task_runner_.get(), &GuiTaskRunner::progress, this,
            [this](const QString& stage, const qulonglong completed, const qulonglong total,
                   const qulonglong completed_bytes, const qulonglong total_bytes) {
                update_progress_status(stage, completed, total, completed_bytes, total_bytes);
                if (total > 0)
                {
                    ui_->progCreateTorrent->setMaximum(static_cast<int>(total));
                    ui_->progCreateTorrent->setValue(static_cast<int>(completed));
                    ui_->progVerifyProgress->setMaximum(static_cast<int>(total));
                    ui_->progVerifyProgress->setValue(static_cast<int>(completed));
                    ui_->progTrackerBatch->setMaximum(static_cast<int>(total));
                    ui_->progTrackerBatch->setValue(static_cast<int>(completed));
                    ui_->lblTrackerProgressFiles->setText(
                        tr("(%1/%2)")
                            .arg(static_cast<qulonglong>(completed))
                            .arg(static_cast<qulonglong>(total)));
                }
            });
    connect(task_runner_.get(), &GuiTaskRunner::failed, this, [this](const QString& message) {
        if (!active_operations_.empty())
        {
            const auto active = active_operations_.back();
            finish_gui_operation(
                active.id, core::LogLevel::Error, active.component, active.operation, "failure",
                {{"reason", "worker_exception"}, {"message", message.toUtf8().toStdString()}});
        }
        if (verification_running_)
        {
            verification_running_ = false;
            verify_file_model_->mark_verification_interrupted();
            set_status(tr("Verification interrupted"));
        }
        show_task_error(message);
    });
    connect(task_runner_.get(), &GuiTaskRunner::finished, this, [this] { set_busy(false); });

    const auto paths =
        frontend::default_config_search_paths(std::nullopt, path_from_text(QDir::currentPath()));
    auto discovered = frontend::discover_config(paths);
    if (discovered)
    {
        const auto discovered_path =
            std::move(discovered).value().value_or(std::filesystem::path{});
        if (!discovered_path.empty())
        {
            ui_->editAdvancedConfigPath->setText(path_to_text(discovered_path));
            reload_configuration();
        }
    }
    if (!config_)
    {
        if (paths.user_config_path)
        {
            ui_->editAdvancedConfigPath->setText(path_to_text(*paths.user_config_path));
        }
        apply_memory_working_set_limit();
        configure_logger();
    }
    refresh_preset_menu();
    set_status(tr("Ready"));
    update_window_title();

    for (auto* widget :
         {static_cast<QWidget*>(ui_->grpCreateInput), static_cast<QWidget*>(ui_->grpInspectInput),
          static_cast<QWidget*>(ui_->grpModifyInput), static_cast<QWidget*>(ui_->grpTrackerInput),
          static_cast<QWidget*>(ui_->grpVerifyTorrent),
          static_cast<QWidget*>(ui_->grpVerifyContent),
          static_cast<QWidget*>(ui_->editCreateInputPath),
          static_cast<QWidget*>(ui_->editInspectTorrentPath),
          static_cast<QWidget*>(ui_->editModifyTorrentPath),
          static_cast<QWidget*>(ui_->editTrackerSourcePath),
          static_cast<QWidget*>(ui_->editVerifyTorrentPath),
          static_cast<QWidget*>(ui_->editVerifyContentPath)})
    {
        widget->setAcceptDrops(true);
        widget->installEventFilter(this);
    }
}

MainWindow::~MainWindow()
{
    logger_->log_event(core::LogLevel::Info, "gui", "session", "finish");
    while (!active_operations_.empty())
    {
        const auto active = active_operations_.back();
        finish_gui_operation(active.id, core::LogLevel::Warning, active.component, active.operation,
                             "cancel", {{"reason", "window_closed"}});
    }
    logger_->set_failure_callback({});
    task_runner_->cancel();
    delete ui_;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!task_runner_->is_running())
    {
        event->accept();
        return;
    }

    OperationCloseDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
    {
        logger_->log_event(core::LogLevel::Info, "gui", "window", "close_cancelled");
        event->ignore();
        return;
    }

    logger_->log_event(core::LogLevel::Info, "gui", "window", "close_confirmed",
                       {{"action", "cancel_and_exit"}});
    task_runner_->cancel();
    event->accept();
}

std::string MainWindow::begin_gui_operation(const std::string_view component,
                                            const std::string_view operation,
                                            const LogFields& fields)
{
    const auto operation_id = logger_->begin_operation(component, operation, fields);
    active_operations_.push_back({operation_id, std::string(component), std::string(operation)});
    return operation_id;
}

void MainWindow::finish_gui_operation(const std::string& operation_id, const core::LogLevel level,
                                      const std::string_view component,
                                      const std::string_view operation,
                                      const std::string_view event, const LogFields& fields)
{
    if (operation_id.empty())
    {
        return;
    }
    logger_->finish_operation(operation_id, level, component, operation, event, fields);
    const auto iterator =
        std::find_if(active_operations_.begin(), active_operations_.end(),
                     [&operation_id](const auto& active) { return active.id == operation_id; });
    if (iterator != active_operations_.end())
    {
        active_operations_.erase(iterator);
    }
}

void MainWindow::configure_logger()
{
    frontend::GuiPreferences preferences;
    std::filesystem::path config_path;
    if (config_)
    {
        preferences = config_->gui_preferences();
        config_path = config_->path();
    }
    logger_->configure(preferences, config_path);
    GuiLogController::Fields fields{{"app", "TorrentCraft"}};
    fields.emplace_back("qt", QT_VERSION_STR);
    fields.emplace_back("platform", QSysInfo::prettyProductName().toUtf8().toStdString());
    if (!config_path.empty())
    {
        fields.emplace_back("config", config_path.u8string());
    }
    logger_->log_event(core::LogLevel::Info, "gui", "session", "start", fields);
}

void MainWindow::open_log_file()
{
    logger_->log_event(core::LogLevel::Info, "gui", "diagnostics", "open_log",
                       {{"requested", "true"}});
    const auto path = logger_->active_log_path();
    if (path.empty())
    {
        NotificationDialog::show_warning(
            this, tr("No log file"),
            tr("Enable file logging and apply the configuration before opening a log file."));
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path_to_text(path))))
    {
        NotificationDialog::show_error(this, tr("Open failed"),
                                       tr("The default editor could not open the log file."));
    }
}

void MainWindow::copy_diagnostic_context()
{
    logger_->log_event(core::LogLevel::Info, "gui", "diagnostics", "copy_context",
                       {{"record_count", "bounded"}});
    const auto context = logger_->diagnostic_context();
    QApplication::clipboard()->setText(
        QString::fromUtf8(context.data(), static_cast<int>(context.size())));
    set_status(tr("Diagnostic context copied"));
}

void MainWindow::show_about()
{
    AboutDialog dialog(this);
    dialog.exec();
}

void MainWindow::choose_file(QLineEdit* target, const QString& caption, const QString& filter)
{
    const auto path = QFileDialog::getOpenFileName(this, caption, target->text().trimmed(), filter);
    if (!path.isEmpty())
    {
        target->setText(QDir::toNativeSeparators(path));
    }
}

void MainWindow::choose_output_file(QLineEdit* target, const QString& caption)
{
    const auto path = QFileDialog::getSaveFileName(this, caption, target->text().trimmed(),
                                                   tr("Torrent files (*.torrent)"));
    if (!path.isEmpty())
    {
        target->setText(QDir::toNativeSeparators(path));
    }
}

void MainWindow::choose_directory(QLineEdit* target, const QString& caption)
{
    const auto path = QFileDialog::getExistingDirectory(this, caption);
    if (!path.isEmpty())
    {
        target->setText(QDir::toNativeSeparators(path));
    }
}

void MainWindow::update_create_output_path()
{
    const auto input_text = ui_->editCreateInputPath->text().trimmed();
    if (input_text.isEmpty())
    {
        if (create_auto_output_path_ &&
            path_from_text(ui_->editCreateOutputPath->text()).lexically_normal() ==
                create_auto_output_path_->lexically_normal())
        {
            ui_->editCreateOutputPath->clear();
            create_auto_output_path_.reset();
        }
        create_auto_output_path_.reset();
        return;
    }
    auto filename = path_from_text(input_text).filename();
    if (filename.empty())
    {
        return;
    }
    filename += ".torrent";
    set_auto_path(ui_->editCreateOutputPath, create_auto_output_path_,
                  default_save_directory() / filename);
}

void MainWindow::update_modify_output_path(const std::filesystem::path& source)
{
    if (!source.filename().empty())
    {
        const auto stem = QString::fromUtf8(source.stem().u8string().c_str());
        const auto filename = stem + QStringLiteral(".torrent");
        set_auto_path(ui_->editModifyOutputPath, modify_auto_output_path_,
                      default_save_directory() / path_from_text(filename));
    }
}

void MainWindow::update_tracker_output_path()
{
    set_auto_path(ui_->editTrackerOutputPath, tracker_auto_output_path_, default_save_directory());
}

void MainWindow::populate_tracker_defaults()
{
    if (!config_)
    {
        tracker_model_->clear();
        tracker_model_->setHorizontalHeaderLabels({tr("Tier"), tr("Tracker")});
        return;
    }
    auto settings = config_->parsed().defaults;
    const auto preferences = config_->gui_preferences();
    if (preferences.default_preset)
    {
        const auto iterator = config_->parsed().presets.find(*preferences.default_preset);
        if (iterator != config_->parsed().presets.end())
        {
            settings = frontend::overlay_settings(settings, iterator->second);
        }
    }
    populate_tracker_values(
        *tracker_model_, settings.tracker_tiers.value_or(std::vector<std::vector<std::string>>{}));
}

std::filesystem::path MainWindow::default_save_directory() const
{
    auto current = path_from_text(QDir::currentPath());
    if (!config_)
    {
        return current;
    }
    const auto preferences = config_->gui_preferences();
    if (preferences.default_save_location == frontend::GuiSaveLocationMode::Specified &&
        preferences.default_save_path && !preferences.default_save_path->empty())
    {
        return path_from_text(QString::fromStdString(*preferences.default_save_path));
    }
    if (preferences.default_save_location == frontend::GuiSaveLocationMode::Recent &&
        preferences.recent_save_path && !preferences.recent_save_path->empty())
    {
        return path_from_text(QString::fromStdString(*preferences.recent_save_path));
    }
    return current;
}

void MainWindow::remember_save_directory(const std::filesystem::path& directory)
{
    if (!config_ || directory.empty())
    {
        return;
    }
    auto preferences = config_->gui_preferences();
    preferences.recent_save_path = directory.lexically_normal().u8string();
    auto updated = config_->set_gui_preferences(preferences);
    if (!updated)
    {
        NotificationDialog::show_warning(this, tr("Configuration warning"),
                                         error_text(updated.error()));
        return;
    }
    auto saved = config_->save();
    if (!saved)
    {
        NotificationDialog::show_warning(this, tr("Configuration warning"),
                                         error_text(saved.error()));
    }
}

std::optional<CreateOptions> MainWindow::create_options_from_ui()
{
    CreateOptionsInput input;
    switch (ui_->cmbCreateFormat->currentIndex())
    {
    case 1:
        input.format = TorrentFormat::V1;
        break;
    case 2:
        input.format = TorrentFormat::V2;
        break;
    default:
        input.format = TorrentFormat::Hybrid;
        break;
    }
    switch (ui_->cmbCreateFileOrder->currentIndex())
    {
    case 1:
        input.file_order_policy = FileOrderPolicy::CanonicalAlignment;
        break;
    case 2:
        input.file_order_policy = FileOrderPolicy::Natural;
        break;
    case 3:
        input.file_order_policy = FileOrderPolicy::BreadthFirst;
        break;
    default:
        input.file_order_policy = FileOrderPolicy::Lexicographical;
        break;
    }
    if (ui_->cmbCreatePieceSize->currentIndex() == 0)
    {
        input.piece_length_strategy = PieceLengthStrategy::Auto;
    }
    else
    {
        input.piece_length_strategy = PieceLengthStrategy::Fixed;
        input.fixed_piece_length =
            16U * 1024U << static_cast<unsigned>(ui_->cmbCreatePieceSize->currentIndex() - 1);
    }
    input.is_private = ui_->chkCreatePrivate->isChecked();
    QString parse_error;
    auto seeds = parse_web_seeds(ui_->editCreateWebSeeds->toPlainText(), &parse_error);
    if (!seeds)
    {
        NotificationDialog::show_error(this, tr("Invalid web seed"), parse_error);
        return std::nullopt;
    }
    input.web_seeds = std::move(*seeds);
    auto trackers = tracker_list_from_model(*create_tracker_model_, &parse_error);
    if (!trackers)
    {
        NotificationDialog::show_error(this, tr("Invalid tracker list"), parse_error);
        return std::nullopt;
    }
    input.tracker_tiers = trackers->tiers();
    auto options = CreateOptions::create(std::move(input));
    if (!options)
    {
        show_error(options.error());
        return std::nullopt;
    }
    return std::move(options).value();
}

core::Result<frontend::CreationSettingsPatch> MainWindow::creation_patch_from_create_form() const
{
    frontend::CreationSettingsPatch patch;
    const auto& defaults = config_ ? config_->parsed().defaults : frontend::CreationSettingsPatch{};
    const auto current_format = ui_->cmbCreateFormat->currentIndex() == 1   ? TorrentFormat::V1
                                : ui_->cmbCreateFormat->currentIndex() == 2 ? TorrentFormat::V2
                                                                            : TorrentFormat::Hybrid;
    if (current_format != defaults.format.value_or(TorrentFormat::Hybrid))
    {
        patch.format = current_format;
    }
    const auto current_order =
        static_cast<FileOrderPolicy>(ui_->cmbCreateFileOrder->currentIndex());
    if (current_order != defaults.file_order.value_or(FileOrderPolicy::Lexicographical))
    {
        patch.file_order = current_order;
    }
    const auto current_piece =
        ui_->cmbCreatePieceSize->currentIndex() == 0
            ? std::optional<std::uint32_t>{}
            : std::optional<std::uint32_t>(16U * 1024U << static_cast<unsigned>(
                                               ui_->cmbCreatePieceSize->currentIndex() - 1));
    const auto default_piece = defaults.piece_size && defaults.piece_size->fixed_kib
                                   ? defaults.piece_size->fixed_kib
                                   : std::optional<std::uint32_t>{};
    if (current_piece != default_piece)
    {
        patch.piece_size = frontend::PieceSizeSetting{current_piece};
    }
    const auto current_private = ui_->chkCreatePrivate->isChecked();
    if (current_private != defaults.is_private.value_or(false))
    {
        patch.is_private = current_private;
    }

    QString tracker_error;
    auto trackers = tracker_list_from_model(*create_tracker_model_, &tracker_error);
    if (!trackers)
    {
        return core::Result<frontend::CreationSettingsPatch>::failure(
            {ErrorCode::ValidationFailed,
             tracker_error.toUtf8().toStdString(),
             {{"create.trackers", tracker_error.toUtf8().toStdString()}}});
    }
    std::vector<std::vector<std::string>> current_tiers;
    for (const auto& tier : trackers->tiers())
    {
        std::vector<std::string> values;
        for (const auto& tracker : tier.trackers())
        {
            values.push_back(tracker.value());
        }
        current_tiers.push_back(std::move(values));
    }
    if (current_tiers != defaults.tracker_tiers.value_or(std::vector<std::vector<std::string>>{}))
    {
        patch.tracker_tiers = std::move(current_tiers);
    }
    const auto current_seeds = nonempty_lines(ui_->editCreateWebSeeds->toPlainText());
    if (current_seeds != defaults.web_seeds.value_or(std::vector<std::string>{}))
    {
        patch.web_seeds = current_seeds;
    }
    const auto current_comment = optional_text(ui_->editCreateComment->toPlainText());
    if (current_comment != defaults.comment)
    {
        patch.comment = current_comment.value_or(std::string{});
    }
    const auto current_creator = ui_->chkCreateCreator->isChecked()
                                     ? optional_text(ui_->editCreateCreator->text())
                                     : std::optional<std::string>{};
    if (ui_->chkCreateCreator->isChecked() &&
        ui_->editCreateCreator->text().trimmed() != QStringLiteral("TorrentCraft"))
    {
        patch.created_by = current_creator.value_or(std::string{});
    }
    const auto current_source = optional_text(ui_->editCreateSource->text());
    if (current_source != defaults.info_source)
    {
        patch.info_source = current_source.value_or(std::string{});
    }
    return core::Result<frontend::CreationSettingsPatch>::success(std::move(patch));
}

void MainWindow::calculate_create_plan()
{
    if (task_runner_->is_running())
    {
        return;
    }
    const auto options = create_options_from_ui();
    if (!options || ui_->editCreateInputPath->text().trimmed().isEmpty())
    {
        NotificationDialog::show_warning(this, tr("Missing input"),
                                         tr("Select a file or folder before calculating pieces."));
        return;
    }
    const core::CreatePlanRequest request{path_from_text(ui_->editCreateInputPath->text()),
                                          *options};
    auto result = std::make_shared<std::optional<core::Result<core::CreatePlan>>>();
    const auto operation_id = begin_gui_operation(
        "gui", "create_plan",
        {{"input", ui_->editCreateInputPath->text().trimmed().toUtf8().toStdString()}});
    set_busy(true);
    set_status(tr("Calculating pieces"));
    task_runner_->start(
        [this, request, result, operation_id](const core::CancellationToken& token) {
            core::TaskContext context{token, progress_callback(task_runner_.get()), logger_,
                                      operation_id};
            *result = service_.plan_create(request, context);
        },
        [this, result, operation_id] {
            if (!*result)
            {
                finish_gui_operation(operation_id, core::LogLevel::Error, "gui", "create_plan",
                                     "failure", {{"reason", "missing_result"}});
                show_task_error(tr("The creation plan did not return a result."));
            }
            else if (!result->value())
            {
                const auto& error = result->value().error();
                finish_gui_operation(operation_id, core::LogLevel::Error, "gui", "create_plan",
                                     "failure",
                                     {{"code", std::to_string(static_cast<int>(error.code))},
                                      {"message", error.message}});
                show_error(error);
            }
            else
            {
                const auto& plan = result->value().value();
                finish_gui_operation(operation_id, core::LogLevel::Info, "gui", "create_plan",
                                     "finish",
                                     {{"piece_count", std::to_string(plan.piece_count)},
                                      {"piece_length", std::to_string(plan.piece_length)}});
                ui_->lblCreatePieces->setText(tr("Pieces: %1 | Piece length: %2")
                                                  .arg(static_cast<qulonglong>(plan.piece_count))
                                                  .arg(format_bytes_iec(plan.piece_length)));
                set_status(tr("Ready"));
            }
        });
}

void MainWindow::create_torrent()
{
    if (ui_->chkCreateDryRun->isChecked())
    {
        calculate_create_plan();
        return;
    }
    if (task_runner_->is_running())
    {
        return;
    }
    const auto options = create_options_from_ui();
    const auto input_text = ui_->editCreateInputPath->text().trimmed();
    if (!options || input_text.isEmpty())
    {
        NotificationDialog::show_warning(this, tr("Missing path"),
                                         tr("Select content before creating a torrent."));
        return;
    }
    auto output_text = ui_->editCreateOutputPath->text().trimmed();
    if (output_text.isEmpty())
    {
        const auto directory = default_save_directory();
        const auto input_path = path_from_text(input_text);
        auto filename = input_path.filename();
        if (filename.empty())
        {
            filename = input_path.filename();
        }
        if (filename.empty())
        {
            filename = "torrent";
        }
        filename += ".torrent";
        output_text = path_to_text(directory / filename);
        ui_->editCreateOutputPath->setText(output_text);
    }
    if (output_text.isEmpty())
    {
        NotificationDialog::show_warning(this, tr("Missing output"),
                                         tr("Select a torrent output path."));
        return;
    }
    core::CreationMetadataInput metadata;
    metadata.comment = optional_text(ui_->editCreateComment->toPlainText());
    if (ui_->chkCreateCreator->isChecked())
    {
        metadata.created_by = ui_->editCreateCreator->text().trimmed().toUtf8().toStdString();
    }
    else
    {
        metadata.created_by = config_ && config_->parsed().defaults.created_by
                                  ? config_->parsed().defaults.created_by
                                  : std::optional<std::string>("TorrentCraft");
    }
    if (ui_->chkCreateDate->isChecked())
    {
        metadata.creation_time_unix_seconds =
            ui_->dateCreateCreation->dateTime().toSecsSinceEpoch();
    }
    else
    {
        metadata.creation_time_unix_seconds = clock_.now_unix_seconds();
    }
    core::CreateInfoInput info;
    info.source = optional_text(ui_->editCreateSource->text());
    const auto disk_io = config_ ? config_->parsed().disk_io : std::nullopt;
    const core::CreateRequest request{path_from_text(input_text),
                                      path_from_text(output_text),
                                      *options,
                                      ui_->chkCreateOverwrite->isChecked(),
                                      std::move(metadata),
                                      std::move(info),
                                      disk_io};
    const auto operation_id = begin_gui_operation(
        "gui", "create",
        {{"input", input_text.toUtf8().toStdString()},
         {"output", output_text.toUtf8().toStdString()},
         {"overwrite", ui_->chkCreateOverwrite->isChecked() ? "true" : "false"}});
    auto result = std::make_shared<std::optional<core::Result<core::CreateResult>>>();
    set_busy(true);
    set_status(tr("Creating torrent"));
    task_runner_->start(
        [this, request, result, operation_id](const core::CancellationToken& token) {
            core::TaskContext context{token, progress_callback(task_runner_.get()), logger_,
                                      operation_id};
            *result = service_.create(request, context);
        },
        [this, result, operation_id] {
            if (!*result)
            {
                finish_gui_operation(operation_id, core::LogLevel::Error, "gui", "create",
                                     "failure", {{"reason", "missing_result"}});
                show_task_error(tr("The creation task did not return a result."));
            }
            else if (!result->value())
            {
                const auto& error = result->value().error();
                const auto event = error.code == ErrorCode::Cancelled ? "cancel" : "failure";
                finish_gui_operation(operation_id,
                                     error.code == ErrorCode::Cancelled ? core::LogLevel::Warning
                                                                        : core::LogLevel::Error,
                                     "gui", "create", event,
                                     {{"code", std::to_string(static_cast<int>(error.code))},
                                      {"message", error.message}});
                show_error(error);
            }
            else
            {
                const auto& created = result->value().value();
                finish_gui_operation(operation_id, core::LogLevel::Info, "gui", "create", "finish",
                                     {{"target", created.target_path.u8string()},
                                      {"payload_bytes", std::to_string(created.payload_bytes)},
                                      {"piece_length", std::to_string(created.piece_length)}});
                NotificationDialog::show_info(
                    this, tr("Torrent created"),
                    tr("Created %1\n%2 pieces.")
                        .arg(path_to_text(created.target_path))
                        .arg(static_cast<qulonglong>(
                            (created.payload_bytes + created.piece_length - 1U) /
                            created.piece_length)));
                remember_save_directory(created.target_path.parent_path());
                set_status(tr("Ready"));
            }
        });
}

void MainWindow::load_inspect_torrent()
{
    if (task_runner_->is_running() || ui_->editInspectTorrentPath->text().trimmed().isEmpty())
    {
        return;
    }
    const auto source = path_from_text(ui_->editInspectTorrentPath->text());
    inspect_loaded_.reset();
    inspect_tracker_model_->clear();
    const auto mode =
        ui_->chkInspectStrict->isChecked() ? core::LoadMode::Strict : core::LoadMode::Lenient;
    const auto operation_id = begin_gui_operation(
        "gui", "inspect",
        {{"path", ui_->editInspectTorrentPath->text().trimmed().toUtf8().toStdString()},
         {"strict", mode == core::LoadMode::Strict ? "true" : "false"}});
    auto loaded = std::make_shared<std::optional<core::Result<core::LoadedTorrent>>>();
    auto inspection = std::make_shared<std::optional<core::Result<core::InspectionReport>>>();
    set_busy(true);
    set_status(tr("Loading torrent"));
    task_runner_->start(
        [this, source, mode, loaded, inspection,
         operation_id](const core::CancellationToken& token) {
            *loaded = service_.load(source, {mode});
            if (!loaded->value())
            {
                return;
            }
            *inspection = service_.inspect(loaded->value().value().document(),
                                           {token, {}, logger_, operation_id});
        },
        [this, loaded, inspection, operation_id] {
            if (!*loaded)
            {
                finish_gui_operation(operation_id, core::LogLevel::Error, "gui", "inspect",
                                     "failure", {{"reason", "missing_result"}});
                show_task_error(tr("The inspection task did not return a result."));
            }
            else if (!loaded->value())
            {
                const auto& error = loaded->value().error();
                finish_gui_operation(
                    operation_id, core::LogLevel::Error, "gui", "inspect", "failure",
                    {{"code", error_code_name(error.code)}, {"message", error.message}});
                show_error(error);
            }
            else
            {
                inspect_loaded_ = loaded->value().value();
                populate_inspect_fields(*inspect_loaded_);
                if (inspection->has_value() && !inspection->value())
                {
                    const auto& error = inspection->value().error();
                    finish_gui_operation(
                        operation_id, core::LogLevel::Error, "gui", "inspect", "failure",
                        {{"code", error_code_name(error.code)}, {"message", error.message}});
                    show_error(error);
                    return;
                }
                else if (inspection->has_value())
                {
                    const auto supported = inspection->value().value().verification_capability ==
                                           core::VerificationCapability::Supported;
                    ui_->lblInspectCapabilityValue->setText(supported ? tr("Supported")
                                                                      : tr("Unsupported"));
                    for (const auto& diagnostic : inspection->value().value().diagnostics)
                    {
                        ui_->treeInspectDiagnostics->addTopLevelItem(
                            new QTreeWidgetItem({QString::fromStdString(diagnostic.message)}));
                    }
                }
                finish_gui_operation(
                    operation_id, core::LogLevel::Info, "gui", "inspect", "finish",
                    {{"file_count",
                      std::to_string(inspect_loaded_->document().info().files().size())},
                     {"diagnostic_count",
                      inspection->has_value()
                          ? std::to_string(inspection->value().value().diagnostics.size())
                          : "0"}});
                set_status(tr("Ready"));
            }
        });
}

void MainWindow::validate_inspect_torrent()
{
    if (!inspect_loaded_ || task_runner_->is_running())
    {
        return;
    }
    auto result = std::make_shared<std::optional<core::Result<core::InspectionReport>>>();
    const auto operation_id = begin_gui_operation(
        "gui", "inspect_validate",
        {{"file_count", std::to_string(inspect_loaded_->document().info().files().size())}});
    set_busy(true);
    task_runner_->start(
        [this, result, operation_id](const core::CancellationToken& token) {
            *result =
                service_.inspect(inspect_loaded_->document(), {token, {}, logger_, operation_id});
        },
        [this, result, operation_id] {
            if (!*result)
            {
                finish_gui_operation(operation_id, core::LogLevel::Error, "gui", "inspect_validate",
                                     "failure", {{"reason", "missing_result"}});
                show_task_error(tr("The validation task did not return a result."));
            }
            else if (!result->value())
            {
                const auto& error = result->value().error();
                finish_gui_operation(
                    operation_id, core::LogLevel::Error, "gui", "inspect_validate", "failure",
                    {{"code", error_code_name(error.code)}, {"message", error.message}});
                show_error(error);
            }
            else
            {
                ui_->lblInspectCapabilityValue->setText(
                    result->value().value().verification_capability ==
                            core::VerificationCapability::Supported
                        ? tr("Supported")
                        : tr("Unsupported"));
                finish_gui_operation(
                    operation_id, core::LogLevel::Info, "gui", "inspect_validate", "finish",
                    {{"diagnostic_count",
                      std::to_string(result->value().value().diagnostics.size())}});
                set_status(tr("Ready"));
            }
        });
}

void MainWindow::populate_inspect_fields(const core::LoadedTorrent& loaded)
{
    const auto& info = loaded.document().info();
    const auto show_padding = config_ && config_->gui_preferences().show_padding_files;
    const auto visible_files =
        std::count_if(info.files().begin(), info.files().end(), [show_padding](const auto& file) {
            return show_padding || !file.attributes().padding;
        });
    ui_->lblInspectNameValue->setText(QString::fromStdString(info.name()));
    ui_->lblInspectFormatValue->setText(format_name(info.format()));
    ui_->lblInspectPayloadBytesValue->setText(QString::number(info.pieces().total_size()));
    ui_->lblInspectPieceLengthValue->setText(QString::number(info.pieces().piece_length()));
    ui_->lblInspectFileCountValue->setText(QString::number(static_cast<qulonglong>(visible_files)));
    ui_->lblInspectPrivateValue->setText(info.is_private() ? tr("Yes") : tr("No"));
    ui_->lblInspectCreatedByValue->setText(optional_string(loaded.document().metadata().creator()));
    ui_->lblInspectInfoHashV1Value->setText(optional_digest(info.info_hashes().v1(), tr("N/A")));
    ui_->lblInspectInfoHashV2Value->setText(optional_digest(info.info_hashes().v2(), tr("N/A")));
    inspect_file_model_->set_files(info.files(), show_padding);
    populate_tracker_model(*inspect_tracker_model_, loaded.document().trackers());
    configure_tracker_table(*ui_->tblInspectTrackers);
    ui_->treeInspectFileTree->collapseAll();
    ui_->treeInspectMetadataFields->clear();
    for (const auto& field : loaded.document().metadata_field_values())
    {
        const auto scope = field.scope == core::MetadataFieldScope::TopLevel ? tr("Top-level")
                           : field.scope == core::MetadataFieldScope::Info   ? tr("Info")
                                                                             : tr("Other");
        auto* item = new QTreeWidgetItem({QString::fromStdString(field.key),
                                          QString::fromStdString(field.value), scope,
                                          QString::fromStdString(field.type)});
        ui_->treeInspectMetadataFields->addTopLevelItem(item);
    }
    ui_->treeInspectDiagnostics->clear();
    for (const auto& warning : loaded.document().warnings())
    {
        ui_->treeInspectDiagnostics->addTopLevelItem(
            new QTreeWidgetItem({QString::fromStdString(warning.field + ": " + warning.message)}));
    }
}

void MainWindow::load_modify_torrent()
{
    if (task_runner_->is_running() || ui_->editModifyTorrentPath->text().trimmed().isEmpty())
    {
        return;
    }
    modify_loaded_.reset();
    const auto source = path_from_text(ui_->editModifyTorrentPath->text());
    const auto mode =
        ui_->chkModifyStrict->isChecked() ? core::LoadMode::Strict : core::LoadMode::Lenient;
    auto result = std::make_shared<std::optional<core::Result<core::LoadedTorrent>>>();
    const auto operation_id = begin_gui_operation(
        "gui", "modify_load",
        {{"path", ui_->editModifyTorrentPath->text().trimmed().toUtf8().toStdString()},
         {"strict", mode == core::LoadMode::Strict ? "true" : "false"}});
    set_busy(true);
    set_status(tr("Loading torrent"));
    task_runner_->start(
        [this, source, mode, result, operation_id](const core::CancellationToken&) {
            *result = service_.load(source, {mode});
        },
        [this, result, operation_id] {
            if (!*result)
            {
                finish_gui_operation(operation_id, core::LogLevel::Error, "gui", "modify_load",
                                     "failure", {{"reason", "missing_result"}});
                show_task_error(tr("The modify task did not return a result."));
            }
            else if (!result->value())
            {
                const auto& error = result->value().error();
                finish_gui_operation(
                    operation_id, core::LogLevel::Error, "gui", "modify_load", "failure",
                    {{"code", error_code_name(error.code)}, {"message", error.message}});
                show_error(error);
            }
            else
            {
                modify_loaded_ = result->value().value();
                populate_modify_fields(*modify_loaded_);
                finish_gui_operation(
                    operation_id, core::LogLevel::Info, "gui", "modify_load", "finish",
                    {{"file_count",
                      std::to_string(modify_loaded_->document().info().files().size())}});
                set_status(tr("Ready"));
            }
        });
}

void MainWindow::populate_modify_fields(const core::LoadedTorrent& loaded)
{
    const auto& document = loaded.document();
    const auto& info = document.info();
    const auto& metadata = document.metadata();
    ui_->editModifyTorrentPath->setText(path_to_text(loaded.source_path()));
    update_modify_output_path(loaded.source_path());
    ui_->editModifyName->setText(QString::fromStdString(info.name()));
    ui_->editModifySource->setText(optional_string(metadata.source()));
    ui_->editModifyComment->setPlainText(optional_string(metadata.comment()));
    ui_->editModifyCreator->setText(optional_string(metadata.creator()));
    ui_->chkModifyPrivate->setChecked(info.is_private());
    ui_->chkModifyCreator->setChecked(is_custom_created_by(metadata.creator()));
    ui_->editModifyCreator->setEnabled(ui_->chkModifyCreator->isChecked());
    const auto creation_time = metadata.creation_time_unix_seconds();
    ui_->chkModifyDate->setChecked(creation_time.has_value());
    if (creation_time.has_value())
    {
        ui_->dateModifyCreation->setDateTime(QDateTime::fromSecsSinceEpoch(creation_time.value()));
    }
    QStringList web_seeds;
    for (const auto& seed : metadata.web_seeds())
    {
        web_seeds << QString::fromStdString(seed.value());
    }
    ui_->editModifyWebSeeds->setPlainText(web_seeds.join(QLatin1Char('\n')));
    QStringList dht_nodes;
    for (const auto& node : metadata.dht_nodes())
    {
        dht_nodes
            << QStringLiteral("%1:%2").arg(QString::fromStdString(node.host())).arg(node.port());
    }
    ui_->editModifyDhtNodes->setPlainText(dht_nodes.join(QLatin1Char('\n')));
    for (auto* check_box :
         {ui_->chkModifyClearComment, ui_->chkModifyClearCreator, ui_->chkModifyClearSource,
          ui_->chkModifyClearDate, ui_->chkModifyClearWebSeeds, ui_->chkModifyClearDhtNodes})
    {
        check_box->setChecked(false);
    }
    populate_tracker_model(*modify_tracker_model_, document.trackers());
    configure_tracker_table(*ui_->tblModifyTrackers);
}

void MainWindow::preview_modify()
{
    save_modify(true);
}

void MainWindow::save_modify(const bool preview_only)
{
    if (!modify_loaded_ || task_runner_->is_running())
    {
        return;
    }
    const auto& original_document = modify_loaded_->document();
    const auto& original_metadata = original_document.metadata();
    std::vector<core::EditAction> actions;
    QStringList changed_fields;
    const auto requested_comment = optional_text(ui_->editModifyComment->toPlainText());
    if (ui_->chkModifyClearComment->isChecked())
    {
        if (original_metadata.comment().has_value())
        {
            changed_fields << tr("Comment");
            actions.emplace_back(core::ClearComment{});
        }
    }
    else if (requested_comment && requested_comment != original_metadata.comment())
    {
        changed_fields << tr("Comment");
        actions.emplace_back(core::SetComment{*requested_comment});
    }
    const auto requested_creator = ui_->chkModifyCreator->isChecked()
                                       ? std::optional<std::string>{ui_->editModifyCreator->text()
                                                                        .trimmed()
                                                                        .toUtf8()
                                                                        .toStdString()}
                                       : std::nullopt;
    if (ui_->chkModifyClearCreator->isChecked())
    {
        if (original_metadata.creator().has_value())
        {
            changed_fields << tr("Created by");
            actions.emplace_back(core::ClearCreator{});
        }
    }
    else if (ui_->chkModifyCreator->isChecked() && requested_creator != original_metadata.creator())
    {
        changed_fields << tr("Created by");
        actions.emplace_back(core::SetCreator{requested_creator.value_or(std::string{})});
    }
    const auto requested_source = optional_text(ui_->editModifySource->text());
    if (ui_->chkModifyClearSource->isChecked())
    {
        if (original_metadata.source().has_value())
        {
            changed_fields << tr("Source");
            actions.emplace_back(core::ClearInfoSource{});
        }
    }
    else if (requested_source && requested_source != original_metadata.source())
    {
        changed_fields << tr("Source");
        actions.emplace_back(core::SetInfoSource{*requested_source});
    }
    const auto requested_name = ui_->editModifyName->text().trimmed().toUtf8().toStdString();
    if (!requested_name.empty() && requested_name != original_document.info().name())
    {
        changed_fields << tr("Name");
        actions.emplace_back(core::SetName{requested_name});
    }
    if (ui_->chkModifyPrivate->isChecked() != modify_loaded_->document().info().is_private())
    {
        changed_fields << tr("Private");
    }
    actions.emplace_back(core::SetPrivate{ui_->chkModifyPrivate->isChecked()});
    if (ui_->chkModifyClearDate->isChecked())
    {
        changed_fields << tr("Creation time");
        actions.emplace_back(core::ClearCreationTime{});
    }
    else if (ui_->chkModifyDate->isChecked())
    {
        changed_fields << tr("Creation time");
        actions.emplace_back(
            core::SetCreationTime{ui_->dateModifyCreation->dateTime().toSecsSinceEpoch()});
    }
    QString parse_error;
    auto seeds = parse_web_seeds(ui_->editModifyWebSeeds->toPlainText(), &parse_error);
    if (!seeds)
    {
        NotificationDialog::show_error(this, tr("Invalid web seed"), parse_error);
        return;
    }
    if (ui_->chkModifyClearWebSeeds->isChecked() || !seeds->empty())
    {
        changed_fields << tr("Web seeds");
        actions.emplace_back(core::ReplaceWebSeeds{std::move(*seeds)});
    }
    auto dht_nodes = parse_dht_nodes(ui_->editModifyDhtNodes->toPlainText(), &parse_error);
    if (!dht_nodes)
    {
        NotificationDialog::show_error(this, tr("Invalid DHT nodes"), parse_error);
        return;
    }
    if (ui_->chkModifyClearDhtNodes->isChecked() || !dht_nodes->empty())
    {
        changed_fields << tr("DHT nodes");
        actions.emplace_back(core::ReplaceDhtNodes{std::move(*dht_nodes)});
    }
    QString tracker_error;
    auto trackers = tracker_list_from_model(*modify_tracker_model_, &tracker_error);
    if (!trackers)
    {
        NotificationDialog::show_error(this, tr("Invalid tracker list"), tracker_error);
        return;
    }
    changed_fields << tr("Trackers");
    actions.emplace_back(core::ReplaceTrackers{std::move(*trackers)});

    struct Outcome
    {
        std::optional<core::EditResult> edit;
        std::optional<core::SaveResult> save;
        std::optional<Error> error;
    };
    auto outcome = std::make_shared<Outcome>();
    const auto dry_run = preview_only || ui_->chkModifyDryRun->isChecked();
    core::SaveRequest save_request;
    const auto destination = ui_->editModifyOutputPath->text().trimmed();
    if (!destination.isEmpty())
    {
        save_request.mode = core::SaveTargetMode::NewPath;
        save_request.destination = path_from_text(destination);
        save_request.allow_overwrite = ui_->chkModifyOverwrite->isChecked();
    }
    save_request.backup = ui_->chkModifyBackup->isChecked();
    const auto operation_id =
        begin_gui_operation("gui", "modify",
                            {{"dry_run", dry_run ? "true" : "false"},
                             {"destination", destination.toUtf8().toStdString()},
                             {"overwrite", ui_->chkModifyOverwrite->isChecked() ? "true" : "false"},
                             {"backup", ui_->chkModifyBackup->isChecked() ? "true" : "false"},
                             {"changed_field_count", std::to_string(changed_fields.size())}});
    set_busy(true);
    set_status(tr("Applying changes"));
    task_runner_->start(
        [this, actions = std::move(actions), outcome, dry_run, save_request,
         operation_id](const core::CancellationToken& token) {
            auto edited = service_.edit(*modify_loaded_, actions);
            if (!edited)
            {
                outcome->error = edited.error();
                return;
            }
            outcome->edit = std::move(edited).value();
            if (dry_run || outcome->edit->disposition != core::EditDisposition::Applied)
            {
                return;
            }
            core::TaskContext context{token, progress_callback(task_runner_.get()), logger_,
                                      operation_id};
            auto saved = service_.save(outcome->edit->loaded, save_request, context);
            if (!saved)
            {
                outcome->error = saved.error();
                return;
            }
            outcome->save = std::move(saved).value();
        },
        [this, outcome, dry_run, changed_fields, operation_id] {
            if (outcome->error)
            {
                finish_gui_operation(
                    operation_id,
                    outcome->error->code == ErrorCode::Cancelled ? core::LogLevel::Warning
                                                                 : core::LogLevel::Error,
                    "gui", "modify",
                    outcome->error->code == ErrorCode::UnsupportedFeature ? "rebuild_required"
                    : outcome->error->code == ErrorCode::Cancelled        ? "cancel"
                                                                          : "failure",
                    {{"code", error_code_name(outcome->error->code)},
                     {"message", outcome->error->message}});
                if (outcome->error->code == ErrorCode::UnsupportedFeature)
                {
                    NotificationDialog::show_warning(this, tr("Rebuild required"),
                                                     error_text(*outcome->error));
                }
                else
                {
                    show_error(*outcome->error);
                }
                return;
            }
            if (!outcome->edit)
            {
                finish_gui_operation(operation_id, core::LogLevel::Error, "gui", "modify",
                                     "failure", {{"reason", "missing_edit_result"}});
                show_task_error(tr("No edit result was produced."));
                return;
            }
            if (outcome->edit->disposition == core::EditDisposition::NeedRebuild)
            {
                finish_gui_operation(operation_id, core::LogLevel::Warning, "gui", "modify",
                                     "rebuild_required", {{"reason", "identity_change"}});
                NotificationDialog::show_warning(
                    this, tr("Rebuild required"),
                    tr("These changes affect the torrent identity and require a "
                       "rebuild. No file "
                       "was written; your draft is preserved."));
                return;
            }
            if (outcome->save)
            {
                finish_gui_operation(
                    operation_id, core::LogLevel::Info, "gui", "modify", "finish",
                    {{"changed_field_count", std::to_string(changed_fields.size())}});
                modify_loaded_ = outcome->save->loaded;
                NotificationDialog::show_info(this, tr("Torrent saved"),
                                              tr("The torrent was saved successfully."));
                remember_save_directory(modify_loaded_->source_path().parent_path());
            }
            else if (dry_run)
            {
                finish_gui_operation(
                    operation_id, core::LogLevel::Info, "gui", "modify", "finish",
                    {{"preview", "true"},
                     {"changed_field_count", std::to_string(changed_fields.size())}});
                const auto summary = changed_fields.isEmpty()
                                         ? tr("No editable fields changed.")
                                         : tr("Fields to modify:\n%1")
                                               .arg(changed_fields.join(QStringLiteral("\n")));
                NotificationDialog::show_info(this, tr("Preview"), summary);
            }
            set_status(tr("Ready"));
        });
}

void MainWindow::invalidate_verify_torrent()
{
    verify_loaded_.reset();
    verify_file_model_->clear();
    ui_->progVerifyProgress->setValue(0);
}

void MainWindow::load_verify_torrent()
{
    const auto path_text = ui_->editVerifyTorrentPath->text().trimmed();
    if (path_text.isEmpty())
    {
        invalidate_verify_torrent();
        return;
    }
    if (task_runner_->is_running() || verify_loaded_)
    {
        return;
    }

    auto loaded = std::make_shared<std::optional<core::Result<core::LoadedTorrent>>>();
    const auto path = path_from_text(path_text);
    const auto operation_id =
        begin_gui_operation("gui", "verify_load", {{"path", path_text.toUtf8().toStdString()}});
    set_busy(true);
    set_status(tr("Loading verification torrent"));
    task_runner_->start(
        [this, loaded, path, operation_id](const core::CancellationToken&) {
            *loaded = service_.load(path);
        },
        [this, loaded, path_text, operation_id] {
            if (ui_->editVerifyTorrentPath->text().trimmed() != path_text)
            {
                finish_gui_operation(operation_id, core::LogLevel::Warning, "gui", "verify_load",
                                     "skip", {{"reason", "path_changed"}});
                return;
            }
            if (!*loaded)
            {
                finish_gui_operation(operation_id, core::LogLevel::Error, "gui", "verify_load",
                                     "failure", {{"reason", "missing_result"}});
                show_task_error(tr("The verification torrent did not return a load result."));
                return;
            }
            if (!loaded->value())
            {
                const auto& error = loaded->value().error();
                finish_gui_operation(
                    operation_id, core::LogLevel::Error, "gui", "verify_load", "failure",
                    {{"code", error_code_name(error.code)}, {"message", error.message}});
                show_error(error);
                set_status(tr("Verification torrent could not be loaded"));
                return;
            }
            verify_loaded_ = loaded->value().value();
            const auto show_padding = config_ && config_->gui_preferences().show_padding_files;
            verify_file_model_->set_files(verify_loaded_->document().info().files(), show_padding);
            ui_->treeVerifyFileList->collapseAll();
            finish_gui_operation(
                operation_id, core::LogLevel::Info, "gui", "verify_load", "finish",
                {{"file_count", std::to_string(verify_loaded_->document().info().files().size())}});
            set_status(tr("Verification torrent loaded"));
        });
}

void MainWindow::start_verify()
{
    if (task_runner_->is_running() || ui_->editVerifyTorrentPath->text().trimmed().isEmpty() ||
        ui_->editVerifyContentPath->text().trimmed().isEmpty())
    {
        NotificationDialog::show_warning(this, tr("Missing path"),
                                         tr("Select both a torrent and its content folder."));
        return;
    }
    if (!verify_loaded_)
    {
        load_verify_torrent();
        return;
    }

    const auto disk_io = config_ ? config_->parsed().disk_io : std::nullopt;
    const auto verify_settings =
        config_ ? config_->parsed().verify.value_or(frontend::VerifyResourceSettings{})
                : frontend::VerifyResourceSettings{};
    auto budget = frontend::resolve_verify_resource_budget(verify_settings);
    if (!budget)
    {
        show_error(budget.error());
        return;
    }
    auto resource_budget = std::move(budget).value();
    const auto hashing_workers =
        resource_budget ? std::to_string(resource_budget->hashing_workers()) : "unset";
    const auto checking_memory_bytes =
        resource_budget ? std::to_string(resource_budget->checking_memory_bytes()) : "unset";
    const auto operation_id = begin_gui_operation(
        "gui", "verify",
        {{"torrent", ui_->editVerifyTorrentPath->text().trimmed().toUtf8().toStdString()},
         {"content", ui_->editVerifyContentPath->text().trimmed().toUtf8().toStdString()},
         {"disk_io", disk_io && *disk_io == core::DiskIoMode::Posix ? "posix" : "mmap"},
         {"hashing_workers", hashing_workers},
         {"checking_memory_bytes", checking_memory_bytes},
         {"piece_length",
          std::to_string(verify_loaded_->document().info().pieces().piece_length())}});

    struct Outcome
    {
        std::optional<core::VerificationReport> report;
        std::optional<Error> error;
    };
    auto outcome = std::make_shared<Outcome>();
    const auto document = verify_loaded_->document();
    const auto content = path_from_text(ui_->editVerifyContentPath->text());
    const auto piece_length = document.info().pieces().piece_length();
    const auto total_bytes = document.info().pieces().total_size();
    verify_total_pieces_ = piece_length == 0U ? 0U
                                              : total_bytes / piece_length +
                                                    (total_bytes % piece_length == 0U ? 0U : 1U);
    verify_completed_pieces_ = 0U;
    ui_->progVerifyProgress->setMaximum(100);
    ui_->progVerifyProgress->setValue(0);
    verify_file_model_->reset_verification();
    verification_running_ = true;
    set_busy(true);
    set_status(tr("Verifying"));
    task_runner_->start(
        [this, outcome, document, content, resource_budget, total_bytes, disk_io,
         operation_id](const core::CancellationToken& token) {
            auto on_progress = [this, total_bytes](const core::VerificationProgress& progress) {
                auto snapshot = progress;
                // Qt owns and releases the queued functor; clang-analyzer cannot model it.
                QMetaObject::invokeMethod( // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
                    this,
                    [this, snapshot = std::move(snapshot), total_bytes] {
                        if (!verification_running_)
                        {
                            return;
                        }
                        verify_file_model_->apply_verification_progress(snapshot);
                        for (const auto& range : snapshot.piece_ranges)
                        {
                            verify_completed_pieces_ += range.end - range.begin;
                        }
                        const auto totals = verify_file_model_->verification_totals();
                        const auto expected =
                            totals.expected_bytes > 0U ? totals.expected_bytes : total_bytes;
                        const auto percent =
                            expected == 0U ? 0ULL
                                           : static_cast<qulonglong>((std::min)(
                                                 100.0, static_cast<double>(totals.hashed_bytes) *
                                                            100.0 / static_cast<double>(expected)));
                        ui_->progVerifyProgress->setValue(static_cast<int>(percent));
                        update_progress_status(QStringLiteral("hashing"), verify_completed_pieces_,
                                               verify_total_pieces_, totals.hashed_bytes, expected);
                    },
                    Qt::QueuedConnection);
            };
            core::VerifyRequest request{document, content, std::move(on_progress), resource_budget,
                                        disk_io};
            core::TaskContext context{token, progress_callback(task_runner_.get()), logger_,
                                      operation_id};
            auto verified = service_.verify(request, context);
            if (!verified)
            {
                outcome->error = verified.error();
                return;
            }
            outcome->report = std::move(verified).value();
        },
        [this, outcome, operation_id, content] {
            verification_running_ = false;
            if (outcome->error)
            {
                const auto& error = *outcome->error;
                if (error.code == ErrorCode::Cancelled)
                {
                    finish_gui_operation(
                        operation_id, core::LogLevel::Warning, "gui", "verify", "cancel",
                        {{"code", error_code_name(error.code)}, {"message", error.message}});
                    verify_file_model_->mark_verification_cancelled();
                    set_status(tr("Verification cancelled"));
                    return;
                }
                verify_file_model_->mark_verification_interrupted();
                finish_gui_operation(
                    operation_id, core::LogLevel::Error, "gui", "verify", "failure",
                    {{"code", error_code_name(error.code)}, {"message", error.message}});
                show_error(error);
                set_status(tr("Verification interrupted"));
                return;
            }
            if (!outcome->report)
            {
                finish_gui_operation(operation_id, core::LogLevel::Error, "gui", "verify",
                                     "failure", {{"reason", "missing_report"}});
                verify_file_model_->mark_verification_interrupted();
                show_task_error(tr("No verification report was produced."));
                return;
            }

            const auto& report = *outcome->report;
            std::size_t failure_count = 0;
            std::size_t suppressed_count = 0;
            std::size_t logged_count = 0;
            for (const auto& file : report.files)
            {
                if (file.findings == core::FileVerificationFinding::None)
                {
                    continue;
                }
                ++failure_count;
                if (logged_count >= 32U)
                {
                    ++suppressed_count;
                    continue;
                }
                const auto full_path =
                    (content / std::filesystem::u8path(file.path.to_string())).u8string();
                logger_->log_event(core::LogLevel::Warning, "gui", "verify_file", "failure",
                                   {{"operation_id", operation_id},
                                    {"path", full_path},
                                    {"findings", finding_names(file.findings)},
                                    {"expected_bytes", std::to_string(file.expected_bytes)},
                                    {"hashed_bytes", std::to_string(file.hashed_bytes)},
                                    {"verified_bytes", std::to_string(file.verified_bytes)},
                                    {"mismatched_bytes", std::to_string(file.mismatched_bytes)}});
                ++logged_count;
            }
            const auto matched = report.outcome == core::VerificationOutcome::Verified;
            finish_gui_operation(operation_id,
                                 matched ? core::LogLevel::Info : core::LogLevel::Warning, "gui",
                                 "verify", "finish",
                                 {{"outcome", verification_outcome_name(report.outcome)},
                                  {"expected_bytes", std::to_string(report.expected_bytes)},
                                  {"hashed_bytes", std::to_string(report.hashed_bytes)},
                                  {"verified_bytes", std::to_string(report.verified_bytes)},
                                  {"mismatched_bytes", std::to_string(report.mismatched_bytes)},
                                  {"file_count", std::to_string(report.files.size())},
                                  {"failure_file_count", std::to_string(failure_count)},
                                  {"suppressed_count", std::to_string(suppressed_count)}});
            verify_file_model_->apply_verification_report(report);
            ui_->progVerifyProgress->setValue(100);
            NotificationDialog::show_info(
                this, tr("Verification complete"),
                matched ? tr("All pieces verified.")
                        : tr("Verification found mismatches or incomplete data."));
            set_status(tr("Ready"));
        });
}

void MainWindow::load_tracker_torrent()
{
    if (task_runner_->is_running() || ui_->editTrackerSourcePath->text().trimmed().isEmpty())
    {
        return;
    }
    const auto source = path_from_text(ui_->editTrackerSourcePath->text());
    const auto operation_id = begin_gui_operation(
        "gui", "tracker_load",
        {{"path", ui_->editTrackerSourcePath->text().trimmed().toUtf8().toStdString()}});
    update_tracker_output_path();
    std::error_code error;
    if (std::filesystem::is_directory(source, error))
    {
        tracker_source_directory_ = source.lexically_normal();
        tracker_loaded_.reset();
        ui_->tblTrackerTorrents->setModel(nullptr);
        auto* files = new QStandardItemModel(this);
        files->setHorizontalHeaderLabels({tr("Filename"), tr("Size"), tr("Status")});
        for (const auto& entry : std::filesystem::directory_iterator(source, error))
        {
            const auto& path = entry.path();
            std::error_code entry_error;
            if (error || !entry.is_regular_file(entry_error) || entry_error ||
                path.extension() != ".torrent")
            {
                continue;
            }

            std::error_code size_error;
            const auto size = entry.file_size(size_error);
            auto* name_item =
                new QStandardItem(QString::fromUtf8(path.filename().u8string().c_str()));
            name_item->setData(path_to_text(path), Qt::UserRole);
            auto* size_item = new QStandardItem(size_error ? QString() : format_bytes_iec(size));
            auto* status_item = new QStandardItem(size_error ? tr("Unavailable") : tr("Ready"));
            files->appendRow({name_item, size_item, status_item});
        }
        ui_->tblTrackerTorrents->setModel(files);
        if (error)
        {
            finish_gui_operation(
                operation_id, core::LogLevel::Error, "gui", "tracker_load", "failure",
                {{"reason", "directory_enumeration"}, {"message", error.message()}});
            show_error({ErrorCode::IoFailure,
                        "failed to enumerate tracker source directory: " + error.message(),
                        {}});
            return;
        }
        auto* file_header = ui_->tblTrackerTorrents->horizontalHeader();
        file_header->setStretchLastSection(false);
        file_header->setSectionResizeMode(0, QHeaderView::Stretch);
        file_header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        file_header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        tracker_model_.reset(new QStandardItemModel(this));
        tracker_model_->setHorizontalHeaderLabels({tr("Tier"), tr("Tracker")});
        ui_->tblTrackerTiers->setModel(tracker_model_.get());
        populate_tracker_defaults();
        configure_tracker_table(*ui_->tblTrackerTiers);
        finish_gui_operation(
            operation_id, core::LogLevel::Info, "gui", "tracker_load", "finish",
            {{"file_count", std::to_string(files->rowCount())}, {"source_kind", "directory"}});
        set_status(tr("Torrent folder loaded; select one file for tracker editing."));
        return;
    }
    if (!tracker_source_directory_ ||
        tracker_source_directory_->lexically_normal() != source.parent_path().lexically_normal())
    {
        tracker_source_directory_.reset();
    }
    auto result = std::make_shared<std::optional<core::Result<core::LoadedTorrent>>>();
    set_busy(true);
    set_status(tr("Loading tracker list"));
    task_runner_->start(
        [this, source, result, operation_id](const core::CancellationToken&) {
            *result = service_.load(source);
        },
        [this, result, operation_id] {
            if (!*result)
            {
                finish_gui_operation(operation_id, core::LogLevel::Error, "gui", "tracker_load",
                                     "failure", {{"reason", "missing_result"}});
                show_task_error(tr("The tracker task did not return a result."));
            }
            else if (!result->value())
            {
                const auto& error = result->value().error();
                finish_gui_operation(
                    operation_id, core::LogLevel::Error, "gui", "tracker_load", "failure",
                    {{"code", error_code_name(error.code)}, {"message", error.message}});
                show_error(error);
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
                finish_gui_operation(
                    operation_id, core::LogLevel::Info, "gui", "tracker_load", "finish",
                    {{"tier_count", std::to_string(tiers.size())}, {"source_kind", "file"}});
                set_status(tr("Tracker list loaded"));
            }
        });
}

void MainWindow::save_tracker_torrent()
{
    if (task_runner_->is_running())
    {
        return;
    }

    QString tracker_error;
    auto trackers = tracker_list_from_model(*tracker_model_, &tracker_error);
    if (!trackers)
    {
        NotificationDialog::show_error(this, tr("Invalid tracker list"), tracker_error);
        return;
    }
    const auto destination_dir = path_from_text(ui_->editTrackerOutputPath->text());
    const auto dry_run = ui_->chkTrackerDryRun->isChecked();
    if (tracker_source_directory_)
    {
        std::error_code error;
        std::vector<std::filesystem::path> files;
        for (const auto& entry :
             std::filesystem::directory_iterator(*tracker_source_directory_, error))
        {
            if (error)
            {
                break;
            }
            if (entry.is_regular_file(error) && entry.path().extension() == ".torrent")
            {
                files.push_back(entry.path());
            }
        }
        if (error)
        {
            show_error({ErrorCode::IoFailure,
                        "failed to enumerate tracker source directory: " + error.message(),
                        {}});
            return;
        }
        std::sort(files.begin(), files.end());
        if (files.empty())
        {
            logger_->log_event(
                core::LogLevel::Warning, "gui", "tracker_batch", "skip",
                {{"path", tracker_source_directory_->u8string()}, {"reason", "no_torrent_files"}});
            NotificationDialog::show_warning(this, tr("No torrent files"),
                                             tr("The selected folder contains no torrent files."));
            return;
        }

        struct BatchOutcome
        {
            struct Failure
            {
                std::filesystem::path path;
                QString reason;
            };
            std::size_t processed{};
            std::vector<Failure> failures;
            std::optional<Error> cancellation;
        };
        auto outcome = std::make_shared<BatchOutcome>();
        const auto operation_id = begin_gui_operation(
            "gui", "tracker_batch",
            {{"source", tracker_source_directory_->u8string()},
             {"destination", destination_dir.u8string()},
             {"dry_run", dry_run ? "true" : "false"},
             {"overwrite", ui_->chkTrackerOverwrite->isChecked() ? "true" : "false"},
             {"backup", ui_->chkTrackerBackup->isChecked() ? "true" : "false"},
             {"file_count", std::to_string(files.size())}});
        const auto overwrite = ui_->chkTrackerOverwrite->isChecked();
        const auto backup = ui_->chkTrackerBackup->isChecked();
        set_busy(true);
        set_status(tr("Converting tracker files"));
        task_runner_->start(
            [this, destination_dir, files, trackers = std::move(*trackers), outcome, dry_run,
             overwrite, backup, operation_id](const core::CancellationToken& token) {
                for (std::size_t index = 0; index < files.size(); ++index)
                {
                    if (token.is_cancelled())
                    {
                        outcome->cancellation = Error{
                            ErrorCode::Cancelled, "tracker batch conversion was cancelled", {}};
                        return;
                    }
                    task_runner_->report({"Tracker batch", index, files.size()});
                    auto loaded = service_.load(files[index]);
                    if (!loaded)
                    {
                        outcome->failures.push_back({files[index], error_text(loaded.error())});
                        continue;
                    }
                    auto edited = service_.edit(loaded.value(), {core::ReplaceTrackers{trackers}});
                    if (!edited)
                    {
                        outcome->failures.push_back({files[index], error_text(edited.error())});
                        continue;
                    }
                    if (edited.value().disposition == core::EditDisposition::NeedRebuild)
                    {
                        outcome->failures.push_back(
                            {files[index], tr("Tracker changes require re-creating the torrent.")});
                        continue;
                    }
                    if (!dry_run && edited.value().disposition == core::EditDisposition::Applied)
                    {
                        const auto target = destination_dir.empty()
                                                ? files[index]
                                                : destination_dir / files[index].filename();
                        core::SaveRequest request;
                        if (!destination_dir.empty())
                        {
                            request.mode = core::SaveTargetMode::NewPath;
                            request.destination = target;
                            request.allow_overwrite = overwrite;
                        }
                        request.backup = backup;
                        core::TaskContext context{token, {}, logger_, operation_id};
                        auto saved = service_.save(edited.value().loaded, request, context);
                        if (!saved)
                        {
                            outcome->failures.push_back({files[index], error_text(saved.error())});
                            continue;
                        }
                    }
                    ++outcome->processed;
                }
                task_runner_->report({"Tracker batch", files.size(), files.size()});
            },
            [this, outcome, dry_run, destination_dir, source_directory = tracker_source_directory_,
             operation_id] {
                if (outcome->cancellation)
                {
                    finish_gui_operation(operation_id, core::LogLevel::Warning, "gui",
                                         "tracker_batch", "cancel",
                                         {{"code", error_code_name(outcome->cancellation->code)},
                                          {"message", outcome->cancellation->message}});
                    show_error(*outcome->cancellation);
                    return;
                }
                std::size_t logged_failures = 0;
                std::size_t suppressed_failures = 0;
                for (const auto& failure : outcome->failures)
                {
                    if (logged_failures >= 32U)
                    {
                        ++suppressed_failures;
                        continue;
                    }
                    logger_->log_event(core::LogLevel::Warning, "gui", "tracker_batch_file",
                                       "failure",
                                       {{"operation_id", operation_id},
                                        {"path", failure.path.u8string()},
                                        {"reason", failure.reason.toUtf8().toStdString()}});
                    ++logged_failures;
                }
                finish_gui_operation(operation_id,
                                     outcome->failures.empty() ? core::LogLevel::Info
                                                               : core::LogLevel::Warning,
                                     "gui", "tracker_batch", "finish",
                                     {{"processed_count", std::to_string(outcome->processed)},
                                      {"failure_count", std::to_string(outcome->failures.size())},
                                      {"suppressed_count", std::to_string(suppressed_failures)},
                                      {"dry_run", dry_run ? "true" : "false"}});
                QString summary = tr("Processed %1 torrent file(s)%2.")
                                      .arg(static_cast<qulonglong>(outcome->processed))
                                      .arg(dry_run ? tr(" No files were written.") : QString());
                if (!outcome->failures.empty())
                {
                    summary += tr("\n\nFailed %1 file(s):")
                                   .arg(static_cast<qulonglong>(outcome->failures.size()));
                    for (const auto& failure : outcome->failures)
                    {
                        summary += QStringLiteral("\n- %1: %2")
                                       .arg(QString::fromUtf8(reinterpret_cast<const char*>(
                                                failure.path.u8string().c_str())),
                                            failure.reason);
                    }
                    NotificationDialog::show_warning(
                        this, tr("Tracker batch completed with errors"), summary);
                }
                else
                {
                    NotificationDialog::show_info(this, tr("Tracker batch complete"), summary);
                }
                if (!dry_run)
                {
                    remember_save_directory(destination_dir.empty() && source_directory
                                                ? *source_directory
                                                : destination_dir);
                }
                set_status(tr("Ready"));
            });
        return;
    }

    if (!tracker_loaded_)
    {
        NotificationDialog::show_warning(this, tr("No torrent loaded"),
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
    const auto operation_id = begin_gui_operation(
        "gui", "tracker_save",
        {{"source", tracker_loaded_->source_path().u8string()},
         {"target", target.u8string()},
         {"dry_run", dry_run ? "true" : "false"},
         {"overwrite", ui_->chkTrackerOverwrite->isChecked() ? "true" : "false"},
         {"backup", ui_->chkTrackerBackup->isChecked() ? "true" : "false"}});
    set_busy(true);
    task_runner_->start(
        [this, trackers = std::move(trackers).value(), edited, outcome, save_request, dry_run,
         operation_id](const core::CancellationToken& token) {
            *edited = service_.edit(*tracker_loaded_, {core::ReplaceTrackers{trackers}});
            if (!edited->value() || dry_run ||
                edited->value().value().disposition != core::EditDisposition::Applied)
            {
                return;
            }
            core::TaskContext context{token, progress_callback(task_runner_.get()), logger_,
                                      operation_id};
            *outcome = service_.save(edited->value().value().loaded, save_request, context);
        },
        [this, edited, outcome, dry_run, operation_id, target] {
            if (!*edited)
            {
                finish_gui_operation(operation_id, core::LogLevel::Error, "gui", "tracker_save",
                                     "failure", {{"reason", "missing_edit_result"}});
                show_task_error(tr("No tracker edit result was produced."));
            }
            else if (!edited->value())
            {
                const auto& error = edited->value().error();
                finish_gui_operation(
                    operation_id, core::LogLevel::Error, "gui", "tracker_save", "failure",
                    {{"code", error_code_name(error.code)}, {"message", error.message}});
                show_error(error);
            }
            else if (edited->value().value().disposition == core::EditDisposition::NeedRebuild)
            {
                finish_gui_operation(operation_id, core::LogLevel::Warning, "gui", "tracker_save",
                                     "rebuild_required", {{"reason", "identity_change"}});
                NotificationDialog::show_warning(
                    this, tr("Rebuild required"),
                    tr("Tracker changes require a rebuild. No file was written."));
            }
            else if (dry_run)
            {
                finish_gui_operation(operation_id, core::LogLevel::Info, "gui", "tracker_save",
                                     "finish", {{"preview", "true"}});
                NotificationDialog::show_info(
                    this, tr("Preview"), tr("Tracker changes are valid; dry run wrote nothing."));
            }
            else if (!*outcome)
            {
                finish_gui_operation(operation_id, core::LogLevel::Error, "gui", "tracker_save",
                                     "failure", {{"reason", "missing_save_result"}});
                show_task_error(tr("No tracker save result was produced."));
            }
            else if (!outcome->value())
            {
                const auto& error = outcome->value().error();
                finish_gui_operation(
                    operation_id, core::LogLevel::Error, "gui", "tracker_save", "failure",
                    {{"code", error_code_name(error.code)}, {"message", error.message}});
                show_error(error);
            }
            else
            {
                finish_gui_operation(operation_id, core::LogLevel::Info, "gui", "tracker_save",
                                     "finish", {{"target", target.u8string()}});
                tracker_loaded_ = outcome->value().value().loaded;
                set_status(tr("Tracker torrent saved"));
                remember_save_directory(tracker_loaded_->source_path().parent_path());
            }
        });
}

void MainWindow::cancel_operation()
{
    if (task_runner_->is_running())
    {
        task_runner_->cancel();
        if (!active_operations_.empty())
        {
            logger_->log_event(core::LogLevel::Warning, active_operations_.back().component,
                               active_operations_.back().operation, "cancel",
                               {{"operation_id", active_operations_.back().id}});
        }
        else
        {
            logger_->log_event(core::LogLevel::Warning, "gui", "operation", "cancel");
        }
        set_status(tr("Cancellation requested"));
    }
}

void MainWindow::apply_memory_working_set_limit()
{
    const auto limit_bytes = config_ ? config_->parsed().memory_working_set_limit_bytes.value_or(
                                           frontend::default_memory_working_set_limit_bytes)
                                     : frontend::default_memory_working_set_limit_bytes;
    auto applied = frontend::apply_memory_working_set_limit_bytes(limit_bytes);
    if (!applied)
    {
        NotificationDialog::show_warning(
            this, tr("Physical memory limit unavailable"),
            tr("TorrentCraft could not apply the process working-set limit.\n%1")
                .arg(error_text(applied.error())));
    }
}

void MainWindow::reload_configuration()
{
    const auto path = ui_->editAdvancedConfigPath->text().trimmed();
    if (path.isEmpty())
    {
        return;
    }
    const auto operation_id =
        begin_gui_operation("gui", "config_reload", {{"path", path.toUtf8().toStdString()}});
    auto config = frontend::ConfigFile::load(path_from_text(path));
    if (!config)
    {
        show_error(config.error());
        return;
    }
    config_ = std::move(config).value();
    apply_memory_working_set_limit();
    populate_advanced_configuration();
    configure_logger();
    const auto chinese = config_->gui_language() == frontend::GuiLanguage::SimplifiedChinese;
    apply_language(chinese ? ui_->actionLanguageChinese : ui_->actionLanguageEnglish);
    refresh_preset_menu();
    finish_gui_operation(
        operation_id, core::LogLevel::Info, "gui", "config_reload", "finish",
        {{"diagnostic_count", std::to_string(config_->parsed().diagnostics.size())},
         {"legacy", config_->parsed().legacy ? "true" : "false"}});
    set_status(tr("Configuration reloaded"));
}

void MainWindow::initialize_configuration()
{
    auto path = ui_->editAdvancedConfigPath->text().trimmed();
    if (path.isEmpty())
    {
        const auto paths = frontend::default_config_search_paths(
            std::nullopt, path_from_text(QDir::currentPath()));
        if (!paths.user_config_path)
        {
            show_error(
                {core::ErrorCode::IoFailure, "could not determine platform user config path", {}});
            return;
        }
        path = path_to_text(*paths.user_config_path);
        ui_->editAdvancedConfigPath->setText(path);
    }
    const auto operation_id =
        begin_gui_operation("gui", "config_initialize", {{"path", path.toUtf8().toStdString()}});
    auto config = frontend::ConfigFile::create(path_from_text(path));
    if (!config)
    {
        show_error(config.error());
        return;
    }
    config_ = std::move(config).value();
    apply_memory_working_set_limit();
    populate_advanced_configuration();
    configure_logger();
    refresh_preset_menu();
    finish_gui_operation(
        operation_id, core::LogLevel::Info, "gui", "config_initialize", "finish",
        {{"diagnostic_count", std::to_string(config_->parsed().diagnostics.size())}});
    set_status(tr("Configuration initialized"));
}

void MainWindow::show_configuration()
{
    const auto path = ui_->editAdvancedConfigPath->text().trimmed();
    if (path.isEmpty())
    {
        NotificationDialog::show_warning(this, tr("No configuration file"),
                                         tr("Initialize or select a configuration file first."));
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
    {
        NotificationDialog::show_error(
            this, tr("Open failed"),
            tr("The default editor could not open the configuration file."));
    }
}

void MainWindow::apply_creation_settings(const frontend::CreationSettingsPatch& settings)
{
    const auto previous_applying = applying_creation_settings_;
    applying_creation_settings_ = true;
    ui_->cmbCreateFormat->setCurrentIndex(settings.format
                                              ? (*settings.format == TorrentFormat::V1   ? 1
                                                 : *settings.format == TorrentFormat::V2 ? 2
                                                                                         : 0)
                                              : 0);
    ui_->cmbCreateFileOrder->setCurrentIndex(
        settings.file_order ? static_cast<int>(*settings.file_order) : 0);
    ui_->cmbCreatePieceSize->setCurrentIndex(
        settings.piece_size && settings.piece_size->fixed_kib
            ? piece_size_combo_index(*settings.piece_size->fixed_kib)
            : 0);
    ui_->chkCreatePrivate->setChecked(settings.is_private.value_or(false));
    populate_tracker_values(*create_tracker_model_, settings.tracker_tiers.value_or(
                                                        std::vector<std::vector<std::string>>{}));
    configure_tracker_table(*ui_->tblCreateTrackers);
    if (settings.comment)
    {
        ui_->editCreateComment->setPlainText(QString::fromStdString(*settings.comment));
    }
    else
    {
        ui_->editCreateComment->clear();
    }
    ui_->editCreateCreator->setText(settings.created_by
                                        ? QString::fromStdString(*settings.created_by)
                                        : QStringLiteral("TorrentCraft"));
    ui_->chkCreateCreator->setChecked(is_custom_created_by(settings.created_by));
    ui_->editCreateCreator->setEnabled(ui_->chkCreateCreator->isChecked());
    ui_->editCreateSource->setText(
        settings.info_source ? QString::fromStdString(*settings.info_source) : QString());
    if (settings.web_seeds)
    {
        QStringList seeds;
        for (const auto& seed : *settings.web_seeds)
        {
            seeds << QString::fromStdString(seed);
        }
        ui_->editCreateWebSeeds->setPlainText(seeds.join(QLatin1Char('\n')));
    }
    else
    {
        ui_->editCreateWebSeeds->clear();
    }
    applying_creation_settings_ = previous_applying;
}

void MainWindow::populate_advanced_configuration()
{
    if (!config_)
    {
        return;
    }
    const auto& parsed = config_->parsed();
    const auto& defaults = parsed.defaults;
    const auto gui = config_->gui_preferences();
    const auto font_index = ui_->cmbAdvancedFont->findData(optional_string(gui.font_family));
    ui_->cmbAdvancedFont->setCurrentIndex(font_index < 0 ? 0 : font_index);
    const auto style_index = ui_->cmbAdvancedStyle->findData(optional_string(gui.style));
    ui_->cmbAdvancedStyle->setCurrentIndex(style_index < 0 ? 0 : style_index);
    apply_gui_display_preferences(gui);
    ui_->cmbAdvancedDefaultPreset->clear();
    ui_->cmbAdvancedDefaultPreset->addItem(tr("Defaults"), QString());
    for (const auto& [name, settings] : parsed.presets)
    {
        static_cast<void>(settings);
        ui_->cmbAdvancedDefaultPreset->addItem(QString::fromStdString(name),
                                               QString::fromStdString(name));
    }
    const auto preset_index =
        ui_->cmbAdvancedDefaultPreset->findData(optional_string(gui.default_preset));
    ui_->cmbAdvancedDefaultPreset->setCurrentIndex(preset_index < 0 ? 0 : preset_index);

    ui_->cmbAdvancedDefaultFormat->setCurrentIndex(
        defaults.format ? (*defaults.format == TorrentFormat::V1   ? 1
                           : *defaults.format == TorrentFormat::V2 ? 2
                                                                   : 0)
                        : 0);
    ui_->cmbAdvancedDefaultFileOrder->setCurrentIndex(
        defaults.file_order ? static_cast<int>(*defaults.file_order) : 0);
    ui_->spinAdvancedDefaultPieceSize->setValue(
        defaults.piece_size && defaults.piece_size->fixed_kib
            ? static_cast<int>(*defaults.piece_size->fixed_kib)
            : 0);
    ui_->chkAdvancedDefaultPrivate->setChecked(defaults.is_private.value_or(false));
    ui_->editAdvancedDefaultTrackerTiers->clear();
    if (defaults.tracker_tiers)
    {
        QStringList tier_text;
        for (const auto& tier : *defaults.tracker_tiers)
        {
            QStringList trackers;
            for (const auto& tracker : tier)
            {
                trackers << QString::fromStdString(tracker);
            }
            tier_text << trackers.join(QLatin1Char('\n'));
        }
        ui_->editAdvancedDefaultTrackerTiers->setPlainText(tier_text.join(QStringLiteral("\n\n")));
    }
    ui_->editAdvancedDefaultWebSeeds->clear();
    if (defaults.web_seeds)
    {
        QStringList seeds;
        for (const auto& seed : *defaults.web_seeds)
        {
            seeds << QString::fromStdString(seed);
        }
        ui_->editAdvancedDefaultWebSeeds->setPlainText(seeds.join(QLatin1Char('\n')));
    }
    ui_->editAdvancedDefaultComment->setText(optional_string(defaults.comment));
    ui_->editAdvancedDefaultCreatedBy->setText(
        optional_string(defaults.created_by, QStringLiteral("TorrentCraft")));
    ui_->editAdvancedDefaultSource->setText(optional_string(defaults.info_source));
    auto effective_defaults = defaults;
    QString active_preset_name;
    if (gui.default_preset)
    {
        const auto iterator = parsed.presets.find(*gui.default_preset);
        if (iterator != parsed.presets.end())
        {
            effective_defaults = frontend::overlay_settings(defaults, iterator->second);
            active_preset_name = QString::fromStdString(*gui.default_preset);
        }
    }
    apply_creation_settings(effective_defaults);
    set_active_preset(active_preset_name);

    const auto verify = parsed.verify.value_or(frontend::VerifyResourceSettings{});
    ui_->spinAdvancedVerifyWorkers->setValue(static_cast<int>(
        verify.hashing_workers.value_or(frontend::default_verify_hashing_workers)));
    ui_->spinAdvancedVerifyMemory->setValue(memory_bytes_to_mib(
        verify.checking_memory_bytes.value_or(frontend::default_verify_checking_memory_bytes)));
    ui_->spinAdvancedMemoryWorkingSetLimit->setValue(
        memory_bytes_to_mib(parsed.memory_working_set_limit_bytes.value_or(
            frontend::default_memory_working_set_limit_bytes)));
    ui_->cmbAdvancedDiskIo->setCurrentIndex(
        parsed.disk_io.value_or(core::DiskIoMode::Mmap) == core::DiskIoMode::Posix ? 1 : 0);

    ui_->cmbAdvancedSaveMode->setCurrentIndex(
        gui.default_save_location == frontend::GuiSaveLocationMode::Recent      ? 1
        : gui.default_save_location == frontend::GuiSaveLocationMode::Specified ? 2
                                                                                : 0);
    ui_->editAdvancedSavePath->setText(optional_string(gui.default_save_path));
    ui_->editAdvancedSavePath->setEnabled(ui_->cmbAdvancedSaveMode->currentIndex() == 2);
    ui_->chkAdvancedShowPaddingFiles->setChecked(gui.show_padding_files);
    ui_->chkAdvancedLogging->setChecked(gui.logging_enabled);
    ui_->cmbAdvancedLogLevel->setCurrentIndex(gui.log_level == frontend::GuiLogLevel::Debug     ? 0
                                              : gui.log_level == frontend::GuiLogLevel::Warning ? 2
                                              : gui.log_level == frontend::GuiLogLevel::Error   ? 3
                                                                                              : 1);
    ui_->editAdvancedLogPath->setText(optional_string(gui.log_path));
    update_create_output_path();
    if (modify_loaded_)
    {
        update_modify_output_path(modify_loaded_->source_path());
    }
    update_tracker_output_path();
    if (tracker_source_directory_ && !tracker_loaded_)
    {
        populate_tracker_defaults();
    }
}

void MainWindow::apply_advanced_configuration()
{
    if (!config_)
    {
        initialize_configuration();
    }
    if (!config_)
    {
        return;
    }
    if (config_->parsed().legacy)
    {
        NotificationDialog::show_warning(
            this, tr("Canonical configuration required"),
            tr("Initialize a canonical configuration file before applying Advanced "
               "settings."));
        return;
    }
    const auto operation_id = begin_gui_operation(
        "gui", "config_apply",
        {{"path", ui_->editAdvancedConfigPath->text().trimmed().toUtf8().toStdString()},
         {"default_format",
          format_name(ui_->cmbAdvancedDefaultFormat->currentIndex() == 1   ? TorrentFormat::V1
                      : ui_->cmbAdvancedDefaultFormat->currentIndex() == 2 ? TorrentFormat::V2
                                                                           : TorrentFormat::Hybrid)
              .toStdString()},
         {"file_order", std::to_string(ui_->cmbAdvancedDefaultFileOrder->currentIndex())},
         {"piece_size_kib", std::to_string(ui_->spinAdvancedDefaultPieceSize->value())},
         {"private", ui_->chkAdvancedDefaultPrivate->isChecked() ? "true" : "false"},
         {"verify_workers", std::to_string(ui_->spinAdvancedVerifyWorkers->value())},
         {"verify_memory_mib", std::to_string(ui_->spinAdvancedVerifyMemory->value())},
         {"working_set_limit_mib", std::to_string(ui_->spinAdvancedMemoryWorkingSetLimit->value())},
         {"disk_io", ui_->cmbAdvancedDiskIo->currentIndex() == 1 ? "posix" : "mmap"},
         {"font", ui_->cmbAdvancedFont->currentData().toString().toUtf8().toStdString()},
         {"style", ui_->cmbAdvancedStyle->currentData().toString().toUtf8().toStdString()},
         {"logging_enabled", ui_->chkAdvancedLogging->isChecked() ? "true" : "false"},
         {"log_level", std::to_string(ui_->cmbAdvancedLogLevel->currentIndex())},
         {"default_preset",
          ui_->cmbAdvancedDefaultPreset->currentData().toString().toUtf8().toStdString()}});
    frontend::CreationSettingsPatch patch;
    patch.format = ui_->cmbAdvancedDefaultFormat->currentIndex() == 1
                       ? std::optional<TorrentFormat>(TorrentFormat::V1)
                   : ui_->cmbAdvancedDefaultFormat->currentIndex() == 2
                       ? std::optional<TorrentFormat>(TorrentFormat::V2)
                       : std::optional<TorrentFormat>(TorrentFormat::Hybrid);
    switch (ui_->cmbAdvancedDefaultFileOrder->currentIndex())
    {
    case 1:
        patch.file_order = FileOrderPolicy::CanonicalAlignment;
        break;
    case 2:
        patch.file_order = FileOrderPolicy::Natural;
        break;
    case 3:
        patch.file_order = FileOrderPolicy::BreadthFirst;
        break;
    default:
        patch.file_order = FileOrderPolicy::Lexicographical;
        break;
    }
    frontend::PieceSizeSetting piece;
    if (ui_->spinAdvancedDefaultPieceSize->value() > 0)
    {
        piece.fixed_kib = static_cast<std::uint32_t>(ui_->spinAdvancedDefaultPieceSize->value());
    }
    patch.piece_size = piece;
    patch.is_private = ui_->chkAdvancedDefaultPrivate->isChecked();
    patch.comment = optional_text(ui_->editAdvancedDefaultComment->text());
    patch.created_by = ui_->editAdvancedDefaultCreatedBy->text().trimmed().toUtf8().toStdString();
    patch.info_source = optional_text(ui_->editAdvancedDefaultSource->text());
    QString parse_error;
    auto tiers =
        parse_tracker_tiers(ui_->editAdvancedDefaultTrackerTiers->toPlainText(), &parse_error);
    if (!tiers)
    {
        finish_gui_operation(operation_id, core::LogLevel::Error, "gui", "config_apply", "failure",
                             {{"reason", "invalid_tracker_tiers"}});
        NotificationDialog::show_error(this, tr("Invalid tracker tiers"), parse_error);
        return;
    }
    std::vector<std::vector<std::string>> tier_values;
    for (const auto& tier : *tiers)
    {
        std::vector<std::string> values;
        for (const auto& tracker : tier.trackers())
        {
            values.push_back(tracker.value());
        }
        tier_values.push_back(std::move(values));
    }
    patch.tracker_tiers = std::move(tier_values);
    patch.web_seeds = nonempty_lines(ui_->editAdvancedDefaultWebSeeds->toPlainText());
    auto updated = config_->set_defaults(patch);
    if (!updated)
    {
        show_error(updated.error());
        return;
    }
    auto workers_key = frontend::parse_config_key("verify.workers");
    auto memory_key = frontend::parse_config_key("verify.memory");
    auto disk_io_key = frontend::parse_config_key("disk_io");
    auto working_set_key = frontend::parse_config_key("memory_working_set_limit");
    if (!workers_key || !memory_key || !disk_io_key || !working_set_key)
    {
        show_task_error(tr("The configuration schema does not expose the required settings."));
        return;
    }
    updated = config_->set_key(workers_key.value(),
                               std::to_string(ui_->spinAdvancedVerifyWorkers->value()));
    if (!updated)
    {
        show_error(updated.error());
        return;
    }
    const auto memory_json = std::optional<std::string>(
        "\"" + std::to_string(ui_->spinAdvancedVerifyMemory->value()) + " MiB\"");
    updated = config_->set_key(memory_key.value(), memory_json);
    if (!updated)
    {
        show_error(updated.error());
        return;
    }
    updated = config_->set_key(
        working_set_key.value(),
        "\"" + std::to_string(ui_->spinAdvancedMemoryWorkingSetLimit->value()) + " MiB\"");
    if (!updated)
    {
        show_error(updated.error());
        return;
    }
    updated =
        config_->set_key(disk_io_key.value(),
                         ui_->cmbAdvancedDiskIo->currentIndex() == 1 ? "\"posix\"" : "\"mmap\"");
    if (!updated)
    {
        show_error(updated.error());
        return;
    }
    frontend::GuiPreferences gui;
    gui.recent_save_path = config_->gui_preferences().recent_save_path;
    gui.default_save_location =
        ui_->cmbAdvancedSaveMode->currentIndex() == 1   ? frontend::GuiSaveLocationMode::Recent
        : ui_->cmbAdvancedSaveMode->currentIndex() == 2 ? frontend::GuiSaveLocationMode::Specified
                                                        : frontend::GuiSaveLocationMode::Current;
    gui.default_save_path = optional_text(ui_->editAdvancedSavePath->text());
    gui.default_preset = optional_text(ui_->cmbAdvancedDefaultPreset->currentData().toString());
    gui.font_family = optional_text(ui_->cmbAdvancedFont->currentData().toString());
    gui.style = optional_text(ui_->cmbAdvancedStyle->currentData().toString());
    gui.show_padding_files = ui_->chkAdvancedShowPaddingFiles->isChecked();
    gui.logging_enabled = ui_->chkAdvancedLogging->isChecked();
    switch (ui_->cmbAdvancedLogLevel->currentIndex())
    {
    case 0:
        gui.log_level = frontend::GuiLogLevel::Debug;
        break;
    case 2:
        gui.log_level = frontend::GuiLogLevel::Warning;
        break;
    case 3:
        gui.log_level = frontend::GuiLogLevel::Error;
        break;
    default:
        gui.log_level = frontend::GuiLogLevel::Info;
        break;
    }
    gui.log_path = optional_text(ui_->editAdvancedLogPath->text());
    updated = config_->set_gui_preferences(gui);
    if (!updated)
    {
        show_error(updated.error());
        return;
    }
    auto saved = config_->save();
    if (!saved)
    {
        show_error(saved.error());
        return;
    }
    apply_memory_working_set_limit();
    populate_advanced_configuration();
    configure_logger();
    if (inspect_loaded_)
    {
        populate_inspect_fields(*inspect_loaded_);
    }
    finish_gui_operation(
        operation_id, core::LogLevel::Info, "gui", "config_apply", "finish",
        {{"verify_workers", std::to_string(ui_->spinAdvancedVerifyWorkers->value())},
         {"verify_memory_mib", std::to_string(ui_->spinAdvancedVerifyMemory->value())},
         {"working_set_limit_mib", std::to_string(ui_->spinAdvancedMemoryWorkingSetLimit->value())},
         {"disk_io", ui_->cmbAdvancedDiskIo->currentIndex() == 1 ? "posix" : "mmap"},
         {"style", ui_->cmbAdvancedStyle->currentData().toString().toUtf8().toStdString()},
         {"font", ui_->cmbAdvancedFont->currentData().toString().toUtf8().toStdString()},
         {"logging_enabled", ui_->chkAdvancedLogging->isChecked() ? "true" : "false"}});
    set_status(tr("Configuration saved"));
}

void MainWindow::reset_advanced_configuration()
{
    if (config_)
    {
        populate_advanced_configuration();
        set_status(tr("Advanced changes reset"));
    }
}

void MainWindow::import_preset()
{
    const auto path = QFileDialog::getOpenFileName(this, tr("Import preset"), QString(),
                                                   tr("JSON files (*.json);;All files (*)"));
    if (path.isEmpty())
    {
        return;
    }
    const auto operation_id =
        begin_gui_operation("gui", "preset_import", {{"path", path.toUtf8().toStdString()}});
    auto preset = frontend::load_preset_file(path_from_text(path));
    if (!preset)
    {
        show_error(preset.error());
        return;
    }
    const auto* defaults = config_ ? &config_->parsed().defaults : nullptr;
    PresetPreviewDialog preview(tr("Imported preset"),
                                preset_preview(preset.value().settings, defaults), this);
    if (preview.exec() == QDialog::Accepted)
    {
        if (!config_)
        {
            initialize_configuration();
        }
        if (!config_)
        {
            finish_gui_operation(operation_id, core::LogLevel::Error, "gui", "preset_import",
                                 "failure", {{"reason", "config_unavailable"}});
            return;
        }
        bool accepted = false;
        const auto name = QInputDialog::getText(this, tr("Import preset"), tr("Preset name:"),
                                                QLineEdit::Normal, QString(), &accepted);
        if (!accepted || name.trimmed().isEmpty())
        {
            finish_gui_operation(operation_id, core::LogLevel::Warning, "gui", "preset_import",
                                 "skip", {{"reason", "name_cancelled"}});
            return;
        }
        const auto name_key = name.trimmed().toUtf8().toStdString();
        const auto exists =
            config_->parsed().presets.find(name_key) != config_->parsed().presets.end();
        if (exists &&
            QMessageBox::question(this, tr("Replace preset"),
                                  tr("A preset with this name already exists. Replace it?")) !=
                QMessageBox::Yes)
        {
            finish_gui_operation(operation_id, core::LogLevel::Warning, "gui", "preset_import",
                                 "skip", {{"reason", "replace_declined"}});
            return;
        }
        auto added = config_->add_preset(name_key, preset.value().settings, true);
        if (!added)
        {
            show_error(added.error());
            return;
        }
        auto saved = config_->save();
        if (!saved)
        {
            show_error(saved.error());
            return;
        }
        apply_creation_settings(
            frontend::overlay_settings(config_->parsed().defaults, preset.value().settings));
        refresh_preset_menu();
        set_active_preset(name.trimmed());
        finish_gui_operation(operation_id, core::LogLevel::Info, "gui", "preset_import", "finish",
                             {{"name", name_key}, {"replaced", exists ? "true" : "false"}});
        set_status(tr("Preset imported: %1").arg(name.trimmed()));
    }
    else
    {
        finish_gui_operation(operation_id, core::LogLevel::Warning, "gui", "preset_import", "skip",
                             {{"reason", "preview_cancelled"}});
    }
}

void MainWindow::save_preset()
{
    if (!config_)
    {
        initialize_configuration();
    }
    if (!config_)
    {
        return;
    }
    bool accepted = false;
    const auto name = QInputDialog::getText(this, tr("Save preset"), tr("Preset name:"),
                                            QLineEdit::Normal, QString(), &accepted);
    if (!accepted || name.trimmed().isEmpty())
    {
        return;
    }
    auto settings = creation_patch_from_create_form();
    if (!settings)
    {
        show_error(settings.error());
        return;
    }
    const auto name_key = name.trimmed().toUtf8().toStdString();
    const auto operation_id = begin_gui_operation("gui", "preset_save", {{"name", name_key}});
    const auto exists = config_->parsed().presets.find(name_key) != config_->parsed().presets.end();
    if (exists &&
        QMessageBox::question(this, tr("Replace preset"),
                              tr("A preset with this name already exists. Replace it?")) !=
            QMessageBox::Yes)
    {
        finish_gui_operation(operation_id, core::LogLevel::Warning, "gui", "preset_save", "skip",
                             {{"reason", "replace_declined"}});
        return;
    }
    auto added = config_->add_preset(name_key, settings.value(), true);
    if (!added)
    {
        show_error(added.error());
        return;
    }
    auto saved = config_->save();
    if (!saved)
    {
        show_error(saved.error());
        return;
    }
    refresh_preset_menu();
    set_active_preset(name.trimmed());
    finish_gui_operation(operation_id, core::LogLevel::Info, "gui", "preset_save", "finish",
                         {{"name", name_key}, {"replaced", exists ? "true" : "false"}});
    set_status(tr("Preset saved"));
}

void MainWindow::remove_preset()
{
    if (!config_ || config_->parsed().presets.empty())
    {
        return;
    }
    QStringList names;
    for (const auto& [name, settings] : config_->parsed().presets)
    {
        static_cast<void>(settings);
        names << QString::fromStdString(name);
    }
    bool accepted = false;
    const auto name =
        QInputDialog::getItem(this, tr("Delete preset"), tr("Preset:"), names, 0, false, &accepted);
    if (!accepted)
    {
        return;
    }
    const auto name_key = name.toUtf8().toStdString();
    const auto operation_id = begin_gui_operation("gui", "preset_remove", {{"name", name_key}});
    auto removed = config_->remove_preset(name_key);
    if (!removed)
    {
        show_error(removed.error());
        return;
    }
    auto saved = config_->save();
    if (!saved)
    {
        show_error(saved.error());
        return;
    }
    refresh_preset_menu();
    if (active_preset_name_ == name)
        set_active_preset(QString());
    finish_gui_operation(operation_id, core::LogLevel::Info, "gui", "preset_remove", "finish",
                         {{"name", name_key}});
}

void MainWindow::clear_current_form()
{
    ui_->editCreateInputPath->clear();
    ui_->editCreateOutputPath->clear();
    create_auto_output_path_.reset();
    create_tracker_model_->clear();
    create_tracker_model_->setHorizontalHeaderLabels({tr("Tier"), tr("Tracker")});
    ui_->chkCreatePrivate->setChecked(false);
    ui_->editCreateComment->clear();
    ui_->chkCreateCreator->setChecked(false);
    ui_->editCreateCreator->clear();
    ui_->editCreateSource->clear();
    ui_->editCreateWebSeeds->clear();
    ui_->lblCreatePieces->clear();
    set_active_preset(QString());
}

void MainWindow::mark_preset_modified()
{
    if (!applying_creation_settings_ && !active_preset_name_.isEmpty() && !preset_modified_)
    {
        preset_modified_ = true;
        logger_->log_event(core::LogLevel::Debug, "gui", "preset", "modified",
                           {{"name", active_preset_name_.toUtf8().toStdString()}});
        update_window_title();
    }
}

void MainWindow::set_active_preset(const QString& name)
{
    const auto changed = active_preset_name_ != name;
    active_preset_name_ = name;
    preset_modified_ = false;
    update_window_title();
    if (changed)
    {
        logger_->log_event(core::LogLevel::Info, "gui", "preset", "active",
                           {{"name", name.toUtf8().toStdString()}});
    }
}

void MainWindow::update_window_title()
{
    auto title = QStringLiteral("TorrentCraft");
    if (!active_preset_name_.isEmpty())
    {
        title += QStringLiteral(" — [") + active_preset_name_;
        if (preset_modified_)
            title += QLatin1Char('*');
        title += QLatin1Char(']');
    }
    setWindowTitle(title);
}

void MainWindow::load_inline_preset(const QString& name)
{
    if (!config_)
    {
        return;
    }
    const auto iterator = config_->parsed().presets.find(name.toUtf8().toStdString());
    if (iterator == config_->parsed().presets.end())
    {
        logger_->log_event(core::LogLevel::Warning, "gui", "preset_load", "skip",
                           {{"name", name.toUtf8().toStdString()}, {"reason", "not_found"}});
        return;
    }
    const auto operation_id =
        begin_gui_operation("gui", "preset_load", {{"name", name.toUtf8().toStdString()}});
    const auto effective = frontend::overlay_settings(config_->parsed().defaults, iterator->second);
    apply_creation_settings(effective);
    set_active_preset(name);
    set_status(tr("Preset loaded: %1").arg(name));
    finish_gui_operation(operation_id, core::LogLevel::Info, "gui", "preset_load", "finish",
                         {{"name", name.toUtf8().toStdString()}});
}

void MainWindow::refresh_preset_menu()
{
    const auto actions = ui_->menuLoadPreset->actions();
    for (auto* action : actions)
    {
        if (action != ui_->actionNoPresetsAvailable)
        {
            ui_->menuLoadPreset->removeAction(action);
            action->deleteLater();
        }
    }
    if (!config_ || config_->parsed().presets.empty())
    {
        ui_->actionNoPresetsAvailable->setVisible(true);
        return;
    }
    ui_->actionNoPresetsAvailable->setVisible(false);
    for (const auto& [name, settings] : config_->parsed().presets)
    {
        static_cast<void>(settings);
        auto* action = ui_->menuLoadPreset->addAction(QString::fromStdString(name));
        action->setData(QString::fromStdString(name));
        connect(action, &QAction::triggered, this,
                [this, action] { load_inline_preset(action->data().toString()); });
    }
}

void MainWindow::apply_language(QAction* action)
{
    const bool chinese = action == ui_->actionLanguageChinese;
    const auto operation_id =
        begin_gui_operation("gui", "language", {{"language", chinese ? "zh_CN" : "en"}});
    if (chinese)
    {
        if (!translator_->load(QStringLiteral(":/torrentcraft/lang/TorrentCraft_zh_CN.qm")))
        {
            finish_gui_operation(operation_id, core::LogLevel::Warning, "gui", "language", "skip",
                                 {{"reason", "translation_unavailable"}});
            NotificationDialog::show_warning(
                this, tr("Language unavailable"),
                tr("The Chinese translation is not available in this build."));
            ui_->actionLanguageEnglish->setChecked(true);
            return;
        }
        qApp->installTranslator(translator_);
    }
    else
    {
        qApp->removeTranslator(translator_);
    }
    action->setChecked(true);
    ui_->retranslateUi(this);
    update_window_title();
    ui_->lblCreatePieces->clear();
    ui_->lblAdvancedDefaultPreset->setText(tr("Default preset:"));
    ui_->cmbAdvancedDefaultPreset->setItemText(0, tr("Defaults"));
    ui_->cmbAdvancedStyle->setItemText(0, tr("Default"));
    ui_->cmbAdvancedFont->setItemText(0, tr("Default"));
    ui_->lblAdvancedVerifyMemory->setText(tr("Verification memory (MiB):"));
    inspect_file_model_->refresh_translations();
    verify_file_model_->refresh_translations();
    for (auto* model : {create_tracker_model_.get(), modify_tracker_model_.get(),
                        tracker_model_.get(), inspect_tracker_model_.get()})
    {
        model->setHorizontalHeaderLabels({tr("Tier"), tr("Tracker")});
    }
    for (auto* table : {ui_->tblCreateTrackers, ui_->tblModifyTrackers, ui_->tblTrackerTiers,
                        ui_->tblInspectTrackers})
    {
        configure_tracker_table(*table);
    }
    if (!config_)
    {
        const auto path = ui_->editAdvancedConfigPath->text().trimmed();
        if (!path.isEmpty())
        {
            auto loaded = frontend::ConfigFile::load(path_from_text(path));
            if (loaded)
            {
                config_ = std::move(loaded).value();
            }
            else if (loaded.error().code != core::ErrorCode::FileNotFound)
            {
                show_error(loaded.error());
                return;
            }
        }
        if (!config_)
        {
            initialize_configuration();
        }
        if (!config_)
        {
            finish_gui_operation(operation_id, core::LogLevel::Error, "gui", "language", "failure",
                                 {{"reason", "config_unavailable"}});
            return;
        }
    }
    if (config_)
    {
        auto updated = config_->set_gui_language(chinese ? frontend::GuiLanguage::SimplifiedChinese
                                                         : frontend::GuiLanguage::English);
        if (updated)
        {
            updated = config_->save();
        }
        if (!updated)
        {
            show_error(updated.error());
        }
        else
        {
            finish_gui_operation(operation_id, core::LogLevel::Info, "gui", "language", "finish",
                                 {{"language", chinese ? "zh_CN" : "en"}});
        }
    }
}

void MainWindow::reset_progress_state()
{
    last_progress_time_.reset();
    last_progress_stage_.clear();
    last_progress_completed_ = 0;
    progress_speed_units_per_second_ = 0.0;
    last_progress_completed_bytes_.reset();
    progress_speed_bytes_per_second_ = 0.0;
}

void MainWindow::update_progress_status(const QString& stage, const qulonglong completed,
                                        const qulonglong total, const qulonglong completed_bytes,
                                        const qulonglong total_bytes)
{
    const auto now = std::chrono::steady_clock::now();
    const auto stage_changed = stage != last_progress_stage_;
    if (!last_progress_time_.has_value() || stage_changed || completed < last_progress_completed_)
    {
        progress_speed_units_per_second_ = 0.0;
    }
    else
    {
        const auto elapsed = std::chrono::duration<double>(now - *last_progress_time_).count();
        if (elapsed > 0.0)
        {
            progress_speed_units_per_second_ =
                static_cast<double>(completed - last_progress_completed_) / elapsed;
        }
    }
    if (total_bytes == 0 || completed_bytes > total_bytes || stage_changed ||
        (last_progress_completed_bytes_ && completed_bytes < *last_progress_completed_bytes_))
    {
        progress_speed_bytes_per_second_ = 0.0;
        last_progress_completed_bytes_.reset();
    }
    else if (last_progress_time_.has_value() && last_progress_completed_bytes_)
    {
        const auto elapsed = std::chrono::duration<double>(now - *last_progress_time_).count();
        if (elapsed > 0.0)
        {
            progress_speed_bytes_per_second_ =
                static_cast<double>(completed_bytes - *last_progress_completed_bytes_) / elapsed;
        }
    }
    if (total_bytes > 0 && completed_bytes <= total_bytes)
    {
        last_progress_completed_bytes_ = completed_bytes;
    }
    last_progress_time_ = now;
    last_progress_stage_ = stage;
    last_progress_completed_ = completed;

    auto status = stage.isEmpty() ? tr("Running") : stage;
    if (total > 0)
    {
        const auto percent =
            std::min(100.0, static_cast<double>(completed) * 100.0 / static_cast<double>(total));
        status += tr(" | Progress: %1/%2 (%3%)")
                      .arg(static_cast<qulonglong>(completed))
                      .arg(static_cast<qulonglong>(total))
                      .arg(QString::number(percent, 'f', 0));
    }
    if (stage.contains(QStringLiteral("hash"), Qt::CaseInsensitive))
    {
        if (progress_speed_units_per_second_ > 0.0)
        {
            status += tr(" | Hash speed: %1")
                          .arg(format_piece_rate(progress_speed_units_per_second_, tr("pieces/s")));
        }
        if (progress_speed_bytes_per_second_ > 0.0)
        {
            status +=
                tr(" | I/O speed: %1").arg(format_bytes_rate_iec(progress_speed_bytes_per_second_));
        }
    }
    set_status(status);
}

void MainWindow::set_busy(const bool busy)
{
    reset_progress_state();
    for (auto* button :
         {ui_->btnCreateCalcPieces,  ui_->btnCreateTorrent,       ui_->btnInspectLoad,
          ui_->btnInspectValidate,   ui_->btnModifyPreview,       ui_->btnModifySave,
          ui_->btnVerifyStart,       ui_->btnTrackerReloadFolder, ui_->btnTrackerBatchConvert,
          ui_->btnModifyTrackerAdd,  ui_->btnModifyTrackerRemove, ui_->btnModifyTrackerEdit,
          ui_->btnModifyTrackerUp,   ui_->btnModifyTrackerDown,   ui_->btnTrackerAdd,
          ui_->btnTrackerRemove,     ui_->btnTrackerEdit,         ui_->btnTrackerMoveUp,
          ui_->btnTrackerMoveDown,   ui_->btnCreateTrackerAdd,    ui_->btnCreateTrackerRemove,
          ui_->btnCreateTrackerEdit, ui_->btnCreateTrackerUp,     ui_->btnCreateTrackerDown})
    {
        button->setEnabled(!busy);
    }
    ui_->btnCreateCancel->setEnabled(busy);
    ui_->btnVerifyCancel->setEnabled(busy);
    ui_->btnTrackerCancel->setEnabled(busy);
}

void MainWindow::set_status(const QString& text)
{
    statusBar()->showMessage(text);
}

void MainWindow::show_error(const Error& error)
{
    const auto level = error.code == ErrorCode::Conflict || error.code == ErrorCode::Cancelled
                           ? core::LogLevel::Warning
                           : core::LogLevel::Error;
    const auto event = error.code == ErrorCode::Cancelled ? "cancel" : "failure";
    const LogFields fields{{"code", error_code_name(error.code)},
                           {"code_number", std::to_string(static_cast<int>(error.code))},
                           {"issue_count", std::to_string(error.issues.size())},
                           {"message", error.message}};
    if (!active_operations_.empty())
    {
        const auto active = active_operations_.back();
        finish_gui_operation(active.id, level, active.component, active.operation, event, fields);
    }
    else
    {
        logger_->log_event(level, "gui", "operation", event, fields);
    }
    const auto dialog_level =
        error.code == ErrorCode::Conflict || error.code == ErrorCode::Cancelled
            ? NotificationDialog::Level::Warning
            : NotificationDialog::Level::Error;
    NotificationDialog dialog(dialog_level, tr("Operation failed"), error_text(error), this);
    dialog.exec();
}

void MainWindow::show_task_error(const QString& message)
{
    const auto utf8_message = message.toUtf8().toStdString();
    if (!active_operations_.empty())
    {
        const auto active = active_operations_.back();
        finish_gui_operation(active.id, core::LogLevel::Error, active.component, active.operation,
                             "failure", {{"reason", "task_error"}, {"message", utf8_message}});
    }
    else
    {
        logger_->log_event(core::LogLevel::Error, "gui", "operation", "failure",
                           {{"message", utf8_message}});
    }
    set_busy(false);
    NotificationDialog::show_error(this, tr("Operation failed"), message);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    const auto target = path_drop_target(watched, *ui_);
    if (target == PathDropTarget::None)
        return QMainWindow::eventFilter(watched, event);

    if (event->type() == QEvent::DragEnter)
    {
        auto* drag = static_cast<QDragEnterEvent*>(event);
        if (local_path_from_drop(*drag->mimeData()))
            drag->acceptProposedAction();
        else
            drag->ignore();
        return true;
    }
    if (event->type() == QEvent::Drop)
    {
        auto* drop = static_cast<QDropEvent*>(event);
        const auto path = local_path_from_drop(*drop->mimeData());
        if (!path)
        {
            drop->ignore();
            return true;
        }

        switch (target)
        {
        case PathDropTarget::Create:
            ui_->editCreateInputPath->setText(*path);
            update_create_output_path();
            break;
        case PathDropTarget::Inspect:
            ui_->editInspectTorrentPath->setText(*path);
            load_inspect_torrent();
            break;
        case PathDropTarget::Modify:
            ui_->editModifyTorrentPath->setText(*path);
            load_modify_torrent();
            break;
        case PathDropTarget::Tracker:
            ui_->editTrackerSourcePath->setText(*path);
            update_tracker_output_path();
            load_tracker_torrent();
            break;
        case PathDropTarget::VerifyTorrent:
            ui_->editVerifyTorrentPath->setText(*path);
            load_verify_torrent();
            break;
        case PathDropTarget::VerifyContent:
            ui_->editVerifyContentPath->setText(*path);
            break;
        case PathDropTarget::None:
            break;
        }
        drop->acceptProposedAction();
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}
