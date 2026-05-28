#include "notesdatabase.h"

#include <QCoreApplication>
#include <QDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>
#include <QtDebug>

NotesDatabase::NotesDatabase(QObject *parent)
    : QObject(parent)
    , m_connectionName(QStringLiteral("notes"))
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    m_path = dataDir + QStringLiteral("/notes.db");
}

NotesDatabase::~NotesDatabase()
{
    if (QSqlDatabase::contains(m_connectionName)) {
        {
            QSqlDatabase db = QSqlDatabase::database(m_connectionName);
            if (db.isOpen()) {
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

QSqlDatabase NotesDatabase::database() const
{
    return QSqlDatabase::database(m_connectionName);
}

bool NotesDatabase::open()
{
    if (!ensureDirectory()) {
        return false;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(m_path);
    if (!db.open()) {
        qWarning() << "Could not open notes database" << m_path << db.lastError().text();
        return false;
    }

    // WAL keeps reads non-blocking while the daemon writes from another
    // process. foreign_keys is off by default in SQLite, turn it on.
    QSqlQuery pragma(db);
    pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    pragma.exec(QStringLiteral("PRAGMA synchronous = NORMAL"));

    return applyMigrations();
}

bool NotesDatabase::ensureDirectory()
{
    QFileInfo info(m_path);
    QDir dir;
    if (!dir.mkpath(info.absolutePath())) {
        qWarning() << "Could not create data directory" << info.absolutePath();
        return false;
    }
    return true;
}

int NotesDatabase::currentSchemaVersion()
{
    QSqlQuery q(database());
    if (!q.exec(QStringLiteral("PRAGMA user_version"))) {
        return 0;
    }
    if (q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}

void NotesDatabase::setSchemaVersion(int version)
{
    QSqlQuery q(database());
    q.exec(QStringLiteral("PRAGMA user_version = %1").arg(version));
}

bool NotesDatabase::applyMigrations()
{
    const int current = currentSchemaVersion();
    if (current == kCurrentSchemaVersion) {
        return true;
    }
    if (current > kCurrentSchemaVersion) {
        qWarning() << "Database schema is newer than this build expects:"
                   << current << ">" << kCurrentSchemaVersion;
        return false;
    }

    QSqlDatabase db = database();
    db.transaction();

    if (current < 1 && !runSchemaV1()) {
        db.rollback();
        return false;
    }

    setSchemaVersion(kCurrentSchemaVersion);
    return db.commit();
}

bool NotesDatabase::runSchemaV1()
{
    QSqlDatabase db = database();
    QSqlQuery q(db);

    const QStringList ddl = {
        QStringLiteral(
            "CREATE TABLE accounts ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  display_name TEXT NOT NULL,"
            "  imap_host TEXT NOT NULL,"
            "  imap_port INTEGER NOT NULL DEFAULT 993,"
            "  security TEXT NOT NULL DEFAULT 'ssl',"
            "  username TEXT NOT NULL,"
            "  password_blob BLOB,"
            "  notes_root TEXT NOT NULL DEFAULT 'Notes',"
            "  hierarchy_delimiter TEXT NOT NULL DEFAULT '/',"
            "  default_folder_id INTEGER,"
            "  last_sync INTEGER,"
            "  sync_status TEXT NOT NULL DEFAULT 'never',"
            "  last_error TEXT"
            ")"),

        QStringLiteral(
            "CREATE TABLE folders ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
            "  parent_id INTEGER REFERENCES folders(id) ON DELETE CASCADE,"
            "  full_path TEXT NOT NULL,"
            "  display_name TEXT NOT NULL,"
            "  uid_validity INTEGER,"
            "  highest_uid INTEGER,"
            "  is_trash INTEGER NOT NULL DEFAULT 0,"
            "  UNIQUE(account_id, full_path)"
            ")"),

        QStringLiteral(
            "CREATE TABLE notes ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  uuid TEXT NOT NULL UNIQUE,"
            "  account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
            "  folder_id INTEGER NOT NULL REFERENCES folders(id) ON DELETE CASCADE,"
            "  title TEXT NOT NULL DEFAULT '',"
            "  body_html TEXT NOT NULL DEFAULT '',"
            "  format TEXT NOT NULL DEFAULT 'rich',"
            "  created INTEGER NOT NULL,"
            "  last_modified INTEGER NOT NULL,"
            "  pinned INTEGER NOT NULL DEFAULT 0,"
            "  locally_dirty INTEGER NOT NULL DEFAULT 0,"
            "  server_uid INTEGER,"
            "  server_modseq INTEGER"
            ")"),

        QStringLiteral("CREATE INDEX idx_notes_folder ON notes(folder_id, last_modified DESC)"),
        QStringLiteral("CREATE INDEX idx_notes_account ON notes(account_id)"),

        QStringLiteral(
            "CREATE TABLE attachments ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  note_id INTEGER NOT NULL REFERENCES notes(id) ON DELETE CASCADE,"
            "  filename TEXT NOT NULL,"
            "  mime_type TEXT NOT NULL,"
            "  size INTEGER NOT NULL,"
            "  local_path TEXT,"
            "  downloaded INTEGER NOT NULL DEFAULT 0"
            ")"),

        // Tombstones survive note deletion so we can win edit-vs-delete races
        // on the next sync. account_id and folder_full_path are kept as plain
        // values (no FK) so a CASCADE delete on the original folder doesn't
        // wipe the tombstone we still need to report upstream.
        QStringLiteral(
            "CREATE TABLE tombstones ("
            "  uuid TEXT PRIMARY KEY,"
            "  account_id INTEGER NOT NULL,"
            "  folder_full_path TEXT NOT NULL,"
            "  deleted_at INTEGER NOT NULL,"
            "  server_uid INTEGER"
            ")"),

        QStringLiteral(
            "CREATE TABLE offline_queue ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  account_id INTEGER NOT NULL,"
            "  note_uuid TEXT,"
            "  operation TEXT NOT NULL,"
            "  payload TEXT,"
            "  created_at INTEGER NOT NULL,"
            "  attempts INTEGER NOT NULL DEFAULT 0"
            ")"),

        // Full-text search across title + body. Kept in sync with the notes
        // table via triggers below. Searches go against `notes_fts`; we filter
        // by account via a JOIN on notes.id.
        //
        // remove_diacritics 1 is safe on SQLite 3.7.4+; value 2 needs 3.27+.
        // fts5 itself has been compiled into Mer/Sailfish SQLite since SFOS 3.x,
        // but we handle its absence gracefully (full-text search is degraded,
        // not fatal).
        QStringLiteral(
            "CREATE VIRTUAL TABLE notes_fts USING fts5("
            "  title, body, content='notes', content_rowid='id',"
            "  tokenize='unicode61 remove_diacritics 1'"
            ")"),

        QStringLiteral(
            "CREATE TRIGGER notes_ai AFTER INSERT ON notes BEGIN"
            "  INSERT INTO notes_fts(rowid, title, body) VALUES (new.id, new.title, new.body_html);"
            "END"),

        QStringLiteral(
            "CREATE TRIGGER notes_ad AFTER DELETE ON notes BEGIN"
            "  INSERT INTO notes_fts(notes_fts, rowid, title, body) VALUES ('delete', old.id, old.title, old.body_html);"
            "END"),

        QStringLiteral(
            "CREATE TRIGGER notes_au AFTER UPDATE ON notes BEGIN"
            "  INSERT INTO notes_fts(notes_fts, rowid, title, body) VALUES ('delete', old.id, old.title, old.body_html);"
            "  INSERT INTO notes_fts(rowid, title, body) VALUES (new.id, new.title, new.body_html);"
            "END"),
    };

    for (const QString &statement : ddl) {
        if (!q.exec(statement)) {
            // FTS5 virtual table and its triggers are optional: if the SQLite
            // build on this device doesn't include fts5, skip them with a
            // warning instead of aborting the whole migration.  The search
            // feature will be degraded (falls back to LIKE queries) but the
            // rest of the app works normally.
            const bool isFts = statement.contains(QStringLiteral("notes_fts"), Qt::CaseInsensitive);
            qWarning() << (isFts ? "Schema v1 FTS5 (optional) failed — search degraded:"
                                 : "Schema v1 failed:")
                       << q.lastError().text();
            if (!isFts) return false;
        }
    }
    return true;
}
