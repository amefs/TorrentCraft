#pragma once

#include <QDialog>
#include <QString>

namespace Ui {
class NotificationDialog;
}

class NotificationDialog final : public QDialog
{
    Q_OBJECT
  public:
    enum class Level
    {
        Info,
        Warning,
        Error,
    };

    explicit NotificationDialog(Level level, QString title, QString message,
                                QWidget* parent = nullptr);
    ~NotificationDialog() override;

    static void show_info(QWidget* parent, const QString& title, const QString& message);
    static void show_warning(QWidget* parent, const QString& title, const QString& message);
    static void show_error(QWidget* parent, const QString& title, const QString& message);

  private:
    Ui::NotificationDialog* ui_;
};
