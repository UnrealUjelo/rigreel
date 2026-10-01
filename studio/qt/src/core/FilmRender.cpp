#include "FilmRender.h"
#include "Paths.h"
#include "StageRuntime.h"
#include "StageScene.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QRegularExpression>
#include <QTimer>
#include <cmath>

namespace {
QString fmt6(double v) { return QString::number(v, 'f', 6); }
}

FilmRender::FilmRender(QObject *parent) : QObject(parent)
{
    m_progress = {{QStringLiteral("running"), false}, {QStringLiteral("frame"), 0}, {QStringLiteral("total"), 0},
                  {QStringLiteral("out"), QJsonValue::Null}, {QStringLiteral("error"), QJsonValue::Null}, {QStringLiteral("stage"), QString()}};
}

void FilmRender::setScene(StageScene *scene) { m_scene = scene; }

FilmRender::~FilmRender()
{
    if (m_enc) m_enc->kill();
}

void FilmRender::update(const QJsonObject &patch)
{
    for (auto it = patch.begin(); it != patch.end(); ++it) m_progress.insert(it.key(), it.value());
    emit progressChanged();
}

QQuickItem *FilmRender::renderView() const
{
    if (!m_scene || !m_scene->view()) return nullptr;
    // the render view lives next to the viewport's View3D in the same QML scene
    QObject *root = m_scene->view()->parent();
    return root ? root->findChild<QQuickItem *>(QStringLiteral("renderView")) : nullptr;
}

