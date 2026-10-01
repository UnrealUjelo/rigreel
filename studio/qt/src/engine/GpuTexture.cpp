#include "GpuTexture.h"
#include "TextureStore.h"

#include <QFileInfo>

#include <QtQuick3DRuntimeRender/ssg/qssgrendercontextcore.h>
#include <QtQuick3DRuntimeRender/ssg/qssgrenderextensions.h>
#include <QtQuick3DRuntimeRender/ssg/qssgrenderhelpers.h>
#include <QtQuick3DRuntimeRender/ssg/qssgrhicontext.h>
#include <rhi/qrhi.h>

namespace {
QRhiTexture::Format rhiFormat(dir::TextureAsset::Format f)
{
    using F = dir::TextureAsset;
    switch (f) {
    case F::BC1: return QRhiTexture::BC1;
    case F::BC2: return QRhiTexture::BC2;
    case F::BC3: return QRhiTexture::BC3;
    case F::BC4: return QRhiTexture::BC4;
    case F::BC5: return QRhiTexture::BC5;
    case F::BC6H: return QRhiTexture::BC6H;
    case F::BC7: return QRhiTexture::BC7;
    case F::RGBA8: return QRhiTexture::RGBA8;
    case F::BGRA8: return QRhiTexture::BGRA8;
    case F::R8: return QRhiTexture::R8;
    case F::RG8: return QRhiTexture::RG8;
    case F::RGBA16F: return QRhiTexture::RGBA16F;
    default: return QRhiTexture::UnknownFormat;
    }
}

class Backend : public QSSGRenderExtension {
public:
    // the id Qt looks render results up by is the address of this node (see getExtensionId in qssgrendergraphobject_p.h);
    // asking the frontend while this node is being created would still return 0
    explicit Backend(dir::TexturePtr data) : m_data(std::move(data)), m_id(QSSGExtensionId{quintptr(static_cast<QSSGRenderGraphObject *>(this))}) {}
    ~Backend() override
    {
        if (m_texture) m_texture->deleteLater();
    }

    bool prepareData(QSSGFrameData &frame) override
    {
        if (!m_texture && m_data && !m_failed) {
            QRhi *rhi = frame.contextInterface()->rhiContext()->rhi();
            const QRhiTexture::Format fmt = rhiFormat(m_data->format);
            if (fmt == QRhiTexture::UnknownFormat || !rhi->isTextureFormatSupported(fmt)) { m_failed = true; return false; }
            QRhiTexture::Flags flags;
            if (m_data->mips.size() > 1) flags |= QRhiTexture::MipMapped;
            if (m_data->srgb) flags |= QRhiTexture::sRGB;
            m_texture = rhi->newTexture(fmt, QSize(m_data->width, m_data->height), 1, flags);
            if (!m_texture->create()) { delete m_texture; m_texture = nullptr; m_failed = true; return false; }
            // a mip-mapped texture needs every level: repeat the smallest one if the file stops early
            const int levels = m_data->mips.size() > 1 ? rhi->mipLevelsForSize(QSize(m_data->width, m_data->height)) : 1;
            QList<QRhiTextureUploadEntry> entries;
            for (int l = 0; l < levels; ++l) {
                const QByteArray &bytes = m_data->mips.value(std::min(l, int(m_data->mips.size()) - 1));
                QRhiTextureSubresourceUploadDescription d(bytes);
                d.setSourceSize(QSize(std::max(1, m_data->width >> l), std::max(1, m_data->height >> l)));
                entries << QRhiTextureUploadEntry(0, l, d);
            }
            QRhiTextureUploadDescription desc;
            desc.setEntries(entries.cbegin(), entries.cend());
            QRhiResourceUpdateBatch *u = rhi->nextResourceUpdateBatch();
            u->uploadTexture(m_texture, desc);
            frame.contextInterface()->rhiContext()->commandBuffer()->resourceUpdate(u);
            m_data.reset();                          // the GPU copy is all we need now
        }
        if (m_texture) QSSGRenderExtensionHelpers::registerRenderResult(frame, m_id, m_texture);
        return m_texture != nullptr;
    }
    void prepareRender(QSSGFrameData &) override {}
    void render(QSSGFrameData &) override {}
    void resetForFrame() override {}
    RenderMode mode() const override { return RenderMode::Standalone; }
    RenderStage stage() const override { return RenderStage::PreColor; }

private:
    dir::TexturePtr m_data;
    QSSGExtensionId m_id;
    QRhiTexture *m_texture = nullptr;
    bool m_failed = false;
};
} // namespace

GpuTexture::GpuTexture(dir::TexturePtr data, QQuick3DObject *parent) : QQuick3DRenderExtension(parent), m_data(std::move(data))
{
    if (m_data && m_data->path.endsWith(QLatin1String(".ktx"), Qt::CaseInsensitive) && QFileInfo::exists(m_data->path)) m_file = m_data->path;
}

QSSGRenderGraphObject *GpuTexture::updateSpatialNode(QSSGRenderGraphObject *node)
{
    if (!node) {
        if (!m_data && !m_file.isEmpty()) m_data = TextureStore::readKtx(m_file);
        node = new Backend(m_data);
        if (!m_file.isEmpty()) m_data.reset();       // the backend uploads it and lets go; the file can bring it back
    }
    return node;
}
