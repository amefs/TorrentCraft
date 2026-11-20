#include "GuiHelpers.hpp"

#include "GuiTaskRunner.hpp"
#include "NotificationDialog.hpp"
#include "TrackerEditDialog.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QLocale>
#include <QObject>
#include <QRegularExpression>
#include <QStandardItemModel>
#include <QTableView>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <limits>

namespace gui_util {

using namespace torrentutils;
using core::Error;
using core::ErrorCode;

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

std::filesystem::path path_from_text(const QString& value)
{
    return std::filesystem::u8path(value.toUtf8().toStdString());
}

QString path_to_text(const std::filesystem::path& value)
{
    const auto text = QString::fromUtf8(value.u8string().c_str());
    return QDir::toNativeSeparators(QDir::fromNativeSeparators(text));
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

} // namespace gui_util
