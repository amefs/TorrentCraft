#include "TrackerRewritePanel.hpp"

#include <QColor>
#include <QEvent>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSize>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStringList>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

namespace core = torrentutils::core;

namespace {

constexpr int kDebounceMilliseconds = 300;
constexpr int kRuleRowSpacing = 8;
constexpr int kPreferredVisibleRules = 2;
const QString kErrorStyle = QStringLiteral("QLineEdit { border: 1px solid #c62828; }");

[[nodiscard]] QString utf8(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

/** Scroll area whose preferred height fits up to kPreferredVisibleRules rule rows. */
class RulesScrollArea final : public QScrollArea
{
  public:
    using QScrollArea::QScrollArea;

    [[nodiscard]] QSize sizeHint() const override
    {
        auto hint = QScrollArea::sizeHint();
        const auto* rows = widget() != nullptr ? widget()->layout() : nullptr;
        if (rows == nullptr)
        {
            return hint;
        }
        auto height = 0;
        auto counted = 0;
        for (int index = 0; index < rows->count() && counted < kPreferredVisibleRules; ++index)
        {
            if (auto* row = rows->itemAt(index)->widget(); row != nullptr)
            {
                height += row->sizeHint().height() + (counted > 0 ? rows->spacing() : 0);
                ++counted;
            }
        }
        hint.setHeight(std::max(height, minimumHeight()));
        return hint;
    }
};

} // namespace

TrackerRewritePanel::TrackerRewritePanel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("TrackerRewritePanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    splitter_ = new QSplitter(Qt::Vertical, this);
    splitter_->setObjectName(QStringLiteral("splitTrackerRewrite"));
    splitter_->setChildrenCollapsible(false);
    layout->addWidget(splitter_);

    // Top pane: the rule rows, the add button with a short hint, and the rule error.
    auto* rules_pane = new QWidget(splitter_);
    auto* rules_pane_layout = new QVBoxLayout(rules_pane);
    rules_pane_layout->setContentsMargins(0, 0, 0, 0);

    auto* rules_widget = new QWidget;
    rules_layout_ = new QVBoxLayout(rules_widget);
    rules_layout_->setContentsMargins(0, 0, 0, 0);
    rules_layout_->setSpacing(kRuleRowSpacing);
    rules_layout_->addStretch(1);
    rules_area_ = new RulesScrollArea(rules_pane);
    rules_area_->setObjectName(QStringLiteral("scrollTrackerRewriteRules"));
    rules_area_->setWidgetResizable(true);
    rules_area_->setFrameShape(QFrame::NoFrame);
    rules_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rules_area_->setWidget(rules_widget);
    rules_pane_layout->addWidget(rules_area_, 1);

    auto* rules_footer = new QHBoxLayout;
    add_rule_button_ = new QPushButton(rules_pane);
    add_rule_button_->setObjectName(QStringLiteral("btnTrackerAddRule"));
    rules_footer->addWidget(add_rule_button_, 0, Qt::AlignTop);
    rules_label_ = new QLabel(rules_pane);
    rules_label_->setObjectName(QStringLiteral("lblTrackerRewriteHint"));
    // Wrapped labels report a height-for-width that QSplitter and the page layout ignore, which
    // let the footer overlap the rules in short windows. Keep the hint and error on one line.
    rules_label_->setForegroundRole(QPalette::PlaceholderText);
    rules_footer->addWidget(rules_label_, 1);
    rules_pane_layout->addLayout(rules_footer);
    connect(add_rule_button_, &QPushButton::clicked, this, [this] {
        const auto index = add_rule();
        find_edit(index)->setFocus();
        rules_area_->ensureWidgetVisible(rule_rows_.back().container);
    });

    error_label_ = new QLabel(rules_pane);
    error_label_->setObjectName(QStringLiteral("lblTrackerRewriteError"));
    error_label_->setStyleSheet(QStringLiteral("color: #c62828;"));
    error_label_->hide();
    rules_pane_layout->addWidget(error_label_);

    // Bottom pane: which torrent is previewed, Preview all, and the per-URL table.
    auto* preview_pane = new QWidget(splitter_);
    auto* preview_layout = new QVBoxLayout(preview_pane);
    preview_layout->setContentsMargins(0, 0, 0, 0);
    auto* preview_header = new QHBoxLayout;
    source_label_ = new QLabel(preview_pane);
    source_label_->setObjectName(QStringLiteral("lblTrackerRewriteSource"));
    preview_header->addWidget(source_label_, 1);
    preview_all_button_ = new QPushButton(preview_pane);
    preview_all_button_->setObjectName(QStringLiteral("btnTrackerPreviewAll"));
    preview_header->addWidget(preview_all_button_);
    preview_layout->addLayout(preview_header);

    preview_model_ = new QStandardItemModel(this);
    preview_table_ = new QTableView(preview_pane);
    preview_table_->setObjectName(QStringLiteral("tblTrackerRewritePreview"));
    preview_table_->setModel(preview_model_);
    preview_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    preview_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    preview_table_->setWordWrap(false);
    preview_table_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    preview_table_->verticalHeader()->hide();
    preview_table_->setMinimumHeight(80);
    preview_layout->addWidget(preview_table_, 1);

    splitter_->addWidget(rules_pane);
    splitter_->addWidget(preview_pane);
    // Rules keep their preferred height (up to two rows); the preview takes the remaining room.
    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 1);

    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(kDebounceMilliseconds);
    connect(debounce_, &QTimer::timeout, this, &TrackerRewritePanel::refresh);

