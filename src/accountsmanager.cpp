#include "accountsmanager.h"

#include "notesdatabase.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QtDebug>

AccountsManager::AccountsManager(NotesDatabase *db, QObject *parent)
    : QObject(parent)
    , m_db(db)
{
}

int AccountsManager::count() const
{
    QSqlQuery q(m_db->database());
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM accounts")) && q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}

static QVariantMap rowToMap(const QSqlQuery &q)
{
    QVariantMap m;
    m.insert(QStringLiteral("id"), q.value(QStringLiteral("id")));
    m.insert(QStringLiteral("displayName"), q.value(QStringLiteral("display_name")));
    m.insert(QStringLiteral("imapHost"), q.value(QStringLiteral("imap_host")));
    m.insert(QStringLiteral("imapPort"), q.value(QStringLiteral("imap_port")));
    m.insert(QStringLiteral("security"), q.value(QStringLiteral("security")));
    m.insert(QStringLiteral("username"), q.value(QStringLiteral("username")));
    m.insert(QStringLiteral("notesRoot"), q.value(QStringLiteral("notes_root")));
    m.insert(QStringLiteral("hierarchyDelimiter"), q.value(QStringLiteral("hierarchy_delimiter")));
    m.insert(QStringLiteral("defaultFolderId"), q.value(QStringLiteral("default_folder_id")));
    m.insert(QStringLiteral("lastSync"), q.value(QStringLiteral("last_sync")));
    m.insert(QStringLiteral("syncStatus"), q.value(QStringLiteral("sync_status")));
    m.insert(QStringLiteral("lastError"), q.value(QStringLiteral("last_error")));
    return m;
}

QVariantList AccountsManager::listAccounts() const
{
    QVariantList out;
    QSqlQuery q(m_db->database());
    if (!q.exec(QStringLiteral("SELECT * FROM accounts ORDER BY display_name"))) {
        qWarning() << "listAccounts:" << q.lastError().text();
        return out;
    }
    while (q.next()) {
        out.append(rowToMap(q));
    }
    return out;
}

QVariantMap AccountsManager::account(qint64 id) const
{
    QSqlQuery q(m_db->database());
    q.prepare(QStringLiteral("SELECT * FROM accounts WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (!q.exec() || !q.next()) {
        return {};
    }
    return rowToMap(q);
}

qint64 AccountsManager::createAccount(const QVariantMap &data)
{
    QSqlQuery q(m_db->database());
    q.prepare(QStringLiteral(
        "INSERT INTO accounts (display_name, imap_host, imap_port, security, username, notes_root, hierarchy_delimiter) "
        "VALUES (:displayName, :imapHost, :imapPort, :security, :username, :notesRoot, :hierarchyDelimiter)"));
    q.bindValue(QStringLiteral(":displayName"), data.value(QStringLiteral("displayName")));
    q.bindValue(QStringLiteral(":imapHost"), data.value(QStringLiteral("imapHost")));
    q.bindValue(QStringLiteral(":imapPort"), data.value(QStringLiteral("imapPort"), 993));
    q.bindValue(QStringLiteral(":security"), data.value(QStringLiteral("security"), QStringLiteral("ssl")));
    q.bindValue(QStringLiteral(":username"), data.value(QStringLiteral("username")));
    q.bindValue(QStringLiteral(":notesRoot"), data.value(QStringLiteral("notesRoot"), QStringLiteral("Notes")));
    q.bindValue(QStringLiteral(":hierarchyDelimiter"), data.value(QStringLiteral("hierarchyDelimiter"), QStringLiteral("/")));
    if (!q.exec()) {
        qWarning() << "createAccount:" << q.lastError().text();
        return -1;
    }
    const qint64 id = q.lastInsertId().toLongLong();
    emit accountsChanged();
    return id;
}

bool AccountsManager::updateAccount(qint64 id, const QVariantMap &data)
{
    QSqlQuery q(m_db->database());
    q.prepare(QStringLiteral(
        "UPDATE accounts SET display_name = :displayName, imap_host = :imapHost, imap_port = :imapPort,"
        " security = :security, username = :username, notes_root = :notesRoot,"
        " hierarchy_delimiter = :hierarchyDelimiter WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    q.bindValue(QStringLiteral(":displayName"), data.value(QStringLiteral("displayName")));
    q.bindValue(QStringLiteral(":imapHost"), data.value(QStringLiteral("imapHost")));
    q.bindValue(QStringLiteral(":imapPort"), data.value(QStringLiteral("imapPort"), 993));
    q.bindValue(QStringLiteral(":security"), data.value(QStringLiteral("security"), QStringLiteral("ssl")));
    q.bindValue(QStringLiteral(":username"), data.value(QStringLiteral("username")));
    q.bindValue(QStringLiteral(":notesRoot"), data.value(QStringLiteral("notesRoot"), QStringLiteral("Notes")));
    q.bindValue(QStringLiteral(":hierarchyDelimiter"), data.value(QStringLiteral("hierarchyDelimiter"), QStringLiteral("/")));
    if (!q.exec()) {
        qWarning() << "updateAccount:" << q.lastError().text();
        return false;
    }
    emit accountsChanged();
    return true;
}

bool AccountsManager::removeAccount(qint64 id)
{
    QSqlQuery q(m_db->database());
    q.prepare(QStringLiteral("DELETE FROM accounts WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (!q.exec()) {
        qWarning() << "removeAccount:" << q.lastError().text();
        return false;
    }
    emit accountsChanged();
    return true;
}

bool AccountsManager::setPassword(qint64 accountId, const QString &password)
{
    // TODO: route through sailfish-secrets once the IMAP client lands. Until
    // then the password lives as an obfuscated blob in the DB column — better
    // than plaintext for casual inspection, but not a real secret store.
    QByteArray blob = password.toUtf8().toBase64();
    QSqlQuery q(m_db->database());
    q.prepare(QStringLiteral("UPDATE accounts SET password_blob = :blob WHERE id = :id"));
    q.bindValue(QStringLiteral(":blob"), blob);
    q.bindValue(QStringLiteral(":id"), accountId);
    if (!q.exec()) {
        qWarning() << "setPassword:" << q.lastError().text();
        return false;
    }
    return true;
}

QString AccountsManager::password(qint64 accountId) const
{
    QSqlQuery q(m_db->database());
    q.prepare(QStringLiteral("SELECT password_blob FROM accounts WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), accountId);
    if (!q.exec() || !q.next()) {
        return {};
    }
    const QByteArray blob = q.value(0).toByteArray();
    return QString::fromUtf8(QByteArray::fromBase64(blob));
}
