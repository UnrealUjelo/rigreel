// Block-compressed texture codecs (BC1-BC5, BC7). Decoding feeds thumbnails and mip generation; the tiny
// encoders only produce the small mip levels a game file does not ship (quality there hardly matters).
#pragma once

#include <QByteArray>
#include <QImage>

namespace re::bcn {

enum Format { BC1, BC2, BC3, BC4, BC5, BC7 };

int blockBytes(Format f);
// Decode a whole level to RGBA8888 (width * height * 4). BC4 -> (r, r, r, 255); BC5 -> (r, g, 0, 255).
QByteArray decode(Format f, const QByteArray &blocks, int width, int height, bool signedNorm = false);
QByteArray encode(Format f, const QByteArray &rgba, int width, int height);
// Box-filter halving of an RGBA8 image (odd sizes clamp).
QByteArray halve(const QByteArray &rgba, int width, int height);

bool selfTest(QString *report);

} // namespace re::bcn
