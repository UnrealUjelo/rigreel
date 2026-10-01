// Frame-accurate offline render of the Director sequence.
//
// The timeline is frame based, so instead of screen-recording in real time we step the runtime one frame at a time
// (render_clock_step), let the engine draw it, grab the game window and pipe the pixels straight into ffmpeg (or
// write numbered PNGs). Physics advances in native-timeline substeps so cloth stays stable at low output rates.
// With "game sound" on, the range is first played once in real time while the Windows output mix is recorded, and
// that recording is muxed under the picture.
#pragma once

#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <atomic>
#include <optional>
#include <thread>

class Bridge;
class GameWindow;

class RenderJob : public QObject
{
    Q_OBJECT
public:
    struct Options {
        QString out;
        int fps = 30;
        std::optional<double> start, end;
        int width = 1920, height = 1080;
        QString quality = QStringLiteral("final");  // final | draft
        bool pngSeq = false;                        // also keep the PNG frames next to an mp4
        QString format = QStringLiteral("mp4");     // mp4 | png
        bool gameAudio = false;
        bool hideHud = true;
    };

    RenderJob(Bridge *bridge, GameWindow *game, QObject *parent = nullptr);
    ~RenderJob() override;

    bool start(const Options &opts);
    void cancel() { m_cancel = true; }
    bool running() const { return m_running; }
    QJsonObject progress() const;

signals:
    void progressChanged();

private:
    void run(Options o);
    void update(const QJsonObject &patch);
    QJsonObject readState() const;
    qint64 send(const QString &op, const QJsonObject &args = {});
    void waitStep(int token, int timeoutMs = 5000);
    void waitClockStopped(int timeoutMs = 5000);
    void waitPosition(double target, int timeoutMs, bool stopped);

    Bridge *m_bridge;
    GameWindow *m_game;
    mutable QMutex m_mutex;
    QJsonObject m_progress;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_cancel{false};
    std::thread m_thread;
};
