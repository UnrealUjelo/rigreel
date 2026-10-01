#include "Bcn.h"

#include <QString>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace re::bcn {

namespace {

// ------------------------------------------------------------------ BC7 tables (D3D11 functional spec)
const quint8 kPartition2[64][16] = {
    {0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1}, {0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1}, {0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1}, {0,0,0,1,0,0,1,1,0,0,1,1,0,1,1,1},
    {0,0,0,0,0,0,0,1,0,0,0,1,0,0,1,1}, {0,0,1,1,0,1,1,1,0,1,1,1,1,1,1,1}, {0,0,0,1,0,0,1,1,0,1,1,1,1,1,1,1}, {0,0,0,0,0,0,0,1,0,0,1,1,0,1,1,1},
    {0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,1}, {0,0,1,1,0,1,1,1,1,1,1,1,1,1,1,1}, {0,0,0,0,0,0,0,1,0,1,1,1,1,1,1,1}, {0,0,0,0,0,0,0,0,0,0,0,1,0,1,1,1},
    {0,0,0,1,0,1,1,1,1,1,1,1,1,1,1,1}, {0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1}, {0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1}, {0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1},
    {0,0,0,0,1,0,0,0,1,1,1,0,1,1,1,1}, {0,1,1,1,0,0,0,1,0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,0,1,0,0,0,1,1,1,0}, {0,1,1,1,0,0,1,1,0,0,0,1,0,0,0,0},
    {0,0,1,1,0,0,0,1,0,0,0,0,0,0,0,0}, {0,0,0,0,1,0,0,0,1,1,0,0,1,1,1,0}, {0,0,0,0,0,0,0,0,1,0,0,0,1,1,0,0}, {0,1,1,1,0,0,1,1,0,0,1,1,0,0,0,1},
    {0,0,1,1,0,0,0,1,0,0,0,1,0,0,0,0}, {0,0,0,0,1,0,0,0,1,0,0,0,1,1,0,0}, {0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0}, {0,0,1,1,0,1,1,0,0,1,1,0,1,1,0,0},
    {0,0,0,1,0,1,1,1,1,1,1,0,1,0,0,0}, {0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0}, {0,1,1,1,0,0,0,1,1,0,0,0,1,1,1,0}, {0,0,1,1,1,0,0,1,1,0,0,1,1,1,0,0},
    {0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1}, {0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1}, {0,1,0,1,1,0,1,0,0,1,0,1,1,0,1,0}, {0,0,1,1,0,0,1,1,1,1,0,0,1,1,0,0},
    {0,0,1,1,1,1,0,0,0,0,1,1,1,1,0,0}, {0,1,0,1,0,1,0,1,1,0,1,0,1,0,1,0}, {0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1}, {0,1,0,1,1,0,1,0,1,0,1,0,0,1,0,1},
    {0,1,1,1,0,0,1,1,1,1,0,0,1,1,1,0}, {0,0,0,1,0,0,1,1,1,1,0,0,1,0,0,0}, {0,0,1,1,0,0,1,0,0,1,0,0,1,1,0,0}, {0,0,1,1,1,0,1,1,1,1,0,1,1,1,0,0},
    {0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0}, {0,0,1,1,1,1,0,0,1,1,0,0,0,0,1,1}, {0,1,1,0,0,1,1,0,1,0,0,1,1,0,0,1}, {0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0},
    {0,1,0,0,1,1,1,0,0,1,0,0,0,0,0,0}, {0,0,1,0,0,1,1,1,0,0,1,0,0,0,0,0}, {0,0,0,0,0,0,1,0,0,1,1,1,0,0,1,0}, {0,0,0,0,0,1,0,0,1,1,1,0,0,1,0,0},
    {0,1,1,0,1,1,0,0,1,0,0,1,0,0,1,1}, {0,0,1,1,0,1,1,0,1,1,0,0,1,0,0,1}, {0,1,1,0,0,0,1,1,1,0,0,1,1,1,0,0}, {0,0,1,1,1,0,0,1,1,1,0,0,0,1,1,0},
    {0,1,1,0,1,1,0,0,1,1,0,0,1,0,0,1}, {0,1,1,0,0,0,1,1,0,0,1,1,1,0,0,1}, {0,1,1,1,1,1,1,0,1,0,0,0,0,0,0,1}, {0,0,0,1,1,0,0,0,1,1,1,0,0,1,1,1},
    {0,0,0,0,1,1,1,1,0,0,1,1,0,0,1,1}, {0,0,1,1,0,0,1,1,1,1,1,1,0,0,0,0}, {0,0,1,0,0,0,1,0,1,1,1,0,1,1,1,0}, {0,1,0,0,0,1,0,0,0,1,1,1,0,1,1,1},
};
const quint8 kPartition3[64][16] = {
    {0,0,1,1,0,0,1,1,0,2,2,1,2,2,2,2}, {0,0,0,1,0,0,1,1,2,2,1,1,2,2,2,1}, {0,0,0,0,2,0,0,1,2,2,1,1,2,2,1,1}, {0,2,2,2,0,0,2,2,0,0,1,1,0,1,1,1},
    {0,0,0,0,0,0,0,0,1,1,2,2,1,1,2,2}, {0,0,1,1,0,0,1,1,0,0,2,2,0,0,2,2}, {0,0,2,2,0,0,2,2,1,1,1,1,1,1,1,1}, {0,0,1,1,0,0,1,1,2,2,1,1,2,2,1,1},
    {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2}, {0,0,0,0,1,1,1,1,1,1,1,1,2,2,2,2}, {0,0,0,0,1,1,1,1,2,2,2,2,2,2,2,2}, {0,0,1,2,0,0,1,2,0,0,1,2,0,0,1,2},
    {0,1,1,2,0,1,1,2,0,1,1,2,0,1,1,2}, {0,1,2,2,0,1,2,2,0,1,2,2,0,1,2,2}, {0,0,1,1,0,1,1,2,1,1,2,2,1,2,2,2}, {0,0,1,1,2,0,0,1,2,2,0,0,2,2,2,0},
    {0,0,0,1,0,0,1,1,0,1,1,2,1,1,2,2}, {0,1,1,1,0,0,1,1,2,0,0,1,2,2,0,0}, {0,0,0,0,1,1,2,2,1,1,2,2,1,1,2,2}, {0,0,2,2,0,0,2,2,0,0,2,2,1,1,1,1},
    {0,1,1,1,0,1,1,1,0,2,2,2,0,2,2,2}, {0,0,0,1,0,0,0,1,2,2,2,1,2,2,2,1}, {0,0,0,0,0,0,1,1,0,1,2,2,0,1,2,2}, {0,0,0,0,1,1,0,0,2,2,1,0,2,2,1,0},
    {0,1,2,2,0,1,2,2,0,0,1,1,0,0,0,0}, {0,0,1,2,0,0,1,2,1,1,2,2,2,2,2,2}, {0,1,1,0,1,2,2,1,1,2,2,1,0,1,1,0}, {0,0,0,0,0,1,1,0,1,2,2,1,1,2,2,1},
    {0,0,2,2,1,1,0,2,1,1,0,2,0,0,2,2}, {0,1,1,0,0,1,1,0,2,0,0,2,2,2,2,2}, {0,0,1,1,0,1,2,2,0,1,2,2,0,0,1,1}, {0,0,0,0,2,0,0,0,2,2,1,1,2,2,2,1},
    {0,0,0,0,0,0,0,2,1,1,2,2,1,2,2,2}, {0,2,2,2,0,0,2,2,0,0,1,2,0,0,1,1}, {0,0,1,1,0,0,1,2,0,0,2,2,0,2,2,2}, {0,1,2,0,0,1,2,0,0,1,2,0,0,1,2,0},
    {0,0,0,0,1,1,1,1,2,2,2,2,0,0,0,0}, {0,1,2,0,1,2,0,1,2,0,1,2,0,1,2,0}, {0,1,2,0,2,0,1,2,1,2,0,1,0,1,2,0}, {0,0,1,1,2,2,0,0,1,1,2,2,0,0,1,1},
    {0,0,1,1,1,1,2,2,2,2,0,0,0,0,1,1}, {0,1,0,1,0,1,0,1,2,2,2,2,2,2,2,2}, {0,0,0,0,0,0,0,0,2,1,2,1,2,1,2,1}, {0,0,2,2,1,1,2,2,0,0,2,2,1,1,2,2},
    {0,0,2,2,0,0,1,1,0,0,2,2,0,0,1,1}, {0,2,2,0,1,2,2,1,0,2,2,0,1,2,2,1}, {0,1,0,1,2,2,2,2,2,2,2,2,0,1,0,1}, {0,0,0,0,2,1,2,1,2,1,2,1,2,1,2,1},
    {0,1,0,1,0,1,0,1,0,1,0,1,2,2,2,2}, {0,2,2,2,0,1,1,1,0,2,2,2,0,1,1,1}, {0,0,0,2,1,1,1,2,0,0,0,2,1,1,1,2}, {0,0,0,0,2,1,1,2,2,1,1,2,2,1,1,2},
    {0,2,2,2,0,1,1,1,0,1,1,1,0,2,2,2}, {0,0,0,2,1,1,1,2,1,1,1,2,0,0,0,2}, {0,1,1,0,0,1,1,0,0,1,1,0,2,2,2,2}, {0,0,0,0,0,0,0,0,2,1,1,2,2,1,1,2},
    {0,1,1,0,0,1,1,0,2,2,2,2,2,2,2,2}, {0,0,2,2,0,0,1,1,0,0,1,1,0,0,2,2}, {0,0,2,2,1,1,2,2,1,1,2,2,0,0,2,2}, {0,0,0,0,0,0,0,0,0,0,0,0,2,1,1,2},
    {0,0,0,2,0,0,0,1,0,0,0,2,0,0,0,1}, {0,2,2,2,1,2,2,2,0,2,2,2,1,2,2,2}, {0,1,0,1,2,2,2,2,2,2,2,2,2,2,2,2}, {0,1,1,1,2,0,1,1,2,2,0,1,2,2,2,0},
};
const quint8 kAnchor2[64] = {
    15,15,15,15,15,15,15,15, 15,15,15,15,15,15,15,15, 15, 2, 8, 2, 2, 8, 8,15,  2, 8, 2, 2, 8, 8, 2, 2,
    15,15, 6, 8, 2, 8,15,15,  2, 8, 2, 2, 2,15,15, 6,  6, 2, 6, 8,15,15, 2, 2, 15,15,15,15,15, 2, 2,15,
};
const quint8 kAnchor3a[64] = {
     3, 3,15,15, 8, 3,15,15,  8, 8, 6, 6, 6, 5, 3, 3,  3, 3, 8,15, 3, 3, 6,10,  5, 8, 8, 6, 8, 5,15,15,
     8,15, 3, 5, 6,10, 8,15, 15, 3,15, 5,15,15,15,15,  3,15, 5, 5, 5, 8, 5,10,  5,10, 8,13,15,12, 3, 3,
};
const quint8 kAnchor3b[64] = {
    15, 8, 8, 3,15,15, 3, 8, 15,15,15,15,15,15,15, 8, 15, 8,15, 3,15, 8,15, 8,  3,15, 6,10,15,15,10, 8,
    15, 3,15,10,10, 8, 9,10,  6,15, 8,15, 3, 6, 6, 8, 15, 3,15,15,15,15,15,15, 15,15,15,15, 3,15,15, 8,
};
const int kWeights2[4] = {0, 21, 43, 64};
const int kWeights3[8] = {0, 9, 18, 27, 37, 46, 55, 64};
const int kWeights4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

struct ModeInfo { int ns, pb, rb, isb, cb, ab, epb, spb, ib, ib2; };
const ModeInfo kModes[8] = {
    {3, 4, 0, 0, 4, 0, 1, 0, 3, 0}, {2, 6, 0, 0, 6, 0, 0, 1, 3, 0}, {3, 6, 0, 0, 5, 0, 0, 0, 2, 0}, {2, 6, 0, 0, 7, 0, 1, 0, 2, 0},
    {1, 0, 2, 1, 5, 6, 0, 0, 2, 3}, {1, 0, 2, 0, 7, 8, 0, 0, 2, 2}, {1, 0, 0, 0, 7, 7, 1, 0, 4, 0}, {2, 6, 0, 0, 5, 5, 1, 0, 2, 0},
};

const int *weightsFor(int bits) { return bits == 2 ? kWeights2 : bits == 3 ? kWeights3 : kWeights4; }
inline int interp(int e0, int e1, int w) { return ((64 - w) * e0 + w * e1 + 32) >> 6; }

struct Bits {
    const quint8 *p;
    int pos = 0;
    quint32 read(int n)
    {
        quint32 v = 0;
        for (int i = 0; i < n; ++i, ++pos) v |= quint32((p[pos >> 3] >> (pos & 7)) & 1) << i;
        return v;
    }
};

void decodeBc7Block(const quint8 *src, quint8 out[16][4])
{
    int mode = 0;
    while (mode < 8 && !(src[0] & (1 << mode))) ++mode;
    if (mode == 8) { std::memset(out, 0, 64); return; }
    const ModeInfo &m = kModes[mode];
    Bits b{src};
    b.pos = mode + 1;
    const int partition = int(b.read(m.pb));
    const int rotation = int(b.read(m.rb));
    const int indexSel = int(b.read(m.isb));

    int ep[6][4] = {};
    for (int c = 0; c < 3; ++c)
        for (int e = 0; e < m.ns * 2; ++e) ep[e][c] = int(b.read(m.cb));
    for (int e = 0; e < m.ns * 2; ++e) ep[e][3] = m.ab ? int(b.read(m.ab)) : 255;

    int cbits = m.cb, abits = m.ab;
    if (m.epb) {
        for (int e = 0; e < m.ns * 2; ++e) {
            const int p = int(b.read(1));
            for (int c = 0; c < 3; ++c) ep[e][c] = (ep[e][c] << 1) | p;
            if (m.ab) ep[e][3] = (ep[e][3] << 1) | p;
        }
        ++cbits;
        if (m.ab) ++abits;
    } else if (m.spb) {
        for (int s = 0; s < m.ns; ++s) {
            const int p = int(b.read(1));
            for (int e = s * 2; e < s * 2 + 2; ++e)
                for (int c = 0; c < 3; ++c) ep[e][c] = (ep[e][c] << 1) | p;
        }
        ++cbits;
    }
    for (int e = 0; e < m.ns * 2; ++e) {
        for (int c = 0; c < 3; ++c) { ep[e][c] <<= (8 - cbits); ep[e][c] |= ep[e][c] >> cbits; }
        if (m.ab) { ep[e][3] <<= (8 - abits); ep[e][3] |= ep[e][3] >> abits; }
    }

    auto subsetOf = [&](int i) { return m.ns == 1 ? 0 : m.ns == 2 ? kPartition2[partition][i] : kPartition3[partition][i]; };
    auto isAnchor = [&](int i) {
        if (i == 0) return true;
        if (m.ns == 2) return i == kAnchor2[partition];
        if (m.ns == 3) return i == kAnchor3a[partition] || i == kAnchor3b[partition];
        return false;
    };
    int idx1[16], idx2[16] = {};
    for (int i = 0; i < 16; ++i) idx1[i] = int(b.read(m.ib - (isAnchor(i) ? 1 : 0)));
    if (m.ib2)
        for (int i = 0; i < 16; ++i) idx2[i] = int(b.read(m.ib2 - (i == 0 ? 1 : 0)));

    for (int i = 0; i < 16; ++i) {
        const int s = subsetOf(i);
        const int *e0 = ep[s * 2], *e1 = ep[s * 2 + 1];
        int ci, ai, cb, ab;
        if (!m.ib2) { ci = ai = idx1[i]; cb = ab = m.ib; }
        else if (indexSel == 0) { ci = idx1[i]; cb = m.ib; ai = idx2[i]; ab = m.ib2; }
        else { ci = idx2[i]; cb = m.ib2; ai = idx1[i]; ab = m.ib; }
        const int *wc = weightsFor(cb), *wa = weightsFor(ab);
        int px[4];
        for (int c = 0; c < 3; ++c) px[c] = interp(e0[c], e1[c], wc[ci]);
        px[3] = m.ab ? interp(e0[3], e1[3], wa[ai]) : 255;
        if (rotation == 1) std::swap(px[0], px[3]);
        else if (rotation == 2) std::swap(px[1], px[3]);
        else if (rotation == 3) std::swap(px[2], px[3]);
        for (int c = 0; c < 4; ++c) out[i][c] = quint8(px[c]);
    }
}

// ------------------------------------------------------------------ BC1-BC5
void color565(quint16 c, int rgb[3])
{
    const int r = (c >> 11) & 31, g = (c >> 5) & 63, bl = c & 31;
    rgb[0] = (r << 3) | (r >> 2);
    rgb[1] = (g << 2) | (g >> 4);
    rgb[2] = (bl << 3) | (bl >> 2);
}

void decodeColorBlock(const quint8 *src, quint8 out[16][4], bool fourAlways)
{
    quint16 c0, c1;
    quint32 bits;
    std::memcpy(&c0, src, 2);
    std::memcpy(&c1, src + 2, 2);
    std::memcpy(&bits, src + 4, 4);
    int pal[4][4];
    color565(c0, pal[0]);
    color565(c1, pal[1]);
    pal[0][3] = pal[1][3] = 255;
    if (c0 > c1 || fourAlways) {
        for (int c = 0; c < 3; ++c) { pal[2][c] = (2 * pal[0][c] + pal[1][c]) / 3; pal[3][c] = (pal[0][c] + 2 * pal[1][c]) / 3; }
        pal[2][3] = pal[3][3] = 255;
    } else {
        for (int c = 0; c < 3; ++c) { pal[2][c] = (pal[0][c] + pal[1][c]) / 2; pal[3][c] = 0; }
        pal[2][3] = 255;
        pal[3][3] = 0;
    }
    for (int i = 0; i < 16; ++i) {
        const int k = (bits >> (2 * i)) & 3;
        for (int c = 0; c < 4; ++c) out[i][c] = quint8(pal[k][c]);
    }
}

void decodeAlphaBlock(const quint8 *src, quint8 out[16], bool isSigned)
{
    int a0 = isSigned ? int(qint8(src[0])) : src[0];
    int a1 = isSigned ? int(qint8(src[1])) : src[1];
    int pal[8];
    pal[0] = a0;
    pal[1] = a1;
    if (a0 > a1) for (int i = 1; i < 7; ++i) pal[i + 1] = ((7 - i) * a0 + i * a1) / 7;
    else {
        for (int i = 1; i < 5; ++i) pal[i + 1] = ((5 - i) * a0 + i * a1) / 5;
        pal[6] = isSigned ? -127 : 0;
        pal[7] = isSigned ? 127 : 255;
    }
    quint64 bits = 0;
    for (int i = 0; i < 6; ++i) bits |= quint64(src[2 + i]) << (8 * i);
    for (int i = 0; i < 16; ++i) {
        const int v = pal[(bits >> (3 * i)) & 7];
        out[i] = quint8(isSigned ? qBound(0, v + 128, 255) : v);
    }
}

// ------------------------------------------------------------------ encoders
quint16 to565(const int c[3])
{
    return quint16(((c[0] * 31 + 127) / 255) << 11 | ((c[1] * 63 + 127) / 255) << 5 | ((c[2] * 31 + 127) / 255));
}

void encodeColorBlock(const quint8 px[16][4], quint8 *dst)
{
    int lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
    for (int i = 0; i < 16; ++i)
        for (int c = 0; c < 3; ++c) { lo[c] = std::min(lo[c], int(px[i][c])); hi[c] = std::max(hi[c], int(px[i][c])); }
    quint16 c0 = to565(hi), c1 = to565(lo);
    if (c0 < c1) std::swap(c0, c1);
    quint32 bits = 0;
    if (c0 != c1) {
        int pal[4][3];
        color565(c0, pal[0]);
        color565(c1, pal[1]);
        for (int c = 0; c < 3; ++c) { pal[2][c] = (2 * pal[0][c] + pal[1][c]) / 3; pal[3][c] = (pal[0][c] + 2 * pal[1][c]) / 3; }
        for (int i = 0; i < 16; ++i) {
            int best = 0, bestD = INT_MAX;
            for (int k = 0; k < 4; ++k) {
                int d = 0;
                for (int c = 0; c < 3; ++c) { const int e = pal[k][c] - px[i][c]; d += e * e; }
                if (d < bestD) { bestD = d; best = k; }
            }
            bits |= quint32(best) << (2 * i);
        }
    }
    std::memcpy(dst, &c0, 2);
    std::memcpy(dst + 2, &c1, 2);
    std::memcpy(dst + 4, &bits, 4);
}

void encodeAlphaBlock(const quint8 v[16], quint8 *dst, bool isSigned)
{
    auto val = [&](int i) { return isSigned ? int(v[i]) - 128 : int(v[i]); };
    int lo = 1000, hi = -1000;
    for (int i = 0; i < 16; ++i) { lo = std::min(lo, val(i)); hi = std::max(hi, val(i)); }
    if (isSigned) { lo = std::max(lo, -127); hi = std::max(hi, -127); }
    int a0 = hi, a1 = lo;
    quint64 bits = 0;
    if (a0 == a1) {
        if (a0 < (isSigned ? 127 : 255)) ++a0; else --a1;
    }
    int pal[8];
    pal[0] = a0;
    pal[1] = a1;
    for (int i = 1; i < 7; ++i) pal[i + 1] = ((7 - i) * a0 + i * a1) / 7;
    for (int i = 0; i < 16; ++i) {
        int best = 0, bestD = INT_MAX;
        for (int k = 0; k < 8; ++k) { const int d = std::abs(pal[k] - val(i)); if (d < bestD) { bestD = d; best = k; } }
        bits |= quint64(best) << (3 * i);
    }
    dst[0] = quint8(qint8(a0));
    dst[1] = quint8(qint8(a1));
    if (!isSigned) { dst[0] = quint8(a0); dst[1] = quint8(a1); }
    for (int i = 0; i < 6; ++i) dst[2 + i] = quint8(bits >> (8 * i));
}

struct BitWriter {
    quint8 *p;
    int pos = 0;
    void write(quint32 v, int n)
    {
        for (int i = 0; i < n; ++i, ++pos)
            if ((v >> i) & 1) p[pos >> 3] |= quint8(1 << (pos & 7));
    }
};

// BC7 mode 6: one subset, RGBA 7.7.7.7 + per-endpoint p-bit, 4-bit indices.
void encodeBc7Block(const quint8 px[16][4], quint8 *dst)
{
    int lo[4] = {255, 255, 255, 255}, hi[4] = {0, 0, 0, 0};
    for (int i = 0; i < 16; ++i)
        for (int c = 0; c < 4; ++c) { lo[c] = std::min(lo[c], int(px[i][c])); hi[c] = std::max(hi[c], int(px[i][c])); }
    int q[2][4], p[2];
    const int *src[2] = {lo, hi};
    for (int e = 0; e < 2; ++e) {
        int ones = 0;
        for (int c = 0; c < 4; ++c) ones += src[e][c] & 1;
        p[e] = ones >= 2 ? 1 : 0;
        for (int c = 0; c < 4; ++c) q[e][c] = qBound(0, (src[e][c] - p[e] + 1) / 2, 127);
    }
    int ep[2][4];
    for (int e = 0; e < 2; ++e)
        for (int c = 0; c < 4; ++c) ep[e][c] = (q[e][c] << 1) | p[e];
    int idx[16];
    int axis[4], len2 = 0;
    for (int c = 0; c < 4; ++c) { axis[c] = ep[1][c] - ep[0][c]; len2 += axis[c] * axis[c]; }
    for (int i = 0; i < 16; ++i) {
        if (!len2) { idx[i] = 0; continue; }
        int dot = 0;
        for (int c = 0; c < 4; ++c) dot += (px[i][c] - ep[0][c]) * axis[c];
        const int w = qBound(0, (dot * 64 + len2 / 2) / len2, 64);
        int best = 0, bestD = 1000;
        for (int k = 0; k < 16; ++k) { const int d = std::abs(kWeights4[k] - w); if (d < bestD) { bestD = d; best = k; } }
        idx[i] = best;
    }
    if (idx[0] >= 8) {                          // pixel 0's index MSB is implicit 0: swap the endpoints
        for (int c = 0; c < 4; ++c) std::swap(q[0][c], q[1][c]);
        std::swap(p[0], p[1]);
        for (int &k : idx) k = 15 - k;
    }
    std::memset(dst, 0, 16);
    BitWriter w{dst};
    w.write(1u << 6, 7);
    for (int c = 0; c < 4; ++c) { w.write(quint32(q[0][c]), 7); w.write(quint32(q[1][c]), 7); }
    w.write(quint32(p[0]), 1);
    w.write(quint32(p[1]), 1);
    w.write(quint32(idx[0]), 3);
    for (int i = 1; i < 16; ++i) w.write(quint32(idx[i]), 4);
}

void readBlockPixels(const QByteArray &rgba, int width, int height, int bx, int by, quint8 px[16][4])
{
    const auto *s = reinterpret_cast<const quint8 *>(rgba.constData());
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            const int sx = std::min(bx * 4 + x, width - 1), sy = std::min(by * 4 + y, height - 1);
            std::memcpy(px[y * 4 + x], s + (qsizetype(sy) * width + sx) * 4, 4);
        }
}

} // namespace

