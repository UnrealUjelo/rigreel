// External animation import: Mixamo / BVH / glTF / FBX -> Blender (headless) -> retarget.py -> runtime pose clip.
//   1. the runtime writes the selected character's rig (export_rig), cached per skeleton code
//   2. Blender converts the file into world-space joint tracks (cached by source mtime)
//   3. retarget.py maps them onto the RE4 skeleton
//   4. import_anim puts the clip on the timeline at the playhead
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTimer>

class Studio;

class AnimImporter : public QObject
{
    Q_OBJECT
public:
    explicit AnimImporter(Studio *studio, QObject *parent = nullptr);
    bool start(const QString &path, double addr, double start, int fps = 60, bool fingers = true, bool loop = false);
    QJsonObject progress() const { return m_progress; }
    bool running() const { return m_progress.value(QStringLiteral("running")).toBool(); }

signals:
    void progressChanged();

private:
    void stage(const QString &s);
    void fail(const QString &msg);
    void finish();
    void waitRig();
    void runBlender();
    void runRetarget();

    Studio *m_studio;
    QJsonObject m_progress;
    QString m_src, m_rig, m_code, m_worldJson, m_out;
    double m_addr = 0, m_start = -1;
    int m_fps = 60;
    bool m_fingers = true, m_loop = false;
    qint64 m_t0 = 0;
    QTimer m_poll;
    QProcess *m_proc = nullptr;
};
