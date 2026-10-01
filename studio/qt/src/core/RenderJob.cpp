#include "RenderJob.h"

#include "Bridge.h"
#include "Capture.h"
#include "GameWindow.h"
#include "LoopbackRecorder.h"
#include "Paths.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>

#include <cmath>
#include <functional>
#include <stdexcept>

namespace {

struct RenderError : std::runtime_error {
    explicit RenderError(const QString &m) : std::runtime_error(m.toStdString()) {}
};

QString fmt6(double v) { return QString::number(v, 'f', 6); }

} // namespace

RenderJob::RenderJob(Bridge *bridge, GameWindow *game, QObject *parent)
    : QObject(parent), m_bridge(bridge), m_game(game)
{
    m_progress = QJsonObject{{QStringLiteral("running"), false}, {QStringLiteral("frame"), 0}, {QStringLiteral("total"), 0},
                             {QStringLiteral("out"), QJsonValue::Null}, {QStringLiteral("error"), QJsonValue::Null}, {QStringLiteral("stage"), QString()}};
}

RenderJob::~RenderJob()
{
    m_cancel = true;
    if (m_thread.joinable())
        m_thread.join();
}

QJsonObject RenderJob::progress() const { QMutexLocker l(&m_mutex); return m_progress; }

void RenderJob::update(const QJsonObject &patch)
{
    {
        QMutexLocker l(&m_mutex);
        for (auto it = patch.begin(); it != patch.end(); ++it)
            m_progress.insert(it.key(), it.value());
    }
    QMetaObject::invokeMethod(this, &RenderJob::progressChanged, Qt::QueuedConnection);
}

bool RenderJob::start(const Options &opts)
{
    if (m_running)
        return false;
    if (m_thread.joinable())
        m_thread.join();
    m_running = true;
    m_cancel = false;
    m_thread = std::thread([this, opts] { run(opts); });
    return true;
}