int blockBytes(Format f) { return (f == BC1 || f == BC4) ? 8 : 16; }

QByteArray decode(Format f, const QByteArray &blocks, int width, int height, bool signedNorm)
{
    QByteArray out(qsizetype(width) * height * 4, Qt::Uninitialized);
    auto *o = reinterpret_cast<quint8 *>(out.data());
    const int bw = (width + 3) / 4, bh = (height + 3) / 4, bb = blockBytes(f);
    const auto *src = reinterpret_cast<const quint8 *>(blocks.constData());
    const qsizetype avail = blocks.size() / bb;
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            const qsizetype bi = qsizetype(by) * bw + bx;
            quint8 px[16][4];
            if (bi >= avail) std::memset(px, 0, sizeof px);
            else {
                const quint8 *b = src + bi * bb;
                switch (f) {
                case BC1: decodeColorBlock(b, px, false); break;
                case BC2:
                    decodeColorBlock(b + 8, px, true);
                    for (int i = 0; i < 16; ++i) { const int a = (b[i / 2] >> (4 * (i & 1))) & 15; px[i][3] = quint8(a * 17); }
                    break;
                case BC3: {
                    decodeColorBlock(b + 8, px, true);
                    quint8 a[16];
                    decodeAlphaBlock(b, a, false);
                    for (int i = 0; i < 16; ++i) px[i][3] = a[i];
                    break;
                }
                case BC4: {
                    quint8 r[16];
                    decodeAlphaBlock(b, r, signedNorm);
                    for (int i = 0; i < 16; ++i) { px[i][0] = px[i][1] = px[i][2] = r[i]; px[i][3] = 255; }
                    break;
                }
                case BC5: {
                    quint8 r[16], g[16];
                    decodeAlphaBlock(b, r, signedNorm);
                    decodeAlphaBlock(b + 8, g, signedNorm);
                    for (int i = 0; i < 16; ++i) { px[i][0] = r[i]; px[i][1] = g[i]; px[i][2] = 0; px[i][3] = 255; }
                    break;
                }
                case BC7: decodeBc7Block(b, px); break;
                }
            }
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    const int px_ = bx * 4 + x, py = by * 4 + y;
                    if (px_ < width && py < height) std::memcpy(o + (qsizetype(py) * width + px_) * 4, px[y * 4 + x], 4);
                }
        }
    return out;
}

