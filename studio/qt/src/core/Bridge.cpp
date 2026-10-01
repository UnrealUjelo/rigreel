#include "Bridge.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>

namespace {

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

// Lua's json writes an empty table as null (or {}): make every list a real array
QJsonArray arr(const QJsonValue &v)
{
    if (v.isArray())
        return v.toArray();
    return {};
}

QJsonObject obj(const QJsonValue &v) { return v.isObject() ? v.toObject() : QJsonObject(); }

QByteArray readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

} // namespace

Bridge::Bridge(const QString &dir, QObject *parent)
    : QObject(parent), m_dir(dir)
{
    // The runtime remembers its last acknowledged command while the Studio restarts independently:
    // epoch milliseconds stay exact in Lua doubles and can never fall below a running game's counter.
    m_nextId = nowMs();
    QDir().mkpath(m_dir);
    connect(&m_pollTimer, &QTimer::timeout, this, &Bridge::poll);
    m_pollTimer.start(30);
    connect(&m_pingTimer, &QTimer::timeout, this, &Bridge::ping);
    m_pingTimer.start(100);
}

QJsonObject Bridge::state() const { QMutexLocker l(&m_mutex); return m_state; }
QJsonObject Bridge::data() const { QMutexLocker l(&m_mutex); return m_data; }
QByteArray Bridge::rawState() const { QMutexLocker l(&m_mutex); return m_rawState; }
int Bridge::pending() const { QMutexLocker l(&m_mutex); return int(m_pending.size()); }
bool Bridge::connected() const { return nowMs() - m_lastStateMs < 2000; }

void Bridge::setGameFocused(bool focused) { m_gameFocused = focused; }

void Bridge::poll()
{
    const QString statePath = m_dir + QStringLiteral("/state.json");
    QFileInfo fi(statePath);
    if (fi.exists()) {
        const qint64 mt = fi.lastModified().toMSecsSinceEpoch() * 1000 + fi.size() % 1000;
        if (mt != m_stateMtime) {
            const QByteArray raw = readFile(statePath);
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                m_stateMtime = mt;
                m_lastStateMs = nowMs();
                const QJsonObject s = normalizeState(doc.object());
                const qint64 ack = qint64(s.value(QStringLiteral("ack")).toDouble());
                const qint64 ver = qint64(s.value(QStringLiteral("data_ver")).toDouble(-1));
                {
                    QMutexLocker l(&m_mutex);
                    m_state = s;
                    m_rawState = raw;
                    m_nextId = std::max(m_nextId, ack);
                    m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(),
                                                   [ack](const QJsonObject &c) { return qint64(c.value(QStringLiteral("cid")).toDouble()) <= ack; }),
                                    m_pending.end());
                }
                emit stateChanged();
                if (ver != m_dataVer) {
                    const QJsonDocument dd = QJsonDocument::fromJson(readFile(m_dir + QStringLiteral("/data.json")), &err);
                    if (err.error == QJsonParseError::NoError && dd.isObject()) {
                        {
                            QMutexLocker l(&m_mutex);
                            m_data = normalizeData(dd.object());
                        }
                        m_dataVer = ver;
                        emit dataChanged();
                    }
                }
            } else {
                ++m_errors; // caught mid-write: the next poll reads the finished file
            }
        }
    }
    const bool c = connected();
    if (c != m_wasConnected) {
        m_wasConnected = c;
        emit connectionChanged(c);
    }
}

void Bridge::ping()
{
    // once a second, and at once when the game gains or loses focus (fly / modal edits must stop when it does)
    const bool f = m_gameFocused.load();
    if (m_writeFailed)
        writeCommands();
    if (f != m_sentFocused || nowMs() - m_lastPingMs >= 1000) {
        m_sentFocused = f;
        m_lastPingMs = nowMs();
        send(QJsonObject{{QStringLiteral("op"), QStringLiteral("ping")}, {QStringLiteral("focused"), f}});
    }
}

qint64 Bridge::send(const QJsonObject &cmdIn)
{
    qint64 cid;
    {
        QMutexLocker l(&m_mutex);
        m_nextId = std::max(m_nextId + 1, nowMs());
        cid = m_nextId;
        QJsonObject cmd = cmdIn;
        cmd.insert(QStringLiteral("cid"), double(cid)); // envelope id; the payload's own "id" (clip / cut ids) is never touched
        m_pending.append(cmd);
        if (m_pending.size() > 400)
            m_pending.erase(m_pending.begin(), m_pending.begin() + (m_pending.size() - 400));
    }
    writeCommands();
    return cid;
}

