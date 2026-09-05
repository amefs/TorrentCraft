#pragma once

#include <QDialog>
#include <QString>

namespace Ui {
class FilterRulesDialog;
}

class FilterRulesDialog final : public QDialog
{
    Q_OBJECT
  public:
    explicit FilterRulesDialog(const QString& rules, QWidget* parent = nullptr);
    ~FilterRulesDialog() override;

    [[nodiscard]] QString rules() const;

  private:
    Ui::FilterRulesDialog* ui_;
};
