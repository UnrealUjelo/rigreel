#include "LoopbackRecorder.h"

#include <QElapsedTimer>
#include <QFile>
#include <QThread>

#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>

#include <algorithm>

#include <chrono>
#include <cmath>
#include <vector>

namespace {

qint64 nowUs()
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return qint64(double(c.QuadPart) * 1e6 / double(f.QuadPart));
}

// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT without pulling in ksmedia.h
const GUID kFloatSubtype = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

template <class T> void release(T *&p) { if (p) { p->Release(); p = nullptr; } }

void writeWavHeader(QFile &f, int rate, int channels, quint32 dataBytes)
{
    auto u32 = [&](quint32 v) { f.write(reinterpret_cast<const char *>(&v), 4); };
    auto u16 = [&](quint16 v) { f.write(reinterpret_cast<const char *>(&v), 2); };
    f.seek(0);
    f.write("RIFF", 4); u32(36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(1); u16(quint16(channels)); u32(quint32(rate)); u32(quint32(rate * channels * 2)); u16(quint16(channels * 2)); u16(16);
    f.write("data", 4); u32(dataBytes);
}

} // namespace

LoopbackRecorder::LoopbackRecorder(QString path) : m_path(std::move(path)) {}

LoopbackRecorder::~LoopbackRecorder()
{
    m_stop = true;
    if (m_thread.joinable())
        m_thread.join();
}

bool LoopbackRecorder::start(QString *error, int timeoutMs)
{
    m_thread = std::thread([this] { run(); });
    QElapsedTimer t;
    t.start();
    while (m_ready == 0 && t.elapsed() < timeoutMs)
        QThread::msleep(5);
    if (m_ready != 1) {
        if (error)
            *error = m_error.isEmpty() ? QStringLiteral("The Windows loopback recorder did not start") : m_error;
        return false;
    }
    return true;
}

double LoopbackRecorder::mark() const { return std::max<qint64>(0, nowUs() - m_startedAt) / 1e6; }

bool LoopbackRecorder::stop(QString *error)
{
    m_stop = true;
    if (m_thread.joinable())
        m_thread.join();
    if (!m_error.isEmpty() && error)
        *error = m_error;
    return m_error.isEmpty();
}

void LoopbackRecorder::run()
{
    // WASAPI is COM: the recording thread needs its own apartment
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator *en = nullptr;
    IMMDevice *dev = nullptr;
    IAudioClient *client = nullptr;
    IAudioCaptureClient *cap = nullptr;
    WAVEFORMATEX *fmt = nullptr;
    QFile out(m_path);
    auto fail = [&](const QString &msg) {
        m_error = msg;
        m_ready = -1;
    };
    do {
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(&en)))) { fail(QStringLiteral("No audio device enumerator")); break; }
        if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) { fail(QStringLiteral("Windows has no default output device")); break; }
        IPropertyStore *props = nullptr;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
                m_device = QString::fromWCharArray(v.pwszVal);
            PropVariantClear(&v);
            props->Release();
        }
        if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(&client)))) { fail(QStringLiteral("Could not open the output device")); break; }
        if (FAILED(client->GetMixFormat(&fmt))) { fail(QStringLiteral("No mix format")); break; }
        if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 10000000 /* 1 s */, 0, fmt, nullptr))) { fail(QStringLiteral("Loopback capture is not available on ") + m_device); break; }
        if (FAILED(client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void **>(&cap)))) { fail(QStringLiteral("No capture service")); break; }
        if (!out.open(QIODevice::WriteOnly)) { fail(QStringLiteral("Cannot write ") + m_path); break; }

        const int channels = fmt->nChannels;
        const int rate = int(fmt->nSamplesPerSec);
        bool isFloat = fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        int bits = fmt->wBitsPerSample;
        if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            auto *ext = reinterpret_cast<WAVEFORMATEXTENSIBLE *>(fmt);
            isFloat = IsEqualGUID(ext->SubFormat, kFloatSubtype);
        }
        writeWavHeader(out, rate, channels, 0);
        client->Start();
        m_startedAt = nowUs();
        m_ready = 1;
        qint64 written = 0; // frames
        std::vector<qint16> pcm;
        while (!m_stop) {
            UINT32 packet = 0;
            if (FAILED(cap->GetNextPacketSize(&packet))) { m_error = QStringLiteral("Device lost during capture"); break; }
            if (packet == 0) {
                // nothing is playing: keep the file in real time with silence
                const qint64 expected = qint64((nowUs() - m_startedAt) / 1e6 * rate);
                if (expected - written > rate / 20) {
                    const qint64 pad = expected - written - rate / 50;
                    pcm.assign(size_t(pad * channels), 0);
                    out.write(reinterpret_cast<const char *>(pcm.data()), qint64(pcm.size() * 2));
                    written += pad;
                }
                QThread::msleep(4);
                continue;
            }
            BYTE *data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
                break;
            pcm.resize(size_t(frames) * channels);
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                std::fill(pcm.begin(), pcm.end(), 0);
            } else if (isFloat && bits == 32) {
                const float *f = reinterpret_cast<const float *>(data);
                for (size_t i = 0; i < pcm.size(); ++i)
                    pcm[i] = qint16(std::lround(std::clamp(f[i], -1.0f, 1.0f) * 32767.0f));
            } else if (bits == 16) {
                memcpy(pcm.data(), data, pcm.size() * 2);
            } else if (bits == 32) {
                const qint32 *s = reinterpret_cast<const qint32 *>(data);
                for (size_t i = 0; i < pcm.size(); ++i)
                    pcm[i] = qint16(s[i] >> 16);
            }
            cap->ReleaseBuffer(frames);
            out.write(reinterpret_cast<const char *>(pcm.data()), qint64(pcm.size() * 2));
            written += frames;
        }
        client->Stop();
        writeWavHeader(out, rate, channels, quint32(written * channels * 2));
        out.close();
    } while (false);
    if (fmt) CoTaskMemFree(fmt);
    release(cap);
    release(client);
    release(dev);
    release(en);
    CoUninitialize();
}