void Bridge::writeCommands()
{
    QMutexLocker l(&m_mutex);
    QJsonArray cmds;
    const int from = std::max<qsizetype>(0, m_pending.size() - 200);
    for (int i = from; i < m_pending.size(); ++i)
        cmds.append(m_pending.at(i));
    const QByteArray body = QJsonDocument(QJsonObject{{QStringLiteral("seq"), double(m_nextId)}, {QStringLiteral("cmds"), cmds}}).toJson(QJsonDocument::Compact);
    // write-then-rename, never in place: the runtime must never read a half-written file. When the rename fails
    // (the runtime has the file open this instant) the next ping tick writes again - pending commands stay queued.
    QSaveFile f(m_dir + QStringLiteral("/cmd.json"));
    f.setDirectWriteFallback(false);
    if (!f.open(QIODevice::WriteOnly) || f.write(body) != body.size() || !f.commit()) {
        ++m_errors;
        m_writeFailed = true;
    } else {
        m_writeFailed = false;
    }
}

QJsonObject Bridge::normalizeState(const QJsonObject &sIn)
{
    QJsonObject s = sIn;
    for (const char *k : {"actors", "cameras", "lights", "constraints", "tests"})
        s.insert(QLatin1String(k), arr(s.value(QLatin1String(k))));
    // sub-objects the panels read directly are always objects (an empty state is still a valid state)
    for (const char *k : {"game", "gizmo", "overlay", "edit", "camera_fly", "drive", "history", "stage"})
        if (!s.value(QLatin1String(k)).isObject())
            s.insert(QLatin1String(k), QJsonObject());
    QJsonArray actors;
    for (const QJsonValue &v : s.value(QStringLiteral("actors")).toArray()) {
        QJsonObject a = v.toObject();
        a.insert(QStringLiteral("layers"), arr(a.value(QStringLiteral("layers"))));
        a.insert(QStringLiteral("banks"), arr(a.value(QStringLiteral("banks"))));
        actors.append(a);
    }
    s.insert(QStringLiteral("actors"), actors);
    QJsonObject sel = obj(s.value(QStringLiteral("selection")));
    sel.insert(QStringLiteral("multi"), arr(sel.value(QStringLiteral("multi"))));
    s.insert(QStringLiteral("selection"), sel);
    QJsonObject seq = obj(s.value(QStringLiteral("sequence")));
    QJsonArray tracks;
    for (const QJsonValue &v : arr(seq.value(QStringLiteral("tracks")))) {
        QJsonObject t = v.toObject();
        QJsonArray clips;
        for (const QJsonValue &cv : arr(t.value(QStringLiteral("clips")))) {
            QJsonObject c = cv.toObject();
            c.insert(QStringLiteral("keys"), arr(c.value(QStringLiteral("keys"))));
            c.insert(QStringLiteral("kease"), arr(c.value(QStringLiteral("kease"))));
            clips.append(c);
        }
        t.insert(QStringLiteral("clips"), clips);
        t.insert(QStringLiteral("keys"), arr(t.value(QStringLiteral("keys"))));
        t.insert(QStringLiteral("cuts"), arr(t.value(QStringLiteral("cuts"))));
        tracks.append(t);
    }
    seq.insert(QStringLiteral("tracks"), tracks);
    seq.insert(QStringLiteral("shots"), arr(seq.value(QStringLiteral("shots"))));
    if (!seq.contains(QStringLiteral("fps")))
        seq.insert(QStringLiteral("fps"), 60);
    if (!seq.contains(QStringLiteral("length")))
        seq.insert(QStringLiteral("length"), 600);
    s.insert(QStringLiteral("sequence"), seq);
    return s;
}

QJsonObject Bridge::normalizeData(const QJsonObject &dIn)
{
    QJsonObject d = dIn;
    for (const char *k : {"scene", "objects", "meshes", "cast", "groups", "owners", "joints", "poses", "projects", "log", "imports", "stages"})
        d.insert(QLatin1String(k), arr(d.value(QLatin1String(k))));
    QJsonObject motions;
    const QJsonObject m = obj(d.value(QStringLiteral("motions")));
    for (auto it = m.begin(); it != m.end(); ++it)
        motions.insert(it.key(), arr(it.value()));
    d.insert(QStringLiteral("motions"), motions);
    d.insert(QStringLiteral("chars"), obj(d.value(QStringLiteral("chars"))));
    QJsonObject cat = obj(d.value(QStringLiteral("catalog")));
    cat.insert(QStringLiteral("results"), arr(cat.value(QStringLiteral("results"))));
    d.insert(QStringLiteral("catalog"), cat);
    return d;
}