    add_rule();
    retranslate_ui();
    refresh();
}

void TrackerRewritePanel::retranslate_ui()
{
    rules_label_->setText(tr("Rules run top to bottom. With .* on, $1 inserts a group."));
    rules_label_->setToolTip(tr("Rules apply from top to bottom. With .* on, Find is a regular "
                                "expression and $1 inserts a captured group."));
    add_rule_button_->setText(tr("+ Add rule"));
    add_rule_button_->setToolTip(tr("Add another find and replace rule"));
    for (std::size_t index = 0; index < rule_rows_.size(); ++index)
    {
        retranslate_row(rule_rows_[index], static_cast<int>(index));
    }
    preview_all_button_->setText(tr("Preview all"));
    preview_all_button_->setToolTip(
        tr("Check every listed torrent and show the result in the Status column"));
    preview_table_->setAccessibleName(tr("Tracker rewrite preview"));
}

void TrackerRewritePanel::retranslate_row(const RuleRow& row, const int index)
{
    const auto number = index + 1;
    row.number->setText(tr("%1.").arg(number));
    row.find_label->setText(tr("Find"));
    row.find->setAccessibleName(tr("Find text for rule %1").arg(number));
    row.replace_label->setText(tr("Replace"));
    row.replace->setAccessibleName(tr("Replacement for rule %1").arg(number));
    row.match_case->setToolTip(tr("Match case"));
    row.match_case->setAccessibleName(tr("Match case for rule %1").arg(number));
    row.regex->setToolTip(tr("Use regular expression"));
    row.regex->setAccessibleName(tr("Use regular expression for rule %1").arg(number));
    row.remove->setToolTip(tr("Remove rule"));
    row.remove->setAccessibleName(tr("Remove rule %1").arg(number));
}

