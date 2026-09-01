#include "TrackerEditDialog.hpp"

#include "NotificationDialog.hpp"
#include "ui_TrackerEditDialog.h"

#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <torrentutils/core/tracker.hpp>
#include <utility>

TrackerEditDialog::TrackerEditDialog(const int tier, QString tracker, const QString& title,
                                     QWidget* parent, const bool multi_tier)
    : QDialog(parent), ui_(new Ui::TrackerEditDialog), multi_tier_(multi_tier)
{
    ui_->setupUi(this);
    setWindowTitle(title);
    ui_->tier->setRange(1, 999);
    ui_->tier->setValue(tier);
    ui_->tracker->setText(std::move(tracker));
    ui_->stackedWidget->setCurrentWidget(multi_tier_ ? ui_->pageMulti : ui_->pageSingle);

    connect(ui_->buttonBox, &QDialogButtonBox::accepted, this, &TrackerEditDialog::accept);
    connect(ui_->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

int TrackerEditDialog::tier() const noexcept
{
    return ui_->tier->value();
}

QString TrackerEditDialog::tracker() const
{
    return multi_tier_ ? ui_->trackers->toPlainText().trimmed() : ui_->tracker->text().trimmed();
}

void TrackerEditDialog::accept()
{
    const auto value = tracker();
    if (value.isEmpty())
    {
        NotificationDialog::show_warning(this, tr("Invalid tracker"),
                                         tr("Tracker URL cannot be empty."));
        return;
    }
    const auto lines =
        value.split(QRegularExpression(QStringLiteral("[\\r\\n]")), Qt::SkipEmptyParts);
    for (const auto& line : lines)
    {
        const auto trimmed = line.trimmed();
        if (trimmed.isEmpty())
        {
            continue;
        }
        const auto parsed = torrentutils::core::TrackerUrl::parse(trimmed.toUtf8().toStdString());
        if (!parsed)
        {
            NotificationDialog::show_warning(this, tr("Invalid tracker"),
                                             QString::fromStdString(parsed.error().message));
            return;
        }
    }
    QDialog::accept();
}

TrackerEditDialog::~TrackerEditDialog()
{
    delete ui_;
}
