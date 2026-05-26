#include "notesmodel.h"

#include "notesdatabase.h"
#include "richtextconverter.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QtDebug>

NotesModel::NotesModel(NotesDatabase *db, QObject *parent)
    : QAbstractListModel(parent)
    , m_db(db)
{
}

void NotesModel::setFolderId(qint64 id)
{
    if (id == m_folderId) {
        return;
    }
    m_folderId = id;
    emit folderIdChanged();
    reload();
}

void NotesModel::setSearchQuery(const QString &query)
{
    if (query == m_searchQuery) {
        return;
    }
    m_searchQuery = query;
    emit searchQueryChanged();
    reload();
}

int NotesModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_rows.size();
}

QVariant NotesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case IdRole: return r.id;
    case UuidRole: return r.uuid;
    case TitleRole: return r.title;
    case PreviewRole: return r.preview;
    case LastModifiedRole: return r.lastModified;
    case PinnedRole: return r.pinned;
    case FormatRole: return r.format;
    case FolderIdRole: return r.folderId;
    default: return {};
    }
}

QHash<int, QByteArray> NotesModel::roleNames() const
{
    return {
        { IdRole, "noteId" },
        { UuidRole, "uuid" },
        { TitleRole, "title" },
        { PreviewRole, "preview" },
        { LastModifiedRole, "lastModified" },
        { PinnedRole, "pinned" },
        { FormatRole, "format" },
        { FolderIdRole, "folderId" },
    };
}

void NotesModel::reload()
{
    beginResetModel();
    m_rows.clear();

    if (m_folderId < 0 && m_searchQuery.isEmpty()) {
        endResetModel();
        return;
    }

    QSqlQuery q(m_db->database());
    if (!m_searchQuery.isEmpty()) {
        // Pinned-first ordering breaks for search results — search returns a
        // flat ranked list ordered by FTS relevance. The QML layer can still
        // visualise pinned notes via the role.
        q.prepare(QStringLiteral(
            "SELECT n.id, n.uuid, n.title, n.body_html, n.last_modified, n.pinned, n.format, n.folder_id "
            "FROM notes n JOIN notes_fts f ON f.rowid = n.id "
            "WHERE notes_fts MATCH :query AND n.folder_id IN ("
            "  SELECT id FROM folders WHERE account_id = ("
            "    SELECT account_id FROM folders WHERE id = :folder"
            "  )"
            ") "
            "ORDER BY rank"));
        q.bindValue(QStringLiteral(":query"), m_searchQuery);
        q.bindValue(QStringLiteral(":folder"), m_folderId);
    } else {
        // Pinned section first (within pinned still sorted by last_modified
        // DESC), then the rest.
        q.prepare(QStringLiteral(
            "SELECT id, uuid, title, body_html, last_modified, pinned, format, folder_id "
            "FROM notes WHERE folder_id = :folder "
            "ORDER BY pinned DESC, last_modified DESC"));
        q.bindValue(QStringLiteral(":folder"), m_folderId);
    }

    if (q.exec()) {
        while (q.next()) {
            Row r;
            r.id = q.value(0).toLongLong();
            r.uuid = q.value(1).toString();
            r.title = q.value(2).toString();
            r.preview = RichTextConverter::previewFromHtml(q.value(3).toString());
            r.lastModified = q.value(4).toLongLong();
            r.pinned = q.value(5).toInt() != 0;
            r.format = q.value(6).toString();
            r.folderId = q.value(7).toLongLong();
            m_rows.append(r);
        }
    } else {
        qWarning() << "NotesModel::reload:" << q.lastError().text();
    }

    endResetModel();
}

bool NotesModel::togglePinned(qint64 noteId)
{
    QSqlQuery q(m_db->database());
    q.prepare(QStringLiteral("UPDATE notes SET pinned = NOT pinned WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), noteId);
    if (!q.exec()) {
        qWarning() << "togglePinned:" << q.lastError().text();
        return false;
    }
    reload();
    return true;
}
