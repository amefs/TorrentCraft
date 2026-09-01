#include "LicenseDialog.hpp"

#include "ui_LicenseDialog.h"

LicenseDialog::LicenseDialog(const QString& title, const QString& text, QWidget* parent)
    : QDialog(parent), ui_(new Ui::LicenseDialog)
{
    ui_->setupUi(this);
    setWindowTitle(title);
    ui_->browser->setPlainText(text);
    connect(ui_->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui_->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::accept);
}
LicenseDialog::~LicenseDialog()
{
    delete ui_;
}
