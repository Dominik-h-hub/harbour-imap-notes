#include "syncworker.h"

#include "imapclient.h"
#include "note.h"
#include "notemessage.h"

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QSettings>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>
#include <QVariant>
#include <QtDebug>

namespace {

QString currentConnectionName()
{
    return QStringLiteral("sync-%1")
        .arg(reinterpret_cast<quintptr>(QThread::currentThread()), 0, 16);
}

QString leafName(const QString &fullPath, const QString &delimiter)
{
    const int idx = fullPath.lastIndexOf(delimiter);
    return idx < 0 ? fullPath : fullPath.mid(idx + delimiter.size());
}

QString parentPath(const QString &fullPath, const QString &delimiter)
{
    const int idx = fullPath.lastIndexOf(delimiter);
    return idx < 0 ? QString() : fullPath.left(idx);
}

bool looksLikeTrashName(const QString &leaf)
{
    static const QStringList markers = {
        QStringLiteral("Deleted Messages"),
        QStringLiteral("Trash"),
        QStringLiteral("Papierkorb"),
        QStringLiteral("Gelöschte Objekte"),
    };
    for (const QString &m : markers) {
        if (leaf.compare(m, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

} // namespace

SyncWorker::SyncWorker(const QString &dbPath, QObject *parent)
    : QObject(parent)
    , m_dbPath(dbPath)
{
}

SyncWorker::~SyncWorker()
{
    if (m_db.isOpen()) m_db.close();
    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

void SyncWorker::initialize()
{
    if (m_initialized) return;
    m_connectionName = currentConnectionName();
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(m_dbPath);
    if (!m_db.open()) {
        qWarning() << "SyncWorker: cannot open" << m_dbPath << m_db.lastError().text();
        return;
    }
    QSqlQuery pragma(m_db);
    pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    m_initialized = true;
}

QList<qint64> SyncWorker::allAccountIds()
{
    QList<qint64> ids;
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT id FROM accounts ORDER BY id"))) {
        while (q.next()) ids.append(q.value(0).toLongLong());
    }
    return ids;
}

bool SyncWorker::loadAccount(qint64 accountId, AccountRow *out)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, display_name, imap_host, imap_port, security, username,"
        " password_blob, notes_root, hierarchy_delimiter "
        "FROM accounts WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), accountId);
    if (!q.exec() || !q.next()) {
        return false;
    }
    out->id = q.value(0).toLongLong();
    out->displayName = q.value(1).toString();
    out->host = q.value(2).toString();
    out->port = q.value(3).toInt();
    out->security = q.value(4).toString();
    out->username = q.value(5).toString();
    const QByteArray blob = q.value(6).toByteArray();
    out->password = QString::fromUtf8(QByteArray::fromBase64(blob));
    out->notesRoot = q.value(7).toString();
    if (out->notesRoot.isEmpty()) out->notesRoot = QStringLiteral("Notes");
    out->hierarchyDelimiter = q.value(8).toString();
    if (out->hierarchyDelimiter.isEmpty()) out->hierarchyDelimiter = QStringLiteral("/");
    return true;
}

void SyncWorker::markAccountStatus(qint64 accountId,
                                   const QString &status,
                                   const QString &error)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE accounts SET last_sync = :ts, sync_status = :st, last_error = :err"
        " WHERE id = :id"));
    q.bindValue(QStringLiteral(":ts"), QDateTime::currentSecsSinceEpoch());
    q.bindValue(QStringLiteral(":st"), status);
    q.bindValue(QStringLiteral(":err"), error);
    q.bindValue(QStringLiteral(":id"), accountId);
    q.exec();
}

void SyncWorker::syncAccount(qint64 accountId)
{
    if (!m_initialized) initialize();
    if (!m_initialized) return;

    const QList<qint64> targets = accountId < 0 ? allAccountIds() : QList<qint64>{ accountId };
    for (qint64 id : targets) {
        emit syncStarted(id);
        const bool ok = runOne(id);
        if (ok) {
            markAccountStatus(id, QStringLiteral("ok"));
        }
        emit syncFinished(id, ok, QString());
    }
    emit databaseChanged();
}

