// Automation for the MCP server and tests: /eval runs JavaScript in the Studio's QML engine with three globals:
//   studio             the Studio object (state, data, cmd(), ...)
//   window.__director  texts(filter, within) / click(text, nth, within) / type(placeholder, text) / text()
//                      over every visible control in every panel, plus the menu bar's actions
// (the same helper names the React UI had, so existing scripts keep working).
#pragma once

#include <QJsonValue>
#include <QMainWindow>
#include <QObject>
#include <QPointer>
#include <QVariantList>

class QQmlEngine;
class QQuickItem;
class QMainWindow;
class Studio;

class UiAutomation : public QObject
{
    Q_OBJECT
public:
    UiAutomation(QQmlEngine *engine, Studio *studio, QObject *parent = nullptr);
    void setMainWindow(QMainWindow *w) { m_main = w; }
    QJsonValue eval(const QString &code);

    Q_INVOKABLE QVariantList texts(const QString &filter = {}, const QString &within = {});
    Q_INVOKABLE QVariantMap click(const QString &text, int nth = 0, const QString &within = {});
    Q_INVOKABLE QVariantMap type(const QString &placeholder, const QString &text);
    Q_INVOKABLE QString text();
    // a QML item by objectName (its functions and properties can then be used from /eval)
    Q_INVOKABLE QObject *item(const QString &objectName);
    // a mouse drag inside that item (item coordinates), in `steps` moves: viewport handles, navigation
    Q_INVOKABLE QVariantMap drag(const QString &objectName, double x0, double y0, double x1, double y1, int steps = 8, int button = 1);

private:
    struct Hit { QString label; QObject *obj; QString kind; QString panel; };
    QList<Hit> collect(const QString &within);
    QList<QQuickItem *> roots() const;

    QQmlEngine *m_engine;
    Studio *m_studio;
    QPointer<QMainWindow> m_main;
};
