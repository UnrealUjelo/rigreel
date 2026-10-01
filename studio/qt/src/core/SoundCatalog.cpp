#include "SoundCatalog.h"

#include "Paths.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtConcurrent/QtConcurrentRun>
#include <QtZlib/zlib.h>

#include <algorithm>

namespace {

QByteArray gunzip(const QByteArray &in)
{
    QByteArray out;
    z_stream zs{};
    if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) // 16+: gzip header
        return {};
    zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(in.data()));
    zs.avail_in = uInt(in.size());
    char buf[1 << 16];
    int ret;
    do {
        zs.next_out = reinterpret_cast<Bytef *>(buf);
        zs.avail_out = sizeof(buf);
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&zs);
            return {};
        }
        out.append(buf, int(sizeof(buf) - zs.avail_out));
    } while (ret != Z_STREAM_END);
    inflateEnd(&zs);
    return out;
}

QString categoryOf(const QString &text)
{
    const QString s = text.toLower();
    auto any = [&](std::initializer_list<const char *> words) {
        for (const char *w : words)
            if (s.contains(QLatin1String(w)))
                return true;
        return false;
    };
    if (any({"bgm", "music", "stinger"})) return QStringLiteral("music");
    if (any({"voice", "_vo_", "dialog", "talk", "conv", "radio"})) return QStringLiteral("voice");
    if (any({"roomtone", "ambient", "ambience", "environment", "wind", "rain", "river", "waterfall"})) return QStringLiteral("ambience");
    if (any({"gui", "menu", "system_se", "ui_"})) return QStringLiteral("ui");
    if (any({"csa", "cse", "cutscene"})) return QStringLiteral("cutscene");
    return QStringLiteral("sfx");
}

} // namespace

SoundCatalog::SoundCatalog() = default;

void SoundCatalog::preload()
{
    if (m_started)
        return;
    m_started = true;
    m_future = QtConcurrent::run([this] { load(); });
}

void SoundCatalog::load()
{
    QFile f(Paths::workspace() + QStringLiteral("/tools/REasy/resources/data/sound/re4.json.gz"));
    if (!f.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("RE4 sound index is missing: ") + f.fileName();
        return;
    }
    const QJsonObject raw = QJsonDocument::fromJson(gunzip(f.readAll())).object();
    if (raw.isEmpty()) {
        m_error = QStringLiteral("Could not read the sound index");
        return;
    }
    const QJsonObject names = raw.value(QStringLiteral("names")).toObject().value(QStringLiteral("event")).toObject();
    const QJsonObject banks = raw.value(QStringLiteral("banks")).toObject();
    const QJsonObject bankEvents = raw.value(QStringLiteral("bank_events")).toObject();
    QVector<Item> items;
    for (auto b = bankEvents.begin(); b != bankEvents.end(); ++b) {
        const QString bank = b.key();
        const QJsonArray paths = banks.value(bank).toObject().value(QStringLiteral("paths")).toArray();
        const QJsonObject events = b.value().toObject();
        for (auto e = events.begin(); e != events.end(); ++e) {
            const QJsonObject rec = e.value().toObject();
            QStringList evNames;
            for (const QJsonValue &v : rec.value(QStringLiteral("names")).toArray())
                if (!evNames.contains(v.toString())) evNames << v.toString();
            for (const QJsonValue &v : names.value(e.key()).toArray())
                if (!evNames.contains(v.toString())) evNames << v.toString();
            Item it;
            it.event = e.key().toLongLong();
            it.name = evNames.isEmpty() ? QStringLiteral("Event ") + e.key() : evNames.first();
            it.names = evNames.mid(1, 3);
            it.bank = bank;
            it.triggers = rec.value(QStringLiteral("trigger_ids")).toArray().toVariantList();
            QStringList trig;
            for (const QVariant &t : it.triggers) trig << t.toString();
            it.hay = (QStringList{it.name} + evNames + QStringList{bank, e.key()} + trig).join(QLatin1Char(' ')).toLower();
            it.category = categoryOf(it.hay);
            it.path = paths.isEmpty() ? QString() : paths.first().toString();
            it.named = !evNames.isEmpty();
            items.append(it);
        }
    }
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        if (a.named != b.named) return a.named;
        const int c = QString::compare(a.name, b.name, Qt::CaseInsensitive);
        return c != 0 ? c < 0 : a.bank < b.bank;
    });
    m_items = std::move(items);
}

QVariantMap SoundCatalog::search(const QString &query, const QString &category, int limit)
{
    preload();
    m_future.waitForFinished();
    const QStringList words = query.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    limit = std::clamp(limit, 1, 500);
    QVariantList out;
    for (const Item &it : m_items) {
        if (!category.isEmpty() && it.category != category)
            continue;
        bool ok = true;
        for (const QString &w : words)
            if (!it.hay.contains(w)) { ok = false; break; }
        if (!ok)
            continue;
        out.append(QVariantMap{{QStringLiteral("event"), it.event}, {QStringLiteral("name"), it.name}, {QStringLiteral("names"), it.names},
                               {QStringLiteral("bank"), it.bank}, {QStringLiteral("category"), it.category}, {QStringLiteral("triggers"), it.triggers},
                               {QStringLiteral("path"), it.path}});
        if (out.size() >= limit)
            break;
    }
    QVariantMap r{{QStringLiteral("results"), out}, {QStringLiteral("total"), m_items.size()}, {QStringLiteral("query"), query}, {QStringLiteral("category"), category}};
    if (!m_error.isEmpty())
        r.insert(QStringLiteral("error"), m_error);
    return r;
}
