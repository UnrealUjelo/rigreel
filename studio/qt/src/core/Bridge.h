// File bridge to the Lua runtime (reframework/data/director/bridge):
//   cmd.json   <- we write {seq, cmds:[...]}: every command not yet acknowledged, each with an envelope id "cid"
//   state.json -> the runtime writes it every few frames (small): selection, sequence, actors, cameras ...
//   data.json  -> the runtime writes it when something big changed (data_ver): catalog, cast, joints, stages ...
// The runtime acks the highest cid it ran; acked commands leave the pending list.
#pragma once

#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QTimer>
#include <atomic>

class Bridge : public QObject
{
    Q_OBJECT
public:
    explicit Bridge(const QString &dir, QObject *parent = nullptr);

    // thread-safe: the render worker sends from its own thread
    qint64 send(const QJsonObject &cmd);

    QJsonObject state() const;       // normalized (lists are always arrays)
    QJsonObject data() const;
    QByteArray rawState() const;
    bool connected() const;          // a state arrived in the last 2 s
    int pending() const;
    int errors() const { return m_errors; }
    qint64 dataVersion() const { return m_dataVer; }
    QString dir() const { return m_dir; }

    // the host tells the runtime whether the game window has keyboard focus (fly / edit keys only work then)
    void setGameFocused(bool focused);

    static QJsonObject normalizeState(const QJsonObject &s);
    static QJsonObject normalizeData(const QJsonObject &d);

signals:
    void stateChanged();
    void dataChanged();
    void connectionChanged(bool connected);

private:
    void poll();
    void ping();
    void writeCommands();

    QString m_dir;
    mutable QMutex m_mutex;
    QJsonObject m_state, m_data;
    QByteArray m_rawState;
    QList<QJsonObject> m_pending;
    qint64 m_nextId = 0;
    qint64 m_dataVer = -1;
    qint64 m_lastStateMs = 0;
    qint64 m_stateMtime = 0;
    int m_errors = 0;
    bool m_wasConnected = false;
    std::atomic<bool> m_gameFocused{false};
    std::atomic<bool> m_writeFailed{false};
    bool m_sentFocused = false;
    qint64 m_lastPingMs = 0;
    QTimer m_pollTimer, m_pingTimer;
};
