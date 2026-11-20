#include "TemplateFieldDialog.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

namespace frontend = torrentutils::frontend;
using frontend::TemplateField;

namespace {

[[nodiscard]] QString utf8(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] QString format_text(const torrentutils::core::TorrentFormat format)
{
    switch (format)
    {
    case torrentutils::core::TorrentFormat::V1:
        return QStringLiteral("V1");
    case torrentutils::core::TorrentFormat::V2:
        return QStringLiteral("V2");
    case torrentutils::core::TorrentFormat::Hybrid:
        return QStringLiteral("Hybrid");
    }
    return {};
}

} // namespace

TemplateFieldDialog::TemplateFieldDialog(const QString& torrent_name,
                                         frontend::TorrentTemplate source, QWidget* parent)
    : QDialog(parent), source_(std::move(source))
{
    setObjectName(QStringLiteral("TemplateFieldDialog"));
    setWindowTitle(tr("Import from torrent: %1").arg(torrent_name));

    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(
        tr("Select the settings to adopt as a preset. Unselected settings keep the default "
           "values. Name, files, piece hashes and creation date are never imported."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    if (!source_.warnings.empty())
    {
        QStringList lines;
        for (const auto& warning : source_.warnings)
        {
            lines << warning_text(warning);
        }
        auto* warnings = new QLabel(lines.join(QLatin1Char('\n')), this);
        warnings->setObjectName(QStringLiteral("lblTemplateWarnings"));
        warnings->setWordWrap(true);
        warnings->setTextInteractionFlags(Qt::TextSelectableByMouse);
        warnings->setFrameShape(QFrame::StyledPanel);
        warnings->setStyleSheet(QStringLiteral("QLabel { padding: 6px; }"));
        layout->addWidget(warnings);
    }

    auto* grid = new QGridLayout;
    int row = 0;
    for (const auto& candidate : source_.fields)
    {
        auto* box = new QCheckBox(field_label(candidate.field), this);
        box->setObjectName(
            QStringLiteral("chkTemplateField_") +
            QString::fromLatin1(
                frontend::template_field_name(candidate.field).data(),
                static_cast<qsizetype>(frontend::template_field_name(candidate.field).size())));
        auto* value = new QLabel(this);
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        if (candidate.available())
        {
            box->setChecked(candidate.default_selected);
            value->setText(value_text(candidate));
        }
        else
        {
            box->setChecked(false);
            box->setEnabled(false);
            value->setEnabled(false);
            value->setText(unavailable_text(candidate));
        }
        box->setToolTip(value->text());
        grid->addWidget(box, row, 0, Qt::AlignTop);
        grid->addWidget(value, row, 1);
        connect(box, &QCheckBox::toggled, this, &TemplateFieldDialog::update_accept_state);
        checkboxes_.emplace_back(candidate.field, box);
        ++row;
    }
    grid->setColumnStretch(1, 1);
    layout->addLayout(grid);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons_);
    update_accept_state();
}

std::vector<TemplateField> TemplateFieldDialog::selected_fields() const
{
    std::vector<TemplateField> result;
    for (const auto& [field, box] : checkboxes_)
    {
        if (box->isEnabled() && box->isChecked())
        {
            result.push_back(field);
        }
    }
    return result;
}

QCheckBox* TemplateFieldDialog::checkbox(const TemplateField field) const
{
    for (const auto& [candidate, box] : checkboxes_)
    {
        if (candidate == field)
        {
            return box;
        }
    }
    return nullptr;
}

QString TemplateFieldDialog::field_label(const TemplateField field)
{
    switch (field)
    {
    case TemplateField::Trackers:
        return tr("Trackers");
    case TemplateField::PieceSize:
        return tr("Piece size");
    case TemplateField::Private:
        return tr("Private");
    case TemplateField::Source:
        return tr("Source");
    case TemplateField::WebSeeds:
        return tr("Web seeds");
    case TemplateField::Format:
        return tr("Format");
    case TemplateField::Comment:
        return tr("Comment");
    case TemplateField::CreatedBy:
        return tr("Created by");
    }
    return {};
}

void TemplateFieldDialog::update_accept_state()
{
    if (auto* ok = buttons_->button(QDialogButtonBox::Ok))
    {
        ok->setEnabled(!selected_fields().empty());
    }
}

QString TemplateFieldDialog::value_text(const frontend::TemplateFieldCandidate& field) const
{
    const auto& patch = source_.adoptable;
    switch (field.field)
    {
    case TemplateField::Trackers: {
        QStringList tiers;
        int tier_number = 1;
        for (const auto& tier :
             patch.tracker_tiers.value_or(std::vector<std::vector<std::string>>{}))
        {
            QStringList urls;
            for (const auto& url : tier)
            {
                urls << utf8(url);
            }
            tiers << tr("Tier %1: %2").arg(tier_number++).arg(urls.join(QStringLiteral(", ")));
        }
        return tiers.join(QLatin1Char('\n'));
    }
    case TemplateField::PieceSize:
        return tr("%1 KiB").arg(
            patch.piece_size && patch.piece_size->fixed_kib ? *patch.piece_size->fixed_kib : 0U);
    case TemplateField::Private:
        return patch.is_private.value_or(false) ? tr("Yes") : tr("No");
    case TemplateField::Source:
        return utf8(patch.info_source.value_or(std::string{}));
    case TemplateField::WebSeeds: {
        QStringList seeds;
        for (const auto& seed : patch.web_seeds.value_or(std::vector<std::string>{}))
        {
            seeds << utf8(seed);
        }
        return seeds.join(QLatin1Char('\n'));
    }
    case TemplateField::Format:
        return patch.format ? format_text(*patch.format) : QString{};
    case TemplateField::Comment:
        return utf8(patch.comment.value_or(std::string{}));
    case TemplateField::CreatedBy:
        return utf8(patch.created_by.value_or(std::string{}));
    }
    return {};
}

QString TemplateFieldDialog::unavailable_text(const frontend::TemplateFieldCandidate& field)
{
    switch (field.unavailability)
    {
    case frontend::TemplateFieldUnavailability::None:
        return {};
    case frontend::TemplateFieldUnavailability::NotPresent:
        return tr("Not present in this torrent");
    case frontend::TemplateFieldUnavailability::Unreadable:
        return tr("Present but unreadable in this torrent");
    case frontend::TemplateFieldUnavailability::UnsupportedPieceSize:
        return tr("Unsupported piece size (%1 bytes); must be a power of two from 16 KiB to 16 MiB")
            .arg(field.rejected_piece_length.value_or(0U));
    }
    return {};
}

QString TemplateFieldDialog::warning_text(const frontend::TemplateWarning& warning)
{
    switch (warning.kind)
    {
    case frontend::TemplateWarning::Kind::NonStandardFields: {
        QStringList keys;
        for (const auto& key : warning.keys)
        {
            keys << utf8(key);
        }
        return tr("Ignored non-standard metadata: %1").arg(keys.join(QStringLiteral(", ")));
    }
    case frontend::TemplateWarning::Kind::InvalidEntriesDropped:
        return tr("%1: ignored %n invalid entries", nullptr, static_cast<int>(warning.count))
            .arg(field_label(warning.field.value_or(TemplateField::Trackers)));
    }
    return {};
}
