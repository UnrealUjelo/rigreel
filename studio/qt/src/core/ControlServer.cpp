#include "ControlServer.h"

#include "Bridge.h"
#include "Capture.h"
#include "GameWindow.h"
#include "Paths.h"
#include "Studio.h"
#include "UiAutomation.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QQuickWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QWidget>

#include <windows.h>

ControlServer::ControlServer(Studio *studio, UiAutomation *automation, QObject *parent)
    : QObject(parent), m_studio(studio), m_auto(automation), m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &ControlServer::onConnection);
}

bool ControlServer::listen(quint16 port) { return m_server->listen(QHostAddress::LocalHost, port); }

void ControlServer::onConnection()
{
    while (QTcpSocket *s = m_server->nextPendingConnection()) {
        connect(s, &QTcpSocket::readyRead, this, [this, s] { onReadable(s); });
        connect(s, &QTcpSocket::disconnected, this, [this, s] { m_buf.remove(s); s->deleteLater(); });
    }
}

void ControlServer::onReadable(QTcpSocket *s)
{
    QByteArray &buf = m_buf[s];
    buf += s->readAll();
    const int headEnd = int(buf.indexOf("\r\n\r\n"));
    if (headEnd < 0)
        return;
    const QList<QByteArray> lines = buf.left(headEnd).split('\n');
    if (lines.isEmpty())
        return;
    const QList<QByteArray> req = lines.first().trimmed().split(' ');
    if (req.size() < 2) {
        s->disconnectFromHost();
        return;
    }
    QHash<QByteArray, QByteArray> headers;
    for (int i = 1; i < lines.size(); ++i) {
        const int c = int(lines[i].indexOf(':'));
        if (c > 0)
            headers.insert(lines[i].left(c).trimmed().toLower(), lines[i].mid(c + 1).trimmed());
    }
    const int len = headers.value("content-length", "0").toInt();
    if (buf.size() < headEnd + 4 + len)
        return; // wait for the rest of the body
    const QByteArray body = buf.mid(headEnd + 4, len);
    buf.clear();
    handle(s, req[0], req[1], body, headers);
}

void ControlServer::reply(QTcpSocket *s, int code, const QByteArray &type, const QByteArray &body, const QList<QPair<QByteArray, QByteArray>> &extra)
{
    QByteArray head = "HTTP/1.1 " + QByteArray::number(code) + (code == 200 ? " OK" : code == 206 ? " Partial Content" : code == 404 ? " Not Found" : " Error") + "\r\n";
    head += "Content-Type: " + type + "\r\n";
    head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    head += "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Headers: Content-Type, Range\r\n";
    for (const auto &h : extra)
        head += h.first + ": " + h.second + "\r\n";
    head += "Connection: close\r\n\r\n";
    s->write(head);
    s->write(body);
    s->flush();
    s->disconnectFromHost();
}

void ControlServer::json(QTcpSocket *s, int code, const QJsonValue &v)
{
    QByteArray body;
    if (v.isObject())
        body = QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact);
    else if (v.isArray())
        body = QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact);
    else
        body = QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact).mid(1).chopped(1);
    reply(s, code, "application/json", body);
}

