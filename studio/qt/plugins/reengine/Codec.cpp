#include "Codec.h"

#include <QLibrary>
#include <QMutex>
#include <QtZlib/zlib.h>

#include <cstring>
#include <vector>

namespace re {

// ------------------------------------------------------------------ zstd (libzstd.dll next to the app)
namespace {
using ZSTD_decompress_t = size_t (*)(void *, size_t, const void *, size_t);
using ZSTD_isError_t = unsigned (*)(size_t);
using ZSTD_getFrameContentSize_t = unsigned long long (*)(const void *, size_t);

struct Zstd {
    ZSTD_decompress_t decompress = nullptr;
    ZSTD_isError_t isError = nullptr;
    ZSTD_getFrameContentSize_t frameSize = nullptr;
    bool loaded = false;
};

Zstd &zstd()
{
    static Zstd z;
    static QMutex mutex;
    QMutexLocker lock(&mutex);
    if (!z.loaded) {
        z.loaded = true;
        static QLibrary lib(QStringLiteral("libzstd"));
        if (lib.load()) {
            z.decompress = reinterpret_cast<ZSTD_decompress_t>(lib.resolve("ZSTD_decompress"));
            z.isError = reinterpret_cast<ZSTD_isError_t>(lib.resolve("ZSTD_isError"));
            z.frameSize = reinterpret_cast<ZSTD_getFrameContentSize_t>(lib.resolve("ZSTD_getFrameContentSize"));
        }
    }
    return z;
}
} // namespace

bool zstdAvailable() { return zstd().decompress != nullptr; }

QByteArray zstdDecompress(const char *src, qint64 size, qint64 expected)
{
    Zstd &z = zstd();
    if (!z.decompress) return {};
    qint64 cap = expected;
    if (cap <= 0 && z.frameSize) {
        const unsigned long long fs = z.frameSize(src, size_t(size));
        if (fs < (1ull << 40)) cap = qint64(fs);
    }
    if (cap <= 0) cap = size * 8;
    QByteArray out(cap, Qt::Uninitialized);
    const size_t n = z.decompress(out.data(), size_t(cap), src, size_t(size));
    if (z.isError(n)) return {};
    out.resize(qint64(n));
    return out;
}

QByteArray inflateAny(const char *src, qint64 size, qint64 expected)
{
    for (int bits : {MAX_WBITS, -MAX_WBITS}) {
        z_stream zs{};
        if (inflateInit2(&zs, bits) != Z_OK) continue;
        QByteArray out(expected > 0 ? expected : size * 4, Qt::Uninitialized);
        zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(src));
        zs.avail_in = uInt(size);
        qint64 done = 0;
        int ret = Z_OK;
        while (ret == Z_OK) {
            if (done == out.size()) out.resize(out.size() * 2);
            zs.next_out = reinterpret_cast<Bytef *>(out.data() + done);
            zs.avail_out = uInt(out.size() - done);
            ret = inflate(&zs, Z_NO_FLUSH);
            done = qint64(zs.total_out);
        }
        inflateEnd(&zs);
        if (ret == Z_STREAM_END) { out.resize(done); return out; }
    }
    return {};
}

