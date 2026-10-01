// dirview: renders one model from a game through the plugin, playing an animation.
//   dirview [--look <model id>] [--anim <motlist>] [--clip <name part>] [--shot <png> --after <s>] [--yaw <deg>]
//           [--game <key part, e.g. re2>] [--stage <stage id>] [--distance <m>] [--pitch <deg>]
#include "GameLibrary.h"
#include "ModelPrep.h"
#include "Pose.h"
#include "SceneActor.h"
#include "StageSet.h"

#include <QMutex>

#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlListReference>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
#include <QtConcurrent>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("Director"));
    app.setApplicationName(QStringLiteral("DirectorStudio"));
    QCommandLineParser cli;
    cli.addOptions({{QStringLiteral("look"), QStringLiteral("model id"), QStringLiteral("id"), QStringLiteral("look:ch0a0z0/0")},
                    {QStringLiteral("anim"), QStringLiteral("motion list"), QStringLiteral("path")},
                    {QStringLiteral("clip"), QStringLiteral("clip name part"), QStringLiteral("text"), QStringLiteral("stand_loop")},
                    {QStringLiteral("shot"), QStringLiteral("save a screenshot"), QStringLiteral("png")},
                    {QStringLiteral("after"), QStringLiteral("seconds before the shot"), QStringLiteral("s"), QStringLiteral("6")},
                    {QStringLiteral("yaw"), QStringLiteral("camera yaw"), QStringLiteral("deg"), QStringLiteral("20")},
                    {QStringLiteral("frame"), QStringLiteral("hold this frame"), QStringLiteral("f")},
                    {QStringLiteral("game"), QStringLiteral("the game whose key contains this"), QStringLiteral("text")},
                    {QStringLiteral("stage"), QStringLiteral("load a map instead of a model"), QStringLiteral("id")},
                    {QStringLiteral("distance"), QStringLiteral("camera distance"), QStringLiteral("m")},
                    {QStringLiteral("pitch"), QStringLiteral("camera pitch"), QStringLiteral("deg")}});
    cli.process(app);

    SceneActor::setMaterialUrl(QUrl(qEnvironmentVariableIsSet("DIRVIEW_TESTMAT") ? QStringLiteral("qrc:/qt/qml/DirView/qml/scene/TestMaterial.qml") : QStringLiteral("qrc:/qt/qml/DirView/qml/scene/ReMaterial.qml")));
    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/DirView/tools/dirview/Main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;
    QObject *win = engine.rootObjects().first();
    win->setProperty("yaw", cli.value(QStringLiteral("yaw")).toDouble());
    if (cli.isSet(QStringLiteral("distance"))) win->setProperty("distance", cli.value(QStringLiteral("distance")).toDouble());
    if (cli.isSet(QStringLiteral("pitch"))) win->setProperty("pitch", cli.value(QStringLiteral("pitch")).toDouble());
    auto info = [win](const QString &s) { win->setProperty("info", s); qInfo().noquote() << s; };

    GameLibrary lib;
    lib.loadPlugins(QCoreApplication::applicationDirPath() + QStringLiteral("/plugins/games"));
    lib.refresh();
    const QVariantList games = lib.gamesJs();
    QVariantMap game = games.value(0).toMap();
    for (const QVariant &g : games)
        if (cli.isSet(QStringLiteral("game")) && g.toMap().value(QStringLiteral("key")).toString().contains(cli.value(QStringLiteral("game")), Qt::CaseInsensitive)) { game = g.toMap(); break; }
    if (games.isEmpty()) { info(QStringLiteral("no supported game installed")); }
    else lib.activate(game.value(QStringLiteral("key")).toString());
    info(QStringLiteral("opening %1 ...").arg(game.value(QStringLiteral("title")).toString()));

    // --stage: a map, the way the Studio loads one (models on worker threads, one texture table)
    const QString stageId = cli.value(QStringLiteral("stage"));
    auto *stageWatcher = new QFutureWatcher<PreparedStage>(&app);
    QObject::connect(stageWatcher, &QFutureWatcherBase::finished, &app, [&]() {
        const PreparedStage ps = stageWatcher->result();
        if (!ps.stage) { info(QStringLiteral("map failed: %1").arg(ps.warnings.join(QStringLiteral("; ")))); return; }
        auto *root = win->findChild<QQuick3DNode *>(QStringLiteral("stage"));
        auto *set = new StageSet;
        set->setParent(root);
        set->setParentItem(root);
        QString err;
        if (!set->build(ps, &engine, &err)) { info(QStringLiteral("map build failed: %1").arg(err)); return; }
        QQmlListReference ext(win->findChild<QObject *>(QStringLiteral("view")), "extensions");
        for (QQuick3DObject *e : set->extensions()) ext.append(e);
        QVector<float> xs, ys, zs;
        for (const dir::StageInstance &si : ps.stage->instances) {
            const QVector3D p = si.world.column(3).toVector3D();
            xs << p.x(); ys << p.y(); zs << p.z();
        }
        auto median = [](QVector<float> &v) { std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end()); return v[v.size() / 2]; };
        if (!xs.isEmpty()) win->setProperty("target", QVector3D(median(xs), median(ys) + 1.2f, median(zs)));
        info(QStringLiteral("%1: %2 placements, %3 meshes, %4 textures").arg(ps.stage->name).arg(set->placements()).arg(ps.models.size()).arg(ps.textures.size()));
    });
    if (!stageId.isEmpty())
        QObject::connect(&lib, &GameLibrary::activeChanged, &app, [&]() {
            if (!lib.ready()) return;
            dir::IGameSource *src = lib.source();
            const QString gameId = lib.gameId();
            info(QStringLiteral("loading the map %1 ...").arg(stageId));
            stageWatcher->setFuture(QtConcurrent::run([src, gameId, stageId]() {
                PreparedStage ps;
                QString err;
                ps.stage = src->loadStage(stageId, &err);
                if (!ps.stage) { ps.warnings << err; return ps; }
                QStringList ids;
                QSet<QString> seen;
                for (const dir::StageInstance &si : ps.stage->instances) if (!seen.contains(si.model)) { seen.insert(si.model); ids << si.model; }
                TextureShare share;
                QMutex mutex;
                QtConcurrent::blockingMap(ids, [&](QString &id) {
                    QString e;
                    PreparedModel pm;
                    if (const dir::ModelPtr m = src->loadModel(id, &e)) pm = prepareModel(m, src, gameId, 1024, &share);
                    pm.textures.clear();
                    QMutexLocker lock(&mutex);
                    if (pm.model) ps.models.insert(id, pm);
                });
                ps.textures = share.textures;
                return ps;
            }));
        });

    struct Loaded { PreparedModel pm; dir::AnimationSetPtr anims; QString error; qint64 ms = 0; };
    auto *watcher = new QFutureWatcher<Loaded>(&app);
    SceneActor *actor = nullptr;
    auto binding = std::make_shared<ClipBinding>();
    QElapsedTimer clock;
    const QString lookId = cli.value(QStringLiteral("look"));

    QObject::connect(&lib, &GameLibrary::activeChanged, &app, [&]() {
        if (!lib.ready()) { info(lib.status()); return; }
        if (!stageId.isEmpty() && !cli.isSet(QStringLiteral("look"))) return;      // a map alone
        info(QStringLiteral("loading %1 ...").arg(lookId));
        dir::IGameSource *src = lib.source();
        const QString gameId = lib.gameId();
        QString anim = cli.value(QStringLiteral("anim"));
        watcher->setFuture(QtConcurrent::run([src, gameId, lookId, anim]() mutable {
            Loaded l;
            QElapsedTimer t;
            t.start();
            dir::ModelPtr model = src->loadModel(lookId, &l.error);
            if (!model) return l;
            l.pm = prepareModel(model, src, gameId, 2048);
            if (anim.isEmpty()) anim = model->info.value(QStringLiteral("general")).toString();
            if (!anim.isEmpty()) l.anims = src->loadAnimations(anim, &l.error);
            l.ms = t.elapsed();
            return l;
        }));
    });
    QObject::connect(watcher, &QFutureWatcherBase::finished, &app, [&]() {
        const Loaded l = watcher->result();
        if (!l.pm.model) { info(QStringLiteral("failed: %1").arg(l.error)); return; }
        auto *stage = win->findChild<QQuick3DNode *>(QStringLiteral("stage"));
        actor = new SceneActor;
        actor->setParent(stage);
        actor->setParentItem(stage);
        QString err;
        if (!actor->build(l.pm, &engine, &err)) { info(QStringLiteral("build failed: %1").arg(err)); return; }
        QQmlListReference ext(win->findChild<QObject *>(QStringLiteral("view")), "extensions");
        for (QQuick3DObject *e : actor->extensions()) ext.append(e);
        QString clipName;
        if (l.anims) {
            const QString want = cli.value(QStringLiteral("clip"));
            const dir::AnimationClip *pick = nullptr;
            for (const auto &c : l.anims->clips) if (c.name.contains(want, Qt::CaseInsensitive) && c.frames > 30) { pick = &c; break; }
            if (!pick && !l.anims->clips.isEmpty()) pick = &l.anims->clips.first();
            static dir::AnimationSetPtr keep;
            keep = l.anims;
            *binding = ClipBinding(pick, actor->skeleton());
            clipName = pick ? QStringLiteral("%1 (%2 frames, %3/%4 tracks bound)").arg(pick->name).arg(pick->frames).arg(binding->boundTracks()).arg(pick->tracks.size()) : QString();
        }
        const QVector3D c = (actor->boundsMin() + actor->boundsMax()) / 2;
        win->setProperty("target", QVector3D(0, c.y(), 0));
        info(QStringLiteral("%1 — %2 pieces, %3 bones, loaded in %4 ms%5\n%6").arg(l.pm.model->name).arg(l.pm.pieces.size()).arg(actor->boneCount()).arg(l.ms)
                 .arg(l.pm.warnings.isEmpty() ? QString() : QStringLiteral(", %1 warnings").arg(l.pm.warnings.size()), clipName));
        for (const QString &w : l.pm.warnings.mid(0, 5)) qWarning().noquote() << w;
        clock.start();
    });
    QTimer tick;
    const bool hold = cli.isSet(QStringLiteral("frame"));
    const float holdFrame = cli.value(QStringLiteral("frame")).toFloat();
    QObject::connect(&tick, &QTimer::timeout, &app, [&]() {
        if (!actor || !binding->isValid()) return;
        Pose p = actor->restPose();
        const float f = hold ? holdFrame : std::fmod(clock.elapsed() / 1000.f * 60.f, std::max(1.f, binding->frames()));
        binding->sample(f, p);
        actor->setPose(p);
    });
    tick.start(16);

    if (cli.isSet(QStringLiteral("shot"))) {
        const QString path = cli.value(QStringLiteral("shot"));
        QTimer::singleShot(int(cli.value(QStringLiteral("after")).toDouble() * 1000), &app, [&, path]() {
            auto *w = qobject_cast<QQuickWindow *>(win);
            w->grabWindow().save(path);
            qInfo().noquote() << "saved" << path;
            app.quit();
        });
    }
    return app.exec();
}
