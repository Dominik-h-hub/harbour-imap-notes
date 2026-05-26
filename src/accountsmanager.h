#ifndef ACCOUNTSMANAGER_H
#define ACCOUNTSMANAGER_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class NotesDatabase;

class AccountsManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY accountsChanged)

public:
    explicit AccountsManager(NotesDatabase *db, QObject *parent = nullptr);

    int count() const;

    Q_INVOKABLE QVariantList listAccounts() const;
    Q_INVOKABLE QVariantMap account(qint64 id) const;

    // Creates an account row. Returns the new account id, or -1 on error.
    // Password is stored separately (see setPassword) so callers can later
    // wire it to a real secrets backend without changing the schema.
    Q_INVOKABLE qint64 createAccount(const QVariantMap &data);
    Q_INVOKABLE bool updateAccount(qint64 id, const QVariantMap &data);
    Q_INVOKABLE bool removeAccount(qint64 id);

    Q_INVOKABLE bool setPassword(qint64 accountId, const QString &password);
    QString password(qint64 accountId) const;

signals:
    void accountsChanged();

private:
    NotesDatabase *m_db;
};

#endif
