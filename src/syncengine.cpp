#include "syncengine.h"

#include "accountsmanager.h"
#include "networkmonitor.h"
#include "notesdatabase.h"
#include "settings.h"
#include "syncworker.h"

#include <QThread>
#include <QVariant>
#include <QtDebug>

SyncEngine::SyncEngine(NotesDatabase *db,
                       AccountsManager *accounts,
                       NetworkMonitor *network,
                       Settings *settings,
                       QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_accounts(accounts)
    , m_network(network)
    , m_settings(settings)
{
    m_thread = new QThread(this);
    m_worker = new SyncWorker(db->path());
    m_worker->moveToThread(m_thread);

    // Open the worker's DB connection on the worker thread once it starts.
    // The worker is owned by us and deleted explicitly in ~SyncEngine — using
    // deleteLater on a finished thread doesn't fire (event loop is gone).
    connect(m_thread, &QThread::started, m_worker, &SyncWorker::initialize);

    connect(this, &SyncEngine::requestSync, m_worker, &SyncWorker::syncAccount, Qt::QueuedConnection);
    connect(this, &SyncEngine::requestTest, m_worker, &SyncWorker::testAccount, Qt::QueuedConnection);

    connect(m_worker, &SyncWorker::syncStarted, this, &SyncEngine::onSyncStarted);
    connect(m_worker, &SyncWorker::syncFinished, this, &SyncEngine::onSyncFinished);
    connect(m_worker, &SyncWorker::databaseChanged, this, &SyncEngine::databaseChanged);
    connect(m_worker, &SyncWorker::accountTestResult, this, &SyncEngine::onTestResult);

    m_thread->start();

    // Periodic sync. The interval setting is in minutes; 5 minutes default.
    m_timer.setSingleShot(false);
    connect(&m_timer, &QTimer::timeout, this, &SyncEngine::onPeriodicTick);
    connect(m_settings, &Settings::syncIntervalMinutesChanged, this, [this]() {
        m_timer.start(m_settings->syncIntervalMinutes() * 60 * 1000);
    });
    m_timer.start(m_settings->syncIntervalMinutes() * 60 * 1000);
}

SyncEngine::~SyncEngine()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(3000);
    }
    delete m_worker;
}

void SyncEngine::syncNow()
{
    if (m_syncing) return;
    emit requestSync(-1);
}

void SyncEngine::syncAccount(qint64 accountId)
{
    emit requestSync(accountId);
}

void SyncEngine::testAccount(const QVariantMap &accountData, const QString &password)
{
    emit requestTest(accountData, password);
}

void SyncEngine::onSyncStarted(qint64 accountId)
{
    Q_UNUSED(accountId)
    if (!m_syncing) {
        m_syncing = true;
        emit syncingChanged();
    }
    m_status = QStringLiteral("syncing");
    emit statusChanged();
}

void SyncEngine::onSyncFinished(qint64 accountId, bool success, QString error)
{
    Q_UNUSED(accountId)
    m_syncing = false;
    emit syncingChanged();

    if (success) {
        m_status = QStringLiteral("ok");
        m_lastError.clear();
    } else {
        m_status = QStringLiteral("error");
        m_lastError = error;
        emit lastErrorChanged();
    }
    m_lastSync = QDateTime::currentDateTime();
    emit lastSyncChanged();
    emit statusChanged();
    // The worker emits databaseChanged() right after this so the AccountsModel
    // refreshes; nothing more to do here.
}

void SyncEngine::onTestResult(bool success, QString message)
{
    emit accountTestResult(success, message);
}

void SyncEngine::onPeriodicTick()
{
    if (m_syncing) return;
    if (m_network && !m_network->online()) return;
    syncNow();
}
