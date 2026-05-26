// Background sync daemon for harbour-imap-notes.
//
// Runs the same SyncWorker as the app, on a wall-clock timer. Started by
// systemd --user (see systemd/harbour-imap-notes-daemon.service). Shares the
// SQLite database with the app via WAL — the app reads on its own connection
// and refreshes its models on the periodic timer.
//
// The daemon is intentionally separate from the app process so syncs keep
// happening when the user has closed the app cover. It is built by
// daemon/daemon.pro and installed as %{_bindir}/harbour-imap-notes-daemon.

#include "syncworker.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QSqlDatabase>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QtDebug>

namespace {

QString defaultDbPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir + QStringLiteral("/notes.db");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName(QStringLiteral("harbour-imap-notes"));
    QCoreApplication::setApplicationName(QStringLiteral("harbour-imap-notes"));

    QCoreApplication app(argc, argv);

    const QString dbPath = defaultDbPath();
    if (!QFile::exists(dbPath)) {
        qInfo() << "Notes database not yet created at" << dbPath
                << "— waiting for the app to initialise it.";
    }

    QThread thread;
    SyncWorker *worker = new SyncWorker(dbPath);
    worker->moveToThread(&thread);
    QObject::connect(&thread, &QThread::started, worker, &SyncWorker::initialize);
    QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
    thread.start();

    auto kickOff = [worker]() {
        QMetaObject::invokeMethod(worker, "syncAccount", Qt::QueuedConnection,
                                  Q_ARG(qint64, -1));
    };

    QTimer timer;
    QSettings store;
    const int minutes = store.value(QStringLiteral("sync/intervalMinutes"), 5).toInt();
    timer.setInterval(qMax(1, minutes) * 60 * 1000);
    QObject::connect(&timer, &QTimer::timeout, &app, kickOff);
    timer.start();

    // Wait a few seconds before the first run so the app has a chance to
    // create the database on a fresh install.
    QTimer::singleShot(5 * 1000, &app, kickOff);

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&thread]() {
        thread.quit();
        thread.wait(3000);
    });

    return app.exec();
}
