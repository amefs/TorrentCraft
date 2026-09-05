#include "LineNumberPlainTextEdit.hpp"

#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QTextBlock>

LineNumberArea::LineNumberArea(QPlainTextEdit* editor) : QWidget(editor), editor_(editor)
{
    setObjectName(QStringLiteral("lineNumberArea"));
}

QSize LineNumberArea::sizeHint() const
{
    return {0, 0};
}

void LineNumberArea::paintEvent(QPaintEvent* event)
{
    static_cast<LineNumberPlainTextEdit*>(editor_)->paint_line_number_area(event);
}

LineNumberPlainTextEdit::LineNumberPlainTextEdit(QWidget* parent)
    : QPlainTextEdit(parent), line_number_area_(new LineNumberArea(this))
{
    connect(this, &QPlainTextEdit::blockCountChanged, this,
            [this](const int) { update_line_number_area_width(); });
    connect(this, &QPlainTextEdit::updateRequest, this,
            [this](const QRect& rect, const int dy) { update_line_number_area(rect, dy); });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this,
            [this] { line_number_area_->update(); });

    update_line_number_area_width();
}

void LineNumberPlainTextEdit::resizeEvent(QResizeEvent* event)
{
    QPlainTextEdit::resizeEvent(event);
    const auto rect = contentsRect();
    line_number_area_->setGeometry(rect.left(), rect.top(), line_number_area_width(),
                                   rect.height());
}

int LineNumberPlainTextEdit::line_number_area_width() const
{
    auto digits = 1;
    auto maximum = qMax(1, blockCount());
    while (maximum >= 10)
    {
        maximum /= 10;
        ++digits;
    }
    return 6 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void LineNumberPlainTextEdit::update_line_number_area_width()
{
    setViewportMargins(line_number_area_width(), 0, 0, 0);
}

void LineNumberPlainTextEdit::update_line_number_area(const QRect& rect, const int dy)
{
    if (dy != 0)
    {
        line_number_area_->scroll(0, dy);
    }
    else
    {
        line_number_area_->update(0, rect.y(), line_number_area_->width(), rect.height());
    }

    if (rect.contains(viewport()->rect()))
    {
        update_line_number_area_width();
    }
}

void LineNumberPlainTextEdit::paint_line_number_area(QPaintEvent* event)
{
    QPainter painter(line_number_area_);
    painter.fillRect(event->rect(), palette().color(QPalette::AlternateBase));
    painter.setPen(palette().color(QPalette::Text));

    auto block = firstVisibleBlock();
    auto block_number = block.blockNumber();
    auto top = blockBoundingGeometry(block).translated(contentOffset()).top();
    auto bottom = top + blockBoundingRect(block).height();

    while (block.isValid() && top <= event->rect().bottom())
    {
        if (block.isVisible() && bottom >= event->rect().top())
        {
            const auto number = QString::number(block_number + 1);
            painter.drawText(0, static_cast<int>(top), line_number_area_->width() - 4,
                             fontMetrics().height(), Qt::AlignRight, number);
        }

        block = block.next();
        top = bottom;
        bottom = top + blockBoundingRect(block).height();
        ++block_number;
    }
}
