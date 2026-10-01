#include "KeyedListModel.h"

KeyedListModel::KeyedListModel(QString keyField, QObject *parent)
    : QAbstractListModel(parent), m_keyField(std::move(keyField)) {}

int KeyedListModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(m_items.size()); }

QVariant KeyedListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    switch (role) {
    case ItemRole: return m_items.at(index.row());
    case KeyRole: return m_keys.at(index.row());
    case IndexRole: return index.row();
    default: return {};
    }
}

QHash<int, QByteArray> KeyedListModel::roleNames() const
{
    return {{ItemRole, "item"}, {KeyRole, "key"}, {IndexRole, "row"}};
}

QString KeyedListModel::keyOf(const QVariantMap &m, int fallback) const
{
    const QVariant k = m.value(m_keyField);
    return k.isValid() ? k.toString() : QString::number(fallback);
}

QVariantMap KeyedListModel::get(int row) const { return row >= 0 && row < m_items.size() ? m_items.at(row) : QVariantMap(); }
int KeyedListModel::indexOf(const QString &key) const { return int(m_keys.indexOf(key)); }

void KeyedListModel::setItems(const QVariantList &list)
{
    QStringList keys;
    QList<QVariantMap> items;
    keys.reserve(list.size());
    items.reserve(list.size());
    for (int i = 0; i < list.size(); ++i) {
        QVariantMap m = list.at(i).toMap();
        QString k = keyOf(m, i);
        while (keys.contains(k)) // duplicate keys would confuse the diff
            k += QLatin1Char('#');
        keys << k;
        items << m;
    }
    const int oldCount = int(m_keys.size());

    // 1) remove rows whose key is gone (from the bottom so indices stay valid)
    for (int i = int(m_keys.size()) - 1; i >= 0; --i) {
        if (!keys.contains(m_keys.at(i))) {
            beginRemoveRows({}, i, i);
            m_keys.removeAt(i);
            m_items.removeAt(i);
            endRemoveRows();
        }
    }
    // 2) walk the new order: move / insert / update in place
    for (int i = 0; i < keys.size(); ++i) {
        const QString &k = keys.at(i);
        const int at = int(m_keys.indexOf(k, i));
        if (at < 0) {
            beginInsertRows({}, i, i);
            m_keys.insert(i, k);
            m_items.insert(i, items.at(i));
            endInsertRows();
        } else {
            if (at != i) {
                beginMoveRows({}, at, at, {}, i);
                m_keys.move(at, i);
                m_items.move(at, i);
                endMoveRows();
            }
            if (m_items.at(i) != items.at(i)) {
                m_items[i] = items.at(i);
                const QModelIndex idx = index(i);
                emit dataChanged(idx, idx, {ItemRole});
            }
        }
    }
    if (oldCount != m_keys.size())
        emit countChanged();
}