// ------------------------------------------------------------------ minimal unsigned big integers
namespace {
using Limbs = std::vector<quint32>;

void trim(Limbs &a) { while (!a.empty() && a.back() == 0) a.pop_back(); }

Limbs fromLE(const uchar *p, size_t n)
{
    Limbs r((n + 3) / 4, 0);
    for (size_t i = 0; i < n; ++i) r[i / 4] |= quint32(p[i]) << (8 * (i % 4));
    trim(r);
    return r;
}

void toLE(const Limbs &a, uchar *out, size_t n)
{
    for (size_t i = 0; i < n; ++i) out[i] = (i / 4 < a.size()) ? uchar(a[i / 4] >> (8 * (i % 4))) : 0;
}

Limbs mul(const Limbs &a, const Limbs &b)
{
    if (a.empty() || b.empty()) return {};
    Limbs r(a.size() + b.size(), 0);
    for (size_t i = 0; i < a.size(); ++i) {
        quint64 carry = 0;
        for (size_t j = 0; j < b.size(); ++j) {
            const quint64 t = quint64(a[i]) * b[j] + r[i + j] + carry;
            r[i + j] = quint32(t);
            carry = t >> 32;
        }
        r[i + b.size()] = quint32(carry);
    }
    trim(r);
    return r;
}

int nlz(quint32 x) { return x ? __builtin_clz(x) : 32; }

// Knuth's algorithm D (Hacker's Delight divmnu): q = u / v, r = u % v. v must be non-zero.
void divmod(const Limbs &u, const Limbs &v, Limbs *q, Limbs *r)
{
    const int m = int(u.size()), n = int(v.size());
    if (m < n) { if (q) q->clear(); if (r) *r = u; return; }
    Limbs qq(m - n + 1, 0), rr(n, 0);
    const quint64 b = 1ull << 32;
    if (n == 1) {
        quint64 k = 0;
        for (int j = m - 1; j >= 0; --j) {
            const quint64 cur = k * b + u[j];
            qq[j] = quint32(cur / v[0]);
            k = cur - quint64(qq[j]) * v[0];
        }
        rr[0] = quint32(k);
    } else {
        const int s = nlz(v[n - 1]);
        std::vector<quint32> vn(n), un(m + 1);
        for (int i = n - 1; i > 0; --i) vn[i] = quint32((quint64(v[i]) << s) | (quint64(v[i - 1]) >> (32 - s)));
        vn[0] = v[0] << s;
        un[m] = quint32(quint64(u[m - 1]) >> (32 - s));
        for (int i = m - 1; i > 0; --i) un[i] = quint32((quint64(u[i]) << s) | (quint64(u[i - 1]) >> (32 - s)));
        un[0] = u[0] << s;
        for (int j = m - n; j >= 0; --j) {
            const quint64 num = quint64(un[j + n]) * b + un[j + n - 1];
            quint64 qhat = num / vn[n - 1];
            quint64 rhat = num - qhat * vn[n - 1];
            while (qhat >= b || qhat * vn[n - 2] > b * rhat + un[j + n - 2]) {
                --qhat;
                rhat += vn[n - 1];
                if (rhat >= b) break;
            }
            qint64 k = 0, t = 0;
            for (int i = 0; i < n; ++i) {
                const quint64 p = qhat * vn[i];
                t = qint64(un[i + j]) - k - qint64(p & 0xFFFFFFFFull);
                un[i + j] = quint32(t);
                k = qint64(p >> 32) - (t >> 32);
            }
            t = qint64(un[j + n]) - k;
            un[j + n] = quint32(t);
            qq[j] = quint32(qhat);
            if (t < 0) {
                qq[j] -= 1;
                quint64 c = 0;
                for (int i = 0; i < n; ++i) {
                    const quint64 sum = quint64(un[i + j]) + vn[i] + c;
                    un[i + j] = quint32(sum);
                    c = sum >> 32;
                }
                un[j + n] = quint32(quint64(un[j + n]) + c);
            }
        }
        for (int i = 0; i < n; ++i)
            rr[i] = quint32((quint64(un[i]) >> s) | (quint64(un[i + 1]) << (32 - s)));
    }
    trim(qq);
    trim(rr);
    if (q) *q = std::move(qq);
    if (r) *r = std::move(rr);
}

Limbs mod(const Limbs &a, const Limbs &m) { Limbs r; divmod(a, m, nullptr, &r); return r; }

Limbs modpow(Limbs base, const Limbs &exp, const Limbs &m)
{
    Limbs result{1};
    base = mod(base, m);
    for (int i = int(exp.size()) * 32 - 1; i >= 0; --i) {
        result = mod(mul(result, result), m);
        if ((exp[i / 32] >> (i % 32)) & 1) result = mod(mul(result, base), m);
    }
    return result;
}

const uchar kKeyModulus[128] = {
    0x7D, 0x0B, 0xF8, 0xC1, 0x7C, 0x23, 0xFD, 0x3B, 0xD4, 0x75, 0x16, 0xD2, 0x33, 0x21, 0xD8, 0x10,
    0x71, 0xF9, 0x7C, 0xD1, 0x34, 0x93, 0xBA, 0x77, 0x26, 0xFC, 0xAB, 0x2C, 0xEE, 0xDA, 0xD9, 0x1C,
    0x89, 0xE7, 0x29, 0x7B, 0xDD, 0x8A, 0xAE, 0x50, 0x39, 0xB6, 0x01, 0x6D, 0x21, 0x89, 0x5D, 0xA5,
    0xA1, 0x3E, 0xA2, 0xC0, 0x8C, 0x93, 0x13, 0x36, 0x65, 0xEB, 0xE8, 0xDF, 0x06, 0x17, 0x67, 0x96,
    0x06, 0x2B, 0xAC, 0x23, 0xED, 0x8C, 0xB7, 0x8B, 0x90, 0xAD, 0xEA, 0x71, 0xC4, 0x40, 0x44, 0x9D,
    0x1C, 0x7B, 0xBA, 0xC4, 0xB6, 0x2D, 0xD6, 0xD2, 0x4B, 0x62, 0xD6, 0x26, 0xFC, 0x74, 0x20, 0x07,
    0xEC, 0xE3, 0x59, 0x9A, 0xE6, 0xAF, 0xB9, 0xA8, 0x35, 0x8B, 0xE0, 0xE8, 0xD3, 0xCD, 0x45, 0x65,
    0xB0, 0x91, 0xC4, 0x95, 0x1B, 0xF3, 0x23, 0x1E, 0xC6, 0x71, 0xCF, 0x3E, 0x35, 0x2D, 0x6B, 0xE3,
};
const uchar kKeyExponent[4] = {0x01, 0x00, 0x01, 0x00};

const uchar kResModulus[32] = {
    0x13, 0xD7, 0x9C, 0x89, 0x88, 0x91, 0x48, 0x10, 0xD7, 0xAA, 0x78, 0xAE, 0xF8, 0x59, 0xDF, 0x7D,
    0x3C, 0x43, 0xA0, 0xD0, 0xBB, 0x36, 0x77, 0xB5, 0xF0, 0x5C, 0x02, 0xAF, 0x65, 0xD8, 0x77, 0x03,
};
const uchar kResExponent[32] = {
    0xC0, 0xC2, 0x77, 0x1F, 0x5B, 0x34, 0x6A, 0x01, 0xC7, 0xD4, 0xD7, 0x85, 0x2E, 0x42, 0x2B, 0x3B,
    0x16, 0x3A, 0x17, 0x13, 0x16, 0xEA, 0x83, 0x30, 0x30, 0xDF, 0x3F, 0xF4, 0x25, 0x93, 0x20, 0x01,
};
} // namespace

