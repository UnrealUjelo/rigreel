#include "ThumbnailProvider.h"
#include "GameLibrary.h"
#include "TextureStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QQuickTextureFactory>
#include <QRunnable>
#include <QThread>
#include <QThreadPool>
#include <algorithm>
#include <QUrl>

namespace {
class ThumbResponse : public QQuickImageResponse, public QRunnable {
public:
    ThumbResponse(GameLibrary *games, const QString &id, int size) : m_games(games), m_id(id), m_size(size) { setAutoDelete(false); }

    QQuickTextureFactory *textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(m_image); }
    QString errorString() const override { return m_error; }

    void run() override
    {
        // "re2rt::look:pl0000/0": that game's picture (an id without a game is the Asset Browser's game)
        const int sep = m_id.indexOf(QLatin1String("::"));
        const QString game = sep > 0 ? m_id.left(sep) : (m_games ? m_games->gameId() : QString());
        const QString local = sep > 0 ? m_id.mid(sep + 2) : m_id;
        dir::IGameSource *src = m_games && !game.isEmpty() ? m_games->sourceForId(game) : nullptr;
        if (src) {
            const QString key = QString::fromLatin1(QCryptographicHash::hash((local + QLatin1Char('@') + QString::number(m_size) + QStringLiteral("t1")).toUtf8(),
                                                                             QCryptographicHash::Sha1).toHex().left(24));
            const QString file = TextureStore::cacheRoot() + QStringLiteral("/%1/thumbs/%2/%3.png").arg(game, key.left(2), key);
            if (QFileInfo::exists(file)) m_image.load(file);
            if (m_image.isNull()) {
                m_image = src->thumbnail(local, m_size, &m_error);
                if (!m_image.isNull()) {
                    QDir().mkpath(QFileInfo(file).absolutePath());
                    m_image.save(file);
                }
            }
        }
        if (m_image.isNull()) {                            // nothing to show: a transparent tile keeps the row layout
            m_image = QImage(m_size, m_size, QImage::Format_ARGB32);
            m_image.fill(Qt::transparent);
        }
        emit finished();
    }

private:
    GameLibrary *m_games;
    QString m_id, m_error;
    int m_size;
    QImage m_image;
};

QThreadPool *thumbPool()
{
    static QThreadPool pool;
    static bool init = [] { pool.setMaxThreadCount(std::max(2, QThread::idealThreadCount() / 2)); return true; }();
    Q_UNUSED(init)
    return &pool;
}
} // namespace

QQuickImageResponse *ThumbnailProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    const int size = requestedSize.isValid() && requestedSize.width() > 0 ? std::clamp(requestedSize.width(), 32, 512) : 128;
    auto *r = new ThumbResponse(m_games, QUrl::fromPercentEncoding(id.toUtf8()), size);
    thumbPool()->start(r);
    return r;
}
