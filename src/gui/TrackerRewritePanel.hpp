#pragma once

#include <QString>
#include <QWidget>
#include <optional>
#include <torrentutils/core/tracker.hpp>
#include <torrentutils/core/tracker_rewrite.hpp>
#include <vector>

class QEvent;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QShowEvent;
class QSplitter;
class QStandardItemModel;
class QTableView;
class QTimer;
class QToolButton;
class QVBoxLayout;

/**
 * Edits find-and-replace tracker rewrite rules, VS Code style, and previews them against one
 * selected torrent. Each rule row has Find and Replace fields plus Match case and regular
 * expression toggles; rows apply top to bottom.
 *
 * Rules are kept only in memory; they may contain tracker passkeys.
 */
class TrackerRewritePanel final : public QWidget
{
    Q_OBJECT
  public:
    explicit TrackerRewritePanel(QWidget* parent = nullptr);

    /** Compiled rules, or empty while the rules are missing or invalid. */
    [[nodiscard]] const std::optional<torrentutils::core::TrackerRewriteRules>&
    rules() const noexcept;

    /** Shows the rewrite of `trackers` for the torrent named `name`; empty clears the preview. */
    void set_preview_source(const QString& name,
                            std::optional<torrentutils::core::TrackerList> trackers);

    /** Recompiles the rules and refreshes the preview immediately. */
    void refresh();
    void clear();

    /** Appends an empty rule row and returns its zero-based index. */
    int add_rule();
    /** Removes the rule row at `index`; the last remaining row is cleared instead. */
    void remove_rule(int index);

    [[nodiscard]] int rule_count() const noexcept;
    [[nodiscard]] QLabel* find_label(int index) const;
    [[nodiscard]] QLineEdit* find_edit(int index) const;
    [[nodiscard]] QLineEdit* replace_edit(int index) const;
    [[nodiscard]] QToolButton* match_case_button(int index) const;
    [[nodiscard]] QToolButton* regex_button(int index) const;
    [[nodiscard]] QToolButton* remove_button(int index) const;
    [[nodiscard]] QPushButton* add_rule_button() const noexcept;
    [[nodiscard]] QTableView* preview_table() const noexcept;
    [[nodiscard]] QPushButton* preview_all_button() const noexcept;
    [[nodiscard]] QLabel* error_label() const noexcept;

    [[nodiscard]] static QString status_text(torrentutils::core::TrackerRewriteStatus status);

  protected:
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;

  signals:
    /** Emitted after the rules were recompiled; `valid` is false when they cannot be used. */
    void rulesChanged(bool valid);

  private:
    struct RuleRow
    {
        QWidget* container{};
        QFrame* separator{};
        QLabel* number{};
        QLabel* find_label{};
        QLineEdit* find{};
        QLabel* replace_label{};
        QLineEdit* replace{};
        QToolButton* match_case{};
        QToolButton* regex{};
        QToolButton* remove{};
    };

    void retranslate_ui();
    void retranslate_row(const RuleRow& row, int index);
    void renumber_rules();
    void fit_rules_pane();
    void compile_rules();
    void update_preview();

    QLabel* rules_label_{};
    QSplitter* splitter_{};
    QScrollArea* rules_area_{};
    QVBoxLayout* rules_layout_{};
    std::vector<RuleRow> rule_rows_;
    QPushButton* add_rule_button_{};
    QLabel* error_label_{};
    QLabel* source_label_{};
    QTableView* preview_table_{};
    QStandardItemModel* preview_model_{};
    QPushButton* preview_all_button_{};
    QTimer* debounce_{};
    QString source_name_;
    std::optional<torrentutils::core::TrackerList> source_trackers_;
    std::optional<torrentutils::core::TrackerRewriteRules> rules_;
};