bool FilmRender::start(const QVariantMap &o, StageRuntime *rt)
{
    if (m_running) return false;
    m_rt = rt;
    QQuickItem *view = renderView();
    if (!m_rt || !view) { update({{QStringLiteral("error"), QStringLiteral("The 3D viewport is not ready")}, {QStringLiteral("stage"), QStringLiteral("error")}}); return false; }
    m_out = o.value(QStringLiteral("out")).toString();
    if (m_out.isEmpty()) {
        QString name = o.value(QStringLiteral("name"), QStringLiteral("director_shot")).toString().trimmed();
        name.remove(QRegularExpression(QStringLiteral("\\.mp4$"), QRegularExpression::CaseInsensitiveOption));
        name.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*]")), QStringLiteral("_"));
        if (name.isEmpty()) name = QStringLiteral("director_shot");
        m_out = Paths::downloads() + QLatin1Char('/') + name + QStringLiteral(".mp4");
    }
    m_fps = std::max(1, o.value(QStringLiteral("fps"), 30).toInt());
    m_w = std::max(64, o.value(QStringLiteral("width"), 1920).toInt()) & ~1;
    m_h = std::max(64, o.value(QStringLiteral("height"), 1080).toInt()) & ~1;
    m_format = o.value(QStringLiteral("output_format"), QStringLiteral("mp4")).toString();
    m_draft = o.value(QStringLiteral("quality")).toString() == QLatin1String("draft");
    m_keepPng = m_format == QLatin1String("png") || o.value(QStringLiteral("png_seq")).toBool();
    m_samples = std::clamp(o.value(QStringLiteral("motion_blur"), 1).toInt(), 1, 64);
    m_shutter = std::clamp(o.value(QStringLiteral("shutter"), 180.0).toDouble(), 1.0, 360.0);
    m_sub = 0;
    m_accum.clear();
    if (m_format == QLatin1String("prores")) m_out.replace(QRegularExpression(QStringLiteral("\\.mp4$"), QRegularExpression::CaseInsensitiveOption), QStringLiteral(".mov"));

    const QJsonObject seq = m_rt->state().value(QStringLiteral("sequence")).toObject();
    m_seqFps = std::max(1, seq.value(QStringLiteral("fps")).toInt(60));
    const double length = seq.value(QStringLiteral("length")).toDouble(600);
    const QJsonArray rng = seq.value(QStringLiteral("range")).toArray();
    const bool hasFrom = o.contains(QStringLiteral("from")) && !o.value(QStringLiteral("from")).isNull();
    const bool hasTo = o.contains(QStringLiteral("to")) && !o.value(QStringLiteral("to")).isNull();
    m_a = hasFrom ? std::max(0.0, o.value(QStringLiteral("from")).toDouble()) : (rng.size() == 2 ? rng[0].toDouble() : 0.0);
    const double b = hasTo ? std::min(length, o.value(QStringLiteral("to")).toDouble()) : (rng.size() == 2 ? std::min(length, rng[1].toDouble()) : length);
    m_guide = seq.value(QStringLiteral("audio")).toObject();
    m_frames.clear();
    m_frameCams.clear();
    if (o.value(QStringLiteral("film")).toBool()) {
        // the film: every shot in its order, its own stretch of the scene through its own camera
        for (const QJsonValue &v : seq.value(QStringLiteral("shots")).toArray()) {
            const QJsonObject sh = v.toObject();
            const double sa = sh.value(QStringLiteral("a")).toDouble(), sb = std::max(sa + 1, sh.value(QStringLiteral("b")).toDouble());
            const int cam = sh.value(QStringLiteral("cam")).toInt();
            for (double t = sa; t < sb - 1e-6; t += double(m_seqFps) / m_fps) { m_frames << t; m_frameCams << cam; }
        }
        m_guide = {};                                                    // guide audio follows scene time, not the edit
        m_a = m_frames.isEmpty() ? 0 : m_frames.first();
    } else {
        const int count = int((b - m_a) * m_fps / m_seqFps) + 1;
        for (int i = 0; i < count; ++i) m_frames << m_a + i * double(m_seqFps) / m_fps;
    }
    if (m_frames.isEmpty() || (m_frameCams.isEmpty() && b <= m_a)) { update({{QStringLiteral("error"), QStringLiteral("The render range is empty")}, {QStringLiteral("stage"), QStringLiteral("error")}}); return false; }

    m_running = true;
    m_cancel = false;
    m_index = 0;
    m_errTail.clear();
    m_savedT = m_rt->time();
    m_tmp = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/director_film_XXXXXX"));
    update({{QStringLiteral("running"), true}, {QStringLiteral("frame"), 0}, {QStringLiteral("total"), int(m_frames.size())}, {QStringLiteral("out"), m_out},
            {QStringLiteral("error"), QJsonValue::Null}, {QStringLiteral("stage"), QStringLiteral("preparing")}, {QStringLiteral("png_dir"), QJsonValue::Null},
            {QStringLiteral("output_format"), m_format}});
    if (m_keepPng) {
        QString base = m_out;
        base.remove(QRegularExpression(QStringLiteral("\\.[A-Za-z0-9]+$")));
        base += QStringLiteral("_frames");
        m_frameDir = base;
        for (int n = 2; QFileInfo::exists(m_frameDir); ++n) m_frameDir = base + QLatin1Char('_') + QString::number(n);
        QDir().mkpath(m_frameDir);
        update({{QStringLiteral("png_dir"), m_frameDir}});
        if (m_format == QLatin1String("png")) update({{QStringLiteral("out"), m_frameDir}});
    }
    if (m_format == QLatin1String("mp4") || m_format == QLatin1String("prores")) {
        const QString ffmpeg = Paths::ffmpeg();
        if (ffmpeg.isEmpty()) { finish(QStringLiteral("ffmpeg not found on PATH")); return false; }
        const bool prores = m_format == QLatin1String("prores");
        const bool audio = !m_guide.isEmpty() && QFileInfo::exists(m_guide.value(QStringLiteral("path")).toString());
        m_videoOut = audio ? m_tmp->path() + (prores ? QStringLiteral("/video.mov") : QStringLiteral("/video.mp4")) : m_out;
        m_enc = std::make_unique<QProcess>();
        m_enc->setProgram(ffmpeg);
        QStringList args{QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-pix_fmt"), QStringLiteral("bgra"),
                         QStringLiteral("-s"), QStringLiteral("%1x%2").arg(m_w).arg(m_h), QStringLiteral("-r"), QString::number(m_fps),
                         QStringLiteral("-i"), QStringLiteral("-")};
        if (prores)   // for editing: ProRes 422 HQ, 10-bit 4:2:2
            args << QStringLiteral("-c:v") << QStringLiteral("prores_ks") << QStringLiteral("-profile:v") << QStringLiteral("3")
                 << QStringLiteral("-pix_fmt") << QStringLiteral("yuv422p10le") << QStringLiteral("-vendor") << QStringLiteral("apl0") << m_videoOut;
        else
            args << QStringLiteral("-c:v") << QStringLiteral("libx264") << QStringLiteral("-preset") << (m_draft ? QStringLiteral("veryfast") : QStringLiteral("slow"))
                 << QStringLiteral("-crf") << (m_draft ? QStringLiteral("26") : QStringLiteral("16")) << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
                 << QStringLiteral("-movflags") << QStringLiteral("+faststart") << m_videoOut;
        m_enc->setArguments(args);
        m_enc->start();
        if (!m_enc->waitForStarted(10000)) { finish(QStringLiteral("ffmpeg did not start")); return false; }
    }
    // the render view: output size, film quality, visible while rendering (it sits under the viewport)
    view->setProperty("draft", m_draft);
    view->setSize(QSizeF(m_w, m_h));
    view->setVisible(true);
    m_rt->renderBegin();
    update({{QStringLiteral("stage"), QStringLiteral("capturing")}});
    QTimer::singleShot(50, this, &FilmRender::step);
    return true;
}

