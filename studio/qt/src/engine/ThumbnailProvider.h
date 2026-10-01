// image://thumb/<model id> — browser thumbnails of the open game's models, drawn by its plugin on worker threads
// and kept as PNG files in the cache (one per model id and size).
#pragma once

#include <QQuickAsyncImageProvider>

class GameLibrary;

class ThumbnailProvider : public QQuickAsyncImageProvider {
public:
    explicit ThumbnailProvider(GameLibrary *games) : m_games(games) {}
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

private:
    GameLibrary *m_games;
};
