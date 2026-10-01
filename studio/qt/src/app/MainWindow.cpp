#include "MainWindow.h"

#include "Bridge.h"
#include "GameLibrary.h"
#include "GameWindow.h"
#include "Paths.h"
#include "Studio.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDockWidget>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWidget>
#include <QRegularExpression>
#include <QScreen>
#include <QSet>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>

#include <windows.h>
#include <psapi.h>

#include <cmath>

namespace {
constexpr int kHotkeyF1 = 1;
const char *kQmlBase = "qrc:/qt/qml/Director/qml/";

QJsonArray tracksOf(const Studio *s) { return s->sequence().value(QStringLiteral("tracks")).toArray(); }
} // namespace

// ---------------------------------------------------------------------------------------------- GameHost
GameHost::GameHost(Studio *studio, QWidget *parent) : QWidget(parent), m_studio(studio)
{
    setAttribute(Qt::WA_NativeWindow);      // a real HWND: its screen rect is exact in physical pixels
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(320, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

QRect GameHost::gameRect() const
{
    RECT r;
    GetWindowRect(HWND(winId()), &r);
    const int W = r.right - r.left, H = r.bottom - r.top;
    int w = W, h = int(std::round(W * 9.0 / 16.0));
    if (h > H) { h = H; w = int(std::round(H * 16.0 / 9.0)); }
    return QRect(r.left + (W - w) / 2, r.top + (H - h) / 2, w, h);
}

void GameHost::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x0c, 0x0c, 0x0e));
    const bool embedded = m_studio->gameWindow()->embedded() && !m_studio->detached();
    if (embedded)
        return;
    p.setPen(QColor(0x88, 0x88, 0x92));
    QFont f = font();
    f.setPixelSize(14);
    p.setFont(f);
    QString msg;
    if (m_studio->detached())
        msg = tr("The game is in its own window.\nView ▸ Dock Game Window brings it back here.");
    else if (!m_studio->gameWindow()->find())
        msg = tr("Resident Evil 4 is not running.\nStart the game - the picture appears here by itself.\n\nGame ▸ Back to the Studio Renderer works without the game.");
    else
        msg = tr("Connecting to the game…");
    if (!m_studio->connected() && m_studio->gameWindow()->find())
        msg += tr("\n\nThe Director runtime is not answering: in the game press Insert ▸ ScriptRunner ▸ Reset scripts.");
    p.drawText(rect(), Qt::AlignCenter, msg);
}

void GameHost::mousePressEvent(QMouseEvent *) { m_studio->focusGame(); }

// ---------------------------------------------------------------------------------------------- MainWindow
MainWindow::MainWindow(QQmlEngine *engine, Studio *studio, QWidget *parent)
    : QMainWindow(parent), m_engine(engine), m_studio(studio)
{
    setWindowTitle(QStringLiteral("RigReel Studio"));
    setWindowIcon(QIcon(QStringLiteral(":/qt/qml/Director/resources/logo.svg")));
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks | QMainWindow::GroupedDragging);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    // centre: the Primary Viewport
    auto *central = new QWidget(this);
    auto *lay = new QVBoxLayout(central);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    QQuickWidget *header = quick(QStringLiteral("panels/ViewportHeader.qml"), central);
    header->setFixedHeight(30);
    header->setClearColor(QColor(0x30, 0x30, 0x30));
    // the picture: the Studio's own 3D view (standalone) or the docked game (live)
    m_centre = new QStackedWidget(central);
    m_viewport = quick(QStringLiteral("scene/Viewport3D.qml"), m_centre);
    m_viewport->setClearColor(QColor(0x3f, 0x3f, 0x3f));
    m_viewport->setFocusPolicy(Qt::StrongFocus);
    m_host = new GameHost(studio, m_centre);
    m_centre->addWidget(m_viewport);
    m_centre->addWidget(m_host);
    lay->addWidget(header);
    lay->addWidget(m_centre, 1);
    setCentralWidget(central);

    buildDocks();
    buildMenus();

    // status bar
    m_conn = new QLabel(this);
    m_projLbl = new QLabel(this);
    m_frameLbl = new QLabel(this);
    m_renderLbl = new QLabel(this);
    statusBar()->addPermanentWidget(m_renderLbl);
    statusBar()->addPermanentWidget(m_projLbl);
    statusBar()->addPermanentWidget(m_frameLbl);
    statusBar()->addPermanentWidget(m_conn);
    connect(studio, &Studio::statusMessage, this, [this](const QString &t, int ms) { statusBar()->showMessage(t, ms); });
    connect(studio, &Studio::renderDialogRequested, this, [this] { showQmlDialog(tr("Export Movie"), QStringLiteral("panels/RenderDialog.qml"), QSize(560, 470)); });
    connect(studio, &Studio::menuRequested, this, [this](const QString &name) {
        if (name == QLatin1String("help"))
            showQmlDialog(tr("Keyboard Shortcuts"), QStringLiteral("panels/HelpSheet.qml"), QSize(760, 620));
        else if (name == QLatin1String("focus:viewport")) {
            activateWindow();
            m_viewport->setFocus(Qt::OtherFocusReason);
            if (QQuickItem *r = m_viewport->rootObject()) r->forceActiveFocus();
        } else if (name.startsWith(QLatin1String("raise:"))) {
            const QString which = name.mid(6);
            QDockWidget *d = which == QLatin1String("assets") ? m_assets : which == QLatin1String("props") ? m_props : which == QLatin1String("timeline") ? m_timeline
                           : which == QLatin1String("console") ? m_console : which == QLatin1String("element") ? m_elem : m_ase;
            d->show();
            d->raise();
        }
    });
    connect(studio, &Studio::detachedChanged, this, [this](bool on) {
        if (on) m_studio->gameWindow()->release();
        syncGame();
        m_host->update();
    });
    connect(studio, &Studio::runtimeModeChanged, this, &MainWindow::applyRuntimeMode);
    connect(studio, &Studio::gamesRequested, this, &MainWindow::showGames);
    studio->setPopupHandler([this](const QVariantList &items) { return popup(items, QCursor::pos()); });
    connect(&m_statusTimer, &QTimer::timeout, this, &MainWindow::updateStatus);
    m_statusTimer.start(250);

    // keep the game glued to the viewport
    connect(&m_gameTimer, &QTimer::timeout, this, &MainWindow::syncGame);
    m_gameTimer.start(50);

    QSettings s(Paths::settingsFile(), QSettings::IniFormat);
    m_defaultState = saveState();
    if (!restoreGeometry(s.value(QStringLiteral("geometry")).toByteArray()))
        resize(1680, 980);
    // a layout saved before the Blender-style one would put the panels back where they were: start fresh once
    if (s.value(QStringLiteral("layoutVersion")).toInt() >= 2) restoreState(s.value(QStringLiteral("state")).toByteArray());

    // live mode registers F1 (anywhere in Windows: mouse and keyboard to the game / back to the Studio)
    applyRuntimeMode();
}

MainWindow::~MainWindow() { UnregisterHotKey(HWND(winId()), kHotkeyF1); }

QQuickWidget *MainWindow::quick(const QString &qml, QWidget *parent)
{
    auto *w = new QQuickWidget(m_engine, parent ? parent : this);
    w->setResizeMode(QQuickWidget::SizeRootObjectToView);
    w->setClearColor(QColor(0x22, 0x22, 0x25));
    w->setObjectName(qml);
    w->setSource(QUrl(QString::fromLatin1(kQmlBase) + qml));
    if (w->status() == QQuickWidget::Error)
        for (const QQmlError &e : w->errors())
            qWarning().noquote() << "QML" << qml << e.toString();
    return w;
}

