#include "Capture.h"

#include <QBuffer>
#include <QImageWriter>
#include <QRect>

#include <windows.h>

#include <algorithm>

namespace {

QImage fromDC(HDC src, int w, int h, bool printWindow, HWND hwnd, bool *ok)
{
    HDC mem = CreateCompatibleDC(src);
    HBITMAP bmp = CreateCompatibleBitmap(src, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    bool good = true;
    if (printWindow)
        good = PrintWindow(hwnd, mem, 2 /* PW_RENDERFULLCONTENT */);
    else
        BitBlt(mem, 0, 0, w, h, src, 0, 0, SRCCOPY | CAPTUREBLT);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    QImage img(w, h, QImage::Format_RGB32);
    GetDIBits(mem, bmp, 0, UINT(h), img.bits(), &bmi, DIB_RGB_COLORS);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    if (ok)
        *ok = good;
    return img;
}

} // namespace

namespace Capture {

QRect windowRect(quintptr hwnd)
{
    RECT r{};
    GetWindowRect(HWND(hwnd), &r);
    return QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
}

QImage screenRect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return {};
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY | CAPTUREBLT);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    QImage img(w, h, QImage::Format_RGB32);
    GetDIBits(mem, bmp, 0, UINT(h), img.bits(), &bmi, DIB_RGB_COLORS);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return img;
}

QImage window(quintptr hwndIn)
{
    HWND hwnd = HWND(hwndIn);
    if (!hwnd || !IsWindow(hwnd))
        return {};
    RECT r{};
    GetClientRect(hwnd, &r);
    const int w = std::max<LONG>(1, r.right), h = std::max<LONG>(1, r.bottom);
    HDC dc = GetDC(hwnd);
    bool ok = false;
    QImage img = fromDC(dc, w, h, true, hwnd, &ok);
    ReleaseDC(hwnd, dc);
    if (!ok) {
        const QRect wr = windowRect(hwndIn);
        return screenRect(wr.x(), wr.y(), wr.width(), wr.height());
    }
    return img;
}

QByteArray png(const QImage &img, int compression)
{
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    QImageWriter w(&buf, "png");
    w.setCompression(compression);
    w.write(img);
    return out;
}

} // namespace Capture
