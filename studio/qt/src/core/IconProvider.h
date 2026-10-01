// image://icon/<name>/<color> : a Lucide SVG (resources/icons) drawn in any colour at any size, so one set of
// stroke icons serves every state (normal / hover / active / disabled) without separate files.
#pragma once
#include <QQuickImageProvider>

class IconProvider : public QQuickImageProvider
{
public:
    IconProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
    // for C++ painters (timeline headers): cached per name / colour / size
    static QImage icon(const QString &name, const QColor &color, int px);
};
