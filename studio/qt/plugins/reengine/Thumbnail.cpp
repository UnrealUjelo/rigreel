#include "Thumbnail.h"

#include <QHash>
#include <QMatrix4x4>
#include <algorithm>
#include <cmath>

namespace re {

namespace {
struct Tex {
    QByteArray rgba;
    int w = 0, h = 0;
    QVector3D sample(float u, float v, float *alpha) const
    {
        if (w <= 0 || h <= 0) { *alpha = 1; return QVector3D(0.72f, 0.72f, 0.72f); }
        u -= std::floor(u);
        v -= std::floor(v);
        const int x = std::clamp(int(u * w), 0, w - 1), y = std::clamp(int(v * h), 0, h - 1);
        const uchar *p = reinterpret_cast<const uchar *>(rgba.constData()) + (qsizetype(y) * w + x) * 4;
        *alpha = p[3] / 255.f;
        return QVector3D(p[0], p[1], p[2]) / 255.f;
    }
};

float edge(const QVector3D &a, const QVector3D &b, float x, float y) { return (b.x() - a.x()) * (y - a.y()) - (b.y() - a.y()) * (x - a.x()); }
} // namespace

QImage renderThumbnail(const dir::ModelAsset &model, const ThumbTexture &texture, int size)
{
    const int S = std::clamp(size, 16, 1024) * 2;                // drawn at twice the size, then halved (anti-aliasing)
    // world bounds of what is drawn
    QVector3D lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
    for (const dir::ModelPiece &p : model.pieces) {
        if (!p.mesh) continue;
        for (const QVector3D &v : p.mesh->positions) {
            const QVector3D w = p.local.map(v);
            lo = QVector3D(std::min(lo.x(), w.x()), std::min(lo.y(), w.y()), std::min(lo.z(), w.z()));
            hi = QVector3D(std::max(hi.x(), w.x()), std::max(hi.y(), w.y()), std::max(hi.z(), w.z()));
        }
    }
    if (lo.x() > hi.x()) return {};
    const QVector3D centre = (lo + hi) * 0.5f;
    const float radius = std::max(0.01f, (hi - lo).length() * 0.5f);
    // camera: front-right, a little above (props face +Z in RE Engine)
    const float yaw = 35.f * 0.0174533f, pitch = 22.f * 0.0174533f;
    const QVector3D dir(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
    QMatrix4x4 view;
    view.lookAt(centre + dir * radius * 3.f, centre, QVector3D(0, 1, 0));
    // orthographic fit of the bounds' corners
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (int i = 0; i < 8; ++i) {
        const QVector3D c = view.map(QVector3D(i & 1 ? hi.x() : lo.x(), i & 2 ? hi.y() : lo.y(), i & 4 ? hi.z() : lo.z()));
        minX = std::min(minX, c.x()); maxX = std::max(maxX, c.x());
        minY = std::min(minY, c.y()); maxY = std::max(maxY, c.y());
    }
    const float span = std::max(maxX - minX, maxY - minY) * 1.08f;
    const float scale = S / std::max(1e-5f, span), ox = (minX + maxX) * 0.5f, oy = (minY + maxY) * 0.5f;
    auto toScreen = [&](const QVector3D &w) {
        const QVector3D v = view.map(w);
        return QVector3D((v.x() - ox) * scale + S * 0.5f, S * 0.5f - (v.y() - oy) * scale, -v.z());
    };
    const QVector3D light = QVector3D(-0.45f, 0.8f, 0.55f).normalized();

    QImage img(S, S, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QVector<float> depth(qsizetype(S) * S, 1e30f);
    QHash<QString, Tex> textures;
    auto lin = [](float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); };
    auto srgb = [](float c) { c = std::clamp(c, 0.f, 1.f); return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f; };

    for (const dir::ModelPiece &p : model.pieces) {
        if (!p.mesh) continue;
        const dir::MeshAsset &mesh = *p.mesh;
        QVector<QVector3D> sv(mesh.vertexCount());
        for (int i = 0; i < mesh.vertexCount(); ++i) sv[i] = toScreen(p.local.map(mesh.positions[i]));
        for (const dir::MeshPart &part : mesh.parts) {
            if (p.hiddenGroups.contains(part.group)) continue;
            const dir::MaterialAsset *mat = part.material >= 0 && part.material < p.materials.size() ? &p.materials[part.material] : nullptr;
            if (mat && (mat->hidden || mat->transparent || mat->shading == QLatin1String("multiply"))) continue;   // glass, decals, occlusion shells
            Tex *tex = nullptr;
            const QString tp = mat ? mat->textures.value(dir::slot::BaseColor) : QString();
            if (!tp.isEmpty()) {
                auto it = textures.find(tp);
                if (it == textures.end()) {
                    Tex t;
                    t.rgba = texture(tp, &t.w, &t.h);
                    it = textures.insert(tp, t);
                }
                tex = &*it;
            }
            const QVector3D tint = mat ? QVector3D(lin(mat->baseColor.x()), lin(mat->baseColor.y()), lin(mat->baseColor.z())) * mat->baseIntensity : QVector3D(1, 1, 1);
            const QVector4D uvT = mat ? mat->baseUv : QVector4D(1, 1, 0, 0);
            const bool cutout = mat && (mat->alphaTest || mat->baseAlpha == QLatin1String("alpha"));
            for (int t = part.indexOffset; t + 2 < part.indexOffset + part.indexCount; t += 3) {
                const quint32 i0 = mesh.indices.value(t), i1 = mesh.indices.value(t + 1), i2 = mesh.indices.value(t + 2);
                if (i0 >= quint32(sv.size()) || i1 >= quint32(sv.size()) || i2 >= quint32(sv.size())) continue;
                const QVector3D a = sv[i0], b = sv[i1], c = sv[i2];
                const float area = edge(a, b, c.x(), c.y());
                if (std::abs(area) < 1e-6f) continue;
                const int x0 = std::max(0, int(std::floor(std::min({a.x(), b.x(), c.x()})))), x1 = std::min(S - 1, int(std::ceil(std::max({a.x(), b.x(), c.x()}))));
                const int y0 = std::max(0, int(std::floor(std::min({a.y(), b.y(), c.y()})))), y1 = std::min(S - 1, int(std::ceil(std::max({a.y(), b.y(), c.y()}))));
                if (x0 > x1 || y0 > y1) continue;
                // flat normal (smooth ones would need per-pixel interpolation; the difference is invisible this small)
                QVector3D n = mesh.normals.size() > int(i0) ? p.local.mapVector(mesh.normals[i0] + mesh.normals[i1] + mesh.normals[i2]) : QVector3D(0, 1, 0);
                n.normalize();
                const float facing = QVector3D::dotProduct(n, dir) < 0 ? -1.f : 1.f;   // two-sided: light the visible side
                const float shade = 0.38f + 0.72f * std::max(0.f, QVector3D::dotProduct(n * facing, light));
                const QVector2D u0 = mesh.uv0.value(i0), u1 = mesh.uv0.value(i1), u2 = mesh.uv0.value(i2);
                for (int y = y0; y <= y1; ++y) {
                    QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
                    for (int x = x0; x <= x1; ++x) {
                        const float px = x + 0.5f, py = y + 0.5f;
                        float w0 = edge(b, c, px, py) / area, w1 = edge(c, a, px, py) / area, w2 = edge(a, b, px, py) / area;
                        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                        const float z = w0 * a.z() + w1 * b.z() + w2 * c.z();
                        float &d = depth[qsizetype(y) * S + x];
                        if (z >= d) continue;
                        const QVector2D uv = u0 * w0 + u1 * w1 + u2 * w2;
                        float alpha = 1;
                        QVector3D col = tex ? tex->sample(uv.x() * uvT.x() + uvT.z(), uv.y() * uvT.y() + uvT.w(), &alpha) : QVector3D(0.72f, 0.72f, 0.72f);
                        if (cutout && alpha < 0.5f) continue;
                        d = z;
                        col = QVector3D(lin(col.x()), lin(col.y()), lin(col.z())) * tint * shade;
                        line[x] = qRgba(int(srgb(col.x()) * 255), int(srgb(col.y()) * 255), int(srgb(col.z()) * 255), 255);
                    }
                }
            }
        }
    }
    return img.scaled(S / 2, S / 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

} // namespace re
