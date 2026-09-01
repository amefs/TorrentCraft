#pragma once

#include <QDialog>

namespace Ui {
class PresetPreviewDialog;
}

class PresetPreviewDialog final : public QDialog
{
    Q_OBJECT
  public:
    explicit PresetPreviewDialog(const QString& name, const QString& preview,
                                 QWidget* parent = nullptr);
    ~PresetPreviewDialog() override;

  private:
    Ui::PresetPreviewDialog* ui_;
};
