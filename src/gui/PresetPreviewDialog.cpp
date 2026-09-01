#include "PresetPreviewDialog.hpp"

#include "ui_PresetPreviewDialog.h"

PresetPreviewDialog::PresetPreviewDialog(const QString& name, const QString& preview,
                                         QWidget* parent)
    : QDialog(parent), ui_(new Ui::PresetPreviewDialog)
{
    ui_->setupUi(this);
    setWindowTitle(tr("Preset preview: %1").arg(name));
    ui_->lblPresetName->setText(name);
    ui_->editPresetPreview->setPlainText(preview);
    connect(ui_->btnBoxPresetPreview, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui_->btnBoxPresetPreview, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

PresetPreviewDialog::~PresetPreviewDialog()
{
    delete ui_;
}
