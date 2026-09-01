#pragma once

#include <QIcon>
#include <QPixmap>

namespace torrentcraft::gui {
inline constexpr auto kLogoResource = ":/torrentcraft/logo/TorrentCraft.svg";
QPixmap render_logo(int size);
QIcon application_icon();
} // namespace torrentcraft::gui
