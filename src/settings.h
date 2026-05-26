#ifndef SETTINGS_H
#define SETTINGS_H

#include <QObject>
#include <QSettings>

class Settings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int syncIntervalMinutes READ syncIntervalMinutes WRITE setSyncIntervalMinutes NOTIFY syncIntervalMinutesChanged)
    Q_PROPERTY(bool syncAttachmentsOverMobile READ syncAttachmentsOverMobile WRITE setSyncAttachmentsOverMobile NOTIFY syncAttachmentsOverMobileChanged)
    Q_PROPERTY(int trashCleanupDays READ trashCleanupDays WRITE setTrashCleanupDays NOTIFY trashCleanupDaysChanged)
    Q_PROPERTY(qint64 defaultAccountId READ defaultAccountId WRITE setDefaultAccountId NOTIFY defaultAccountIdChanged)
    Q_PROPERTY(qint64 defaultFolderId READ defaultFolderId WRITE setDefaultFolderId NOTIFY defaultFolderIdChanged)

public:
    explicit Settings(QObject *parent = nullptr);

    int syncIntervalMinutes() const;
    void setSyncIntervalMinutes(int minutes);

    bool syncAttachmentsOverMobile() const;
    void setSyncAttachmentsOverMobile(bool enabled);

    int trashCleanupDays() const;
    void setTrashCleanupDays(int days);

    qint64 defaultAccountId() const;
    void setDefaultAccountId(qint64 id);

    qint64 defaultFolderId() const;
    void setDefaultFolderId(qint64 id);

signals:
    void syncIntervalMinutesChanged();
    void syncAttachmentsOverMobileChanged();
    void trashCleanupDaysChanged();
    void defaultAccountIdChanged();
    void defaultFolderIdChanged();

private:
    QSettings m_store;
};

#endif
