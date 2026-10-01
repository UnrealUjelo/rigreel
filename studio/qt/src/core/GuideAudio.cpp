#include "GuideAudio.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioOutput>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QUrl>

#include <cmath>

GuideAudio::GuideAudio(QObject *parent) : QObject(parent)
{
    m_player = new QMediaPlayer(this);
    m_output = new QAudioOutput(this);
    m_player->setAudioOutput(m_output);
}

void GuideAudio::load(const QString &path)
{
    m_path = path;
    m_peaks.clear();
    m_raw.clear();
    m_rawFrames = 0;
    m_duration = 0;
    m_error.clear();
    m_player->stop();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        m_player->setSource({});
        if (!path.isEmpty())
            m_error = QStringLiteral("missing file");
        emit peaksChanged();
        return;
    }
    m_player->setSource(QUrl::fromLocalFile(path));
    delete m_decoder;
    m_decoder = new QAudioDecoder(this);
    m_decoding = true;
    // the waveform: max |sample| per 1/200 s bucket of channel 0
    connect(m_decoder, &QAudioDecoder::bufferReady, this, [this] {
        const QAudioBuffer buf = m_decoder->read();
        if (!buf.isValid())
            return;
        const QAudioFormat f = buf.format();
        m_rate = f.sampleRate();
        const int ch = std::max(1, f.channelCount());
        const int per = std::max(1, m_rate / 200);
        const qsizetype frames = buf.frameCount();
        for (qsizetype i = 0; i < frames; ++i) {
            float v = 0;
            switch (f.sampleFormat()) {
            case QAudioFormat::Float: v = std::abs(buf.constData<float>()[i * ch]); break;
            case QAudioFormat::Int16: v = std::abs(buf.constData<qint16>()[i * ch] / 32768.0f); break;
            case QAudioFormat::Int32: v = std::abs(buf.constData<qint32>()[i * ch] / 2147483648.0f); break;
            case QAudioFormat::UInt8: v = std::abs((buf.constData<quint8>()[i * ch] - 128) / 128.0f); break;
            default: break;
            }
            const qint64 bucket = (m_rawFrames + i) / per;
            if (bucket >= m_peaks.size())
                m_peaks.resize(bucket + 1);
            if (v > m_peaks[bucket])
                m_peaks[bucket] = v;
        }
        m_rawFrames += frames;
    });
    connect(m_decoder, &QAudioDecoder::finished, this, [this] {
        m_decoding = false;
        m_duration = m_rate > 0 ? double(m_rawFrames) / m_rate : 0;
        emit peaksChanged();
        if (m_duration > 0)
            emit durationKnown(m_duration);
    });
    connect(m_decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), this, [this](QAudioDecoder::Error) {
        m_decoding = false;
        m_error = m_decoder->errorString();
        emit peaksChanged();
    });
    m_decoder->setSource(QUrl::fromLocalFile(path));
    m_decoder->start();
    emit peaksChanged();
}

void GuideAudio::sync(const QJsonObject &seq)
{
    const QJsonObject a = seq.value(QStringLiteral("audio")).toObject();
    const QString path = a.value(QStringLiteral("path")).toString();
    if (path != m_path)
        load(path);
    if (path.isEmpty())
        return;
    const double fps = std::max(1.0, seq.value(QStringLiteral("fps")).toDouble(60));
    const double offset = a.value(QStringLiteral("offset")).toDouble();
    const double target = (seq.value(QStringLiteral("t")).toDouble() - offset) / fps; // seconds into the file
    const bool playing = seq.value(QStringLiteral("playing")).toBool();
    const double speed = seq.value(QStringLiteral("speed")).toDouble(1);
    m_output->setVolume(float(std::clamp(a.contains(QStringLiteral("volume")) ? a.value(QStringLiteral("volume")).toDouble(1) : 1.0, 0.0, 1.0)));
    const double dur = m_duration > 0 ? m_duration : a.value(QStringLiteral("duration")).toDouble(1e9);
    const qint64 posMs = m_player->position();
    if (playing) {
        if (target < 0 || target > dur) {
            if (m_player->playbackState() == QMediaPlayer::PlayingState)
                m_player->pause();
            return;
        }
        if (std::abs(m_player->playbackRate() - speed) > 1e-3)
            m_player->setPlaybackRate(speed);
        if (m_player->playbackState() != QMediaPlayer::PlayingState) {
            m_player->setPosition(qint64(target * 1000));
            m_player->play();
        } else if (std::abs(posMs / 1000.0 - target) > 0.15) {
            m_player->setPosition(qint64(target * 1000));
        }
    } else {
        if (m_player->playbackState() == QMediaPlayer::PlayingState)
            m_player->pause();
        if (target >= 0 && std::abs(posMs / 1000.0 - target) > 0.04)
            m_player->setPosition(qint64(target * 1000));
    }
}
