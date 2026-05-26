#ifndef SYNCENGINE_H
#define SYNCENGINE_H

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

class AccountsManager;
class NetworkMonitor;
class NotesDatabase;
class Settings;
class SyncWorker;
class QThread;

// QML-facing facade. Owns the worker thread that runs SyncWorker (which
// performs the actual IMAP I/O against its own SQLite connection). Slots
// callable from QML are forwarded to the worker via queued signals.
class SyncEngine : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QDateTime lastSync READ lastSync NOTIFY lastSyncChanged)
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    SyncEngine(NotesDatabase *db,
               AccountsManager *accounts,
               NetworkMonitor *network,
               Settings *settings,
               QObject *parent = nullptr);
    ~SyncEngine() override;

    QString status() const { return m_status; }
    QDateTime lastSync() const { return m_lastSync; }
    bool syncing() const { return m_syncing; }
    QString lastError() const { return m_lastError; }

public slots:
    void syncNow();
    void syncAccount(qint64 accountId);
    void testAccount(const QVariantMap &accountData, const QString &password);

signals:
    void statusChanged();
    void lastSyncChanged();
    void syncingChanged();
    void lastErrorChanged();
    void accountTestResult(bool success, const QString &message);
    void databaseChanged();

    // Internal — connected to the worker via QueuedConnection.
    void requestSync(qint64 accountId);
    void requestTest(QVariantMap data, QString password);

private slots:
    void onSyncStarted(qint64 accountId);
    void onSyncFinished(qint64 accountId, bool success, QString error);
    void onTestResult(bool success, QString message);
    void onPeriodicTick();

private:
    NotesDatabase *m_db;
    AccountsManager *m_accounts;
    NetworkMonitor *m_network;
    Settings *m_settings;

    QThread *m_thread = nullptr;
    SyncWorker *m_worker = nullptr;
    QTimer m_timer;

    QString m_status = QStringLiteral("idle");
    QDateTime m_lastSync;
    bool m_syncing = false;
    QString m_lastError;
};

#endif
