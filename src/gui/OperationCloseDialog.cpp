#include "OperationCloseDialog.hpp"

#include "ui_OperationCloseDialog.h"

OperationCloseDialog::OperationCloseDialog(QWidget* parent)
    : QDialog(parent), ui_(new Ui::OperationCloseDialog)
{
    ui_->setupUi(this);
    ui_->lblMessage->setText(tr("A task is still running. Cancel it and exit?"));
    connect(ui_->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui_->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

OperationCloseDialog::~OperationCloseDialog()
{
    delete ui_;
}
