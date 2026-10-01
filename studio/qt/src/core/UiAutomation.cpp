#include "UiAutomation.h"

#include "Studio.h"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QJSEngine>
#include <QJsonDocument>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMetaMethod>
#include <QMouseEvent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQuickWindow>

namespace {

bool effectivelyVisible(QQuickItem *it)
{
    // hover-revealed action rows (automationReveal) count as visible: a script cannot hover first
    for (QQuickItem *p = it; p; p = p->parentItem())
        if ((!p->isVisible() && !p->property("automationReveal").toBool()) || p->opacity() <= 0.01)
            return false;
    return it->width() > 0 && it->height() > 0;
}

bool clickable(QQuickItem *it)
{
    const QMetaObject *mo = it->metaObject();
    return mo->indexOfSignal("clicked()") >= 0 || mo->indexOfSignal("clicked(QQuickMouseEvent*)") >= 0 || mo->indexOfMethod("click()") >= 0
        || it->property("automationClick").isValid();
}

QString labelOf(QQuickItem *it)
{
    QStringList parts;
    for (const char *p : {"text", "title", "tooltip", "automationName"}) {
        const QVariant v = it->property(p);
        if (v.isValid() && v.canConvert<QString>() && !v.toString().trimmed().isEmpty())
            parts << v.toString().trimmed();
    }
    parts.removeDuplicates();
    return parts.join(QLatin1Char(' ')).simplified();
}

void walk(QQuickItem *it, QList<QQuickItem *> &out)
{
    out << it;
    for (QQuickItem *c : it->childItems())
        walk(c, out);
}

} // namespace

UiAutomation::UiAutomation(QQmlEngine *engine, Studio *studio, QObject *parent)
    : QObject(parent), m_engine(engine), m_studio(studio) {}

QList<QQuickItem *> UiAutomation::roots() const
{
    QList<QQuickItem *> out;
    for (QWidget *w : QApplication::allWidgets())
        if (auto *qw = qobject_cast<QQuickWidget *>(w))
            if (qw->isVisible() && qw->rootObject())
                out << qw->rootObject();
    return out;
}

QList<UiAutomation::Hit> UiAutomation::collect(const QString &within)
{
    QList<Hit> hits;
    for (QWidget *w : QApplication::allWidgets()) {
        auto *qw = qobject_cast<QQuickWidget *>(w);
        if (!qw || !qw->isVisible() || !qw->rootObject())
            continue;
        QString panel = qw->objectName();
        for (QWidget *p = qw; p; p = p->parentWidget())
            if (auto *d = qobject_cast<QDockWidget *>(p)) { panel = d->windowTitle(); break; }
        if (!within.isEmpty() && !panel.contains(within, Qt::CaseInsensitive))
            continue;
        QList<QQuickItem *> items;
        walk(qw->rootObject(), items);
        for (QQuickItem *it : items) {
            if (!clickable(it) || !effectivelyVisible(it))
                continue;
            const QString l = labelOf(it);
            if (!l.isEmpty())
                hits.append({l, it, QString::fromLatin1(it->metaObject()->className()), panel});
        }
    }
    if (m_main && (within.isEmpty() || within.contains(QLatin1String("menu"), Qt::CaseInsensitive))) {
        std::function<void(QMenu *, const QString &)> addMenu = [&](QMenu *m, const QString &prefix) {
            for (QAction *a : m->actions()) {
                if (a->isSeparator() || !a->isVisible())
                    continue;
                const QString t = prefix + QLatin1String(" > ") + a->text().remove(QLatin1Char('&'));
                if (a->menu())
                    addMenu(a->menu(), t);
                else
                    hits.append({t, a, QStringLiteral("QAction"), QStringLiteral("menu")});
            }
        };
        for (QAction *a : m_main->menuBar()->actions())
            if (a->menu())
                addMenu(a->menu(), a->text().remove(QLatin1Char('&')));
    }
    return hits;
}

QVariantList UiAutomation::texts(const QString &filter, const QString &within)
{
    QVariantList out;
    int i = 0;
    for (const Hit &h : collect(within)) {
        if (!filter.isEmpty() && !h.label.contains(filter, Qt::CaseInsensitive))
            continue;
        out.append(QVariantMap{{QStringLiteral("i"), i++}, {QStringLiteral("text"), h.label}, {QStringLiteral("tag"), h.kind}, {QStringLiteral("cls"), h.panel}});
        if (out.size() >= 200)
            break;
    }
    return out;
}

