#ifndef ACCOUNT_H
#define ACCOUNT_H

#include <QObject>
#include <QString>
#include <QDateTime>

class Account
{
    Q_GADGET
    Q_PROPERTY(qint64 id MEMBER id)
    Q_PROPERTY(QString displayName MEMBER displayName)
    Q_PROPERTY(QString imapHost MEMBER imapHost)
    Q_PROPERTY(int imapPort MEMBER imapPort)
    Q_PROPERTY(QString security MEMBER security)
    Q_PROPERTY(QString username MEMBER username)
    Q_PROPERTY(QString notesRoot MEMBER notesRoot)
    Q_PROPERTY(QString hierarchyDelimiter MEMBER hierarchyDelimiter)
    Q_PROPERTY(qint64 defaultFolderId MEMBER defaultFolderId)
    Q_PROPERTY(QDateTime lastSync MEMBER lastSync)
    Q_PROPERTY(QString syncStatus MEMBER syncStatus)
    Q_PROPERTY(QString lastError MEMBER lastError)

public:
    qint64 id = -1;
    QString displayName;
    QString imapHost;
    int imapPort = 993;
    QString security = QStringLiteral("ssl");
    QString username;
    QString notesRoot = QStringLiteral("Notes");
    QString hierarchyDelimiter = QStringLiteral("/");
    qint64 defaultFolderId = -1;
    QDateTime lastSync;
    QString syncStatus = QStringLiteral("never");
    QString lastError;
};

#endif