void decryptEntryTable(QByteArray &table, const QByteArray &encryptedKey128)
{
    if (encryptedKey128.size() != 128) return;
    const Limbs m = fromLE(kKeyModulus, 128), e = fromLE(kKeyExponent, 4);
    const Limbs k = modpow(fromLE(reinterpret_cast<const uchar *>(encryptedKey128.constData()), 128), e, m);
    uchar key[128];
    toLE(k, key, 128);
    auto *p = reinterpret_cast<uchar *>(table.data());
    const qint64 n = table.size();
    for (qint64 i = 0; i < n; ++i) p[i] ^= uchar((quint64(i) + quint64(key[i % 32]) * key[i % 29]) & 0xFF);
}

QByteArray decryptResource(const QByteArray &data)
{
    if (data.size() < 8) return data;
    quint64 outSize = 0;
    std::memcpy(&outSize, data.constData(), 8);
    const qint64 blocks = (data.size() - 8) / 128;
    if (outSize > quint64(blocks) * 8 + 8) return {};
    const Limbs m = fromLE(kResModulus, 32), e = fromLE(kResExponent, 32);
    QByteArray out(qint64(outSize), Qt::Uninitialized);
    auto *o = reinterpret_cast<uchar *>(out.data());
    const auto *in = reinterpret_cast<const uchar *>(data.constData()) + 8;
    for (qint64 bI = 0; bI < blocks; ++bI) {
        const Limbs b1 = fromLE(in + bI * 128, 64), b2 = fromLE(in + bI * 128 + 64, 64);
        const Limbs k = modpow(b1, e, m);
        Limbs q;
        if (!k.empty()) divmod(b2, k, &q, nullptr);
        uchar eight[8];
        toLE(q, eight, 8);
        const qint64 off = bI * 8;
        for (int i = 0; i < 8 && off + i < qint64(outSize); ++i) o[off + i] = eight[i];
    }
    return out;
}

} // namespace re
