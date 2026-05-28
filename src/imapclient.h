#ifndef IMAPCLIENT_H
#define IMAPCLIENT_H

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

class QSslSocket;

// Synchronous wrapper around QSslSocket that speaks IMAP4rev1.
// All public methods block while talking to the server, so an instance must
// run on a worker thread (the sync engine owns one ImapClient per account on
// its own QThread).
//
// The class is intentionally low-level: it covers the IMAP verbs we need and
// keeps higher-level decisions — what to fetch, how to merge with the local
// DB, how to handle Apple-Notes wire format — in the sync engine layer. Apple
// header serialisation lives in NoteMessage.
class ImapClient : public QObject
{
    Q_OBJECT
public:
    enum Security {
        ImplicitSsl,
        StartTls,
    };

    struct FolderInfo {
        QString fullPath;
        QString delimiter;
        bool selectable = true;
    };

    struct SelectResult {
        quint32 uidValidity = 0;
        quint32 uidNext = 0;
        quint32 exists = 0;
        quint64 highestModSeq = 0; // 0 when CONDSTORE is unavailable
    };

    explicit ImapClient(QObject *parent = nullptr);
    ~ImapClient() override;

    // ----- connection -----
    bool connectToHost(const QString &host, int port, Security security);
    bool login(const QString &username, const QString &password);
    void disconnectFromHost();
    bool isConnected() const;

    // Capabilities reported by the server after login.
    bool hasCapability(const QString &name) const;

    // ----- folder ops -----
    bool listFolders(const QString &reference,
                     const QString &pattern,
                     QList<FolderInfo> *out);
    bool createFolder(const QString &path);
    bool deleteFolder(const QString &path);
    bool renameFolder(const QString &oldPath, const QString &newPath);

    // ----- message ops (require selectFolder first) -----
    bool selectFolder(const QString &path, SelectResult *out);
    QString selectedFolder() const { return m_selectedFolder; }

    bool fetchAllUids(QList<quint32> *out);
    bool fetchUidsSince(quint32 sinceUid, QList<quint32> *out);
    bool fetchFullMessage(quint32 uid, QByteArray *out);
    bool fetchHeader(quint32 uid, const QString &headerName, QString *out);

    bool appendMessage(const QString &folder,
                       const QByteArray &rawMessage,
                       quint32 *newUid = nullptr);

    bool markDeletedAndExpunge(const QList<quint32> &uids);
    bool moveMessage(quint32 uid, const QString &destinationFolder);

    // ----- IDLE (RFC 2177) -----
    bool waitForActivity(int maxSeconds = 25 * 60);
    void cancelIdle();

    QString lastError() const { return m_lastError; }

signals:
    void disconnected();
    void error(const QString &message);

private:
    // Low-level I/O
    QByteArray readPhysicalLine();
    QByteArray readBytes(int n);

    // Mid-level response handling
    struct ImapResponse {
        QList<QByteArray> untagged; // lines/blocks starting with '*'
        QByteArray        tagLine;  // the TAG OK/NO/BAD line
        bool              ok = false;
    };
    ImapResponse readResponse(const QByteArray &tag);

    // Command helpers
    QByteArray   nextTag();
    ImapResponse runCommand(const QByteArray &cmdWithoutTag);
    // For APPEND-style commands that need a continuation (+) before sending data
    ImapResponse runAppend(const QString &folder,
                           const QByteArray &rawMessage,
                           quint32 *newUid);

    bool refreshCapabilities();
    void setError(const QString &context, const QString &detail = {});

    QSslSocket   *m_socket = nullptr;
    quint32       m_tagCounter = 0;
    QString       m_lastError;
    QString       m_selectedFolder;
    QString       m_hierarchyDelimiter = QStringLiteral("/");
    QSet<QString> m_capabilities;
    int           m_cancelPipe[2] = { -1, -1 };
};

#endif
