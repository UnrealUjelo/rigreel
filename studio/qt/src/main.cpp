// RigReel Studio - make animated films with the games you own, Qt 6 C++ / QML / Qt Quick 3D.
//
// Standalone (default): game plugins (plugins/games/*.dll) read the installed game's archives and the Studio
// animates and renders the film itself. Live: the running game animates the film through the Director runtime
// (REFramework Lua, mod/Director); the Studio docks the game window and talks to it through the bridge files.
// Both serve 127.0.0.1:47931 for the MCP tools.
#include "ControlServer.h"
#include "IconProvider.h"
#include "MainWindow.h"
#include "Paths.h"
#include "Studio.h"
#include "Theme.h"
#include "UiAutomation.h"

#include "GameWindow.h"
#include "StageScene.h"
#include "ThumbnailProvider.h"

#include <QApplication>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QMessageBox>
#include <QTextStream>
#include <QTime>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTcpSocket>

// A GUI app has no console: warnings (QML errors included) go beside the application's settings file.
static void logToFile(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    static QFile f(QFileInfo(Paths::settingsFile()).absolutePath() + QStringLiteral("/rigreel.log"));
    if (!f.isOpen()) {
        f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
    }
    const char *lvl = type == QtDebugMsg ? "debug" : type == QtInfoMsg ? "info" : type == QtWarningMsg ? "warn" : type == QtCriticalMsg ? "error" : "fatal";
    QTextStream ts(&f);
    ts << QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")) << ' ' << lvl << ' ' << msg;
    if (ctx.file)
        ts << "  (" << ctx.file << ':' << ctx.line << ')';
    ts << '\n';
    ts.flush();
}

int main(int argc, char *argv[])
{
    qInstallMessageHandler(logToFile);
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    // Keep the internal application name stable so existing QSettings continue to load after the public rebrand.
    QApplication::setApplicationName(QStringLiteral("Director Studio"));
    QApplication::setApplicationDisplayName(QStringLiteral("RigReel Studio"));
    QApplication::setOrganizationName(QStringLiteral("Director"));
    Theme::apply(app);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // one Studio at a time: two would fight over the game window and the bridge
    {
        QTcpSocket probe;
        probe.connectToHost(QStringLiteral("127.0.0.1"), 47931);
        if (probe.waitForConnected(300)) {
            QMessageBox::warning(nullptr, QStringLiteral("RigReel Studio"),
                                 QStringLiteral("Another RigReel Studio instance is already running (port 47931 is taken).\n\nClose it first, then start this one again."));
            return 1;
        }
    }

    qmlRegisterType<StageScene>("Director.Engine", 1, 0, "StageScene");
    QQmlEngine engine;
    engine.addImageProvider(QStringLiteral("icon"), new IconProvider);
    Studio studio(&engine);
    engine.addImageProvider(QStringLiteral("thumb"), new ThumbnailProvider(studio.games()));
    engine.rootContext()->setContextProperty(QStringLiteral("studio"), &studio);
    UiAutomation automation(&engine, &studio);

    MainWindow win(&engine, &studio);
    automation.setMainWindow(&win);
    ControlServer server(&studio, &automation);
    server.setMainWindow(&win);
    if (!server.listen(47931))
        qWarning("control server: port 47931 unavailable");
    win.show();
    // live mode: one double-click starts everything, the game through Steam if it is not running yet (--no-game skips it)
    if (!studio.standalone() && !QCoreApplication::arguments().contains(QStringLiteral("--no-game")) && !studio.gameWindow()->find()) {
        studio.setStatus(QStringLiteral("Starting Resident Evil 4 through Steam… the picture appears here when the game is up."), 20000);
        QDesktopServices::openUrl(QUrl(QStringLiteral("steam://rungameid/2050650")));
    }
    return app.exec();
}
