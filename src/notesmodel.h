#ifndef NOTESMODEL_H
#define NOTESMODEL_H

#include <QAbstractListModel>
#include <QString>
#include <QVector>

class NotesDatabase;

class NotesModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(qint64 folderId READ folderId WRITE setFolderId NOTIFY folderIdChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchQueryChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        UuidRole,
        TitleRole,
        PreviewRole,
        LastModifiedRole,
        PinnedRole,
        FormatRole,
        FolderIdRole,
    };

    explicit NotesModel(NotesDatabase *db, QObject *parent = nullptr);

    qint64 folderId() const { return m_folderId; }
    void setFolderId(qint64 id);

    QString searchQuery() const { return m_searchQuery; }
    void setSearchQuery(const QString &query);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void reload();
    Q_INVOKABLE bool togglePinned(qint64 noteId);

signals:
    void folderIdChanged();
    void searchQueryChanged();

private:
    struct Row {
        qint64 id;
        QString uuid;
        QString title;
        QString preview;
        qint64 lastModified;
        bool pinned;
        QString format;
        qint64 folderId;
    };

    bool ftsAvailable() const;

    NotesDatabase *m_db;
    qint64 m_folderId = -1;
    QString m_searchQuery;
    QVector<Row> m_rows;
    mutable int m_ftsAvailable = -1; // -1 unknown, 0 no, 1 yes
};

#endif
