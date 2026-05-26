#include "notesmanager.h"

#include "notesdatabase.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <QtDebug>

NotesManager::NotesManager(NotesDatabase *db, QObject *parent)
    : QObject(parent)
    , m_db(db)
{
}

QVariantMap NotesManager::note(qint64 noteId) const
{
    QSqlQuery q(m_db->database());
    q.prepare(QStringLiteral(
        "SELECT id, uuid, account_id, folder_id, title, body_html, format,"
        " created, last_modified, pinned, locally_dirty, server_uid "
        "FROM notes WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), noteId);
    if (!q.exec() || !q.next()) {
        return {};
    }
    QVariantMap m;
    m.insert(QStringLiteral("id"), q.value(0));
    m.insert(QStringLiteral("uuid"), q.value(1));
    m.insert(QStringLiteral("accountId"), q.value(2));
    m.insert(QStringLiteral("folderId"), q.value(3));
    m.insert(QStringLiteral("title"), q.value(4));
    m.insert(QStringLiteral("bodyHtml"), q.value(5));
    m.insert(QStringLiteral("format"), q.value(6));
    m.insert(QStringLiteral("created"), q.value(7));
    m.insert(QStringLiteral("lastModified"), q.value(8));
    m.insert(QStringLiteral("pinned"), q.value(9));
    m.insert(QStringLiteral("locallyDirty"), q.value(10));
    m.insert(QStringLiteral("serverUid"), q.value(11));
    return m;
}

qint64 NotesManager::createNote(qint64 folderId,
                                const QString &title,
                                const QString &bodyHtml,
                                const QString &format)
{
    QSqlQuery accountLookup(m_db->database());
    accountLookup.prepare(QStringLiteral("SELECT account_id FROM folders WHERE id = :id"));
    accountLookup.bindValue(QStringLiteral(":id"), folderId);
    if (!accountLookup.exec() || !accountLookup.next()) {
        qWarning() << "createNote: unknown folder" << folderId;
        return -1;
    }
    const qint64 accountId = accountLookup.value(0).toLongLong();

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper();

    QSqlQuery q(m_db->database());
    q.prepare(QStringLiteral(
        "INSERT INTO notes (uuid, account_id, folder_id, title, body_html, format,"
        " created, last_modified, pinned, locally_dirty, server_uid)"
        " VALUES (:uuid, :account, :folder, :title, :body, :format,"
        " :created, :modified, 0, 1, 0)"));
    q.bindValue(QStringLiteral(":uuid"), uuid);
    q.bindValue(QStringLiteral(":account"), accountId);
    q.bindValue(QStringLiteral(":folder"), folderId);
    q.bindValue(QStringLiteral(":title"), title);
    q.bindValue(QStringLiteral(":body"), bodyHtml);
    q.bindValue(QStringLiteral(":format"), format);
    q.bindValue(QStringLiteral(":created"), now);
    q.bindValue(QStringLiteral(":modified"), now);
    if (!q.exec()) {
        qWarning() << "createNote:" << q.lastError().text();
        return -1;
    }
    emit notesChanged();
    return q.lastInsertId().toLongLong();
}

bool NotesManager::updateNote(qint64 noteId,
                              const QString &title,
                              const QString &bodyHtml,
                              const QString &format)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QSqlQuery q(m_db->database());
    q.prepare(QStringLiteral(
        "UPDATE notes SET title = :title, body_html = :body, format = :format,"
        " last_modified = :modified, locally_dirty = 1 WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), noteId);
    q.bindValue(QStringLiteral(":title"), title);
    q.bindValue(QStringLiteral(":body"), bodyHtml);
    q.bindValue(QStringLiteral(":format"), format);
    q.bindValue(QStringLiteral(":modified"), now);
    if (!q.exec()) {
        qWarning() << "updateNote:" << q.lastError().text();
        return false;
    }
    emit notesChanged();
    return true;
}

qint64 NotesManager::trashFolderIdFor(qint64 accountId) const
{
    return trashFolderId(accountId);
}

qint64 NotesManager::trashFolderId(qint64 accountId) const
{
    QSqlQuery q(m_db->database());
    q.prepare(QStringLiteral(
        "SELECT id FROM folders WHERE account_id = :a AND is_trash = 1 LIMIT 1"));
    q.bindValue(QStringLiteral(":a"), accountId);
    if (q.exec() && q.next()) {
        return q.value(0).toLongLong();
    }
    return -1;
}

bool NotesManager::moveToTrash(qint64 noteId)
{
    QSqlQuery lookup(m_db->database());
    lookup.prepare(QStringLiteral(
        "SELECT n.account_id, n.folder_id, n.uuid, n.server_uid, f.full_path "
        "FROM notes n JOIN folders f ON f.id = n.folder_id WHERE n.id = :id"));
    lookup.bindValue(QStringLiteral(":id"), noteId);
    if (!lookup.exec() || !lookup.next()) {
        return false;
    }
    const qint64 accountId = lookup.value(0).toLongLong();
    const qint64 sourceFolderId = lookup.value(1).toLongLong();
    const QString uuid = lookup.value(2).toString();
    const quint32 serverUid = lookup.value(3).toUInt();
    const QString sourcePath = lookup.value(4).toString();

    qint64 trashId = trashFolderId(accountId);
    if (trashId < 0) {
        // Trash row will be inserted by the sync engine once it discovers the
        // server-side Deleted Messages folder. Fall back to a hard delete +
        // tombstone so the user's intent is preserved.
        return deleteNotePermanently(noteId);
    }

    // Stash the original folder path inside the tombstone payload so the sync
    // engine can MOVE the message into trash on the server and so a future
    // restore can find its way home.
    QSqlQuery upd(m_db->database());
    upd.prepare(QStringLiteral(
        "UPDATE notes SET folder_id = :trash, locally_dirty = 1,"
        " last_modified = :mod WHERE id = :id"));
    upd.bindValue(QStringLiteral(":trash"), trashId);
    upd.bindValue(QStringLiteral(":id"), noteId);
    upd.bindValue(QStringLiteral(":mod"), QDateTime::currentSecsSinceEpoch());
    if (!upd.exec()) {
        qWarning() << "moveToTrash:" << upd.lastError().text();
        return false;
    }

    // Tombstone keyed by uuid + original folder lets the sync engine reissue
    // a server-side MOVE without losing the source location, which a follow-
    // up restore needs to recover the original placement.
    QSqlQuery ts(m_db->database());
    ts.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO tombstones (uuid, account_id, folder_full_path, deleted_at, server_uid)"
        " VALUES (:uuid, :a, :path, :at, :uid)"));
    ts.bindValue(QStringLiteral(":uuid"), uuid);
    ts.bindValue(QStringLiteral(":a"), accountId);
    ts.bindValue(QStringLiteral(":path"), sourcePath);
    ts.bindValue(QStringLiteral(":at"), QDateTime::currentSecsSinceEpoch());
    ts.bindValue(QStringLiteral(":uid"), serverUid);
    ts.exec();

    Q_UNUSED(sourceFolderId)
    emit notesChanged();
    return true;
}

bool NotesManager::restoreFromTrash(qint64 noteId)
{
    QSqlQuery lookup(m_db->database());
    lookup.prepare(QStringLiteral(
        "SELECT n.uuid, n.account_id, t.folder_full_path "
        "FROM notes n LEFT JOIN tombstones t ON t.uuid = n.uuid WHERE n.id = :id"));
    lookup.bindValue(QStringLiteral(":id"), noteId);
    if (!lookup.exec() || !lookup.next()) {
        return false;
    }
    const QString uuid = lookup.value(0).toString();
    const qint64 accountId = lookup.value(1).toLongLong();
    const QString originalPath = lookup.value(2).toString();
    if (originalPath.isEmpty()) {
        qWarning() << "restoreFromTrash: no recorded original folder for" << noteId;
        return false;
    }

    QSqlQuery folderLookup(m_db->database());
    folderLookup.prepare(QStringLiteral(
        "SELECT id FROM folders WHERE account_id = :a AND full_path = :p LIMIT 1"));
    folderLookup.bindValue(QStringLiteral(":a"), accountId);
    folderLookup.bindValue(QStringLiteral(":p"), originalPath);
    if (!folderLookup.exec() || !folderLookup.next()) {
        qWarning() << "restoreFromTrash: original folder" << originalPath << "is gone";
        return false;
    }
    const qint64 targetFolderId = folderLookup.value(0).toLongLong();

    QSqlQuery upd(m_db->database());
    upd.prepare(QStringLiteral(
        "UPDATE notes SET folder_id = :target, locally_dirty = 1,"
        " last_modified = :mod WHERE id = :id"));
    upd.bindValue(QStringLiteral(":target"), targetFolderId);
    upd.bindValue(QStringLiteral(":id"), noteId);
    upd.bindValue(QStringLiteral(":mod"), QDateTime::currentSecsSinceEpoch());
    if (!upd.exec()) {
        return false;
    }

    QSqlQuery dropTs(m_db->database());
    dropTs.prepare(QStringLiteral("DELETE FROM tombstones WHERE uuid = :uuid"));
    dropTs.bindValue(QStringLiteral(":uuid"), uuid);
    dropTs.exec();

    emit notesChanged();
    return true;
}

bool NotesManager::deleteNotePermanently(qint64 noteId)
{
    QSqlQuery lookup(m_db->database());
    lookup.prepare(QStringLiteral(
        "SELECT n.uuid, n.account_id, n.server_uid, f.full_path "
        "FROM notes n JOIN folders f ON f.id = n.folder_id WHERE n.id = :id"));
    lookup.bindValue(QStringLiteral(":id"), noteId);
    if (!lookup.exec() || !lookup.next()) {
        return false;
    }
    const QString uuid = lookup.value(0).toString();
    const qint64 accountId = lookup.value(1).toLongLong();
    const quint32 serverUid = lookup.value(2).toUInt();
    const QString folderPath = lookup.value(3).toString();

    QSqlDatabase db = m_db->database();
    db.transaction();

    QSqlQuery ts(db);
    ts.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO tombstones (uuid, account_id, folder_full_path, deleted_at, server_uid)"
        " VALUES (:uuid, :a, :path, :at, :uid)"));
    ts.bindValue(QStringLiteral(":uuid"), uuid);
    ts.bindValue(QStringLiteral(":a"), accountId);
    ts.bindValue(QStringLiteral(":path"), folderPath);
    ts.bindValue(QStringLiteral(":at"), QDateTime::currentSecsSinceEpoch());
    ts.bindValue(QStringLiteral(":uid"), serverUid);
    if (!ts.exec()) {
        db.rollback();
        return false;
    }

    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM notes WHERE id = :id"));
    del.bindValue(QStringLiteral(":id"), noteId);
    if (!del.exec()) {
        db.rollback();
        return false;
    }
    db.commit();
    emit notesChanged();
    return true;
}

