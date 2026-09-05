#pragma once

#include <QPlainTextEdit>

class QPaintEvent;
class QResizeEvent;

class LineNumberArea final : public QWidget
{
  public:
    explicit LineNumberArea(QPlainTextEdit* editor);

    [[nodiscard]] QSize sizeHint() const override;

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    QPlainTextEdit* editor_;
};

class LineNumberPlainTextEdit final : public QPlainTextEdit
{
  public:
    explicit LineNumberPlainTextEdit(QWidget* parent = nullptr);

  protected:
    void resizeEvent(QResizeEvent* event) override;

  private:
    friend class LineNumberArea;

    [[nodiscard]] int line_number_area_width() const;
    void update_line_number_area_width();
    void update_line_number_area(const QRect& rect, int dy);
    void paint_line_number_area(QPaintEvent* event);

    LineNumberArea* line_number_area_;
};
