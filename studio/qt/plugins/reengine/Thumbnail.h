// Browser thumbnails without a GPU: a model drawn by a small software rasteriser (z-buffer, colour textures at a
// low mip, one key light), seen from the front-right and a little above, on a transparent background.
#pragma once

#include <director/Assets.h>
#include <QImage>
#include <functional>

namespace re {

// texture(path) returns RGBA8 of a small level (w x h), or an empty array
using ThumbTexture = std::function<QByteArray(const QString &path, int *w, int *h)>;

QImage renderThumbnail(const dir::ModelAsset &model, const ThumbTexture &texture, int size);

} // namespace re
