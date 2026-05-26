#include "foldersmodel.h"

#include "notesdatabase.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QtDebug>

FoldersModel::FoldersModel(NotesDatabase *db, QObject *parent)
    : QAbstractListModel(parent)
    , m_db(db)
{
}

void FoldersModel::setAccountId(qint64 id)
{
    if (id == m_accountId) {
        return;
    }
    m_accountId = id;
    emit accountIdChanged();
    reload();
}

int FoldersModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_rows.size();
}

QVariant FoldersModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case IdRole: return r.id;
    case ParentIdRole: return r.parentId;
    case DisplayNameRole: return r.displayName;
    case FullPathRole: return r.fullPath;
    case IsTrashRole: return r.isTrash;
    case NoteCountRole: return r.noteCount;
    default: return {};
    }
}

QHash<int, QByteArray> FoldersModel::roleNames() const
{
    return {
        { IdRole, "folderId" },
        { ParentIdRole, "parentId" },
        { DisplayNameRole, "displayName" },
        { FullPathRole, "fullPath" },
        { IsTrashRole, "isTrash" },
        { NoteCountRole, "noteCount" },
    };
}

void FoldersModel::reload()
{
    beginResetModel();
    m_rows.clear();

    if (m_accountId < 0) {
        endResetModel();
        return;
    }

    QSqlQuery q(m_db->database());
    q.prepare(QStringLiteral(
        "SELECT f.id, COALESCE(f.parent_id, 0), f.display_name, f.full_path, f.is_trash,"
        "       (SELECT COUNT(*) FROM notes n WHERE n.folder_id = f.id) AS note_count "
        "FROM folders f WHERE f.account_id = :id ORDER BY f.full_path"));
    q.bindValue(QStringLiteral(":id"), m_accountId);
    if (q.exec()) {
        while (q.next()) {
            Row r;
            r.id = q.value(0).toLongLong();
            r.parentId = q.value(1).toLongLong();
            r.displayName = q.value(2).toString();
            r.fullPath = q.value(3).toString();
            r.isTrash = q.value(4).toInt() != 0;
            r.noteCount = q.value(5).toInt();
            m_rows.append(r);
        }
    } else {
        qWarning() << "FoldersModel::reload:" << q.lastError().text();
    }

    endResetModel();
}