QVariantMap UiAutomation::click(const QString &text, int nth, const QString &within)
{
    QList<Hit> matches;
    for (const Hit &h : collect(within))
        if (h.label.contains(text, Qt::CaseInsensitive))
            matches << h;
    if (nth < 0 || nth >= matches.size())
        return {{QStringLiteral("clicked"), QVariant()}, {QStringLiteral("matches"), matches.size()}};
    const Hit &h = matches.at(nth);
    if (auto *a = qobject_cast<QAction *>(h.obj)) {
        a->trigger();
    } else if (auto *it = qobject_cast<QQuickItem *>(h.obj)) {
        const QMetaObject *mo = it->metaObject();
        if (mo->indexOfMethod("click()") >= 0) {
            QMetaObject::invokeMethod(it, "click");
        } else if (mo->indexOfSignal("clicked()") >= 0) {
            QMetaObject::invokeMethod(it, "clicked");
        } else {
            // synthesize a left click in the middle of the item
            QQuickWindow *win = it->window();
            const QPointF c = it->mapToScene(QPointF(it->width() / 2, it->height() / 2));
            if (win) {
                QMouseEvent press(QEvent::MouseButtonPress, c, c, it->mapToGlobal(QPointF(it->width() / 2, it->height() / 2)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QMouseEvent release(QEvent::MouseButtonRelease, c, c, it->mapToGlobal(QPointF(it->width() / 2, it->height() / 2)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(win, &press);
                QCoreApplication::sendEvent(win, &release);
            }
        }
    }
    return {{QStringLiteral("clicked"), h.label}, {QStringLiteral("matches"), matches.size()}};
}

QVariantMap UiAutomation::type(const QString &placeholder, const QString &text)
{
    for (QQuickItem *root : roots()) {
        QList<QQuickItem *> items;
        walk(root, items);
        for (QQuickItem *it : items) {
            const QVariant ph = it->property("placeholderText");
            const QString name = it->objectName();
            if (!effectivelyVisible(it))
                continue;
            if ((ph.isValid() && ph.toString().contains(placeholder, Qt::CaseInsensitive)) || (!name.isEmpty() && name == placeholder)) {
                it->setProperty("text", text);
                if (it->metaObject()->indexOfSignal("editingFinished()") >= 0)
                    QMetaObject::invokeMethod(it, "editingFinished");
                if (it->metaObject()->indexOfSignal("accepted()") >= 0)
                    QMetaObject::invokeMethod(it, "accepted");
                return {{QStringLiteral("typed"), true}, {QStringLiteral("into"), ph.toString()}};
            }
        }
    }
    return {{QStringLiteral("typed"), false}};
}

QObject *UiAutomation::item(const QString &objectName)
{
    for (QQuickItem *root : roots()) {
        if (root->objectName() == objectName) return root;
        QList<QQuickItem *> items;
        walk(root, items);
        for (QQuickItem *it : items) if (it->objectName() == objectName) return it;
    }
    return nullptr;
}

QVariantMap UiAutomation::drag(const QString &objectName, double x0, double y0, double x1, double y1, int steps, int button)
{
    auto *it = qobject_cast<QQuickItem *>(item(objectName));
    QQuickWindow *win = it ? it->window() : nullptr;
    if (!win) return {{QStringLiteral("dragged"), false}};
    const Qt::MouseButton b = button == 2 ? Qt::RightButton : button == 4 ? Qt::MiddleButton : Qt::LeftButton;
    auto send = [&](QEvent::Type type, double x, double y, Qt::MouseButton which, Qt::MouseButtons held) {
        const QPointF local(x, y), scene = it->mapToScene(local);
        QMouseEvent e(type, scene, scene, it->mapToGlobal(local), which, held, Qt::NoModifier);
        QCoreApplication::sendEvent(win, &e);
    };
    send(QEvent::MouseButtonPress, x0, y0, b, b);
    steps = std::max(1, steps);
    for (int i = 1; i <= steps; ++i) {
        const double t = double(i) / steps;
        send(QEvent::MouseMove, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, Qt::NoButton, b);
        QCoreApplication::processEvents();
    }
    send(QEvent::MouseButtonRelease, x1, y1, b, Qt::NoButton);
    return {{QStringLiteral("dragged"), true}};
}

QString UiAutomation::text()
{
    QStringList out;
    for (QQuickItem *root : roots()) {
        QList<QQuickItem *> items;
        walk(root, items);
        for (QQuickItem *it : items) {
            const QVariant v = it->property("text");
            if (v.isValid() && effectivelyVisible(it) && !v.toString().trimmed().isEmpty())
                out << v.toString().trimmed();
        }
    }
    return out.join(QLatin1Char('\n')).left(20000);
}

QJsonValue UiAutomation::eval(const QString &code)
{
    QJSValue helper = m_engine->newQObject(this);
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    QJSValue window = m_engine->newObject();
    window.setProperty(QStringLiteral("__director"), helper);
    QJSValue studio = m_engine->newQObject(m_studio);
    QQmlEngine::setObjectOwnership(m_studio, QQmlEngine::CppOwnership);
    // eval inside a function so the script sees window / studio as plain names and its last expression is returned
    QJSValue fn = m_engine->evaluate(QStringLiteral("(function(window, studio, __code) { return eval(__code); })"));
    QJSValue r = fn.call({window, studio, QJSValue(code)});
    if (r.isError())
        return QJsonValue(QStringLiteral("error: ") + r.toString());
    const QVariant v = r.toVariant();
    if (v.metaType().id() == QMetaType::QVariantMap || v.metaType().id() == QMetaType::QVariantList)
        return QJsonValue::fromVariant(QJsonDocument::fromVariant(v).toVariant());
    return QJsonValue::fromVariant(v);
}