bool SyncWorker::runOne(qint64 accountId)
{
    AccountRow acc;
    if (!loadAccount(accountId, &acc)) {
        markAccountStatus(accountId, QStringLiteral("error"), tr("Account not found"));
        return false;
    }

    ImapClient client;
    const ImapClient::Security security =
        acc.security == QStringLiteral("starttls")
            ? ImapClient::StartTls : ImapClient::ImplicitSsl;
    if (!client.connectToHost(acc.host, acc.port, security)) {
        markAccountStatus(accountId, QStringLiteral("error"), client.lastError());
        return false;
    }
    if (!client.login(acc.username, acc.password)) {
        markAccountStatus(accountId, QStringLiteral("error"), client.lastError());
        return false;
    }

    if (!reconcileFolders(client, acc)) {
        markAccountStatus(accountId, QStringLiteral("error"), client.lastError());
        return false;
    }
    if (!ensureTrashFolder(client, acc)) {
        // Non-fatal: trash will be created the next time the user moves a
        // note. Just log and keep syncing.
        qWarning() << "ensureTrashFolder failed for" << acc.displayName;
    }
    if (!pushTombstones(client, acc)) {
        markAccountStatus(accountId, QStringLiteral("warning"), client.lastError());
    }
    if (!pushDirtyNotes(client, acc)) {
        markAccountStatus(accountId, QStringLiteral("warning"), client.lastError());
    }

    // Pull every selectable folder one by one. A future iteration with
    // CONDSTORE/QRESYNC will replace this with a single incremental query.
    QSqlQuery folders(m_db);
    folders.prepare(QStringLiteral(
        "SELECT id, full_path FROM folders WHERE account_id = :a"));
    folders.bindValue(QStringLiteral(":a"), acc.id);
    folders.exec();
    QList<QPair<qint64, QString>> folderList;
    while (folders.next()) {
        folderList.append({ folders.value(0).toLongLong(), folders.value(1).toString() });
    }
    for (const auto &f : folderList) {
        if (!pullFolder(client, acc, f.first, f.second)) {
            qWarning() << "pullFolder failed for" << f.second << client.lastError();
        }
    }

    runTrashCleanup(acc);
    client.disconnectFromHost();
    return true;
}