int TrackerRewritePanel::add_rule()
{
    RuleRow row;
    row.container = new QWidget;
    auto* row_layout = new QVBoxLayout(row.container);
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(kRuleRowSpacing);
    row.separator = new QFrame(row.container);
    row.separator->setFrameShape(QFrame::HLine);
    row.separator->setFrameShadow(QFrame::Sunken);
    row_layout->addWidget(row.separator);
    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(4);
    row_layout->addLayout(grid);

    const auto fixed_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    row.number = new QLabel(row.container);
    auto number_font = row.number->font();
    number_font.setBold(true);
    row.number->setFont(number_font);
    row.number->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row.number->setMinimumWidth(row.number->fontMetrics().horizontalAdvance(QStringLiteral("00.")));

    row.find_label = new QLabel(row.container);
    row.find = new QLineEdit(row.container);
    row.find->setObjectName(QStringLiteral("editTrackerRewriteFind"));
    row.find->setFont(fixed_font);
    row.find->setClearButtonEnabled(true);
    row.find->setPlaceholderText(QStringLiteral("tracker.old.example"));
    row.find_label->setBuddy(row.find);
    row.replace_label = new QLabel(row.container);
    row.replace = new QLineEdit(row.container);
    row.replace->setObjectName(QStringLiteral("editTrackerRewriteReplace"));
    row.replace->setFont(fixed_font);
    row.replace->setClearButtonEnabled(true);
    row.replace->setPlaceholderText(QStringLiteral("tracker.new.example"));
    row.replace_label->setBuddy(row.replace);

    // Square buttons as tall as the edits; toggles stay framed so their state is visible.
    const auto side = row.find->sizeHint().height();
    const auto make_button = [&row, side](const QString& name, const QString& text) {
        auto* button = new QToolButton(row.container);
        button->setObjectName(name);
        button->setText(text);
        button->setFixedSize(side, side);
        return button;
    };
    row.match_case =
        make_button(QStringLiteral("btnTrackerRewriteMatchCase"), QStringLiteral("Aa"));
    row.match_case->setCheckable(true);
    row.match_case->setChecked(true);
    row.regex = make_button(QStringLiteral("btnTrackerRewriteRegex"), QStringLiteral(".*"));
    row.regex->setCheckable(true);
    row.remove = make_button(QStringLiteral("btnTrackerRewriteRemove"), QStringLiteral("\u00d7"));
    row.remove->setAutoRaise(true);

    grid->addWidget(row.number, 0, 0);
    grid->addWidget(row.find_label, 0, 1);
    grid->addWidget(row.find, 0, 2);
    grid->addWidget(row.match_case, 0, 3);
    grid->addWidget(row.regex, 0, 4);
    grid->addWidget(row.remove, 0, 5);
    grid->addWidget(row.replace_label, 1, 1);
    grid->addWidget(row.replace, 1, 2);
    grid->setColumnStretch(2, 1);

    const auto restart = [this] { debounce_->start(); };
    connect(row.find, &QLineEdit::textChanged, this, restart);
    connect(row.replace, &QLineEdit::textChanged, this, restart);
    connect(row.match_case, &QToolButton::toggled, this, restart);
    connect(row.regex, &QToolButton::toggled, this, restart);
    QWidget* container = row.container;
    connect(row.remove, &QToolButton::clicked, this, [this, container] {
        for (std::size_t index = 0; index < rule_rows_.size(); ++index)
        {
            if (rule_rows_[index].container == container)
            {
                remove_rule(static_cast<int>(index));
                return;
            }
        }
    });

    // The stretch item stays last so rows pack at the top.
    rules_layout_->insertWidget(rules_layout_->count() - 1, row.container);
    rule_rows_.push_back(row);
    renumber_rules();
    debounce_->start();
    return static_cast<int>(rule_rows_.size()) - 1;
}

void TrackerRewritePanel::remove_rule(const int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= rule_rows_.size())
    {
        return;
    }
    if (rule_rows_.size() == 1U)
    {
        const auto& row = rule_rows_.front();
        row.find->clear();
        row.replace->clear();
        row.match_case->setChecked(true);
        row.regex->setChecked(false);
        return;
    }
    const auto position = rule_rows_.begin() + index;
    position->container->deleteLater();
    rules_layout_->removeWidget(position->container);
    rule_rows_.erase(position);
    renumber_rules();
    debounce_->start();
}

void TrackerRewritePanel::renumber_rules()
{
    for (std::size_t index = 0; index < rule_rows_.size(); ++index)
    {
        retranslate_row(rule_rows_[index], static_cast<int>(index));
        rule_rows_[index].remove->setEnabled(rule_rows_.size() > 1U);
    }
    rule_rows_.front().separator->hide();
    for (std::size_t index = 1; index < rule_rows_.size(); ++index)
    {
        rule_rows_[index].separator->show();
    }
    // One whole rule stays visible however far the splitter is dragged; more rules scroll.
    rules_area_->setMinimumHeight(rule_rows_.front().container->sizeHint().height());
    rules_area_->updateGeometry();
    fit_rules_pane();
}

