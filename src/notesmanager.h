#ifndef NOTESMANAGER_H
#define NOTESMANAGER_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class NotesDatabase;

// QML-facing facade for note CRUD. The sync engine reads/writes the same
// tables but goes directly through QSqlDatabase on its worker thread, so this
// class only owns the main-thread access path.
class NotesManager : public QObject
{
    Q_OBJECT

public:
    explicit NotesManager(NotesDatabase *db, QObject *parent = nullptr);

    // Returns the full note record (title, bodyHtml, format, ...) so the
    // editor can populate its fields. Empty map when not found.
    Q_INVOKABLE QVariantMap note(qint64 noteId) const;

    // Create a new note in the given folder. Returns the new note id.
    Q_INVOKABLE qint64 createNote(qint64 folderId,
                                  const QString &title,
                                  const QString &bodyHtml,
                                  const QString &format = QStringLiteral("rich"));

    Q_INVOKABLE bool updateNote(qint64 noteId,
                                const QString &title,
                                const QString &bodyHtml,
                                const QString &format);

    // Soft-delete: move to the account's trash folder. The actual IMAP move
    // happens on the next sync — locally we just retag the folder_id and
    // record a tombstone so the sync engine knows to push the move upstream.
    Q_INVOKABLE bool moveToTrash(qint64 noteId);

    // Restore a note from trash back into the folder it came from. If the
    // original folder is gone we leave it in trash with an error log.
    Q_INVOKABLE bool restoreFromTrash(qint64 noteId);

    // Permanent delete: drops the row and writes a tombstone so the sync
    // engine expunges the server message on the next pass.
    Q_INVOKABLE bool deleteNotePermanently(qint64 noteId);

    Q_INVOKABLE bool moveNote(qint64 noteId, qint64 targetFolderId);

    // Folder CRUD (server-aware: marks the row as locally-pending so sync
    // mirrors it upstream). For now we only track the local mirror — the
    // sync engine creates/renames/deletes on the server via ImapClient.
    Q_INVOKABLE qint64 createFolder(qint64 accountId,
                                    const QString &parentFullPath,
                                    const QString &name);
    Q_INVOKABLE bool renameFolder(qint64 folderId, const QString &newDisplayName);
    Q_INVOKABLE bool deleteFolder(qint64 folderId);

    // Returns the local folder id for this account's trash folder, or -1
    // if it hasn't been created/discovered yet.
    Q_INVOKABLE qint64 trashFolderIdFor(qint64 accountId) const;

signals:
    void notesChanged();
    void foldersChanged();

private:
    qint64 trashFolderId(qint64 accountId) const;
    QString joinPath(const QString &parent, const QString &child, const QString &delimiter) const;

    NotesDatabase *m_db;
};

#endif
