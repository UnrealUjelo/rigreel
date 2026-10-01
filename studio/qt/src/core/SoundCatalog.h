// Search over RE4's Wwise event index (REasy's re4.json.gz): ~13,600 events with names, banks and trigger ids.
// Loaded once in the background; searching is a plain substring match over a prepared haystack.
#pragma once

#include <QFuture>
#include <QString>
#include <QVariantMap>
#include <QVector>

class SoundCatalog
{
public:
    SoundCatalog();
    void preload();
    QVariantMap search(const QString &query, const QString &category, int limit);

private:
    struct Item {
        qint64 event;
        QString name;
        QStringList names;
        QString bank, category, path, hay;
        QVariantList triggers;
        bool named;
    };
    void load();
    QVector<Item> m_items;
    QString m_error;
    QFuture<void> m_future;
    bool m_started = false;
};
