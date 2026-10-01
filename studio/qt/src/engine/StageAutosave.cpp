// Autosave and crash recovery: the film is the Studio's own document, so the Studio keeps it safe. Unsaved
// changes are written every minute to Documents/Director Studio/autosave/; saving the film removes that copy. A
// session that did not end normally leaves a flag behind, and the next start offers the copy back
// (state.recovery; recover_autosave / discard_autosave).
#include "StageRuntime.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>

using namespace film;

QString StageRuntime::autosavePath() const
{
    const QString d = QDir(projectsDir() + QStringLiteral("/../autosave")).absolutePath();
    QDir().mkpath(d);
    return d + QStringLiteral("/autosave.film.json");
}

void StageRuntime::initAutosave()
{
    QSettings settings;
    const bool crashed = settings.value(QStringLiteral("session/running")).toBool();
    settings.setValue(QStringLiteral("session/running"), true);
    const QString file = autosavePath();
    if (crashed && QFileInfo::exists(file)) {
        QFile f(file);
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
            const QJsonObject meta = o.value(QStringLiteral("autosave")).toObject();
            const Document d = fromJson(o);
            if (!d.actors.isEmpty() || !d.seq.tracks.isEmpty() || !d.cameras.isEmpty() || !d.lights.isEmpty() || !d.stageId.isEmpty())
                m_recovery = {{QStringLiteral("project"), meta.value(QStringLiteral("project")).toString(QStringLiteral("untitled"))},
                              {QStringLiteral("time"), QFileInfo(file).lastModified().toString(QStringLiteral("d MMM HH:mm"))},
                              {QStringLiteral("actors"), int(d.actors.size())}, {QStringLiteral("tracks"), int(d.seq.tracks.size())}};
        }
    }
    connect(&m_autosaveTimer, &QTimer::timeout, this, &StageRuntime::autosave);
    m_autosaveTimer.start(60 * 1000);
}

void StageRuntime::autosave()
{
    if (m_editSerial == m_autosavedSerial || m_rendering || !m_recovery.isEmpty()) return;   // nothing new / a recovery is pending
    QJsonObject o = toJson(m_doc);
    o.insert(QStringLiteral("autosave"), QJsonObject{{QStringLiteral("project"), m_project}, {QStringLiteral("time"), QDateTime::currentDateTime().toString(Qt::ISODate)}});
    QSaveFile f(autosavePath());
    if (f.open(QIODevice::WriteOnly) && f.write(QJsonDocument(o).toJson(QJsonDocument::Compact)) > 0 && f.commit()) m_autosavedSerial = m_editSerial;
}

// the film was saved (or deliberately thrown away): the copy is no longer needed
void StageRuntime::dropAutosave()
{
    QFile::remove(autosavePath());
    m_autosavedSerial = m_editSerial;
}

// a normal exit: the next start does not offer a recovery
void StageRuntime::endSession()
{
    autosave();
    QSettings().setValue(QStringLiteral("session/running"), false);
}

void StageRuntime::registerAutosaveOps()
{
    auto &o = m_ops;
    o[QStringLiteral("recover_autosave")] = [this](const QJsonObject &) {
        QFile f(autosavePath());
        if (!f.open(QIODevice::ReadOnly)) { m_recovery = {}; return; }
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        restore(o);
        m_project = o.value(QStringLiteral("autosave")).toObject().value(QStringLiteral("project")).toString(QStringLiteral("untitled"));
        m_recovery = {};
        m_t = 0;
        m_playing = false;
        setActiveCamera(0);
        ++m_editSerial;                                    // unsaved until the person saves it
        log(QStringLiteral("info"), QStringLiteral("recovered %1 from the autosave: save it to keep it").arg(m_project));
    };
    o[QStringLiteral("discard_autosave")] = [this](const QJsonObject &) {
        m_recovery = {};
        dropAutosave();
    };
    m_readonly << QStringLiteral("discard_autosave");
}