QByteArray ControlServer::screenshot(const QString &target, double scale)
{
    QImage img;
    GameWindow *g = m_studio->gameWindow();
    if (target == QLatin1String("game") && m_studio->standalone() && m_main) {
        // the Studio's own picture of the film
        if (auto *vp = m_main->findChild<QQuickWidget *>(QStringLiteral("scene/Viewport3D.qml")))
            img = vp->grabFramebuffer();
    } else if (target == QLatin1String("game")) {
        img = Capture::window(g->find());
    } else if (target == QLatin1String("studio") && m_main) {
        // our own widgets render even when another window covers the Studio; the game (a separate window
        // sitting over the viewport) is grabbed on its own and composited at its place
        img = m_main->grab().toImage();
        const qreal dpr = m_main->devicePixelRatioF();
        if (g->embedded() && !g->rendering()) {
            const QImage gi = Capture::window(g->hwnd());
            RECT wr;
            GetWindowRect(HWND(m_main->winId()), &wr);
            const QRect gr = Capture::windowRect(g->hwnd());
            QPainter p(&img);
            // grab() is in device pixels of the client area; window rects are physical
            POINT origin{0, 0};
            ClientToScreen(HWND(m_main->winId()), &origin);
            p.drawImage(QRectF((gr.x() - origin.x) / dpr, (gr.y() - origin.y) / dpr, gr.width() / dpr, gr.height() / dpr), gi);
        }
    } else {
        img = Capture::screenRect(0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    }
    if (img.isNull())
        return {};
    if (scale < 0.999)
        img = img.scaled(std::max(1, int(img.width() * scale)), std::max(1, int(img.height() * scale)), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return Capture::png(img, 6);
}

void ControlServer::handle(QTcpSocket *s, const QByteArray &method, const QByteArray &target, const QByteArray &body, const QHash<QByteArray, QByteArray> &headers)
{
    const QUrl url(QString::fromUtf8(target));
    const QString path = url.path();
    const QUrlQuery q(url);
    const QJsonObject in = QJsonDocument::fromJson(body.isEmpty() ? QByteArray("{}") : body).object();

    if (method == "OPTIONS") {
        reply(s, 204, "text/plain", {}, {{"Access-Control-Allow-Methods", "GET, POST, OPTIONS"}});
        return;
    }
    if (method == "GET") {
        if (path == QLatin1String("/state"))
            return json(s, 200, m_studio->hostStateJson());
        if (path == QLatin1String("/data"))
            return json(s, 200, m_studio->dataObj());
        if (path == QLatin1String("/sounds"))
            return json(s, 200, QJsonObject::fromVariantMap(m_studio->searchSounds(q.queryItemValue(QStringLiteral("q")), q.queryItemValue(QStringLiteral("category")),
                                                                                q.hasQueryItem(QStringLiteral("limit")) ? q.queryItemValue(QStringLiteral("limit")).toInt() : 200)));
        if (path == QLatin1String("/flags"))
            return json(s, 200, QJsonObject{{QStringLiteral("free_cursor"), true}, {QStringLiteral("game_focused"), m_studio->gameFocused()},
                                            {QStringLiteral("bridge_dir"), Paths::bridgeDir()}, {QStringLiteral("ui"), QStringLiteral("qt")},
                                            {QStringLiteral("hwnd"), double(m_main ? quintptr(m_main->winId()) : 0)}});
        if (path == QLatin1String("/render"))
            return json(s, 200, m_studio->renderJson());
        if (path == QLatin1String("/import"))
            return json(s, 200, m_studio->importJson());
        if (path == QLatin1String("/log")) {
            const int n = q.hasQueryItem(QStringLiteral("n")) ? q.queryItemValue(QStringLiteral("n")).toInt() : 60;
            return json(s, 200, QJsonObject{{QStringLiteral("log"), QJsonArray::fromStringList(m_studio->runtimeLog(n))}});
        }
        if (path == QLatin1String("/media")) {
            QFile f(q.queryItemValue(QStringLiteral("path"), QUrl::FullyDecoded));
            if (!f.open(QIODevice::ReadOnly))
                return json(s, 404, QJsonObject{{QStringLiteral("error"), QStringLiteral("no such media file")}});
            const QByteArray all = f.readAll();
            const QString ext = QFileInfo(f.fileName()).suffix().toLower();
            const QByteArray type = ext == QLatin1String("png") ? "image/png" : ext == QLatin1String("jpg") ? "image/jpeg" : ext == QLatin1String("wav") ? "audio/wav"
                : ext == QLatin1String("mp3") ? "audio/mpeg" : ext == QLatin1String("mp4") ? "video/mp4" : "application/octet-stream";
            Q_UNUSED(headers)
            return reply(s, 200, type, all, {{"Accept-Ranges", "none"}});
        }
        if (path == QLatin1String("/screenshot")) {
            const QByteArray png = screenshot(q.queryItemValue(QStringLiteral("target")).isEmpty() ? QStringLiteral("game") : q.queryItemValue(QStringLiteral("target")),
                                              q.hasQueryItem(QStringLiteral("scale")) ? q.queryItemValue(QStringLiteral("scale")).toDouble() : 0.5);
            if (png.isEmpty())
                return json(s, 500, QJsonObject{{QStringLiteral("error"), QStringLiteral("capture failed (is the game running?)")}});
            return reply(s, 200, "image/png", png);
        }
        return json(s, 404, QJsonObject{{QStringLiteral("error"), QStringLiteral("unknown path")}});
    }
    if (method == "POST") {
        if (path == QLatin1String("/send"))
            return json(s, 200, QJsonObject{{QStringLiteral("id"), double(m_studio->sendJson(in))}});
        if (path == QLatin1String("/eval"))
            return json(s, 200, QJsonObject{{QStringLiteral("result"), m_auto->eval(in.value(QStringLiteral("code")).toString())}});
        if (path == QLatin1String("/focus")) {
            if (in.value(QStringLiteral("target")).toString() == QLatin1String("game"))
                m_studio->focusGame();
            else
                m_studio->focusStudio();
            return json(s, 200, QJsonObject{{QStringLiteral("ok"), true}});
        }
        if (path == QLatin1String("/game")) {
            const QString act = in.value(QStringLiteral("action")).toString();
            GameWindow *g = m_studio->gameWindow();
            if (act == QLatin1String("render")) {
                const QRect r = g->renderMode(true, in.value(QStringLiteral("w")).toInt(1920), in.value(QStringLiteral("h")).toInt(1080));
                QJsonObject o = QJsonObject::fromVariantMap(g->status());
                o.insert(QStringLiteral("rect"), QJsonArray{r.x(), r.y(), r.width(), r.height()});
                return json(s, 200, o);
            }
            if (act == QLatin1String("embed")) {
                g->renderMode(false);
                m_studio->setDetached(false);
                return json(s, 200, QJsonObject::fromVariantMap(g->status()));
            }
            if (act == QLatin1String("release")) {
                m_studio->setDetached(true);
                return json(s, 200, QJsonObject::fromVariantMap(g->status()));
            }
            return json(s, 400, QJsonObject{{QStringLiteral("error"), QStringLiteral("action must be render|embed|release")}});
        }
        if (path == QLatin1String("/import")) {
            const bool ok = m_studio->importAnim(in.value(QStringLiteral("path")).toString(), in.value(QStringLiteral("addr")).toDouble(),
                                                 in.value(QStringLiteral("start")).toDouble(-1));
            QJsonObject o = m_studio->importJson();
            o.insert(QStringLiteral("started"), ok);
            return json(s, 200, o);
        }
        if (path == QLatin1String("/pick"))
            return json(s, 200, QJsonObject{{QStringLiteral("path"), m_studio->pickFile(in.value(QStringLiteral("kind")).toString(QStringLiteral("audio")))}});
        if (path == QLatin1String("/render")) {
            const bool ok = m_studio->startRender(in.toVariantMap());
            QJsonObject o = m_studio->renderJson();
            o.insert(QStringLiteral("started"), ok);
            return json(s, 200, o);
        }
        return json(s, 404, QJsonObject{{QStringLiteral("error"), QStringLiteral("unknown path")}});
    }
    json(s, 405, QJsonObject{{QStringLiteral("error"), QStringLiteral("method not allowed")}});
}
