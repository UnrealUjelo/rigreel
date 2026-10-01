// Little-endian reader over a byte buffer. Never throws: reading past the end returns zeros and clears ok().
#pragma once

#include <QByteArray>
#include <QMatrix4x4>
#include <QString>
#include <QVector3D>
#include <QVector4D>
#include <cstring>
#include <type_traits>

namespace re {

class Reader {
public:
    Reader() = default;
    explicit Reader(const QByteArray &data, qint64 base = 0) : m_data(data), m_p(reinterpret_cast<const uchar *>(data.constData())), m_size(data.size()), m_pos(base) {}

    qint64 size() const { return m_size; }
    qint64 pos() const { return m_pos; }
    bool ok() const { return m_ok; }
    void fail() { m_ok = false; }
    void seek(qint64 p) { m_pos = p; if (p < 0 || p > m_size) m_ok = false; }
    void skip(qint64 n) { seek(m_pos + n); }
    void align(int a) { if (a > 1 && m_pos % a) m_pos += a - m_pos % a; }
    bool has(qint64 off, qint64 n) const { return off >= 0 && n >= 0 && off + n <= m_size; }
    const uchar *ptr(qint64 off = -1) const { return m_p + (off < 0 ? m_pos : off); }
    const QByteArray &data() const { return m_data; }

    template <typename T> T at(qint64 off) const
    {
        static_assert(std::is_trivially_copyable_v<T>);
        T v{};
        if (has(off, sizeof(T))) std::memcpy(&v, m_p + off, sizeof(T));
        return v;
    }
    template <typename T> T get()
    {
        T v{};
        if (has(m_pos, sizeof(T))) std::memcpy(&v, m_p + m_pos, sizeof(T));
        else m_ok = false;
        m_pos += sizeof(T);
        return v;
    }
    quint8 u8() { return get<quint8>(); }
    qint8 i8() { return get<qint8>(); }
    quint16 u16() { return get<quint16>(); }
    qint16 i16() { return get<qint16>(); }
    quint32 u32() { return get<quint32>(); }
    qint32 i32() { return get<qint32>(); }
    quint64 u64() { return get<quint64>(); }
    qint64 i64() { return get<qint64>(); }
    float f32() { return get<float>(); }
    QVector3D vec3() { float a = f32(), b = f32(), c = f32(); return {a, b, c}; }
    QVector4D vec4() { float a = f32(), b = f32(), c = f32(), d = f32(); return {a, b, c, d}; }
    // RE Engine stores matrices row-major with the translation in the last row (DirectX style);
    // QMatrix4x4 is column-major in memory, so the 16 floats map straight onto its data().
    QMatrix4x4 mat4()
    {
        float m[16];
        for (float &f : m) f = f32();
        QMatrix4x4 r;
        std::memcpy(r.data(), m, sizeof(m));
        return r;
    }
    QByteArray bytes(qint64 n)
    {
        if (!has(m_pos, n)) { m_ok = false; m_pos += n; return {}; }
        QByteArray b(reinterpret_cast<const char *>(m_p + m_pos), n);
        m_pos += n;
        return b;
    }
    QByteArray bytesAt(qint64 off, qint64 n) const
    {
        if (!has(off, n)) return {};
        return QByteArray(reinterpret_cast<const char *>(m_p + off), n);
    }
    // zero-terminated UTF-16LE at an absolute offset
    QString wstringAt(qint64 off) const
    {
        if (off <= 0 || off >= m_size) return {};
        qint64 e = off;
        while (e + 1 < m_size && (m_p[e] | m_p[e + 1])) e += 2;
        return QString::fromUtf16(reinterpret_cast<const char16_t *>(m_p + off), (e - off) / 2);
    }
    QString stringAt(qint64 off) const
    {
        if (off <= 0 || off >= m_size) return {};
        qint64 e = off;
        while (e < m_size && m_p[e]) ++e;
        return QString::fromUtf8(reinterpret_cast<const char *>(m_p + off), e - off);
    }
    QString wstringPtr() { return wstringAt(i64()); }       // u64 offset to a wide string

private:
    QByteArray m_data;
    const uchar *m_p = nullptr;
    qint64 m_size = 0;
    qint64 m_pos = 0;
    bool m_ok = true;
};

} // namespace re
