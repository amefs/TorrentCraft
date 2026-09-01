#pragma once

#include <QDialog>
#include <QString>

namespace Ui {
class TrackerEditDialog;
}

class TrackerEditDialog final : public QDialog
{
    Q_OBJECT
  public:
    explicit TrackerEditDialog(int tier, QString tracker, const QString& title,
                               QWidget* parent = nullptr, bool multi_tier = false);
    ~TrackerEditDialog() override;

    [[nodiscard]] int tier() const noexcept;
    [[nodiscard]] QString tracker() const;

  public slots:
    void accept() override;

  private:
    Ui::TrackerEditDialog* ui_;
    bool multi_tier_{};
};
