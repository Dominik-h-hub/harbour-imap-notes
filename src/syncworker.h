#ifndef SYNCWORKER_H
#define SYNCWORKER_H

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVariantMap>

class ImapClient;

// Runs on the SyncEngine's worker thread. Opens its own SQLite connection
// (separate from the main-thread NotesDatabase connection — WAL keeps reads
// non-blocking) and implements the full sync algorithm: folder reconciliation,
// tombstone push with edit-wins-over-delete, dirty-note push, remote pull,
// trash cleanup.
class SyncWorker : public QObject
{
    Q_OBJECT

public:
    explicit SyncWorker(const QString &dbPath, QObject *parent = nullptr);
    ~SyncWorker() override;

public slots:
    void initialize();
    void syncAccount(qint64 accountId);
    void testAccount(QVariantMap data, QString password);

signals:
    void syncStarted(qint64 accountId);
    void syncFinished(qint64 accountId, bool success, QString error);
    void databaseChanged();
    void accountTestResult(bool success, QString message);

private:
    struct AccountRow {
        qint64 id = -1;
        QString displayName;
        QString host;
        int port = 993;
        QString security;
        QString username;
        QString password;
        QString notesRoot;
        QString hierarchyDelimiter;
    };

    bool loadAccount(qint64 accountId, AccountRow *out);
    QList<qint64> allAccountIds();

    bool runOne(qint64 accountId);
    bool reconcileFolders(ImapClient &c, const AccountRow &acc);
    bool ensureTrashFolder(ImapClient &c, const AccountRow &acc);
    bool pushTombstones(ImapClient &c, const AccountRow &acc);
    bool pushDirtyNotes(ImapClient &c, const AccountRow &acc);
    bool pullFolder(ImapClient &c, const AccountRow &acc, qint64 folderId, const QString &fullPath);
    void runTrashCleanup(const AccountRow &acc);

    void markAccountStatus(qint64 accountId,
                           const QString &status,
                           const QString &error = QString());

    QString m_dbPath;
    QString m_connectionName;
    QSqlDatabase m_db;
    bool m_initialized = false;
};

#endif