bool SyncWorker::reconcileFolders(ImapClient &c, const AccountRow &acc)
{
    QList<ImapClient::FolderInfo> serverFolders;
    const QString pattern = acc.notesRoot + acc.hierarchyDelimiter + QStringLiteral("*");
    if (!c.listFolders(QString(), acc.notesRoot, &serverFolders)) {
        return false;
    }
    // Pick up subfolders too.
    QList<ImapClient::FolderInfo> children;
    if (!c.listFolders(QString(), pattern, &children)) {
        // Some servers return the subtree only when queried this way; not
        // having subfolders is OK.
    }
    serverFolders.append(children);

    QString delimiter = acc.hierarchyDelimiter;
    if (!serverFolders.isEmpty() && !serverFolders.first().delimiter.isEmpty()) {
        delimiter = serverFolders.first().delimiter;
    }

    // Auto-create the notes root if the server didn't list it. This is the
    // first-run case the spec calls out.
    bool rootSeen = false;
    for (const auto &f : serverFolders) {
        if (f.fullPath == acc.notesRoot) { rootSeen = true; break; }
    }
    if (!rootSeen) {
        if (c.createFolder(acc.notesRoot)) {
            ImapClient::FolderInfo info;
            info.fullPath = acc.notesRoot;
            info.delimiter = delimiter;
            serverFolders.prepend(info);
        }
    }

    QSet<QString> seenPaths;
    for (const auto &f : serverFolders) {
        if (!f.fullPath.startsWith(acc.notesRoot)) continue;
        seenPaths.insert(f.fullPath);
        const QString display = leafName(f.fullPath, delimiter);

        QSqlQuery sel(m_db);
        sel.prepare(QStringLiteral(
            "SELECT id FROM folders WHERE account_id = :a AND full_path = :p"));
        sel.bindValue(QStringLiteral(":a"), acc.id);
        sel.bindValue(QStringLiteral(":p"), f.fullPath);
        if (sel.exec() && sel.next()) continue;

        // Resolve parent id (may be NULL for the root).
        qint64 parentId = -1;
        const QString parent = parentPath(f.fullPath, delimiter);
        if (!parent.isEmpty()) {
            QSqlQuery parentLookup(m_db);
            parentLookup.prepare(QStringLiteral(
                "SELECT id FROM folders WHERE account_id = :a AND full_path = :p"));
            parentLookup.bindValue(QStringLiteral(":a"), acc.id);
            parentLookup.bindValue(QStringLiteral(":p"), parent);
            if (parentLookup.exec() && parentLookup.next()) {
                parentId = parentLookup.value(0).toLongLong();
            }
        }

        const bool isTrash = looksLikeTrashName(display);

        QSqlQuery ins(m_db);
        ins.prepare(QStringLiteral(
            "INSERT INTO folders (account_id, parent_id, full_path, display_name, is_trash)"
            " VALUES (:a, :p, :path, :name, :trash)"));
        ins.bindValue(QStringLiteral(":a"), acc.id);
        ins.bindValue(QStringLiteral(":p"), parentId < 0 ? QVariant() : QVariant(parentId));
        ins.bindValue(QStringLiteral(":path"), f.fullPath);
        ins.bindValue(QStringLiteral(":name"), display);
        ins.bindValue(QStringLiteral(":trash"), isTrash ? 1 : 0);
        ins.exec();
    }

    // Remove DB rows for folders the server no longer has — but keep any
    // folder still holding locally-dirty notes so the next push has a target.
    QSqlQuery existing(m_db);
    existing.prepare(QStringLiteral(
        "SELECT id, full_path FROM folders WHERE account_id = :a"));
    existing.bindValue(QStringLiteral(":a"), acc.id);
    existing.exec();
    while (existing.next()) {
        const qint64 fid = existing.value(0).toLongLong();
        const QString path = existing.value(1).toString();
        if (seenPaths.contains(path)) continue;

        QSqlQuery dirty(m_db);
        dirty.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM notes WHERE folder_id = :f AND locally_dirty = 1"));
        dirty.bindValue(QStringLiteral(":f"), fid);
        if (dirty.exec() && dirty.next() && dirty.value(0).toInt() > 0) continue;

        QSqlQuery del(m_db);
        del.prepare(QStringLiteral("DELETE FROM folders WHERE id = :id"));
        del.bindValue(QStringLiteral(":id"), fid);
        del.exec();
    }

    // Persist the discovered delimiter back to accounts so QML-side folder
    // dialogs build correct paths.
    QSqlQuery up(m_db);
    up.prepare(QStringLiteral("UPDATE accounts SET hierarchy_delimiter = :d WHERE id = :id"));
    up.bindValue(QStringLiteral(":d"), delimiter);
    up.bindValue(QStringLiteral(":id"), acc.id);
    up.exec();
    return true;
}

bool SyncWorker::ensureTrashFolder(ImapClient &c, const AccountRow &acc)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id FROM folders WHERE account_id = :a AND is_trash = 1 LIMIT 1"));
    q.bindValue(QStringLiteral(":a"), acc.id);
    if (q.exec() && q.next()) return true;

    const QString trashPath = acc.notesRoot + acc.hierarchyDelimiter
                              + QStringLiteral("Deleted Messages");
    if (!c.createFolder(trashPath)) {
        // CREATE returns NO if the folder already exists on some servers —
        // ignore and continue.
    }
    return reconcileFolders(c, acc);
}

