#include "GameWindow.h"

#include <QFileInfo>
#include <QMutexLocker>
#include <QtGlobal>

#include <windows.h>

#include <algorithm>
#include <tuple>
#include <vector>

namespace {

constexpr int kGwlpHwndParent = -8; // GWLP_HWNDPARENT: the owner of a top-level window

QString gameExe()
{
    const QString env = qEnvironmentVariable("DIRECTOR_GAME_EXE");
    return env.isEmpty() ? QStringLiteral("re4.exe") : env.toLower();
}

QString processName(DWORD pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return {};
    wchar_t buf[1024];
    DWORD size = 1024;
    QString out;
    if (QueryFullProcessImageNameW(h, 0, buf, &size))
        out = QFileInfo(QString::fromWCharArray(buf, int(size))).fileName().toLower();
    CloseHandle(h);
    return out;
}

struct FindCtx {
    QString exe;
    std::vector<std::tuple<int, qint64, HWND>> found; // visible, area, hwnd
};

BOOL CALLBACK enumProc(HWND hwnd, LPARAM lp)
{
    auto *ctx = reinterpret_cast<FindCtx *>(lp);
    // owned top-level windows still count: an unclean Studio exit can leave the game hidden under a dead owner
    if (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD)
        return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (processName(pid) == ctx->exe) {
        RECT r;
        GetWindowRect(hwnd, &r);
        ctx->found.emplace_back(IsWindowVisible(hwnd) ? 1 : 0, qint64(r.right - r.left) * (r.bottom - r.top), hwnd);
    }
    return TRUE;
}

} // namespace

GameWindow::GameWindow(QObject *parent) : QObject(parent) {}

GameWindow::~GameWindow() { release(); }

quintptr GameWindow::find()
{
    QMutexLocker l(&m_lock);
    if (!(m_hwnd && IsWindow(HWND(m_hwnd)))) {
        FindCtx ctx{gameExe(), {}};
        EnumWindows(enumProc, LPARAM(&ctx));
        std::sort(ctx.found.begin(), ctx.found.end(), [](const auto &a, const auto &b) { return a > b; });
        m_hwnd = ctx.found.empty() ? 0 : quintptr(std::get<2>(ctx.found.front()));
        m_embedded = false;
        m_haveOrig = false;
    }
    return m_hwnd;
}

bool GameWindow::embed(quintptr owner, const QRect &screenRect)
{
    if (!find())
        return false;
    {
        QMutexLocker l(&m_lock);
        HWND h = HWND(m_hwnd);
        if (!m_embedded) {
            m_origStyle = GetWindowLongPtrW(h, GWL_STYLE);
            m_origExStyle = GetWindowLongPtrW(h, GWL_EXSTYLE);
            m_origOwner = GetWindowLongPtrW(h, kGwlpHwndParent);
            RECT r;
            GetWindowRect(h, &r);
            m_origRect = QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
            m_haveOrig = true;
            const LONG_PTR style = (m_origStyle & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_CHILD)) | WS_POPUP | WS_VISIBLE;
            const LONG_PTR ex = m_origExStyle & ~(WS_EX_APPWINDOW | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME);
            SetWindowLongPtrW(h, GWL_STYLE, style);
            SetWindowLongPtrW(h, GWL_EXSTYLE, ex);
            SetWindowLongPtrW(h, kGwlpHwndParent, LONG_PTR(owner)); // owner, not parent
            m_owner = owner;
            m_embedded = true;
            m_rect = QRect();
        }
    }
    setRect(screenRect, true);
    return true;
}

