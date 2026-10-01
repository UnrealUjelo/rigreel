// Frame grabs. PrintWindow(PW_RENDERFULLCONTENT) hands us the last frame DWM composited for the window even when
// other windows cover it, so the game can be captured while the editor or a dialog sits on top.
#pragma once
#include <QImage>
#include <QByteArray>

namespace Capture {
QImage window(quintptr hwnd);                      // client area; falls back to a screen grab
QImage screenRect(int x, int y, int w, int h);     // physical pixels
QRect windowRect(quintptr hwnd);
QByteArray png(const QImage &img, int compression = 1);
}