bool SyncWorker::pushTombstones(ImapClient &c, const AccountRow &acc)
{
    QSqlQuery ts(m_db);
    ts.prepare(QStringLiteral(
        "SELECT uuid, folder_full_path, deleted_at, server_uid "
        "FROM tombstones WHERE account_id = :a"));
    ts.bindValue(QStringLiteral(":a"), acc.id);
    if (!ts.exec()) return false;

    QSqlQuery trashLookup(m_db);
    trashLookup.prepare(QStringLiteral(
        "SELECT full_path FROM folders WHERE account_id = :a AND is_trash = 1 LIMIT 1"));
    trashLookup.bindValue(QStringLiteral(":a"), acc.id);
    QString trashPath;
    if (trashLookup.exec() && trashLookup.next()) {
        trashPath = trashLookup.value(0).toString();
    }

    while (ts.next()) {
        const QString uuid = ts.value(0).toString();
        const QString folderPath = ts.value(1).toString();
        const qint64 deletedAt = ts.value(2).toLongLong();
        const quint32 serverUid = ts.value(3).toUInt();

        ImapClient::SelectResult sel;
        if (!c.selectFolder(folderPath, &sel)) continue;

        // Edit-wins-over-delete: if the remote message exists and was edited
        // after our local delete, drop the tombstone instead of pushing.
        if (serverUid > 0) {
            QString remoteLastMod;
            c.fetchHeader(serverUid, QStringLiteral("X-Last-Modified"), &remoteLastMod);
            const QDateTime remoteDt = QDateTime::fromString(remoteLastMod, Qt::ISODate);
            if (remoteDt.isValid()
                && remoteDt.toSecsSinceEpoch() > deletedAt) {
                QSqlQuery drop(m_db);
                drop.prepare(QStringLiteral("DELETE FROM tombstones WHERE uuid = :u"));
                drop.bindValue(QStringLiteral(":u"), uuid);
                drop.exec();
                continue;
            }
        }

        if (serverUid > 0) {
            if (!trashPath.isEmpty() && folderPath != trashPath) {
                c.moveMessage(serverUid, trashPath);
            } else {
                c.markDeletedAndExpunge({ serverUid });
            }
        }

        QSqlQuery drop(m_db);
        drop.prepare(QStringLiteral("DELETE FROM tombstones WHERE uuid = :u"));
        drop.bindValue(QStringLiteral(":u"), uuid);
        drop.exec();
    }
    return true;
}

bool SyncWorker::pushDirtyNotes(ImapClient &c, const AccountRow &acc)
{
    QSqlQuery dirty(m_db);
    dirty.prepare(QStringLiteral(
        "SELECT n.id, n.uuid, n.title, n.body_html, n.format,"
        " n.created, n.last_modified, n.server_uid, f.full_path "
        "FROM notes n JOIN folders f ON f.id = n.folder_id "
        "WHERE n.account_id = :a AND n.locally_dirty = 1"));
    dirty.bindValue(QStringLiteral(":a"), acc.id);
    if (!dirty.exec()) return false;

    struct Pending {
        qint64 noteId;
        QString uuid;
        QString title;
        QString bodyHtml;
        QString format;
        QDateTime created;
        QDateTime modified;
        quint32 serverUid;
        QString folderPath;
    };
    QList<Pending> pending;
    while (dirty.next()) {
        Pending p;
        p.noteId = dirty.value(0).toLongLong();
        p.uuid = dirty.value(1).toString();
        p.title = dirty.value(2).toString();
        p.bodyHtml = dirty.value(3).toString();
        p.format = dirty.value(4).toString();
        p.created = QDateTime::fromSecsSinceEpoch(dirty.value(5).toLongLong());
        p.modified = QDateTime::fromSecsSinceEpoch(dirty.value(6).toLongLong());
        p.serverUid = dirty.value(7).toUInt();
        p.folderPath = dirty.value(8).toString();
        pending.append(p);
    }

    for (const Pending &p : pending) {
        Note n;
        n.uuid = p.uuid;
        n.title = p.title;
        n.bodyHtml = p.bodyHtml;
        n.format = p.format;
        n.created = p.created;
        n.lastModified = p.modified;
        const QByteArray raw = NoteMessage::serialize(
            n, acc.username + QStringLiteral("@notes.invalid"));

        // Apple's pattern is to APPEND a fresh copy of the note and then
        // delete the prior UID — this keeps the server-side message tree
        // monotone and avoids fighting the RFC 3501 "messages are immutable"
        // rule. UIDPLUS gives us the new UID directly.
        ImapClient::SelectResult sel;
        if (!c.selectFolder(p.folderPath, &sel)) continue;

        quint32 newUid = 0;
        if (!c.appendMessage(p.folderPath, raw, &newUid)) {
            qWarning() << "APPEND failed for note" << p.uuid;
            continue;
        }
        if (p.serverUid > 0) {
            c.markDeletedAndExpunge({ p.serverUid });
        }

        QSqlQuery upd(m_db);
        upd.prepare(QStringLiteral(
            "UPDATE notes SET locally_dirty = 0, server_uid = :uid WHERE id = :id"));
        upd.bindValue(QStringLiteral(":uid"), newUid);
        upd.bindValue(QStringLiteral(":id"), p.noteId);
        upd.exec();
    }
    return true;
}