QDockWidget *MainWindow::dock(const QString &title, const QString &name, const QString &qml)
{
    auto *d = new QDockWidget(title, this);
    d->setObjectName(name);
    d->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    d->setWidget(quick(qml, d));
    m_docks << d;
    // tabbed panels show only their tab (like SFM); a panel on its own keeps a slim title bar to drag it by
    auto refresh = [this] { QTimer::singleShot(0, this, [this] {
        for (QDockWidget *x : std::as_const(m_docks)) {
            const bool tabbed = !x->isFloating() && !tabifiedDockWidgets(x).isEmpty();
            QWidget *cur = x->titleBarWidget();
            if (tabbed && !cur) x->setTitleBarWidget(new QWidget(x));
            else if (!tabbed && cur) { x->setTitleBarWidget(nullptr); cur->deleteLater(); }
        }
    }); };
    connect(d, &QDockWidget::dockLocationChanged, this, refresh);
    connect(d, &QDockWidget::topLevelChanged, this, refresh);
    connect(d, &QDockWidget::visibilityChanged, this, refresh);
    return d;
}

void MainWindow::buildDocks()
{
    m_ase = dock(tr("Outliner"), QStringLiteral("ase"), QStringLiteral("panels/AnimationSetEditor.qml"));
    m_elem = dock(tr("Element Viewer"), QStringLiteral("element"), QStringLiteral("panels/ElementViewer.qml"));
    m_assets = dock(tr("Asset Browser"), QStringLiteral("assets"), QStringLiteral("panels/AssetBrowser.qml"));
    m_props = dock(tr("Properties"), QStringLiteral("props"), QStringLiteral("panels/Properties.qml"));
    m_timeline = dock(tr("Timeline"), QStringLiteral("timeline"), QStringLiteral("panels/Timeline.qml"));
    m_console = dock(tr("Console"), QStringLiteral("console"), QStringLiteral("panels/Console.qml"));
    defaultLayout();
}

void MainWindow::defaultLayout()
{
    for (QDockWidget *d : std::as_const(m_docks)) {
        d->setFloating(false);
        d->show();
    }
    // Blender's Layout: the Outliner above the Properties on the right, the Timeline below the view; the Asset
    // Browser (and the Element Viewer) on the left
    addDockWidget(Qt::LeftDockWidgetArea, m_assets);
    addDockWidget(Qt::LeftDockWidgetArea, m_elem);
    tabifyDockWidget(m_assets, m_elem);
    addDockWidget(Qt::RightDockWidgetArea, m_ase);
    addDockWidget(Qt::RightDockWidgetArea, m_props);
    splitDockWidget(m_ase, m_props, Qt::Vertical);
    addDockWidget(Qt::BottomDockWidgetArea, m_timeline);
    addDockWidget(Qt::BottomDockWidgetArea, m_console);
    tabifyDockWidget(m_timeline, m_console);
    m_assets->raise();
    m_timeline->raise();
    resizeDocks({m_assets, m_ase}, {330, 360}, Qt::Horizontal);
    resizeDocks({m_ase, m_props}, {300, 560}, Qt::Vertical);
    resizeDocks({m_timeline}, {260}, Qt::Vertical);
}

double MainWindow::frame() const { return std::floor(m_studio->sequence().value(QStringLiteral("t")).toDouble()); }

QVariant MainWindow::popup(const QVariantList &items, const QPoint &globalPos)
{
    QMenu menu(this);
    QHash<QAction *, QVariant> values;
    std::function<void(QMenu *, const QVariantList &)> fill = [&](QMenu *m, const QVariantList &list) {
        for (const QVariant &v : list) {
            const QVariantMap it = v.toMap();
            if (it.value(QStringLiteral("separator")).toBool()) { m->addSeparator(); continue; }
            if (it.contains(QStringLiteral("header"))) {
                QAction *h = m->addSection(it.value(QStringLiteral("header")).toString());
                Q_UNUSED(h)
                continue;
            }
            if (it.contains(QStringLiteral("items"))) {
                QMenu *sub = m->addMenu(it.value(QStringLiteral("text")).toString());
                fill(sub, it.value(QStringLiteral("items")).toList());
                continue;
            }
            QAction *a = m->addAction(it.value(QStringLiteral("text")).toString());
            if (it.contains(QStringLiteral("checked"))) { a->setCheckable(true); a->setChecked(it.value(QStringLiteral("checked")).toBool()); }
            if (it.contains(QStringLiteral("enabled"))) a->setEnabled(it.value(QStringLiteral("enabled")).toBool());
            if (it.contains(QStringLiteral("shortcut"))) a->setShortcut(QKeySequence(it.value(QStringLiteral("shortcut")).toString()));
            if (it.contains(QStringLiteral("tip"))) a->setToolTip(it.value(QStringLiteral("tip")).toString());
            values.insert(a, it.value(QStringLiteral("value"), it.value(QStringLiteral("text"))));
        }
    };
    fill(&menu, items);
    menu.setToolTipsVisible(true);
    QAction *chosen = menu.exec(globalPos);
    return chosen ? values.value(chosen) : QVariant();
}

void MainWindow::showQmlDialog(const QString &title, const QString &qml, const QSize &size)
{
    auto *dlg = new QDialog(this, Qt::Dialog | Qt::WindowCloseButtonHint);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(title);
    auto *l = new QVBoxLayout(dlg);
    l->setContentsMargins(0, 0, 0, 0);
    QQuickWidget *w = quick(qml, dlg);
    l->addWidget(w);
    if (w->rootObject())
        connect(w->rootObject(), SIGNAL(closeRequested()), dlg, SLOT(close()));
    dlg->resize(size);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void MainWindow::promptProjectName(bool saveAs)
{
    bool ok = false;
    const QString cur = m_studio->stateObj().value(QStringLiteral("game")).toObject().value(QStringLiteral("project")).toString();
    QString name = QInputDialog::getText(this, saveAs ? tr("Save Project As") : tr("New Project"),
                                         saveAs ? tr("Project name:") : tr("Name of the new project.\nThis clears the stage: your cast, props, cameras, lights and timeline.\nSave first to keep them."),
                                         QLineEdit::Normal, saveAs ? cur : QString(), &ok).trimmed();
    name.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*]")), QStringLiteral("_"));
    if (!ok || name.isEmpty())
        return;
    m_studio->cmd(saveAs ? QStringLiteral("project_save") : QStringLiteral("project_new"), {{QStringLiteral("name"), name}});
    m_studio->setStatus(saveAs ? tr("Saved %1").arg(name) : tr("New project %1").arg(name));
}