void FilmRender::step()
{
    if (!m_running) return;
    if (m_cancel) { finish(QStringLiteral("Render cancelled")); return; }
    if (m_index >= m_frames.size()) { finish({}); return; }
    QQuickItem *view = renderView();
    if (!view) { finish(QStringLiteral("The 3D viewport closed")); return; }
    // with motion blur, sub-frame m_sub of the open shutter (film frames are 1 / seqFps apart)
    double t = m_frames[m_index];
    if (m_samples > 1) {
        const double frameSpan = double(m_seqFps) / m_fps, open = frameSpan * m_shutter / 360.0;
        t += (m_sub + 0.5) / m_samples * open;
    }
    m_rt->renderStep(t, m_frameCams.isEmpty() ? -1 : m_frameCams[m_index]);
    const QSharedPointer<QQuickItemGrabResult> grab = view->grabToImage(QSize(m_w, m_h));
    if (!grab) { finish(QStringLiteral("The picture could not be grabbed")); return; }
    connect(grab.data(), &QQuickItemGrabResult::ready, this, [this, grab]() { onFrame(grab->image()); });
}

void FilmRender::onFrame(const QImage &frame)
{
    if (!m_running) return;
    QImage img = frame.convertToFormat(QImage::Format_RGB32);
    if (img.width() != m_w || img.height() != m_h) img = img.scaled(m_w, m_h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (m_samples > 1) {
        // add this sub-frame; the frame is written once the shutter has closed
        if (m_accum.size() != qsizetype(m_w) * m_h * 3) m_accum.fill(0, qsizetype(m_w) * m_h * 3);
        quint32 *acc = m_accum.data();
        for (int y = 0; y < m_h; ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            for (int x = 0; x < m_w; ++x, acc += 3) { acc[0] += qRed(line[x]); acc[1] += qGreen(line[x]); acc[2] += qBlue(line[x]); }
        }
        if (++m_sub < m_samples) { update({{QStringLiteral("sub"), m_sub}}); QTimer::singleShot(0, this, &FilmRender::step); return; }
        const quint32 n = quint32(m_samples), half = n / 2;
        acc = m_accum.data();
        for (int y = 0; y < m_h; ++y) {
            QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < m_w; ++x, acc += 3) line[x] = qRgb(int((acc[0] + half) / n), int((acc[1] + half) / n), int((acc[2] + half) / n));
        }
        m_accum.fill(0);
        m_sub = 0;
    }
    if (m_keepPng) img.save(m_frameDir + QStringLiteral("/f%1.png").arg(m_index, 5, 10, QLatin1Char('0')), "png", 90);
    if (m_enc) {
        for (int y = 0; y < m_h; ++y) m_enc->write(reinterpret_cast<const char *>(img.constScanLine(y)), qint64(m_w) * 4);
        while (m_enc->bytesToWrite() > 64LL * 1024 * 1024) m_enc->waitForBytesWritten(1000);
        m_errTail += m_enc->readAllStandardError();
        if (m_errTail.size() > 8000) m_errTail = m_errTail.right(4000);
        if (m_enc->state() != QProcess::Running) { finish(QStringLiteral("ffmpeg stopped: ") + QString::fromUtf8(m_errTail.right(600))); return; }
    }
    ++m_index;
    update({{QStringLiteral("frame"), m_index}});
    QTimer::singleShot(0, this, &FilmRender::step);
}

