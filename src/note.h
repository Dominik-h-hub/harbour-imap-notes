#ifndef NOTE_H
#define NOTE_H

#include <QObject>
#include <QString>
#include <QDateTime>

class Note
{
    Q_GADGET
    Q_PROPERTY(qint64 id MEMBER id)
    Q_PROPERTY(QString uuid MEMBER uuid)
    Q_PROPERTY(qint64 accountId MEMBER accountId)
    Q_PROPERTY(qint64 folderId MEMBER folderId)
    Q_PROPERTY(QString title MEMBER title)
    Q_PROPERTY(QString bodyHtml MEMBER bodyHtml)
    Q_PROPERTY(QString format MEMBER format)
    Q_PROPERTY(QDateTime created MEMBER created)
    Q_PROPERTY(QDateTime lastModified MEMBER lastModified)
    Q_PROPERTY(bool pinned MEMBER pinned)
    Q_PROPERTY(bool locallyDirty MEMBER locallyDirty)
    Q_PROPERTY(quint32 serverUid MEMBER serverUid)

public:
    enum Format {
        RichText,
        PlainText,
    };
    Q_ENUM(Format)

    qint64 id = -1;
    QString uuid;
    qint64 accountId = -1;
    qint64 folderId = -1;
    QString title;
    QString bodyHtml;
    QString format = QStringLiteral("rich");
    QDateTime created;
    QDateTime lastModified;
    bool pinned = false;
    bool locallyDirty = false;
    quint32 serverUid = 0;
};

#endif