void MainWindow::openProject()
{
    QStringList list;
    for (const QJsonValue &v : m_studio->dataObj().value(QStringLiteral("projects")).toArray())
        list << v.toString();
    if (list.isEmpty()) {
        QMessageBox::information(this, tr("Open Project"), tr("No saved projects yet. File ▸ Save creates one."));
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getItem(this, tr("Open Project"), tr("Project:"), list, 0, false, &ok);
    if (ok && !name.isEmpty()) {
        m_studio->cmd(QStringLiteral("project_load"), {{QStringLiteral("name"), name}});
        m_studio->setStatus(tr("Opened %1").arg(name));
    }
}

// selection-dependent editing keys, the same rules everywhere (timeline, graph, viewport)
void MainWindow::key(const QString &what)
{
    const QJsonObject st = m_studio->stateObj();
    const QJsonObject sel = st.value(QStringLiteral("selection")).toObject().value(QStringLiteral("clip")).toObject();
    const bool hasClip = !sel.isEmpty() && sel.contains(QStringLiteral("id"));
    const bool isCut = sel.value(QStringLiteral("kind")).toString() == QLatin1String("camera");
    const QVariantMap clipRef{{QStringLiteral("track"), sel.value(QStringLiteral("track")).toInt()}, {QStringLiteral("id"), sel.value(QStringLiteral("id")).toInt()}};
    const bool keys = m_studio->keyCount() > 0;
    const double t = frame();
    if (what == QLatin1String("delete")) {
        if (keys) { m_studio->cmd(QStringLiteral("remove_keys"), {{QStringLiteral("items"), m_studio->keySelList()}}); m_studio->clearKeySel(); }
        else if (hasClip) { m_studio->cmd(isCut ? QStringLiteral("remove_cut") : QStringLiteral("remove_clip"), clipRef); m_studio->send(QStringLiteral("select_clip"), {}); }
        else {
            // nothing on the timeline picked: the selected characters / props leave the film (spawned ones only)
            const QJsonObject s = st.value(QStringLiteral("selection")).toObject();
            QSet<qint64> ids;
            for (const QJsonValue &v : s.value(QStringLiteral("multi")).toArray()) ids.insert(qint64(v.toDouble()));
            if (s.contains(QStringLiteral("actor"))) ids.insert(qint64(s.value(QStringLiteral("actor")).toDouble()));
            QList<qint64> spawned;
            for (const QJsonValue &v : st.value(QStringLiteral("actors")).toArray()) {
                const QJsonObject a = v.toObject();
                if (ids.contains(qint64(a.value(QStringLiteral("id")).toDouble())) && a.value(QStringLiteral("spawned")).toBool()) spawned << qint64(a.value(QStringLiteral("id")).toDouble());
            }
            if (spawned.size() > 1) m_studio->cmd(QStringLiteral("group_remove"), {});
            else if (spawned.size() == 1) m_studio->cmd(QStringLiteral("destroy_object"), {{QStringLiteral("addr"), double(spawned.first())}});
        }
    } else if (what == QLatin1String("copy")) {
        if (keys) { m_studio->send(QStringLiteral("copy_keys"), {{QStringLiteral("items"), m_studio->keySelList()}}); m_lastCopyKeys = true; m_studio->setStatus(tr("%1 keys copied").arg(m_studio->keyCount())); }
        else if (hasClip && !isCut) { m_clipClip = clipRef; m_lastCopyKeys = false; m_studio->setStatus(tr("Clip copied")); }
    } else if (what == QLatin1String("paste")) {
        if (m_lastCopyKeys && st.value(QStringLiteral("key_clipboard")).toInt() > 0) m_studio->cmd(QStringLiteral("paste_keys"), {{QStringLiteral("t"), t}});
        else if (!m_clipClip.isEmpty()) { QVariantMap a = m_clipClip; a.insert(QStringLiteral("t"), t); m_studio->cmd(QStringLiteral("paste_clip"), a); }
    } else if (what == QLatin1String("duplicate")) {
        if (hasClip && !isCut) m_studio->cmd(QStringLiteral("duplicate_clip"), clipRef);
    } else if (what == QLatin1String("split")) {
        if (hasClip && !isCut) { QVariantMap a = clipRef; a.insert(QStringLiteral("t"), t); m_studio->cmd(QStringLiteral("split_clip"), a); }
    } else if (what == QLatin1String("trimL") || what == QLatin1String("trimR")) {
        if (hasClip && !isCut) { QVariantMap a = clipRef; a.insert(QStringLiteral("t"), t); a.insert(QStringLiteral("side"), what == QLatin1String("trimL") ? QStringLiteral("left") : QStringLiteral("right")); m_studio->cmd(QStringLiteral("trim_clip"), a); }
    } else if (what == QLatin1String("selectAll")) {
        QVariantList all;
        for (const QJsonValue &tv : tracksOf(m_studio)) {
            const QJsonObject tr = tv.toObject();
            const QString k = tr.value(QStringLiteral("kind")).toString();
            const int idx = tr.value(QStringLiteral("idx")).toInt();
            if (k == QLatin1String("xform") || k == QLatin1String("cammove"))
                for (const QJsonValue &kv : tr.value(QStringLiteral("keys")).toArray())
                    all << QVariantMap{{QStringLiteral("track"), idx}, {QStringLiteral("t"), kv.toObject().value(QStringLiteral("t")).toDouble()}};
            else if (k == QLatin1String("pose"))
                for (const QJsonValue &cv : tr.value(QStringLiteral("clips")).toArray())
                    for (const QJsonValue &kv : cv.toObject().value(QStringLiteral("keys")).toArray())
                        all << QVariantMap{{QStringLiteral("track"), idx}, {QStringLiteral("id"), cv.toObject().value(QStringLiteral("id")).toInt()},
                                           {QStringLiteral("t"), cv.toObject().value(QStringLiteral("start")).toDouble() + kv.toDouble()}};
        }
        m_studio->setKeySelList(all);
    } else if (what == QLatin1String("escape")) {
        if (keys) m_studio->clearKeySel();
        else m_studio->send(QStringLiteral("select_clip"), {});
    } else if (what == QLatin1String("prevKey") || what == QLatin1String("nextKey")) {
        // keys of the selected character (or every key when nothing is selected)
        const double actor = st.value(QStringLiteral("selection")).toObject().value(QStringLiteral("actor")).toDouble();
        QList<double> times;
        for (const QJsonValue &tv : tracksOf(m_studio)) {
            const QJsonObject tr = tv.toObject();
            if (actor && tr.value(QStringLiteral("actor")).toDouble() != actor && tr.value(QStringLiteral("kind")).toString() != QLatin1String("cammove")) continue;
            for (const QJsonValue &kv : tr.value(QStringLiteral("keys")).toArray()) times << kv.toObject().value(QStringLiteral("t")).toDouble();
            for (const QJsonValue &cv : tr.value(QStringLiteral("clips")).toArray()) {
                const QJsonObject c = cv.toObject();
                for (const QJsonValue &kv : c.value(QStringLiteral("keys")).toArray()) times << c.value(QStringLiteral("start")).toDouble() + kv.toDouble();
                times << c.value(QStringLiteral("start")).toDouble();
            }
            for (const QJsonValue &cv : tr.value(QStringLiteral("cuts")).toArray()) times << cv.toObject().value(QStringLiteral("start")).toDouble();
        }
        std::sort(times.begin(), times.end());
        const double now = m_studio->sequence().value(QStringLiteral("t")).toDouble();
        double target = -1;
        if (what == QLatin1String("nextKey")) { for (double x : times) if (x > now + 0.5) { target = x; break; } }
        else { for (auto it = times.rbegin(); it != times.rend(); ++it) if (*it < now - 0.5) { target = *it; break; } }
        if (target >= 0) m_studio->send(QStringLiteral("seq_seek"), {{QStringLiteral("t"), target}});
    }
}

void MainWindow::buildMenus()
{
    auto act = [this](QMenu *m, const QString &text, const QKeySequence &ks, std::function<void()> fn, const QString &tip = QString()) {
        QAction *a = m->addAction(text);
        if (!ks.isEmpty()) {
            a->setShortcut(ks);
            a->setShortcutContext(Qt::ApplicationShortcut);
        }
        if (!tip.isEmpty()) a->setStatusTip(tip);
        connect(a, &QAction::triggered, this, [fn] { fn(); });
        return a;
    };
    auto toggle = [this, act](QMenu *m, const QString &text, const QKeySequence &ks, std::function<bool()> get, std::function<void(bool)> set) {
        QAction *a = act(m, text, ks, [] {});
        a->setCheckable(true);
        disconnect(a, &QAction::triggered, this, nullptr);
        connect(a, &QAction::triggered, this, [set](bool on) { set(on); });
        connect(m, &QMenu::aboutToShow, a, [a, get] { a->setChecked(get()); });
        return a;
    };
    Studio *s = m_studio;
    auto st = [s]() { return s->stateObj(); };
    auto sel = [s]() { return s->stateObj().value(QStringLiteral("selection")).toObject(); };

    // ---- File
    QMenu *file = menuBar()->addMenu(tr("&File"));
    act(file, tr("New Project…"), QKeySequence(QStringLiteral("Ctrl+N")), [this] { promptProjectName(false); });
    act(file, tr("Open Project…"), QKeySequence(QStringLiteral("Ctrl+O")), [this] { openProject(); });
    QMenu *recent = file->addMenu(tr("Open Recent"));
    connect(recent, &QMenu::aboutToShow, this, [this, recent] {
        recent->clear();
        const QJsonArray projects = m_studio->dataObj().value(QStringLiteral("projects")).toArray();
        if (projects.isEmpty()) recent->addAction(tr("(no saved projects)"))->setEnabled(false);
        for (const QJsonValue &v : projects) {
            const QString n = v.toString();
            connect(recent->addAction(n), &QAction::triggered, this, [this, n] { m_studio->cmd(QStringLiteral("project_load"), {{QStringLiteral("name"), n}}); });
        }
    });
    act(file, tr("Save"), QKeySequence::Save, [this] {
        const QString cur = m_studio->stateObj().value(QStringLiteral("game")).toObject().value(QStringLiteral("project")).toString();
        if (cur.isEmpty() || cur == QLatin1String("untitled")) { promptProjectName(true); return; }
        m_studio->cmd(QStringLiteral("project_save"), {{QStringLiteral("name"), cur}});
        m_studio->setStatus(tr("Saved %1").arg(cur));
    });
    act(file, tr("Save As…"), QKeySequence(QStringLiteral("Ctrl+Shift+S")), [this] { promptProjectName(true); });
    file->addSeparator();
    QMenu *imp = file->addMenu(tr("Import"));
    act(imp, tr("Animation (Mixamo / BVH / FBX / glTF)…"), QKeySequence(QStringLiteral("Ctrl+I")), [this, sel] {
        const double actor = sel().value(QStringLiteral("actor")).toDouble();
        if (!actor) { QMessageBox::information(this, tr("Import Animation"), tr("Select a character first (Animation Set Editor or click it in the picture).")); return; }
        const QString p = m_studio->pickFile(QStringLiteral("anim"));
        if (!p.isEmpty()) m_studio->importAnim(p, actor, frame());
    });
    act(imp, tr("Audio Track…"), {}, [this] {
        const QString p = m_studio->pickFile(QStringLiteral("audio"));
        if (!p.isEmpty()) m_studio->cmd(QStringLiteral("seq_audio"), {{QStringLiteral("path"), p}, {QStringLiteral("name"), QFileInfo(p).fileName()}, {QStringLiteral("offset"), frame()}});
    });
    QMenu *exp = file->addMenu(tr("Export"));
    act(exp, tr("Movie…"), QKeySequence(QStringLiteral("Ctrl+Shift+M")), [this] { m_studio->showRenderDialog(); });
    act(exp, tr("Animation for Blender"), {}, [this] { m_studio->cmd(QStringLiteral("export_anim"), {}); m_studio->setStatus(tr("Exported to director/export")); });
    QMenu *folders = file->addMenu(tr("Open Folder"));
    for (const auto &pair : {std::pair{tr("Renders (Downloads)"), QStringLiteral("renders")}, {tr("Projects"), QStringLiteral("projects")},
                             {tr("Exports"), QStringLiteral("export")}, {tr("Poses"), QStringLiteral("poses")}, {tr("Runtime bridge"), QStringLiteral("bridge")}}) {
        const QString which = pair.second;
        act(folders, pair.first, {}, [this, which] { m_studio->openFolder(which); });
    }
    file->addSeparator();
    act(file, tr("Exit"), QKeySequence(QStringLiteral("Alt+F4")), [this] { close(); });

    // ---- Game: which of your games the film is made from
    QMenu *game = menuBar()->addMenu(tr("&Game"));
    act(game, tr("Games…"), QKeySequence(QStringLiteral("Ctrl+Shift+G")), [this] { showGames(); }, tr("The games you own that Director can read"));
    QMenu *openGame = game->addMenu(tr("Open Game"));
    connect(openGame, &QMenu::aboutToShow, this, [this, openGame] {
        openGame->clear();
        GameLibrary *lib = m_studio->games();
        const QVariantList list = lib->gamesJs();
        if (list.isEmpty()) openGame->addAction(tr("(no supported games found — Games… adds a folder)"))->setEnabled(false);
        for (const QVariant &v : list) {
            const QVariantMap g = v.toMap();
            const QString key = g.value(QStringLiteral("key")).toString();
            QAction *a = openGame->addAction(g.value(QStringLiteral("title")).toString());
            a->setCheckable(true);
            a->setChecked(key == lib->activeKey());
            a->setStatusTip(g.value(QStringLiteral("folder")).toString());
            connect(a, &QAction::triggered, this, [this, key] { m_studio->setRuntimeMode(QStringLiteral("standalone")); m_studio->games()->activate(key); });
        }
    });
    act(game, tr("Add a Game Folder…"), {}, [this] { m_studio->addGameFolder(); }, tr("A game Director did not find in your Steam libraries"));
    game->addSeparator();
    QAction *live = act(game, tr("Direct the Running Game (Resident Evil 4, live)"), {}, [this] { connectToGame(); },
                        tr("Animate inside the running game through the Director mod instead of the Studio's own renderer"));
    QAction *back = act(game, tr("Back to the Studio Renderer"), {}, [this] { m_studio->setRuntimeMode(QStringLiteral("standalone")); });
    connect(game, &QMenu::aboutToShow, this, [this, live, back] {
        live->setVisible(m_studio->standalone());
        back->setVisible(!m_studio->standalone());
    });

    // ---- Edit
    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    QAction *undo = act(edit, tr("Undo"), QKeySequence(QStringLiteral("Ctrl+Z")), [s] { s->send(QStringLiteral("undo")); });
    QAction *redo = act(edit, tr("Redo"), QKeySequence(QStringLiteral("Ctrl+Shift+Z")), [s] { s->send(QStringLiteral("redo")); });
    redo->setShortcuts({QKeySequence(QStringLiteral("Ctrl+Shift+Z")), QKeySequence(QStringLiteral("Ctrl+Y"))});
    connect(edit, &QMenu::aboutToShow, this, [st, undo, redo] {
        const QJsonObject h = st().value(QStringLiteral("history")).toObject();
        undo->setEnabled(h.value(QStringLiteral("undo")).toInt() > 0);
        redo->setEnabled(h.value(QStringLiteral("redo")).toInt() > 0);
        undo->setText(h.value(QStringLiteral("last")).toString().isEmpty() ? QObject::tr("Undo") : QObject::tr("Undo %1").arg(h.value(QStringLiteral("last")).toString()));
        redo->setText(h.value(QStringLiteral("next")).toString().isEmpty() ? QObject::tr("Redo") : QObject::tr("Redo %1").arg(h.value(QStringLiteral("next")).toString()));
    });
    edit->addSeparator();
    act(edit, tr("Copy"), QKeySequence::Copy, [this] { key(QStringLiteral("copy")); });
    act(edit, tr("Paste at Playhead"), QKeySequence::Paste, [this] { key(QStringLiteral("paste")); });
    act(edit, tr("Duplicate Clip"), QKeySequence(QStringLiteral("Ctrl+D")), [this] { key(QStringLiteral("duplicate")); });
    act(edit, tr("Delete"), QKeySequence::Delete, [this] { key(QStringLiteral("delete")); });
    edit->addSeparator();
    act(edit, tr("Split Clip at Playhead"), QKeySequence(QStringLiteral("Ctrl+B")), [this] { key(QStringLiteral("split")); });
    act(edit, tr("Trim Clip Start to Playhead"), QKeySequence(QStringLiteral("[")), [this] { key(QStringLiteral("trimL")); });
    act(edit, tr("Trim Clip End to Playhead"), QKeySequence(QStringLiteral("]")), [this] { key(QStringLiteral("trimR")); });
    edit->addSeparator();
    act(edit, tr("Select All Keys"), QKeySequence(QStringLiteral("Ctrl+A")), [this] { key(QStringLiteral("selectAll")); });
    act(edit, tr("Deselect"), QKeySequence(QStringLiteral("Esc")), [this] { key(QStringLiteral("escape")); });
    edit->addSeparator();
    act(edit, tr("Key the Selection"), QKeySequence(QStringLiteral("K")), [this, s] { s->cmd(QStringLiteral("key_selection"), {{QStringLiteral("t"), frame()}}); },
        tr("Pose, position or camera of whatever is selected, at the playhead"));
    act(edit, tr("Key the Camera"), QKeySequence(QStringLiteral("M")), [s] { s->cmd(QStringLiteral("key_camera"), {}); });
    toggle(edit, tr("Auto-Key"), QKeySequence(QStringLiteral("Shift+K")), [st] { return st().value(QStringLiteral("autokey")).toBool(); },
           [s](bool on) { s->send(QStringLiteral("autokey"), {{QStringLiteral("value"), on}}); });
    QMenu *interp = edit->addMenu(tr("Key Interpolation"));
    for (const auto &pair : {std::pair{tr("Smooth (ease in and out)"), QStringLiteral("smooth")}, {tr("Linear"), QStringLiteral("linear")}, {tr("Hold (step)"), QStringLiteral("hold")},
                             {tr("Ease In"), QStringLiteral("in")}, {tr("Ease Out"), QStringLiteral("out")}, {tr("Strong Ease"), QStringLiteral("cubic")}}) {
        const QString e = pair.second;
        act(interp, pair.first, {}, [s, e] { if (s->keyCount()) s->cmd(QStringLiteral("set_ease"), {{QStringLiteral("items"), s->keySelList()}, {QStringLiteral("ease"), e}}); });
    }

    // ---- Scene
    QMenu *scene = menuBar()->addMenu(tr("&Scene"));
    act(scene, tr("New Camera from View"), QKeySequence(QStringLiteral("C")), [s] { s->cmd(QStringLiteral("cam_capture")); }, tr("A scene camera exactly where the view is now"));
    QMenu *camp = scene->addMenu(tr("Camera on the Selected Character"));
    for (const auto &pair : {std::pair{tr("Close-up"), QStringLiteral("close")}, {tr("Medium shot"), QStringLiteral("medium")}, {tr("Low hero shot"), QStringLiteral("hero")},
                             {tr("Wide shot"), QStringLiteral("wide")}, {tr("Tracking shot"), QStringLiteral("track")}}) {
        const QString k = pair.second;
        act(camp, pair.first, {}, [s, sel, k] { const double a = sel().value(QStringLiteral("actor")).toDouble(); if (a) s->cmd(QStringLiteral("cam_preset"), {{QStringLiteral("kind"), k}, {QStringLiteral("addr"), a}}); });
    }
    camp->addSeparator();
    act(camp, tr("Follow camera"), {}, [s] { s->cmd(QStringLiteral("cam_follow")); });
    act(camp, tr("Orbit camera"), {}, [s] { s->cmd(QStringLiteral("cam_orbit")); });
    act(camp, tr("Aim camera (stays, keeps looking)"), {}, [s] { s->cmd(QStringLiteral("cam_lookat")); });
    QMenu *lights = scene->addMenu(tr("Add Light"));
    act(lights, tr("Spot Light"), {}, [s] { s->cmd(QStringLiteral("light_add"), {{QStringLiteral("kind"), QStringLiteral("spot")}}); });
    act(lights, tr("Point Light"), {}, [s] { s->cmd(QStringLiteral("light_add"), {{QStringLiteral("kind"), QStringLiteral("point")}}); });
    m_liveOnly << scene->addSeparator();
    m_liveOnly << toggle(scene, tr("Empty Stage (hide the game's characters)"), {}, [st] { return st().value(QStringLiteral("stage_clean")).toBool(); },
                         [s](bool on) { s->send(QStringLiteral("stage_clean"), {{QStringLiteral("value"), on}}); });
    m_liveOnly << toggle(scene, tr("Freeze the World"), QKeySequence(QStringLiteral("Ctrl+Shift+F")), [st] { return st().value(QStringLiteral("game")).toObject().value(QStringLiteral("frozen")).toBool(); },
                         [s](bool on) { s->send(QStringLiteral("game_freeze"), {{QStringLiteral("value"), on}}); });
    m_liveOnly << toggle(scene, tr("Click Props in the Picture"), {}, [st] { return st().value(QStringLiteral("pick_props")).toBool(); },
                         [s](bool on) { s->send(QStringLiteral("pick_props"), {{QStringLiteral("value"), on}}); });
    m_liveOnly << toggle(scene, tr("Hide the Game HUD"), {}, [st] { return st().value(QStringLiteral("hud_hidden")).toBool(); },
                         [s](bool on) { s->send(QStringLiteral("hud"), {{QStringLiteral("value"), !on}}); });
    m_liveOnly << toggle(scene, tr("Hide Leon's Weapon"), {}, [st] { return st().value(QStringLiteral("hide_weapons")).toBool(); },
                         [s](bool on) { s->send(QStringLiteral("hide_weapons"), {{QStringLiteral("value"), on}}); });
    m_liveOnly << scene->addSeparator();
    m_liveOnly << act(scene, tr("Rescan the World"), QKeySequence(QStringLiteral("Ctrl+R")), [s] { s->send(QStringLiteral("refresh")); });
    m_liveOnly << act(scene, tr("Hand Everything Back to the Game"), {}, [s] { s->send(QStringLiteral("release_all")); });

    // ---- View
    QMenu *view = menuBar()->addMenu(tr("&View"));
    QMenu *vcam = view->addMenu(tr("Viewport Camera"));
    connect(vcam, &QMenu::aboutToShow, this, [this, vcam, st] {
        vcam->clear();
        const QJsonObject state = st();
        const QString mode = state.value(QStringLiteral("camera_view")).toString();
        auto add = [&](const QString &text, bool on, std::function<void()> fn, const QString &keys = QString()) {
            QAction *a = vcam->addAction(text);
            a->setCheckable(true);
            a->setChecked(on);
            if (!keys.isEmpty()) a->setShortcut(QKeySequence(keys));
            connect(a, &QAction::triggered, this, [fn] { fn(); });
        };
        add(tr("Work Camera (free, never rendered)"), mode == QLatin1String("work"), [this] { m_studio->send(QStringLiteral("cam_work"), {{QStringLiteral("value"), true}}); });
        add(tr("Scene Camera (follows the shot's cuts)"), mode == QLatin1String("scene"), [this] { m_studio->send(QStringLiteral("cam_scene")); });
        if (!m_studio->standalone())
            add(tr("Game Camera"), mode == QLatin1String("game") || (!state.value(QStringLiteral("camera_override")).toBool() && mode.isEmpty()),
                [this] { m_studio->send(QStringLiteral("cam_live"), {{QStringLiteral("i"), 0}}); });
        vcam->addSeparator();
        for (const QJsonValue &v : state.value(QStringLiteral("cameras")).toArray()) {
            const QJsonObject c = v.toObject();
            const int i = c.value(QStringLiteral("i")).toInt();
            add(c.value(QStringLiteral("name")).toString(), c.value(QStringLiteral("live")).toBool() && mode != QLatin1String("work") && mode != QLatin1String("scene"),
                [this, i] { m_studio->send(QStringLiteral("cam_live"), {{QStringLiteral("i"), i}}); });
        }
    });
    act(view, tr("Frame the Selection"), QKeySequence(QStringLiteral("F")), [s] { s->frameSelection(); }, tr("Bring the selected character into the Work Camera's view"))
        ->setShortcuts({QKeySequence(QStringLiteral("F")), QKeySequence(Qt::KeypadModifier | Qt::Key_Period)});
    {
        // Blender's numpad views; the number row works too (no numpad on many keyboards)
        QMenu *vp = view->addMenu(tr("Viewpoint"));
        m_standaloneOnly << vp->menuAction();
        struct V { const char *text; Qt::Key key; bool ctrl; const char *cmd; };
        const V views[] = {
            {"Front", Qt::Key_1, false, "front"}, {"Back", Qt::Key_1, true, "back"},
            {"Right", Qt::Key_3, false, "right"}, {"Left", Qt::Key_3, true, "left"},
            {"Top", Qt::Key_7, false, "top"},     {"Bottom", Qt::Key_7, true, "bottom"},
            {"Opposite Side", Qt::Key_9, false, "flip"},
            {"Perspective / Orthographic", Qt::Key_5, false, "ortho"},
            {"Camera", Qt::Key_0, false, "camera"},
            {"Orbit Left", Qt::Key_4, false, "orbit:left"}, {"Orbit Right", Qt::Key_6, false, "orbit:right"},
            {"Orbit Up", Qt::Key_8, false, "orbit:up"},     {"Orbit Down", Qt::Key_2, false, "orbit:down"},
        };
        for (const V &v : views) {
            const QString cmd = QString::fromLatin1(v.cmd);
            QAction *a = act(vp, tr(v.text), {}, [s, cmd] { s->viewCommand(cmd); });
            const int mod = v.ctrl ? int(Qt::ControlModifier) : 0;
            a->setShortcuts({QKeySequence(mod | int(Qt::KeypadModifier) | int(v.key)), QKeySequence(mod | int(v.key))});
            a->setShortcutContext(Qt::ApplicationShortcut);
            if (v.key == Qt::Key_9 || v.key == Qt::Key_0 || v.key == Qt::Key_4) vp->addSeparator();
        }
        act(vp, tr("Frame All"), QKeySequence(Qt::KeypadModifier | Qt::Key_Home), [s] { s->viewCommand(QStringLiteral("all")); });
        // shading (Z: a quick menu of the four, like Blender's pie)
        QMenu *sh = view->addMenu(tr("Shading"));
        m_standaloneOnly << sh->menuAction();
        const std::pair<const char *, const char *> modes[] = {{"Wireframe", "wire"}, {"Solid", "solid"}, {"Material Preview", "material"}, {"Rendered", "rendered"}};
        for (const auto &m : modes) {
            const QString mode = QString::fromLatin1(m.second);
            QAction *a = act(sh, tr(m.first), {}, [s, mode] { s->setShading(mode); });
            a->setCheckable(true);
            connect(sh, &QMenu::aboutToShow, a, [a, s, mode] { a->setChecked(s->shading() == mode); });
        }
        m_standaloneOnly << act(view, tr("Shading Menu"), QKeySequence(QStringLiteral("Z")), [this, s] {
            const QVariant r = popup({QVariantMap{{QStringLiteral("header"), tr("Shading")}},
                                      QVariantMap{{QStringLiteral("text"), tr("Wireframe")}, {QStringLiteral("value"), QStringLiteral("wire")}, {QStringLiteral("checked"), s->shading() == QLatin1String("wire")}},
                                      QVariantMap{{QStringLiteral("text"), tr("Solid")}, {QStringLiteral("value"), QStringLiteral("solid")}, {QStringLiteral("checked"), s->shading() == QLatin1String("solid")}},
                                      QVariantMap{{QStringLiteral("text"), tr("Material Preview")}, {QStringLiteral("value"), QStringLiteral("material")}, {QStringLiteral("checked"), s->shading() == QLatin1String("material")}},
                                      QVariantMap{{QStringLiteral("text"), tr("Rendered")}, {QStringLiteral("value"), QStringLiteral("rendered")}, {QStringLiteral("checked"), s->shading() == QLatin1String("rendered")}}},
                                     QCursor::pos());
            if (r.isValid()) s->setShading(r.toString());
        });
        // Pose Mode (Ctrl+Tab, Blender)
        act(view, tr("Toggle Pose Mode"), QKeySequence(QStringLiteral("Ctrl+Tab")), [s, st] {
            const bool pose = st().value(QStringLiteral("gizmo")).toObject().value(QStringLiteral("target")).toString() == QLatin1String("bone");
            s->send(QStringLiteral("gizmo"), {{QStringLiteral("target"), pose ? QStringLiteral("actor") : QStringLiteral("bone")}, {QStringLiteral("enabled"), true}});
            s->send(QStringLiteral("overlay_skeleton"), {{QStringLiteral("value"), !pose}});
        });
    }
    m_liveOnly << toggle(view, tr("Fly the View (WASD + mouse in the game)"), {}, [st] { return st().value(QStringLiteral("camera_fly")).toObject().value(QStringLiteral("on")).toBool(); },
                         [s](bool on) { s->send(QStringLiteral("cam_fly"), {{QStringLiteral("value"), on}}); if (on) QTimer::singleShot(120, s, [s] { s->focusGame(); }); });
    m_liveOnly << toggle(view, tr("Edit in the Picture (click, drag, orbit)"), {}, [st] { return st().value(QStringLiteral("edit")).toObject().value(QStringLiteral("on")).toBool(); },
                         [s](bool on) { s->send(QStringLiteral("edit_mode"), {{QStringLiteral("value"), on}}); if (on) QTimer::singleShot(120, s, [s] { s->focusGame(); }); });
    view->addSeparator();
    QMenu *tools = view->addMenu(tr("Manipulator"));
    act(tools, tr("Select"), QKeySequence(QStringLiteral("Q")), [s] { s->send(QStringLiteral("gizmo"), {{QStringLiteral("enabled"), false}}); });
    act(tools, tr("Move"), QKeySequence(QStringLiteral("W")), [s] { s->send(QStringLiteral("gizmo"), {{QStringLiteral("enabled"), true}, {QStringLiteral("tool"), QStringLiteral("move")}}); });
    act(tools, tr("Rotate"), QKeySequence(QStringLiteral("E")), [s] { s->send(QStringLiteral("gizmo"), {{QStringLiteral("enabled"), true}, {QStringLiteral("tool"), QStringLiteral("rotate")}}); });
    act(tools, tr("Scale"), QKeySequence(QStringLiteral("R")), [s] { s->send(QStringLiteral("gizmo"), {{QStringLiteral("enabled"), true}, {QStringLiteral("tool"), QStringLiteral("scale")}}); });
    tools->addSeparator();
    act(tools, tr("World / Local Axes"), QKeySequence(QStringLiteral("X")), [s, st] {
        const bool local = st().value(QStringLiteral("gizmo")).toObject().value(QStringLiteral("mode")).toString() == QLatin1String("local");
        s->send(QStringLiteral("gizmo"), {{QStringLiteral("mode"), local ? QStringLiteral("world") : QStringLiteral("local")}});
    });
    QMenu *ov = view->addMenu(tr("Overlays"));
    toggle(ov, tr("Skeleton"), QKeySequence(QStringLiteral("Shift+B")), [st] { return st().value(QStringLiteral("skeleton")).toBool(); }, [s](bool on) { s->send(QStringLiteral("overlay_skeleton"), {{QStringLiteral("value"), on}}); });
    toggle(ov, tr("Onion Skin"), {}, [st] { return st().value(QStringLiteral("onion")).toBool(); }, [s](bool on) { s->send(QStringLiteral("overlay_onion"), {{QStringLiteral("value"), on}}); });
    ov->addSeparator();
    for (const auto &pair : {std::pair{tr("Letterbox Off"), 0.0}, {tr("Letterbox 1.85 : 1"), 1.85}, {tr("Letterbox 2 : 1"), 2.0}, {tr("Letterbox 2.39 : 1 (scope)"), 2.39}}) {
        const double v = pair.second;
        QAction *a = act(ov, pair.first, {}, [s, v] { s->send(QStringLiteral("cam_overlay"), {{QStringLiteral("fields"), QVariantMap{{QStringLiteral("letterbox"), v}}}}); });
        a->setCheckable(true);
        connect(ov, &QMenu::aboutToShow, a, [a, st, v] { a->setChecked(std::abs(st().value(QStringLiteral("overlay")).toObject().value(QStringLiteral("letterbox")).toDouble() - v) < 0.01); });
    }
    ov->addSeparator();
    for (const auto &pair : {std::pair{tr("Rule of Thirds"), QStringLiteral("thirds")}, {tr("Safe Areas"), QStringLiteral("safe")}, {tr("Centre Cross"), QStringLiteral("center")}}) {
        const QString k = pair.second;
        toggle(ov, pair.first, {}, [st, k] { return st().value(QStringLiteral("overlay")).toObject().value(k).toBool(); },
               [s, k](bool on) { s->send(QStringLiteral("cam_overlay"), {{QStringLiteral("fields"), QVariantMap{{k, on}}}}); });
    }
    m_standaloneOnly << ov->addSeparator();
    m_standaloneOnly << toggle(ov, tr("Floor Grid"), {}, [st] { const QJsonValue v = st().value(QStringLiteral("overlay")).toObject().value(QStringLiteral("floor")); return v.isUndefined() || v.toBool(); },
                               [s](bool on) { s->send(QStringLiteral("cam_overlay"), {{QStringLiteral("fields"), QVariantMap{{QStringLiteral("floor"), on}}}}); });
    m_standaloneOnly << toggle(ov, tr("Floor in Exported Movies"), {}, [st] { return st().value(QStringLiteral("overlay")).toObject().value(QStringLiteral("render_floor")).toBool(); },
                               [s](bool on) { s->send(QStringLiteral("cam_overlay"), {{QStringLiteral("fields"), QVariantMap{{QStringLiteral("render_floor"), on}}}}); });
    view->addSeparator();
    QMenu *pb = view->addMenu(tr("Playback"));
    act(pb, tr("Play / Pause"), QKeySequence(QStringLiteral("Space")), [s] { s->send(QStringLiteral("seq_toggle")); });
    act(pb, tr("Go to Start"), QKeySequence(QStringLiteral("Home")), [s] {
        const QJsonArray r = s->sequence().value(QStringLiteral("range")).toArray();
        s->send(QStringLiteral("seq_seek"), {{QStringLiteral("t"), r.size() == 2 ? r[0].toDouble() : 0.0}});
    });
    act(pb, tr("Go to End"), QKeySequence(QStringLiteral("End")), [s] {
        const QJsonArray r = s->sequence().value(QStringLiteral("range")).toArray();
        s->send(QStringLiteral("seq_seek"), {{QStringLiteral("t"), r.size() == 2 ? r[1].toDouble() : s->sequence().value(QStringLiteral("length")).toDouble()}});
    });
    act(pb, tr("Previous Frame"), QKeySequence(QStringLiteral("Left")), [this, s] { s->send(QStringLiteral("seq_seek"), {{QStringLiteral("t"), std::max(0.0, frame() - 1)}}); });
    act(pb, tr("Next Frame"), QKeySequence(QStringLiteral("Right")), [this, s] { s->send(QStringLiteral("seq_seek"), {{QStringLiteral("t"), frame() + 1}}); });
    act(pb, tr("Back 10 Frames"), QKeySequence(QStringLiteral("Shift+Left")), [this, s] { s->send(QStringLiteral("seq_seek"), {{QStringLiteral("t"), std::max(0.0, frame() - 10)}}); });
    act(pb, tr("Forward 10 Frames"), QKeySequence(QStringLiteral("Shift+Right")), [this, s] { s->send(QStringLiteral("seq_seek"), {{QStringLiteral("t"), frame() + 10}}); });
    act(pb, tr("Previous Key"), QKeySequence(QStringLiteral("Up")), [this] { key(QStringLiteral("prevKey")); });
    act(pb, tr("Next Key"), QKeySequence(QStringLiteral("Down")), [this] { key(QStringLiteral("nextKey")); });
    pb->addSeparator();
    act(pb, tr("Set Time Selection Start"), QKeySequence(QStringLiteral("I")), [this, s] { s->send(QStringLiteral("seq_range"), {{QStringLiteral("a"), frame()}}); });
    act(pb, tr("Set Time Selection End"), QKeySequence(QStringLiteral("O")), [this, s] { s->send(QStringLiteral("seq_range"), {{QStringLiteral("b"), frame()}}); });
    act(pb, tr("Clear Time Selection"), QKeySequence(QStringLiteral("Alt+O")), [s] { s->send(QStringLiteral("seq_range"), {{QStringLiteral("clear"), true}}); });
    toggle(pb, tr("Loop"), QKeySequence(QStringLiteral("L")), [s] { return s->sequence().value(QStringLiteral("loop")).toBool(); }, [s](bool on) { s->send(QStringLiteral("seq_set"), {{QStringLiteral("loop"), on}}); });
    view->addSeparator();
    act(view, tr("Clip Editor"), QKeySequence(QStringLiteral("F2")), [this, s] { m_lastEditor = s->editor(); s->setEditor(QStringLiteral("clip")); m_timeline->raise(); });
    act(view, tr("Motion Editor"), QKeySequence(QStringLiteral("F3")), [this, s] { m_lastEditor = s->editor(); s->setEditor(QStringLiteral("motion")); m_timeline->raise(); });
    act(view, tr("Graph Editor"), QKeySequence(QStringLiteral("F4")), [this, s] { m_lastEditor = s->editor(); s->setEditor(QStringLiteral("graph")); m_timeline->raise(); });
    act(view, tr("Toggle Last Editor"), QKeySequence(QStringLiteral("Tab")), [this, s] { const QString cur = s->editor(); s->setEditor(m_lastEditor); m_lastEditor = cur; });
    m_liveOnly << view->addSeparator();
    m_liveOnly << toggle(view, tr("Game in Its Own Window"), {}, [s] { return s->detached(); }, [s](bool on) { s->setDetached(on); });
    m_liveOnly << act(view, tr("Mouse to the Game / Back to the Studio"), {}, [s] { s->toggleFocus(); }, tr("F1 does this from anywhere"));

    // ---- Windows
    m_windows = menuBar()->addMenu(tr("&Windows"));
    for (QDockWidget *d : std::as_const(m_docks))
        m_windows->addAction(d->toggleViewAction());
    m_windows->addSeparator();
    act(m_windows, tr("Reset Layout"), {}, [this] { defaultLayout(); });

    // ---- Help
    QMenu *help = menuBar()->addMenu(tr("&Help"));
    act(help, tr("Keyboard Shortcuts"), QKeySequence(QStringLiteral("Ctrl+F1")), [this] { showQmlDialog(tr("Keyboard Shortcuts"), QStringLiteral("panels/HelpSheet.qml"), QSize(780, 640)); });
    act(help, tr("Runtime Log"), {}, [this] { m_console->show(); m_console->raise(); });
    act(help, tr("About RigReel Studio"), {}, [this] {
        QMessageBox::about(this, tr("RigReel Studio"), tr("<b>RigReel Studio %1</b><br>Make animated films with the characters, sets and motion of the games you own. "
                                                            "RigReel reads the installed game's files; nothing is copied or shared.<br><br>Game plugins: RE Engine.<br><br>Qt %2 · C++ / QML / Qt Quick 3D<br>Icons: Lucide (ISC).")
                                                            .arg(m_studio->version(), QString::fromLatin1(qVersion())));
    });
}

void MainWindow::showGames()
{
    showQmlDialog(tr("Games"), QStringLiteral("panels/GamesDialog.qml"), QSize(720, 520));
}

// The centre shows the Studio's own picture (standalone) or the docked game window (live).
void MainWindow::applyRuntimeMode()
{
    const bool standalone = m_studio->standalone();
    m_centre->setCurrentWidget(standalone ? static_cast<QWidget *>(m_viewport) : m_host);
    for (QAction *a : std::as_const(m_liveOnly)) a->setVisible(!standalone);
    for (QAction *a : std::as_const(m_standaloneOnly)) a->setVisible(standalone);
    if (standalone) {
        m_studio->gameWindow()->release();
        UnregisterHotKey(HWND(winId()), kHotkeyF1);
    } else {
        RegisterHotKey(HWND(winId()), kHotkeyF1, MOD_NOREPEAT, VK_F1);
        QTimer::singleShot(0, this, &MainWindow::syncGame);
    }
}

// Live mode: the running Resident Evil 4 with the Director mod animates the film; start it through Steam if needed.
void MainWindow::connectToGame()
{
    m_studio->setRuntimeMode(QStringLiteral("live"));
    if (!m_studio->gameWindow()->find()) {
        m_studio->setStatus(tr("Starting Resident Evil 4 through Steam… the picture appears here when the game is up."), 20000);
        QDesktopServices::openUrl(QUrl(QStringLiteral("steam://rungameid/2050650")));
    }
}

void MainWindow::syncGame()
{
    if (m_studio->standalone())
        return;
    GameWindow *g = m_studio->gameWindow();
    if (m_studio->detached() || g->rendering() || isMinimized() || !isVisible())
        return;
    const QRect r = m_host->gameRect();
    if (r.width() < 64)
        return;
    if (g->embedded()) g->setRect(r);
    else if (g->find()) { g->embed(quintptr(winId()), r); m_host->update(); }
}

void MainWindow::updateStatus()
{
    const bool c = m_studio->connected();
    if (m_studio->standalone()) {
        GameLibrary *lib = m_studio->games();
        m_conn->setText(lib->ready() ? tr("<span style='color:#7fd18a'>●</span> %1").arg(lib->activeTitle().toHtmlEscaped())
                        : lib->opening() ? tr("<span style='color:#e0a040'>●</span> %1").arg(lib->status().toHtmlEscaped())
                                         : tr("<span style='color:#e06060'>●</span> No game open"));
    } else
        m_conn->setText(c ? tr("<span style='color:#7fd18a'>●</span> Game connected") : m_studio->gameWindow()->find() ? tr("<span style='color:#e0a040'>●</span> Runtime not answering") : tr("<span style='color:#e06060'>●</span> Game not running"));
    const QJsonObject seq = m_studio->sequence();
    const double t = seq.value(QStringLiteral("t")).toDouble();
    m_frameLbl->setText(tr("frame %1 / %2  ·  %3 fps").arg(int(std::floor(t))).arg(int(seq.value(QStringLiteral("length")).toDouble(600))).arg(seq.value(QStringLiteral("fps")).toInt(60)));
    const QString proj = m_studio->stateObj().value(QStringLiteral("game")).toObject().value(QStringLiteral("project")).toString();
    PROCESS_MEMORY_COUNTERS pmc{};
    K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    m_projLbl->setText(tr("%1%2  ·  Mem %3 MB").arg(proj.isEmpty() ? tr("untitled") : proj, m_studio->dirty() ? QStringLiteral(" •") : QString()).arg(pmc.WorkingSetSize / (1024 * 1024)));
    const QVariantMap r = m_studio->render();
    if (r.value(QStringLiteral("running")).toBool())
        m_renderLbl->setText(r.value(QStringLiteral("stage")).toString() == QLatin1String("recording audio") ? tr("Rendering: recording sound…")
                                                                                                          : tr("Rendering %1 / %2").arg(r.value(QStringLiteral("frame")).toInt()).arg(r.value(QStringLiteral("total")).toInt()));
    else
        m_renderLbl->clear();
    setWindowTitle(QStringLiteral("RigReel Studio — %1%2").arg(proj.isEmpty() ? tr("untitled") : proj, m_studio->dirty() ? QStringLiteral(" *") : QString()));
    m_host->update();
}

void MainWindow::showEvent(QShowEvent *e)
{
    QMainWindow::showEvent(e);
    QTimer::singleShot(200, this, &MainWindow::syncGame);
}

bool MainWindow::nativeEvent(const QByteArray &type, void *message, qintptr *result)
{
    MSG *msg = static_cast<MSG *>(message);
    if (msg->message == WM_HOTKEY && msg->wParam == kHotkeyF1) {
        m_studio->toggleFocus();
        return true;
    }
    if (msg->message == WM_WINDOWPOSCHANGED || msg->message == WM_MOVE)
        QTimer::singleShot(0, this, &MainWindow::syncGame);
    return QMainWindow::nativeEvent(type, message, result);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (m_studio->dirty() > 0) {
        const auto b = QMessageBox::question(this, tr("RigReel Studio"), tr("There are unsaved changes. Save the project before closing?"),
                                             QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (b == QMessageBox::Cancel) { e->ignore(); return; }
        if (b == QMessageBox::Save) {
            const QString cur = m_studio->stateObj().value(QStringLiteral("game")).toObject().value(QStringLiteral("project")).toString();
            m_studio->cmd(QStringLiteral("project_save"), {{QStringLiteral("name"), cur.isEmpty() ? QStringLiteral("untitled") : cur}});
        }
    }
    QSettings s(Paths::settingsFile(), QSettings::IniFormat);
    s.setValue(QStringLiteral("geometry"), saveGeometry());
    s.setValue(QStringLiteral("state"), saveState());
    s.setValue(QStringLiteral("layoutVersion"), 2);
    m_gameTimer.stop();
    m_studio->gameWindow()->release();
    QMainWindow::closeEvent(e);
}