void TrackerRewritePanel::fit_rules_pane()
{
    // The splitter keeps its sizes when rules are added or removed, so move room between the
    // preview and the rules until the rules have their preferred height (up to two rows).
    if (splitter_ == nullptr || splitter_->count() != 2 || !splitter_->isVisible())
    {
        return;
    }
    auto* rules_pane = splitter_->widget(0);
    if (auto* pane_layout = rules_pane->layout(); pane_layout != nullptr)
    {
        pane_layout->invalidate();
        pane_layout->activate();
    }
    auto sizes = splitter_->sizes();
    const auto wanted = rules_pane->sizeHint().height();
    const auto spare = sizes[1] - splitter_->widget(1)->minimumSizeHint().height();
    // Growing is limited by how far the preview can shrink; shrinking returns all the room.
    const auto delta = std::min(wanted - sizes[0], spare);
    if (delta == 0)
    {
        return;
    }
    sizes[0] += delta;
    sizes[1] -= delta;
    splitter_->setSizes(sizes);
}

void TrackerRewritePanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // The splitter lays out its panes on first show; fit the rules once that has happened.
    QTimer::singleShot(0, this, &TrackerRewritePanel::fit_rules_pane);
}

void TrackerRewritePanel::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        retranslate_ui();
        compile_rules();
        update_preview();
    }
}

const std::optional<core::TrackerRewriteRules>& TrackerRewritePanel::rules() const noexcept
{
    return rules_;
}

void TrackerRewritePanel::set_preview_source(const QString& name,
                                             std::optional<core::TrackerList> trackers)
{
    source_name_ = name;
    source_trackers_ = std::move(trackers);
    update_preview();
}

void TrackerRewritePanel::refresh()
{
    debounce_->stop();
    compile_rules();
    update_preview();
    emit rulesChanged(rules_.has_value());
}

void TrackerRewritePanel::clear()
{
    while (rule_rows_.size() > 1U)
    {
        remove_rule(static_cast<int>(rule_rows_.size()) - 1);
    }
    remove_rule(0);
    source_name_.clear();
    source_trackers_.reset();
    refresh();
}

int TrackerRewritePanel::rule_count() const noexcept
{
    return static_cast<int>(rule_rows_.size());
}

QLineEdit* TrackerRewritePanel::find_edit(const int index) const
{
    return rule_rows_.at(static_cast<std::size_t>(index)).find;
}

QLineEdit* TrackerRewritePanel::replace_edit(const int index) const
{
    return rule_rows_.at(static_cast<std::size_t>(index)).replace;
}

QToolButton* TrackerRewritePanel::match_case_button(const int index) const
{
    return rule_rows_.at(static_cast<std::size_t>(index)).match_case;
}

QToolButton* TrackerRewritePanel::regex_button(const int index) const
{
    return rule_rows_.at(static_cast<std::size_t>(index)).regex;
}

QToolButton* TrackerRewritePanel::remove_button(const int index) const
{
    return rule_rows_.at(static_cast<std::size_t>(index)).remove;
}

QLabel* TrackerRewritePanel::find_label(const int index) const
{
    return rule_rows_.at(static_cast<std::size_t>(index)).find_label;
}

QPushButton* TrackerRewritePanel::add_rule_button() const noexcept
{
    return add_rule_button_;
}

QTableView* TrackerRewritePanel::preview_table() const noexcept
{
    return preview_table_;
}

QPushButton* TrackerRewritePanel::preview_all_button() const noexcept
{
    return preview_all_button_;
}

QLabel* TrackerRewritePanel::error_label() const noexcept
{
    return error_label_;
}

QString TrackerRewritePanel::status_text(const core::TrackerRewriteStatus status)
{
    switch (status)
    {
    case core::TrackerRewriteStatus::Unchanged:
        return tr("Unchanged");
    case core::TrackerRewriteStatus::Changed:
        return tr("Changed");
    case core::TrackerRewriteStatus::Merged:
        return tr("Merged (duplicate)");
    case core::TrackerRewriteStatus::Invalid:
        return tr("Invalid");
    }
    return {};
}

