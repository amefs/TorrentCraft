#include "Logo.hpp"

#include <QPainter>
#include <QSvgRenderer>

namespace torrentcraft::gui {
QPixmap render_logo(const int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    QSvgRenderer renderer(QString::fromLatin1(kLogoResource));
    renderer.render(&painter);
    return pixmap;
}

QIcon application_icon()
{
    QIcon icon;
    for (const auto size : {16, 24, 32, 48, 64, 128, 256})
        icon.addPixmap(render_logo(size));
    return icon;
}
} // namespace torrentcraft::gui
