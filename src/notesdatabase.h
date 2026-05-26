#ifndef NOTESDATABASE_H
#define NOTESDATABASE_H

#include <QObject>
#include <QSqlDatabase>
#include <QString>

class NotesDatabase : public QObject
{
    Q_OBJECT

public:
    explicit NotesDatabase(QObject *parent = nullptr);
    ~NotesDatabase() override;

    bool open();
    QString path() const { return m_path; }
    QSqlDatabase database() const;

    // Schema version this build expects. Bump and add a migration when
    // the schema changes.
    static const int kCurrentSchemaVersion = 1;

private:
    bool ensureDirectory();
    bool applyMigrations();
    bool runSchemaV1();
    int currentSchemaVersion();
    void setSchemaVersion(int version);

    QString m_path;
    QString m_connectionName;
};

#endif
