#pragma once

#include <QDialog>

namespace Ui {
class OperationCloseDialog;
}

class OperationCloseDialog final : public QDialog
{
    Q_OBJECT
  public:
    explicit OperationCloseDialog(QWidget* parent = nullptr);
    ~OperationCloseDialog() override;

  private:
    Ui::OperationCloseDialog* ui_;
};