void TrackerRewritePanel::compile_rules()
{
    rules_.reset();
    std::vector<core::TrackerRewriteRuleSpec> specs;
    std::vector<std::size_t> row_indices;
    for (std::size_t index = 0; index < rule_rows_.size(); ++index)
    {
        const auto& row = rule_rows_[index];
        row.find->setStyleSheet({});
        if (row.find->text().isEmpty())
        {
            continue;
        }
        core::TrackerRewriteRuleSpec spec;
        spec.find = row.find->text().toUtf8().toStdString();
        spec.replace = row.replace->text().toUtf8().toStdString();
        spec.syntax = row.regex->isChecked() ? core::TrackerRewriteSyntax::Regex
                                             : core::TrackerRewriteSyntax::Literal;
        spec.match_case = row.match_case->isChecked();
        specs.push_back(std::move(spec));
        row_indices.push_back(index);
    }
    if (specs.empty())
    {
        error_label_->hide();
        return;
    }

    auto compiled = core::TrackerRewriteRules::compile(specs);
    if (compiled)
    {
        rules_ = std::move(compiled).value();
        error_label_->hide();
        return;
    }
    const auto& error = compiled.error();
    QString detail = utf8(error.message);
    if (!error.issues.empty())
    {
        detail = utf8(error.issues.front().message);
        const auto& field = error.issues.front().field;
        const auto open = field.find('[');
        const auto close = field.find(']');
        if (open != std::string::npos && close != std::string::npos && close > open + 1U)
        {
            const auto index = std::stoul(field.substr(open + 1U, close - open - 1U));
            if (index < row_indices.size())
            {
                const auto& row = rule_rows_[row_indices[index]];
                row.find->setStyleSheet(kErrorStyle);
                error_label_->setText(tr("Rule %1: %2").arg(row_indices[index] + 1U).arg(detail));
                error_label_->show();
                return;
            }
        }
    }
    error_label_->setText(detail);
    error_label_->show();
}

void TrackerRewritePanel::update_preview()
{
    preview_model_->clear();
    preview_model_->setHorizontalHeaderLabels(
        {tr("Tier"), tr("Original URL"), tr("Result"), tr("Status")});
    if (!source_trackers_)
    {
        source_label_->setText(tr("Select a torrent in the list below to preview the rewrite."));
        return;
    }
    if (!rules_)
    {
        source_label_->setText(
            tr("Preview: %1 (enter valid rules to see the result)").arg(source_name_));
    }
    else
    {
        source_label_->setText(tr("Preview: %1").arg(source_name_));
    }

    std::optional<core::TrackerRewritePreview> preview;
    if (rules_)
    {
        preview = core::preview_tracker_rewrite(*source_trackers_, *rules_);
    }
    std::size_t position = 0;
    for (std::size_t tier = 0; tier < source_trackers_->tiers().size(); ++tier)
    {
        for (const auto& tracker : source_trackers_->tiers()[tier].trackers())
        {
            QList<QStandardItem*> row{new QStandardItem(QString::number(tier + 1U)),
                                      new QStandardItem(utf8(tracker.value()))};
            if (preview && position < preview->changes.size())
            {
                const auto& change = preview->changes[position];
                auto* status = new QStandardItem(status_text(change.status));
                if (!change.reason.empty())
                {
                    status->setToolTip(utf8(change.reason));
                    status->setText(
                        tr("%1: %2").arg(status_text(change.status), utf8(change.reason)));
                }
                if (change.status == core::TrackerRewriteStatus::Invalid)
                {
                    status->setForeground(QColor(0xc6, 0x28, 0x28));
                }
                row << new QStandardItem(utf8(change.to)) << status;
            }
            else
            {
                row << new QStandardItem << new QStandardItem;
            }
            for (auto* item : row)
            {
                item->setToolTip(item->toolTip().isEmpty() ? item->text() : item->toolTip());
            }
            preview_model_->appendRow(row);
            ++position;
        }
    }
    auto* header = preview_table_->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    header->setStretchLastSection(true);
}
