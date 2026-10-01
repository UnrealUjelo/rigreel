#include "IconProvider.h"

#include <QFile>
#include <QHash>
#include <QMutex>
#include <QPainter>
#include <QSvgRenderer>

IconProvider::IconProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage IconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    static QMutex mutex;
    static QHash<QString, QByteArray> cache;
    const int slash = int(id.indexOf(QLatin1Char('/')));
    const QString name = slash < 0 ? id : id.left(slash);
    QString color = slash < 0 ? QStringLiteral("#d0d0d0") : id.mid(slash + 1);
    if (!color.startsWith(QLatin1Char('#')))
        color.prepend(QLatin1Char('#'));
    QByteArray svg;
    {
        QMutexLocker l(&mutex);
        auto it = cache.find(name);
        if (it == cache.end()) {
            QFile f(QStringLiteral(":/qt/qml/Director/resources/icons/") + name + QStringLiteral(".svg"));
            if (!f.open(QIODevice::ReadOnly)) {
                f.setFileName(QStringLiteral(":/qt/qml/Director/resources/icons/circle-help.svg"));
                f.open(QIODevice::ReadOnly);
            }
            it = cache.insert(name, f.readAll());
        }
        svg = it.value();
    }
    svg.replace("currentColor", color.toUtf8());
    const QSize s = requestedSize.isValid() && requestedSize.width() > 0 ? requestedSize : QSize(24, 24);
    QImage img(s, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QSvgRenderer r(svg);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    r.render(&p);
    if (size)
        *size = s;
    return img;
}

QImage IconProvider::icon(const QString &name, const QColor &color, int px)
{
    static QHash<QString, QImage> cache;
    const QString key = name + QLatin1Char('/') + color.name(QColor::HexArgb) + QLatin1Char('/') + QString::number(px);
    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return it.value();
    IconProvider p;
    QSize s;
    QImage img = p.requestImage(name + QLatin1Char('/') + color.name(QColor::HexRgb), &s, QSize(px * 2, px * 2));
    img.setDevicePixelRatio(2.0);
    if (color.alpha() < 255) {
        QImage faded(img.size(), QImage::Format_ARGB32_Premultiplied);
        faded.fill(Qt::transparent);
        QPainter pp(&faded);
        pp.setOpacity(color.alphaF());
        pp.drawImage(0, 0, img);
        pp.end();
        faded.setDevicePixelRatio(2.0);
        img = faded;
    }
    cache.insert(key, img);
    return img;
}
