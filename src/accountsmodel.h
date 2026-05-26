#ifndef ACCOUNTSMODEL_H
#define ACCOUNTSMODEL_H

#include <QAbstractListModel>
#include <QVector>

class NotesDatabase;
class AccountsManager;

class AccountsModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        DisplayNameRole,
        ImapHostRole,
        UsernameRole,
        LastSyncRole,
        SyncStatusRole,
        LastErrorRole,
    };

    AccountsModel(NotesDatabase *db, AccountsManager *manager, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void reload();

private:
    struct Row {
        qint64 id;
        QString displayName;
        QString imapHost;
        QString username;
        qint64 lastSync;
        QString syncStatus;
        QString lastError;
    };

    NotesDatabase *m_db;
    AccountsManager *m_manager;
    QVector<Row> m_rows;
};

#endif
