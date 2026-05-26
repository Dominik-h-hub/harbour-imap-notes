#ifndef FOLDERSMODEL_H
#define FOLDERSMODEL_H

#include <QAbstractListModel>
#include <QVector>

class NotesDatabase;

class FoldersModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(qint64 accountId READ accountId WRITE setAccountId NOTIFY accountIdChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        ParentIdRole,
        DisplayNameRole,
        FullPathRole,
        IsTrashRole,
        NoteCountRole,
    };

    explicit FoldersModel(NotesDatabase *db, QObject *parent = nullptr);

    qint64 accountId() const { return m_accountId; }
    void setAccountId(qint64 id);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void reload();

signals:
    void accountIdChanged();

private:
    struct Row {
        qint64 id;
        qint64 parentId;
        QString displayName;
        QString fullPath;
        bool isTrash;
        int noteCount;
    };

    NotesDatabase *m_db;
    qint64 m_accountId = -1;
    QVector<Row> m_rows;
};

#endif
