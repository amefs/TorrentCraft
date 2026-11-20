#pragma once

#include <QDialog>
#include <torrentutils/frontend/torrent_template.hpp>
#include <utility>
#include <vector>

class QCheckBox;
class QDialogButtonBox;

/**
 * Lets the user choose which template fields of a torrent template become a preset.
 *
 * Each field shows the exact value that would be adopted. Fields the torrent cannot supply are
 * disabled and explain why.
 */
class TemplateFieldDialog final : public QDialog
{
    Q_OBJECT
  public:
    explicit TemplateFieldDialog(const QString& torrent_name,
                                 torrentutils::frontend::TorrentTemplate source,
                                 QWidget* parent = nullptr);

    [[nodiscard]] std::vector<torrentutils::frontend::TemplateField> selected_fields() const;
    [[nodiscard]] QCheckBox* checkbox(torrentutils::frontend::TemplateField field) const;

    [[nodiscard]] static QString field_label(torrentutils::frontend::TemplateField field);
    [[nodiscard]] static QString
    warning_text(const torrentutils::frontend::TemplateWarning& warning);

  private:
    void update_accept_state();
    [[nodiscard]] QString
    value_text(const torrentutils::frontend::TemplateFieldCandidate& field) const;
    [[nodiscard]] static QString
    unavailable_text(const torrentutils::frontend::TemplateFieldCandidate& field);

    torrentutils::frontend::TorrentTemplate source_;
    std::vector<std::pair<torrentutils::frontend::TemplateField, QCheckBox*>> checkboxes_;
    QDialogButtonBox* buttons_{};
};