void FilmRender::finish(const QString &errorIn)
{
    QString error = errorIn;
    if (QQuickItem *view = renderView()) view->setVisible(false);
    if (m_rt) {
        m_rt->renderEnd();
        m_rt->send(QStringLiteral("seq_seek"), {{QStringLiteral("t"), m_savedT}});
    }
    if (m_enc) {
        if (error.isEmpty()) {
            update({{QStringLiteral("stage"), QStringLiteral("encoding")}});
            while (m_enc->bytesToWrite() > 0 && m_enc->waitForBytesWritten(5000)) {}
            m_enc->closeWriteChannel();
            m_enc->waitForFinished(-1);
            m_errTail += m_enc->readAllStandardError();
            if (m_enc->exitStatus() != QProcess::NormalExit || m_enc->exitCode() != 0) error = QStringLiteral("ffmpeg: ") + QString::fromUtf8(m_errTail.right(800));
        } else {
            m_enc->kill();
            m_enc->waitForFinished(3000);
        }
        // guide audio under the picture
        if (error.isEmpty() && m_videoOut != m_out) {
            update({{QStringLiteral("stage"), QStringLiteral("audio")}});
            const double duration = m_frames.size() / double(m_fps);
            const double off = m_guide.value(QStringLiteral("offset")).toDouble();
            const double skip = std::max(0.0, (m_a - off) / m_seqFps);
            const int delayMs = int(std::lround(std::max(0.0, (off - m_a) / m_seqFps) * 1000));
            const double vol = m_guide.value(QStringLiteral("volume")).toDouble(1.0);
            QProcess mux;
            mux.start(Paths::ffmpeg(), {QStringLiteral("-y"), QStringLiteral("-i"), m_videoOut, QStringLiteral("-ss"), fmt6(skip), QStringLiteral("-i"),
                                        m_guide.value(QStringLiteral("path")).toString(), QStringLiteral("-filter_complex"),
                                        QStringLiteral("[1:a]volume=%1,adelay=%2:all=1,apad,atrim=duration=%3,asetpts=PTS-STARTPTS[guide]")
                                            .arg(QString::number(vol, 'f', 4)).arg(delayMs).arg(fmt6(duration)),
                                        QStringLiteral("-map"), QStringLiteral("0:v:0"), QStringLiteral("-map"), QStringLiteral("[guide]"),
                                        QStringLiteral("-c:v"), QStringLiteral("copy"), QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-b:a"),
                                        QStringLiteral("192k"), QStringLiteral("-t"), fmt6(duration), QStringLiteral("-movflags"), QStringLiteral("+faststart"), m_out});
            mux.waitForFinished(-1);
            if (mux.exitCode() != 0) error = QStringLiteral("audio mux: ") + QString::fromUtf8(mux.readAllStandardError().right(600));
        }
        m_enc.reset();
    }
    m_tmp.reset();
    m_running = false;
    update({{QStringLiteral("error"), error.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(error)},
            {QStringLiteral("stage"), error.isEmpty() ? QStringLiteral("done") : QStringLiteral("error")}, {QStringLiteral("running"), false}});
}
