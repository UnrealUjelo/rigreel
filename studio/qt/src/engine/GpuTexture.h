// A game texture uploaded to the GPU as is: native block format (BC1-BC7), every mip level, sRGB where the game
// says so. Plugged into materials through Texture.textureProvider (Qt Quick 3D render extension), because Qt's
// own file loaders drop mip levels or reject block formats on Direct3D.
#pragma once

#include <director/Assets.h>
#include <QtQuick3D/QQuick3DRenderExtension>

class GpuTexture : public QQuick3DRenderExtension {
    Q_OBJECT
public:
    explicit GpuTexture(dir::TexturePtr data, QQuick3DObject *parent = nullptr);
    // the CPU copy is dropped once the renderer has it; a recreated render node reloads the cache file
    dir::TexturePtr data() const { return m_data; }

protected:
    QSSGRenderGraphObject *updateSpatialNode(QSSGRenderGraphObject *node) override;

private:
    dir::TexturePtr m_data;
    QString m_file;                    // KTX cache file the data came from ("" = keep the data)
};
