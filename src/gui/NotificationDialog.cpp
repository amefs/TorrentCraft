#include "NotificationDialog.hpp"

#include "ui_NotificationDialog.h"

#include <QApplication>
#include <QStyle>

NotificationDialog::NotificationDialog(const Level level, QString title, QString message,
                                       QWidget* parent)
    : QDialog(parent), ui_(new Ui::NotificationDialog)
{
    ui_->setupUi(this);
    setWindowTitle(title);
    ui_->lblNotificationTitle->setText(std::move(title));
    ui_->editNotificationMessage->setPlainText(std::move(message));

    QStyle::StandardPixmap icon = QStyle::SP_MessageBoxInformation;
    switch (level)
    {
    case Level::Info:
        icon = QStyle::SP_MessageBoxInformation;
        break;
    case Level::Warning:
        icon = QStyle::SP_MessageBoxWarning;
        break;
    case Level::Error:
        icon = QStyle::SP_MessageBoxCritical;
        break;
    }
    ui_->lblNotificationIcon->setPixmap(style()->standardIcon(icon).pixmap(48, 48));
    connect(ui_->btnBoxNotification, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui_->btnBoxNotification, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

NotificationDialog::~NotificationDialog()
{
    delete ui_;
}

void NotificationDialog::show_info(QWidget* parent, const QString& title, const QString& message)
{
    NotificationDialog(Level::Info, title, message, parent).exec();
}

void NotificationDialog::show_warning(QWidget* parent, const QString& title, const QString& message)
{
    NotificationDialog(Level::Warning, title, message, parent).exec();
}

void NotificationDialog::show_error(QWidget* parent, const QString& title, const QString& message)
{
    NotificationDialog(Level::Error, title, message, parent).exec();
}