bool SyncWorker::pullFolder(ImapClient &c, const AccountRow &acc,
                            qint64 folderId, const QString &fullPath)
{
    ImapClient::SelectResult sel;
    if (!c.selectFolder(fullPath, &sel)) return false;

    QList<quint32> uids;
    if (!c.fetchAllUids(&uids)) return false;

    // Build local UID set for this folder so we can detect server-side deletes.
    QSqlQuery localQ(m_db);
    localQ.prepare(QStringLiteral(
        "SELECT id, uuid, server_uid, last_modified, locally_dirty FROM notes WHERE folder_id = :f"));
    localQ.bindValue(QStringLiteral(":f"), folderId);
    localQ.exec();
    struct LocalRow { qint64 id; QString uuid; quint32 uid; qint64 lastMod; bool dirty; };
    QList<LocalRow> local;
    QHash<quint32, int> uidToLocal;
    while (localQ.next()) {
        LocalRow lr;
        lr.id = localQ.value(0).toLongLong();
        lr.uuid = localQ.value(1).toString();
        lr.uid = localQ.value(2).toUInt();
        lr.lastMod = localQ.value(3).toLongLong();
        lr.dirty = localQ.value(4).toInt() != 0;
        if (lr.uid > 0) uidToLocal.insert(lr.uid, local.size());
        local.append(lr);
    }
    QSet<quint32> serverSet;
    for (quint32 u : uids) serverSet.insert(u);

    // Fetch + insert new ones.
    for (quint32 uid : uids) {
        if (uidToLocal.contains(uid)) continue;

        QByteArray raw;
        if (!c.fetchFullMessage(uid, &raw)) continue;
        NoteMessage::Parsed parsed;
        if (!NoteMessage::parse(raw, &parsed)) continue;

        // UUID-based dedupe: a note may already exist locally under a
        // different folder (e.g. user moved it via iOS — we'll catch up).
        QSqlQuery byUuid(m_db);
        byUuid.prepare(QStringLiteral("SELECT id, last_modified FROM notes WHERE uuid = :u"));
        byUuid.bindValue(QStringLiteral(":u"), parsed.uuid);
        byUuid.exec();
        if (byUuid.next()) {
            const qint64 existingId = byUuid.value(0).toLongLong();
            const qint64 existingMod = byUuid.value(1).toLongLong();
            const qint64 remoteMod = parsed.lastModified.toSecsSinceEpoch();
            if (remoteMod >= existingMod) {
                QSqlQuery upd(m_db);
                upd.prepare(QStringLiteral(
                    "UPDATE notes SET title = :t, body_html = :b, format = :f,"
                    " last_modified = :m, server_uid = :uid, folder_id = :folder,"
                    " locally_dirty = 0 WHERE id = :id"));
                upd.bindValue(QStringLiteral(":t"), parsed.title);
                upd.bindValue(QStringLiteral(":b"), parsed.bodyHtml);
                upd.bindValue(QStringLiteral(":f"), parsed.format);
                upd.bindValue(QStringLiteral(":m"), remoteMod);
                upd.bindValue(QStringLiteral(":uid"), uid);
                upd.bindValue(QStringLiteral(":folder"), folderId);
                upd.bindValue(QStringLiteral(":id"), existingId);
                upd.exec();
            }
            continue;
        }

        const qint64 created = parsed.created.isValid()
            ? parsed.created.toSecsSinceEpoch() : QDateTime::currentSecsSinceEpoch();
        const qint64 lastMod = parsed.lastModified.isValid()
            ? parsed.lastModified.toSecsSinceEpoch() : created;

        QSqlQuery ins(m_db);
        ins.prepare(QStringLiteral(
            "INSERT INTO notes (uuid, account_id, folder_id, title, body_html, format,"
            " created, last_modified, pinned, locally_dirty, server_uid)"
            " VALUES (:uuid, :a, :folder, :title, :body, :format,"
            " :created, :modified, 0, 0, :uid)"));
        ins.bindValue(QStringLiteral(":uuid"), parsed.uuid);
        ins.bindValue(QStringLiteral(":a"), acc.id);
        ins.bindValue(QStringLiteral(":folder"), folderId);
        ins.bindValue(QStringLiteral(":title"), parsed.title);
        ins.bindValue(QStringLiteral(":body"), parsed.bodyHtml);
        ins.bindValue(QStringLiteral(":format"), parsed.format.isEmpty() ? QStringLiteral("rich") : parsed.format);
        ins.bindValue(QStringLiteral(":created"), created);
        ins.bindValue(QStringLiteral(":modified"), lastMod);
        ins.bindValue(QStringLiteral(":uid"), uid);
        ins.exec();
    }

    // Server-side deletes: a UID we knew about that no longer appears at the
    // server is treated as deleted. Skip locally-dirty rows because they may
    // be replacements still being pushed.
    for (const LocalRow &lr : local) {
        if (lr.uid == 0) continue;
        if (serverSet.contains(lr.uid)) continue;
        if (lr.dirty) continue;
        QSqlQuery del(m_db);
        del.prepare(QStringLiteral("DELETE FROM notes WHERE id = :id"));
        del.bindValue(QStringLiteral(":id"), lr.id);
        del.exec();
    }
    return true;
}

