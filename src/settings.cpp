#include "settings.h"

namespace {
constexpr const char *kSyncInterval = "sync/intervalMinutes";
constexpr const char *kSyncOverMobile = "sync/attachmentsOverMobile";
constexpr const char *kTrashCleanup = "trash/cleanupDays";
constexpr const char *kDefaultAccount = "newNote/defaultAccountId";
constexpr const char *kDefaultFolder = "newNote/defaultFolderId";
} // namespace

Settings::Settings(QObject *parent)
    : QObject(parent)
{
}

int Settings::syncIntervalMinutes() const
{
    return m_store.value(kSyncInterval, 5).toInt();
}

void Settings::setSyncIntervalMinutes(int minutes)
{
    if (minutes == syncIntervalMinutes()) {
        return;
    }
    m_store.setValue(kSyncInterval, minutes);
    emit syncIntervalMinutesChanged();
}

bool Settings::syncAttachmentsOverMobile() const
{
    return m_store.value(kSyncOverMobile, false).toBool();
}

void Settings::setSyncAttachmentsOverMobile(bool enabled)
{
    if (enabled == syncAttachmentsOverMobile()) {
        return;
    }
    m_store.setValue(kSyncOverMobile, enabled);
    emit syncAttachmentsOverMobileChanged();
}

int Settings::trashCleanupDays() const
{
    return m_store.value(kTrashCleanup, 30).toInt();
}

void Settings::setTrashCleanupDays(int days)
{
    if (days == trashCleanupDays()) {
        return;
    }
    m_store.setValue(kTrashCleanup, days);
    emit trashCleanupDaysChanged();
}

qint64 Settings::defaultAccountId() const
{
    return m_store.value(kDefaultAccount, -1).toLongLong();
}

void Settings::setDefaultAccountId(qint64 id)
{
    if (id == defaultAccountId()) {
        return;
    }
    m_store.setValue(kDefaultAccount, id);
    emit defaultAccountIdChanged();
}

qint64 Settings::defaultFolderId() const
{
    return m_store.value(kDefaultFolder, -1).toLongLong();
}

void Settings::setDefaultFolderId(qint64 id)
{
    if (id == defaultFolderId()) {
        return;
    }
    m_store.setValue(kDefaultFolder, id);
    emit defaultFolderIdChanged();
}
