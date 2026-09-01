#pragma once

#include <QDialog>
#include <QString>

namespace Ui {
class LicenseDialog;
}

class LicenseDialog final : public QDialog
{
    Q_OBJECT
  public:
    explicit LicenseDialog(const QString& title, const QString& text, QWidget* parent = nullptr);
    ~LicenseDialog() override;

  private:
    Ui::LicenseDialog* ui_;
};
