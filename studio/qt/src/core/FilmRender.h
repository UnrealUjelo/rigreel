// Export Movie for the standalone runtime: the film is stepped frame by frame (no wall clock), drawn by a second
// View3D that imports the viewport's scene at the output resolution, grabbed and piped into ffmpeg (or saved as
// PNGs). The guide audio track is muxed under the picture. Same options and progress shape as RenderJob.
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTemporaryDir>
#include <QVariantMap>
#include <QVector>
#include <memory>

class StageRuntime;
class StageScene;
class QQuickItem;

class FilmRender : public QObject {
    Q_OBJECT
public:
    explicit FilmRender(QObject *parent = nullptr);
    ~FilmRender() override;

    void setRuntime(StageRuntime *rt) { m_rt = rt; }
    void setScene(StageScene *scene);
    bool start(const QVariantMap &opts, StageRuntime *rt);
    void cancel() { m_cancel = true; }
    bool running() const { return m_running; }
    QJsonObject progress() const { return m_progress; }

signals:
    void progressChanged();

private:
    void step();
    void onFrame(const QImage &img);
    void finish(const QString &error);
    void update(const QJsonObject &patch);
    QQuickItem *renderView() const;

    StageRuntime *m_rt = nullptr;
    QPointer<StageScene> m_scene;
    QJsonObject m_progress;
    bool m_running = false, m_cancel = false;
    QVector<double> m_frames;
    QVector<int> m_frameCams;                  // a film render: the shot camera of each frame (else empty)
    int m_index = 0, m_w = 1920, m_h = 1080, m_fps = 30, m_seqFps = 60;
    // motion blur: each output frame is the average of m_samples pictures taken while the shutter is open
    // (from the frame's time over shutter/360 of a frame: a cut on a frame boundary never mixes two shots)
    int m_samples = 1, m_sub = 0;
    double m_shutter = 180;
    QVector<quint32> m_accum;
    QString m_out, m_format, m_frameDir, m_videoOut;
    bool m_keepPng = false, m_draft = false;
    QJsonObject m_guide;
    double m_a = 0;
    std::unique_ptr<QProcess> m_enc;
    std::unique_ptr<QTemporaryDir> m_tmp;
    QByteArray m_errTail;
    double m_savedT = 0;
};
