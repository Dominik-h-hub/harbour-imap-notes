#include "accountsmodel.h"

#include "accountsmanager.h"
#include "notesdatabase.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QtDebug>

AccountsModel::AccountsModel(NotesDatabase *db, AccountsManager *manager, QObject *parent)
    : QAbstractListModel(parent)
    , m_db(db)
    , m_manager(manager)
{
    connect(m_manager, &AccountsManager::accountsChanged, this, &AccountsModel::reload);
    reload();
}

int AccountsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_rows.size();
}

QVariant AccountsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case IdRole: return r.id;
    case DisplayNameRole: return r.displayName;
    case ImapHostRole: return r.imapHost;
    case UsernameRole: return r.username;
    case LastSyncRole: return r.lastSync;
    case SyncStatusRole: return r.syncStatus;
    case LastErrorRole: return r.lastError;
    default: return {};
    }
}

QHash<int, QByteArray> AccountsModel::roleNames() const
{
    return {
        { IdRole, "accountId" },
        { DisplayNameRole, "displayName" },
        { ImapHostRole, "imapHost" },
        { UsernameRole, "username" },
        { LastSyncRole, "lastSync" },
        { SyncStatusRole, "syncStatus" },
        { LastErrorRole, "lastError" },
    };
}

void AccountsModel::reload()
{
    beginResetModel();
    m_rows.clear();

    QSqlQuery q(m_db->database());
    if (q.exec(QStringLiteral(
            "SELECT id, display_name, imap_host, username, last_sync, sync_status, last_error "
            "FROM accounts ORDER BY display_name"))) {
        while (q.next()) {
            Row r;
            r.id = q.value(0).toLongLong();
            r.displayName = q.value(1).toString();
            r.imapHost = q.value(2).toString();
            r.username = q.value(3).toString();
            r.lastSync = q.value(4).toLongLong();
            r.syncStatus = q.value(5).toString();
            r.lastError = q.value(6).toString();
            m_rows.append(r);
        }
    } else {
        qWarning() << "AccountsModel::reload:" << q.lastError().text();
    }

    endResetModel();
}
