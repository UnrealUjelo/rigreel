// Local HTTP control endpoint (127.0.0.1:47931, loopback only) used by the MCP server, render.py-style scripts and
// tests. Same routes as the old Python host, so every existing tool keeps working:
//   GET  /state /data /flags /render /import /log?n= /sounds?q=&category=&limit= /media?path= /screenshot?target=&scale=
//   POST /send {op,...}   /eval {code}   /focus {target}   /game {action}   /import {...}   /pick {kind}   /render {...}
#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>

class QTcpServer;
class QTcpSocket;
class Studio;
class UiAutomation;
class QWidget;

class ControlServer : public QObject
{
    Q_OBJECT
public:
    ControlServer(Studio *studio, UiAutomation *automation, QObject *parent = nullptr);
    bool listen(quint16 port = 47931);
    void setMainWindow(QWidget *w) { m_main = w; }
    QByteArray screenshot(const QString &target, double scale);

private:
    void onConnection();
    void onReadable(QTcpSocket *s);
    void handle(QTcpSocket *s, const QByteArray &method, const QByteArray &target, const QByteArray &body, const QHash<QByteArray, QByteArray> &headers);
    void reply(QTcpSocket *s, int code, const QByteArray &type, const QByteArray &body, const QList<QPair<QByteArray, QByteArray>> &extra = {});
    void json(QTcpSocket *s, int code, const QJsonValue &v);

    Studio *m_studio;
    UiAutomation *m_auto;
    QPointer<QWidget> m_main;
    QTcpServer *m_server;
    QHash<QTcpSocket *, QByteArray> m_buf;
};