QJsonObject RenderJob::readState() const
{
    QFile f(m_bridge->dir() + QStringLiteral("/state.json"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

qint64 RenderJob::send(const QString &op, const QJsonObject &args)
{
    QJsonObject c = args;
    c.insert(QStringLiteral("op"), op);
    return m_bridge->send(c);
}

void RenderJob::waitStep(int token, int timeoutMs)
{
    // the runtime draws the requested fixed-time simulation step, then holds time again
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        const QJsonObject clock = readState().value(QStringLiteral("render_clock")).toObject();
        if (!clock.value(QStringLiteral("error")).isNull() && clock.contains(QStringLiteral("error")))
            throw RenderError(QStringLiteral("Render clock: ") + clock.value(QStringLiteral("error")).toVariant().toString());
        if (clock.value(QStringLiteral("active")).toBool() && clock.value(QStringLiteral("phase")).toString() == QLatin1String("held")
            && clock.value(QStringLiteral("ready")).toDouble() >= token)
            return;
        QThread::msleep(5);
    }
    throw RenderError(QStringLiteral("The game did not finish the requested render frame"));
}

void RenderJob::waitClockStopped(int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        const QJsonObject clock = readState().value(QStringLiteral("render_clock")).toObject();
        if (!clock.value(QStringLiteral("active")).toBool())
            return;
        QThread::msleep(10);
    }
    throw RenderError(QStringLiteral("The game did not restore normal timing after rendering"));
}

void RenderJob::waitPosition(double target, int timeoutMs, bool stopped)
{
    QElapsedTimer t;
    t.start();
    QJsonObject latest;
    while (t.elapsed() < timeoutMs) {
        latest = readState().value(QStringLiteral("sequence")).toObject();
        const double at = latest.value(QStringLiteral("t")).toDouble();
        if (std::abs(at - target) <= 0.35 && (!stopped || !latest.value(QStringLiteral("playing")).toBool()))
            return;
        QThread::msleep(15);
    }
    throw RenderError(QStringLiteral("The timeline did not reach frame %1 during audio capture").arg(target));
}

void RenderJob::run(Options o)
{
    update({{QStringLiteral("running"), true}, {QStringLiteral("frame"), 0}, {QStringLiteral("total"), 0}, {QStringLiteral("out"), o.out},
            {QStringLiteral("error"), QJsonValue::Null}, {QStringLiteral("stage"), QStringLiteral("preparing")}, {QStringLiteral("png_dir"), QJsonValue::Null},
            {QStringLiteral("output_format"), o.format}, {QStringLiteral("audio_device"), QJsonValue::Null}});
    QTemporaryDir tmp(QDir::tempPath() + QStringLiteral("/director_render_XXXXXX"));
    bool restoreGame = false, clockActive = false;
    QString error;
    try {
        if (o.format != QLatin1String("mp4") && o.format != QLatin1String("png"))
            throw RenderError(QStringLiteral("Output format must be mp4 or png"));
        if (o.out.isEmpty() || o.fps <= 0)
            throw RenderError(QStringLiteral("An output name and a positive frame rate are required"));
        const QString ffmpeg = Paths::ffmpeg();
        if (o.format == QLatin1String("mp4") && ffmpeg.isEmpty())
            throw RenderError(QStringLiteral("ffmpeg not found on PATH"));
        if (!m_game->find())
            throw RenderError(QStringLiteral("The game window was not found"));

        const QJsonObject st = readState();
        const QJsonObject seq = st.value(QStringLiteral("sequence")).toObject();
        const int seqFps = std::max(1, seq.value(QStringLiteral("fps")).toInt(60));
        const double length = seq.value(QStringLiteral("length")).toDouble(600);
        const QJsonArray rng = seq.value(QStringLiteral("range")).toArray();
        const double a = o.start ? std::max(0.0, *o.start) : (rng.size() == 2 ? rng[0].toDouble() : 0.0);
        const double b = o.end ? std::min(length, *o.end) : (rng.size() == 2 ? std::min(length, rng[1].toDouble()) : length);
        const QJsonObject guide = seq.value(QStringLiteral("audio")).toObject();
        // fractional timeline positions keep the requested output rate (60 Hz timeline -> 2.5 frames per 24 fps frame)
        const int count = int((b - a) * o.fps / seqFps) + 1;
        QVector<double> frames;
        for (int i = 0; i < count; ++i)
            frames << a + i * double(seqFps) / o.fps;
        if (frames.isEmpty() || b <= a)
            throw RenderError(QStringLiteral("The render range is empty"));
        update({{QStringLiteral("total"), int(frames.size())}});

        QString frameDir;
        const bool keepPng = o.format == QLatin1String("png") || o.pngSeq;
        if (keepPng) {
            // a fresh permanent folder; completed frames survive a failure
            QString base = o.out;
            base.remove(QRegularExpression(QStringLiteral("\\.[A-Za-z0-9]+$")));
            base += QStringLiteral("_frames");
            frameDir = base;
            for (int n = 2; QFileInfo::exists(frameDir); ++n)
                frameDir = base + QLatin1Char('_') + QString::number(n);
            QDir().mkpath(frameDir);
            update({{QStringLiteral("png_dir"), frameDir}});
            if (o.format == QLatin1String("png"))
                update({{QStringLiteral("out"), frameDir}});
        }

        // game sound: play the range once in real time and record what Windows plays
        QString gameWav;
        double gameOffset = 0;
        if (o.format == QLatin1String("mp4") && o.gameAudio) {
            update({{QStringLiteral("stage"), QStringLiteral("recording audio")}});
            gameWav = tmp.path() + QStringLiteral("/game_mix.wav");
            const QJsonValue oldRange = seq.value(QStringLiteral("range"));
            const bool oldLoop = seq.value(QStringLiteral("loop")).toBool();
            const double oldSpeed = seq.value(QStringLiteral("speed")).toDouble(1);
            const bool frozen = st.value(QStringLiteral("game")).toObject().value(QStringLiteral("frozen")).toBool();
            LoopbackRecorder rec(gameWav);
            QString recErr;
            auto restorePlayback = [&] {
                send(QStringLiteral("seq_pause"));
                send(QStringLiteral("seq_speed"), {{QStringLiteral("value"), oldSpeed}});
                send(QStringLiteral("seq_set"), {{QStringLiteral("loop"), oldLoop}});
                if (oldRange.isArray() && oldRange.toArray().size() == 2)
                    send(QStringLiteral("seq_range"), {{QStringLiteral("a"), oldRange.toArray()[0]}, {QStringLiteral("b"), oldRange.toArray()[1]}});
                else
                    send(QStringLiteral("seq_range"), {{QStringLiteral("clear"), true}});
                if (frozen)
                    send(QStringLiteral("game_freeze"), {{QStringLiteral("value"), true}});
            };
            try {
                send(QStringLiteral("seq_pause"));
                send(QStringLiteral("seq_speed"), {{QStringLiteral("value"), 1}});
                send(QStringLiteral("seq_set"), {{QStringLiteral("loop"), false}});
                send(QStringLiteral("seq_range"), {{QStringLiteral("a"), a}, {QStringLiteral("b"), b}});
                if (frozen)
                    send(QStringLiteral("game_freeze"), {{QStringLiteral("value"), false}});
                send(QStringLiteral("seq_seek"), {{QStringLiteral("t"), a}});
                waitPosition(a, 5000, true);
                if (!rec.start(&recErr))
                    throw RenderError(QStringLiteral("Could not capture game audio: ") + recErr);
                gameOffset = rec.mark();
                send(QStringLiteral("seq_play"));
                const double dur = std::max(0.1, (b - a) / seqFps);
                waitPosition(b, int(std::max(10.0, dur * 2.5 + 5) * 1000), true);
            } catch (...) {
                rec.stop(nullptr);
                restorePlayback();
                throw;
            }
            if (!rec.stop(&recErr))
                throw RenderError(QStringLiteral("Game-audio capture failed: ") + recErr);
            restorePlayback();
            update({{QStringLiteral("audio_device"), rec.device()}});
        }

        restoreGame = true;
        const QRect rect = m_game->renderMode(true, o.width, o.height);
        // quiet the editor: no gizmos or camera markers, HUD off, sequence held at the first frame
        send(QStringLiteral("gizmo"), {{QStringLiteral("enabled"), false}});
        send(QStringLiteral("show_cameras"), {{QStringLiteral("value"), false}});
        if (o.hideHud)
            send(QStringLiteral("hud"), {{QStringLiteral("value"), false}});
        send(QStringLiteral("seq_pause"));
        send(QStringLiteral("render_clock_begin"), {{QStringLiteral("fps"), o.fps}});
        clockActive = true;
        send(QStringLiteral("render_clock_step"), {{QStringLiteral("token"), 1}, {QStringLiteral("t"), frames[0]}, {QStringLiteral("seconds"), 0}});
        waitStep(1);
        const int W = rect.width() > 0 ? rect.width() : o.width, H = rect.height() > 0 ? rect.height() : o.height;

        const bool hasGuide = !guide.isEmpty() && QFileInfo::exists(guide.value(QStringLiteral("path")).toString());
        const bool hasAudio = (!gameWav.isEmpty() && QFileInfo::exists(gameWav)) || hasGuide;
        const QString videoOut = hasAudio ? tmp.path() + QStringLiteral("/video.mp4") : o.out;
        QProcess enc;
        if (o.format == QLatin1String("mp4")) {
            QDir().mkpath(QFileInfo(o.out).absolutePath());
            const bool draft = o.quality == QLatin1String("draft");
            enc.setProgram(ffmpeg);
            enc.setArguments({QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-pix_fmt"), QStringLiteral("bgra"),
                              QStringLiteral("-s"), QStringLiteral("%1x%2").arg(W).arg(H), QStringLiteral("-r"), QString::number(o.fps), QStringLiteral("-i"), QStringLiteral("-"),
                              QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"), draft ? QStringLiteral("veryfast") : QStringLiteral("slow"),
                              QStringLiteral("-crf"), draft ? QStringLiteral("26") : QStringLiteral("16"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                              QStringLiteral("-movflags"), QStringLiteral("+faststart"), videoOut});
            enc.setProcessChannelMode(QProcess::SeparateChannels);
            enc.start();
            if (!enc.waitForStarted(10000))
                throw RenderError(QStringLiteral("ffmpeg did not start"));
        }

        update({{QStringLiteral("stage"), QStringLiteral("capturing")}});
        double previous = frames[0];
        int token = 1;
        QByteArray errTail;
        for (int i = 0; i < frames.size(); ++i) {
            if (m_cancel)
                throw RenderError(QStringLiteral("Render cancelled"));
            const double f = frames[i];
            // keep physics steps at or below the sequence's native frame: GPU cloth is a fixed-step solver and gets
            // unstable when a 30 fps capture jumps two 60 Hz frames at once. Only the output frame is captured.
            while (i && f - previous > 1e-7) {
                const double step = std::min(1.0, f - previous);
                previous = std::min(f, previous + step);
                ++token;
                send(QStringLiteral("render_clock_step"), {{QStringLiteral("token"), token}, {QStringLiteral("t"), previous}, {QStringLiteral("seconds"), step / seqFps}});
                waitStep(token);
            }
            QImage img = Capture::window(m_game->hwnd());
            if (img.isNull())
                throw RenderError(QStringLiteral("Could not capture the game window"));
            if (img.width() != W || img.height() != H)
                img = img.scaled(W, H, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            img = img.convertToFormat(QImage::Format_RGB32);
            if (keepPng)
                img.save(frameDir + QStringLiteral("/f%1.png").arg(i, 5, 10, QLatin1Char('0')), "png", 90);
            if (o.format == QLatin1String("mp4")) {
                for (int y = 0; y < H; ++y) {
                    enc.write(reinterpret_cast<const char *>(img.constScanLine(y)), qint64(W) * 4);
                }
                while (enc.bytesToWrite() > 64LL * 1024 * 1024)
                    enc.waitForBytesWritten(1000);
                errTail += enc.readAllStandardError();
                if (errTail.size() > 8000)
                    errTail = errTail.right(4000);
                if (enc.state() != QProcess::Running)
                    throw RenderError(QStringLiteral("ffmpeg stopped: ") + QString::fromUtf8(errTail.right(600)));
            }
            update({{QStringLiteral("frame"), i + 1}});
        }
        if (o.format == QLatin1String("mp4")) {
            update({{QStringLiteral("stage"), QStringLiteral("encoding")}});
            while (enc.bytesToWrite() > 0 && enc.waitForBytesWritten(5000)) {}
            enc.closeWriteChannel();
            enc.waitForFinished(-1);
            errTail += enc.readAllStandardError();
            if (enc.exitStatus() != QProcess::NormalExit || enc.exitCode() != 0)
                throw RenderError(QStringLiteral("ffmpeg: ") + QString::fromUtf8(errTail.right(800)));
            if (hasAudio) {
                update({{QStringLiteral("stage"), QStringLiteral("audio")}});
                const double duration = frames.size() / double(o.fps);
                QStringList args{QStringLiteral("-y"), QStringLiteral("-i"), videoOut};
                QString filter, label;
                if (!gameWav.isEmpty() && QFileInfo::exists(gameWav)) {
                    args << QStringLiteral("-ss") << fmt6(gameOffset) << QStringLiteral("-i") << gameWav;
                    filter = QStringLiteral("[1:a]aresample=48000,apad,atrim=duration=%1,asetpts=PTS-STARTPTS[game]").arg(fmt6(duration));
                    label = QStringLiteral("[game]");
                } else {
                    const double off = guide.value(QStringLiteral("offset")).toDouble();
                    const double skip = std::max(0.0, (a - off) / seqFps);
                    const int delayMs = int(std::lround(std::max(0.0, (off - a) / seqFps) * 1000));
                    const double vol = guide.contains(QStringLiteral("volume")) && !guide.value(QStringLiteral("volume")).isNull() ? guide.value(QStringLiteral("volume")).toDouble() : 1.0;
                    args << QStringLiteral("-ss") << fmt6(skip) << QStringLiteral("-i") << guide.value(QStringLiteral("path")).toString();
                    filter = QStringLiteral("[1:a]volume=%1,adelay=%2:all=1,apad,atrim=duration=%3,asetpts=PTS-STARTPTS[guide]").arg(QString::number(vol, 'f', 4)).arg(delayMs).arg(fmt6(duration));
                    label = QStringLiteral("[guide]");
                }
                args << QStringLiteral("-filter_complex") << filter << QStringLiteral("-map") << QStringLiteral("0:v:0") << QStringLiteral("-map") << label
                     << QStringLiteral("-c:v") << QStringLiteral("copy") << QStringLiteral("-c:a") << QStringLiteral("aac") << QStringLiteral("-b:a") << QStringLiteral("192k")
                     << QStringLiteral("-t") << fmt6(duration) << QStringLiteral("-movflags") << QStringLiteral("+faststart") << o.out;
                QProcess mux;
                mux.start(ffmpeg, args);
                mux.waitForFinished(-1);
                if (mux.exitCode() != 0)
                    throw RenderError(QStringLiteral("audio mux: ") + QString::fromUtf8(mux.readAllStandardError().right(600)));
            }
        }
    } catch (const std::exception &e) {
        error = QString::fromStdString(e.what());
    }
    if (restoreGame) {
        // one failed restore must not prevent the others or leave the progress stuck
        const QList<std::function<void()>> restores{
            [&] { if (clockActive) { send(QStringLiteral("render_clock_end")); waitClockStopped(); } },
            [&] { send(QStringLiteral("gizmo"), {{QStringLiteral("enabled"), true}}); },
            [&] { send(QStringLiteral("show_cameras"), {{QStringLiteral("value"), true}}); },
            [&] { if (o.hideHud) send(QStringLiteral("hud"), {{QStringLiteral("value"), true}}); },
            [&] { m_game->renderMode(false); },
        };
        for (const auto &r : restores) {
            try { r(); } catch (const std::exception &e) {
                if (error.isEmpty())
                    error = QStringLiteral("Could not restore the editor: ") + QString::fromStdString(e.what());
            }
        }
    }
    update({{QStringLiteral("error"), error.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(error)},
            {QStringLiteral("stage"), error.isEmpty() ? QStringLiteral("done") : QStringLiteral("error")}, {QStringLiteral("running"), false}});
    m_running = false;
}