QByteArray encode(Format f, const QByteArray &rgba, int width, int height)
{
    const int bw = (width + 3) / 4, bh = (height + 3) / 4, bb = blockBytes(f);
    QByteArray out(qsizetype(bw) * bh * bb, Qt::Uninitialized);
    auto *o = reinterpret_cast<quint8 *>(out.data());
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            quint8 px[16][4];
            readBlockPixels(rgba, width, height, bx, by, px);
            quint8 *d = o + (qsizetype(by) * bw + bx) * bb;
            switch (f) {
            case BC1: encodeColorBlock(px, d); break;
            case BC2:
                std::memset(d, 0, 8);
                for (int i = 0; i < 16; ++i) d[i / 2] |= quint8(((px[i][3] + 8) / 17) << (4 * (i & 1)));
                encodeColorBlock(px, d + 8);
                break;
            case BC3: {
                quint8 a[16];
                for (int i = 0; i < 16; ++i) a[i] = px[i][3];
                encodeAlphaBlock(a, d, false);
                encodeColorBlock(px, d + 8);
                break;
            }
            case BC4: {
                quint8 r[16];
                for (int i = 0; i < 16; ++i) r[i] = px[i][0];
                encodeAlphaBlock(r, d, false);
                break;
            }
            case BC5: {
                quint8 r[16], g[16];
                for (int i = 0; i < 16; ++i) { r[i] = px[i][0]; g[i] = px[i][1]; }
                encodeAlphaBlock(r, d, false);
                encodeAlphaBlock(g, d + 8, false);
                break;
            }
            case BC7: encodeBc7Block(px, d); break;
            }
        }
    return out;
}

