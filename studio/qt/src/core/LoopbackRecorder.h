// Records the Windows default output device (what you hear) through WASAPI loopback into a 16-bit WAV.
// Loopback delivers nothing while nothing plays, so silence is padded from the wall clock: the file always runs
// in real time, and mark() gives the offset of a moment inside it.
#pragma once

#include <QString>
#include <atomic>
#include <thread>

class LoopbackRecorder
{
public:
    explicit LoopbackRecorder(QString path);
    ~LoopbackRecorder();
    bool start(QString *error, int timeoutMs = 5000);
    double mark() const;          // seconds since capture start
    bool stop(QString *error);
    QString device() const { return m_device; }

private:
    void run();
    QString m_path, m_device, m_error;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<int> m_ready{0};   // 0 waiting, 1 running, -1 failed
    std::atomic<qint64> m_startedAt{0};
};
