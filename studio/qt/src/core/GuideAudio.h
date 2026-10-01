// The audio guide track: the Studio plays the file in sync with the runtime transport (the game knows nothing
// about it) and decodes a peak envelope for the waveform in the Clip Editor. Renders mux the same file.
#pragma once

#include <QObject>
#include <QVector>
#include <QJsonObject>

class QMediaPlayer;
class QAudioOutput;
class QAudioDecoder;

class GuideAudio : public QObject
{
    Q_OBJECT
public:
    explicit GuideAudio(QObject *parent = nullptr);
    void sync(const QJsonObject &sequence);   // called on every state update
    const QVector<float> &peaks() const { return m_peaks; }
    double duration() const { return m_duration; }
    QString path() const { return m_path; }
    bool decoding() const { return m_decoding; }
    QString error() const { return m_error; }

signals:
    void peaksChanged();
    void durationKnown(double seconds);

private:
    void load(const QString &path);
    QMediaPlayer *m_player;
    QAudioOutput *m_output;
    QAudioDecoder *m_decoder = nullptr;
    QString m_path, m_error;
    QVector<float> m_peaks;       // max |sample| per bucket
    QVector<float> m_raw;
    double m_duration = 0;
    qint64 m_rawFrames = 0;
    int m_rate = 48000;
    bool m_decoding = false;
};