QByteArray halve(const QByteArray &rgba, int width, int height)
{
    const int w = std::max(1, width / 2), h = std::max(1, height / 2);
    QByteArray out(qsizetype(w) * h * 4, Qt::Uninitialized);
    const auto *s = reinterpret_cast<const quint8 *>(rgba.constData());
    auto *o = reinterpret_cast<quint8 *>(out.data());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int x0 = std::min(2 * x, width - 1), x1 = std::min(2 * x + 1, width - 1);
            const int y0 = std::min(2 * y, height - 1), y1 = std::min(2 * y + 1, height - 1);
            for (int c = 0; c < 4; ++c) {
                const int sum = s[(qsizetype(y0) * width + x0) * 4 + c] + s[(qsizetype(y0) * width + x1) * 4 + c]
                              + s[(qsizetype(y1) * width + x0) * 4 + c] + s[(qsizetype(y1) * width + x1) * 4 + c];
                o[(qsizetype(y) * w + x) * 4 + c] = quint8((sum + 2) / 4);
            }
        }
    return out;
}

bool selfTest(QString *report)
{
    QString r;
    bool ok = true;
    for (int p = 0; p < 64; ++p) {
        if (kPartition2[p][0] != 0 || kPartition2[p][kAnchor2[p]] != 1) { ok = false; r += QStringLiteral("partition2 %1 anchor mismatch\n").arg(p); }
        if (kPartition3[p][0] != 0 || kPartition3[p][kAnchor3a[p]] != 1 || kPartition3[p][kAnchor3b[p]] != 2) {
            ok = false;
            r += QStringLiteral("partition3 %1 anchor mismatch\n").arg(p);
        }
    }
    // round trip a smooth gradient through every encoder
    QByteArray img(16 * 16 * 4, 0);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            auto *px = reinterpret_cast<quint8 *>(img.data()) + (y * 16 + x) * 4;
            px[0] = quint8(x * 16); px[1] = quint8(y * 16); px[2] = quint8(128); px[3] = quint8(255 - x * 8);
        }
    for (Format f : {BC1, BC3, BC4, BC5, BC7}) {
        const QByteArray back = decode(f, encode(f, img, 16, 16), 16, 16);
        int worst = 0;
        const int channels = f == BC4 ? 1 : f == BC5 ? 2 : f == BC1 ? 3 : 4;
        for (int i = 0; i < 256; ++i)
            for (int c = 0; c < channels; ++c)
                worst = std::max(worst, std::abs(int(quint8(back[i * 4 + c])) - int(quint8(img[i * 4 + c]))));
        r += QStringLiteral("BC%1 round trip worst error %2\n").arg(f == BC7 ? 7 : int(f) + 1).arg(worst);
        if (worst > 40) ok = false;
    }
    if (report) *report = r;
    return ok;
}

} // namespace re::bcn
