#include "AnimImporter.h"

#include "Paths.h"
#include "Studio.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

AnimImporter::AnimImporter(Studio *studio, QObject *parent) : QObject(parent), m_studio(studio)
{
    m_progress = QJsonObject{{QStringLiteral("running"), false}, {QStringLiteral("stage"), QString()}, {QStringLiteral("error"), QJsonValue::Null},
                             {QStringLiteral("result"), QJsonValue::Null}, {QStringLiteral("file"), QJsonValue::Null}};
    connect(&m_poll, &QTimer::timeout, this, &AnimImporter::waitRig);
}

void AnimImporter::stage(const QString &s)
{
    m_progress.insert(QStringLiteral("stage"), s);
    emit progressChanged();
}

void AnimImporter::fail(const QString &msg)
{
    m_poll.stop();
    m_progress.insert(QStringLiteral("error"), msg);
    finish();
}

void AnimImporter::finish()
{
    m_progress.insert(QStringLiteral("running"), false);
    m_progress.insert(QStringLiteral("stage"), QStringLiteral("done"));
    emit progressChanged();
}

bool AnimImporter::start(const QString &path, double addr, double start, int fps, bool fingers, bool loop)
{
    if (running())
        return false;
    m_src = path;
    m_fps = fps;
    m_fingers = fingers;
    m_loop = loop;
    m_start = start;
    m_progress = QJsonObject{{QStringLiteral("running"), true}, {QStringLiteral("stage"), QStringLiteral("starting")}, {QStringLiteral("error"), QJsonValue::Null},
                             {QStringLiteral("result"), QJsonValue::Null}, {QStringLiteral("file"), path}};
    emit progressChanged();
    if (!QFileInfo::exists(path)) {
        fail(QStringLiteral("File not found: ") + path);
        return true;
    }
    // the character: the given one, else the selected one
    const QJsonObject st = m_studio->stateObj();
    const double sel = st.value(QStringLiteral("selection")).toObject().value(QStringLiteral("actor")).toDouble();
    m_addr = 0;
    for (const QJsonValue &v : st.value(QStringLiteral("actors")).toArray()) {
        const double id = v.toObject().value(QStringLiteral("id")).toDouble();
        if ((addr > 0 && id == addr) || (addr <= 0 && id == sel))
            m_addr = id;
    }
    if (m_addr <= 0) {
        fail(QStringLiteral("Select a character first"));
        return true;
    }
    stage(QStringLiteral("capturing rig"));
    m_t0 = QDateTime::currentMSecsSinceEpoch();
    m_studio->sendJson(QJsonObject{{QStringLiteral("op"), QStringLiteral("export_rig")}, {QStringLiteral("addr"), m_addr}});
    m_poll.start(200);
    return true;
}

void AnimImporter::waitRig()
{
    const QString last = m_studio->dataObj().value(QStringLiteral("last_rig")).toString();
    if (!last.isEmpty()) {
        const QString rig = m_studio->importRoot() + QLatin1Char('/') + QString(last).remove(QStringLiteral("director/"));
        QFileInfo fi(rig);
        if (fi.exists() && fi.lastModified().toMSecsSinceEpoch() > m_t0 - 1000) {
            m_poll.stop();
            m_rig = rig;
            QFile f(rig);
            f.open(QIODevice::ReadOnly);
            m_code = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("code")).toString(QStringLiteral("rig"));
            runBlender();
            return;
        }
    }
    if (QDateTime::currentMSecsSinceEpoch() - m_t0 > 6000)
        fail(m_studio->standalone() ? QStringLiteral("The character's rig could not be written (still loading?)")
                                    : QStringLiteral("The rig capture did not arrive (is the game running?)"));
}

void AnimImporter::runBlender()
{
    const QFileInfo src(m_src);
    const QString cache = m_studio->importRoot() + QStringLiteral("/import/_src");
    QDir().mkpath(cache);
    m_worldJson = cache + QLatin1Char('/') + src.completeBaseName() + QLatin1Char('_') + QString::number(src.lastModified().toSecsSinceEpoch()) + QStringLiteral(".json");
    if (QFileInfo::exists(m_worldJson)) { // cached by source mtime
        runRetarget();
        return;
    }
    stage(QStringLiteral("converting with Blender"));
    const QString blender = Paths::findBlender();
    if (blender.isEmpty()) {
        fail(QStringLiteral("Blender not found (looked in Program Files\\Blender Foundation)"));
        return;
    }
    const QString script = Paths::workspace() + QStringLiteral("/tools/anim_import/blender_export_anim.py");
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        const QString tail = QString::fromUtf8(m_proc->readAll().right(1500));
        m_proc->deleteLater();
        m_proc = nullptr;
        if (code != 0 || !QFileInfo::exists(m_worldJson)) {
            fail(QStringLiteral("Blender export failed: ") + tail);
            return;
        }
        runRetarget();
    });
    m_proc->start(blender, {QStringLiteral("-b"), QStringLiteral("--python"), script, QStringLiteral("--"), m_src, m_worldJson, QStringLiteral("--fps"), QStringLiteral("30")});
}

void AnimImporter::runRetarget()
{
    stage(QStringLiteral("retargeting"));
    const QString stem = QFileInfo(m_src).completeBaseName();
    m_out = m_studio->importRoot() + QStringLiteral("/import/") + m_code + QLatin1Char('/') + stem + QStringLiteral(".json");
    QStringList args{Paths::workspace() + QStringLiteral("/studio/retarget.py"), m_worldJson, m_rig, m_out, QStringLiteral("--fps"), QString::number(m_fps), QStringLiteral("--json")};
    if (!m_fingers)
        args << QStringLiteral("--no-fingers");
    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::finished, this, [this, stem](int code, QProcess::ExitStatus) {
        const QByteArray out = m_proc->readAllStandardOutput();
        const QByteArray err = m_proc->readAllStandardError();
        m_proc->deleteLater();
        m_proc = nullptr;
        if (code != 0) {
            fail(QStringLiteral("Retarget failed: ") + QString::fromUtf8(err.right(1200)));
            return;
        }
        QJsonObject info;
        for (const QByteArray &line : out.split('\n'))
            if (line.trimmed().startsWith('{'))
                info = QJsonDocument::fromJson(line.trimmed()).object();
        stage(QStringLiteral("adding to the timeline"));
        QJsonObject c{{QStringLiteral("op"), QStringLiteral("import_anim")}, {QStringLiteral("addr"), m_addr},
                      {QStringLiteral("file"), QStringLiteral("director/import/%1/%2.json").arg(m_code, stem)}, {QStringLiteral("loop"), m_loop}};
        if (m_start >= 0)
            c.insert(QStringLiteral("start"), m_start);
        m_studio->sendJson(c);
        info.insert(QStringLiteral("code"), m_code);
        info.insert(QStringLiteral("clip"), stem);
        m_progress.insert(QStringLiteral("result"), info);
        finish();
    });
    m_proc->start(Paths::python(), args);
}