bool NotesManager::moveNote(qint64 noteId, qint64 targetFolderId)
{
    QSqlQuery q(m_db->database());
    q.prepare(QStringLiteral(
        "UPDATE notes SET folder_id = :target, locally_dirty = 1,"
        " last_modified = :mod WHERE id = :id"));
    q.bindValue(QStringLiteral(":target"), targetFolderId);
    q.bindValue(QStringLiteral(":id"), noteId);
    q.bindValue(QStringLiteral(":mod"), QDateTime::currentSecsSinceEpoch());
    if (!q.exec()) {
        return false;
    }
    emit notesChanged();
    return true;
}

QString NotesManager::joinPath(const QString &parent, const QString &child, const QString &delimiter) const
{
    if (parent.isEmpty()) return child;
    return parent + delimiter + child;
}

qint64 NotesManager::createFolder(qint64 accountId,
                                  const QString &parentFullPath,
                                  const QString &name)
{
    QSqlQuery delimLookup(m_db->database());
    delimLookup.prepare(QStringLiteral("SELECT hierarchy_delimiter FROM accounts WHERE id = :a"));
    delimLookup.bindValue(QStringLiteral(":a"), accountId);
    QString delimiter = QStringLiteral("/");
    if (delimLookup.exec() && delimLookup.next()) {
        delimiter = delimLookup.value(0).toString();
    }

    const QString fullPath = joinPath(parentFullPath, name, delimiter);

    qint64 parentId = -1;
    if (!parentFullPath.isEmpty()) {
        QSqlQuery parentLookup(m_db->database());
        parentLookup.prepare(QStringLiteral(
            "SELECT id FROM folders WHERE account_id = :a AND full_path = :p"));
        parentLookup.bindValue(QStringLiteral(":a"), accountId);
        parentLookup.bindValue(QStringLiteral(":p"), parentFullPath);
        if (parentLookup.exec() && parentLookup.next()) {
            parentId = parentLookup.value(0).toLongLong();
        }
    }

    QSqlQuery q(m_db->database());
    q.prepare(QStringLiteral(
        "INSERT INTO folders (account_id, parent_id, full_path, display_name, is_trash)"
        " VALUES (:a, :p, :path, :name, 0)"));
    q.bindValue(QStringLiteral(":a"), accountId);
    q.bindValue(QStringLiteral(":p"), parentId < 0 ? QVariant() : QVariant(parentId));
    q.bindValue(QStringLiteral(":path"), fullPath);
    q.bindValue(QStringLiteral(":name"), name);
    if (!q.exec()) {
        qWarning() << "createFolder:" << q.lastError().text();
        return -1;
    }
    emit foldersChanged();
    return q.lastInsertId().toLongLong();
}

bool NotesManager::renameFolder(qint64 folderId, const QString &newDisplayName)
{
    QSqlQuery q(m_db->database());
    q.prepare(QStringLiteral(
        "UPDATE folders SET display_name = :name WHERE id = :id"));
    q.bindValue(QStringLiteral(":name"), newDisplayName);
    q.bindValue(QStringLiteral(":id"), folderId);
    if (!q.exec()) {
        return false;
    }
    // Server-side rename is the sync engine's job — it sees a mismatch
    // between display_name and the leaf of full_path and reconciles.
    emit foldersChanged();
    return true;
}

bool NotesManager::deleteFolder(qint64 folderId)
{
    QSqlQuery q(m_db->database());
    q.prepare(QStringLiteral("DELETE FROM folders WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), folderId);
    if (!q.exec()) {
        return false;
    }
    emit foldersChanged();
    return true;
}
