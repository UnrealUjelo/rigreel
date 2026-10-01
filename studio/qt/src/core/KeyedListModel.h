// A list model fed with plain maps (from the runtime state) that keeps its delegates alive: rows are matched by a
// key, so an update only emits dataChanged for rows that really changed, and inserts / removes for the rest.
// QML reads a row as `model.item` (the map) and `model.key`.
#pragma once

#include <QAbstractListModel>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class KeyedListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(KeyedModel)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString keyField READ keyField WRITE setKeyField)
public:
    enum Roles { ItemRole = Qt::UserRole + 1, KeyRole, IndexRole };
    explicit KeyedListModel(QObject *parent = nullptr) : KeyedListModel(QStringLiteral("key"), parent) {}
    KeyedListModel(QString keyField, QObject *parent);
    QString keyField() const { return m_keyField; }
    void setKeyField(const QString &k) { m_keyField = k; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void setItems(const QVariantList &items);
    int count() const { return int(m_keys.size()); }
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int indexOf(const QString &key) const;

signals:
    void countChanged();

private:
    QString keyOf(const QVariantMap &m, int fallback) const;
    QString m_keyField;
    QStringList m_keys;
    QList<QVariantMap> m_items;
};
