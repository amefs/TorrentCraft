#include "FilterRulesDialog.hpp"

#include "ui_FilterRulesDialog.h"

#include <QDialogButtonBox>

FilterRulesDialog::FilterRulesDialog(const QString& rules, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FilterRulesDialog)
{
    ui_->setupUi(this);
    ui_->rules->setPlainText(rules);

    connect(ui_->buttonBox, &QDialogButtonBox::accepted, this, &FilterRulesDialog::accept);
    connect(ui_->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString FilterRulesDialog::rules() const
{
    return ui_->rules->toPlainText();
}

FilterRulesDialog::~FilterRulesDialog()
{
    delete ui_;
}