void SyncWorker::runTrashCleanup(const AccountRow &acc)
{
    // Settings live in QSettings which is thread-safe per Qt docs. Read the
    // threshold here so the worker doesn't depend on the Settings object.
    QSettings store;
    const int days = store.value(QStringLiteral("trash/cleanupDays"), 30).toInt();
    if (days <= 0) return; // 0 = keep forever

    const qint64 cutoff = QDateTime::currentSecsSinceEpoch() - qint64(days) * 24 * 3600;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "DELETE FROM notes WHERE account_id = :a AND folder_id IN"
        " (SELECT id FROM folders WHERE account_id = :a AND is_trash = 1)"
        " AND last_modified < :cutoff"));
    q.bindValue(QStringLiteral(":a"), acc.id);
    q.bindValue(QStringLiteral(":cutoff"), cutoff);
    q.exec();
}

void SyncWorker::testAccount(QVariantMap data, QString password)
{
    if (!m_initialized) initialize();

    const QString host = data.value(QStringLiteral("imapHost")).toString();
    const int port = data.value(QStringLiteral("imapPort"), 993).toInt();
    const QString security = data.value(QStringLiteral("security"), QStringLiteral("ssl")).toString();
    const QString user = data.value(QStringLiteral("username")).toString();
    const QString notesRoot = data.value(QStringLiteral("notesRoot"), QStringLiteral("Notes")).toString();

    ImapClient c;
    if (!c.connectToHost(host, port,
                         security == QStringLiteral("starttls")
                             ? ImapClient::StartTls : ImapClient::ImplicitSsl)) {
        emit accountTestResult(false, c.lastError());
        return;
    }
    if (!c.login(user, password)) {
        emit accountTestResult(false, c.lastError());
        return;
    }
    QList<ImapClient::FolderInfo> folders;
    if (!c.listFolders(QString(), notesRoot, &folders)) {
        emit accountTestResult(false, c.lastError());
        return;
    }
    c.disconnectFromHost();

    if (folders.isEmpty()) {
        emit accountTestResult(true,
            tr("Connection OK. Notes folder will be created on first sync."));
    } else {
        emit accountTestResult(true, tr("Connection OK"));
    }
}