void GameWindow::setRect(const QRect &screenRect, bool force)
{
    QMutexLocker l(&m_lock);
    if (!(m_embedded && m_hwnd && IsWindow(HWND(m_hwnd))) || m_rendering)
        return;
    const QRect r(screenRect.x(), screenRect.y(), std::max(64, screenRect.width()), std::max(64, screenRect.height()));
    // a game that is still starting up (or switches display mode) puts its own frame back: take it off again
    HWND h = HWND(m_hwnd);
    const LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE);
    const bool framed = style & (WS_CAPTION | WS_THICKFRAME);
    if (framed) {
        SetWindowLongPtrW(h, GWL_STYLE, (style & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_CHILD)) | WS_POPUP | WS_VISIBLE);
        SetWindowLongPtrW(h, GWL_EXSTYLE, GetWindowLongPtrW(h, GWL_EXSTYLE) & ~(WS_EX_APPWINDOW | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME));
    }
    if (GetWindowLongPtrW(h, kGwlpHwndParent) != LONG_PTR(m_owner))
        SetWindowLongPtrW(h, kGwlpHwndParent, LONG_PTR(m_owner));
    if (!force && !framed && r == m_rect)
        return;
    m_rect = r;
    SetWindowPos(HWND(m_hwnd), HWND_TOP, r.x(), r.y(), r.width(), r.height(), SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

QRect GameWindow::renderMode(bool on, int w, int h)
{
    if (!find())
        return {};
    QMutexLocker l(&m_lock);
    HWND hw = HWND(m_hwnd);
    if (on) {
        const int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
        w = std::min(w, sw);
        h = std::min(h, sh);
        if (!m_embedded) {
            const LONG_PTR style = (GetWindowLongPtrW(hw, GWL_STYLE) & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU)) | WS_POPUP | WS_VISIBLE;
            SetWindowLongPtrW(hw, GWL_STYLE, style);
        }
        m_rendering = true;
        SetWindowPos(hw, HWND_TOP, 0, 0, w, h, SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE);
        return QRect(0, 0, w, h);
    }
    m_rendering = false;
    if (m_embedded && m_rect.isValid())
        SetWindowPos(hw, HWND_TOP, m_rect.x(), m_rect.y(), m_rect.width(), m_rect.height(), SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE);
    return m_rect;
}

void GameWindow::forceForeground(quintptr target)
{
    // SetForegroundWindow from a background process only works while attached to the foreground thread's input
    HWND hwnd = HWND(target);
    HWND fg = GetForegroundWindow();
    if (!hwnd || fg == hwnd)
        return;
    const DWORD cur = GetCurrentThreadId();
    const DWORD fgTid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    if (fgTid && fgTid != cur)
        AttachThreadInput(cur, fgTid, TRUE);
    ClipCursor(nullptr);
    SetForegroundWindow(hwnd);
    if (fgTid && fgTid != cur)
        AttachThreadInput(cur, fgTid, FALSE);
}

void GameWindow::focus()
{
    if (m_hwnd)
        forceForeground(m_hwnd);
}

bool GameWindow::isForeground() const
{
    return m_hwnd && GetForegroundWindow() == HWND(m_hwnd);
}

void GameWindow::unclipIfClipped()
{
    // RE4 clips the cursor to its window while focused; release it so the mouse can leave the picture
    if (!(m_embedded && m_hwnd))
        return;
    RECT clip, gr;
    GetClipCursor(&clip);
    GetWindowRect(HWND(m_hwnd), &gr);
    if (clip.left == gr.left && clip.top == gr.top && clip.right == gr.right && clip.bottom == gr.bottom)
        ClipCursor(nullptr);
}

void GameWindow::keepCursorCentred()
{
    if (!(m_embedded && m_hwnd) || !isForeground())
        return;
    RECT r;
    GetWindowRect(HWND(m_hwnd), &r);
    const int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    POINT p;
    GetCursorPos(&p);
    // SetCursorPos does not feed back into the engine's raw mouse motion (verified)
    if (std::abs(p.x - cx) > 40 || std::abs(p.y - cy) > 40)
        SetCursorPos(cx, cy);
}

void GameWindow::release()
{
    QMutexLocker l(&m_lock);
    if (!(m_embedded && m_hwnd && IsWindow(HWND(m_hwnd)))) {
        m_embedded = false;
        return;
    }
    HWND h = HWND(m_hwnd);
    SetWindowLongPtrW(h, kGwlpHwndParent, LONG_PTR(m_origOwner));
    if (m_haveOrig) {
        SetWindowLongPtrW(h, GWL_STYLE, m_origStyle);
        SetWindowLongPtrW(h, GWL_EXSTYLE, m_origExStyle);
        SetWindowPos(h, HWND_TOP, m_origRect.x(), m_origRect.y(), m_origRect.width(), m_origRect.height(), SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    m_embedded = false;
    m_rect = QRect();
}

QVariantMap GameWindow::status() const
{
    QMutexLocker l(&m_lock);
    return {{QStringLiteral("hwnd"), qulonglong(m_hwnd)},
            {QStringLiteral("embedded"), m_embedded},
            {QStringLiteral("rect"), QVariantList{m_rect.x(), m_rect.y(), m_rect.width(), m_rect.height()}},
            {QStringLiteral("found"), bool(m_hwnd && IsWindow(HWND(m_hwnd)))},
            {QStringLiteral("rendering"), m_rendering}};
}
