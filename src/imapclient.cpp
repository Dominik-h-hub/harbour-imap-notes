// IMAP4rev1 client implemented on top of QSslSocket.
// No external libraries required — uses only Qt5Network.
//
// All public methods are synchronous/blocking and are designed to run on a
// dedicated worker thread.  The implementation follows RFC 3501 (IMAP4rev1),
// RFC 2177 (IDLE), RFC 6851 (MOVE), and RFC 4315 (UIDPLUS APPENDUID).

#include "imapclient.h"

#include <QSslSocket>
#include <QtDebug>

#include <fcntl.h>
#include <sys/select.h>
#include <unistd.h>

// ────────────────────────────────────────────────────────────────────────────
//  Constants
// ────────────────────────────────────────────────────────────────────────────

static const int kConnectTimeoutMs = 30000;
static const int kReadTimeoutMs    = 30000;
static const int kWriteTimeoutMs   = 15000;

// ────────────────────────────────────────────────────────────────────────────
//  Internal helpers
// ────────────────────────────────────────────────────────────────────────────

namespace {

// Return a properly IMAP-quoted (double-quoted) representation of a UTF-8
// mailbox name.
QByteArray quotedMailbox(const QString &name)
{
    const QByteArray utf8 = name.toUtf8();
    bool needsQuote = utf8.isEmpty();
    for (char c : utf8) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc > 127 || c == ' ' || c == '(' || c == ')' || c == '{' ||
            c == '"' || c == '\\' || c == '%' || c == '*') {
            needsQuote = true;
            break;
        }
    }
    if (!needsQuote) return utf8;

    QByteArray q;
    q.reserve(utf8.size() + 2);
    q += '"';
    for (char c : utf8) {
        if (c == '"' || c == '\\') q += '\\';
        q += c;
    }
    q += '"';
    return q;
}

// Build a comma-separated UID set string from a list.
QByteArray uidSet(const QList<quint32> &uids)
{
    QByteArray s;
    for (int i = 0; i < uids.size(); ++i) {
        if (i > 0) s += ',';
        s += QByteArray::number(uids[i]);
    }
    return s;
}

// Extract the content between the *first* pair of square brackets in a line,
// e.g. "* OK [UIDVALIDITY 42] some text" → "UIDVALIDITY 42".
QByteArray bracketContent(const QByteArray &line)
{
    int open  = line.indexOf('[');
    int close = line.indexOf(']', open);
    if (open < 0 || close <= open) return {};
    return line.mid(open + 1, close - open - 1);
}

// Scan the parenthesised FETCH attribute list (the part after "N FETCH ")
// for an attribute named `attrName` and return its value.
// Literal data (already inlined by readResponse) appears as:
//   ATTR {N}\n<N bytes>\n
QByteArray fetchAttrValue(const QByteArray &block, const QByteArray &attrName)
{
    const QByteArray upper = block.toUpper();
    const QByteArray key   = attrName.toUpper();

    int pos = 0;
    while ((pos = upper.indexOf(key, pos)) >= 0) {
        bool leftOk  = (pos == 0 || upper[pos - 1] == ' '
                        || upper[pos - 1] == '(' || upper[pos - 1] == '\n');
        if (!leftOk) { ++pos; continue; }

        int after = pos + key.size();
        // Skip optional "[…]" for BODY[…]
        if (after < upper.size() && upper[after] == '[') {
            int end = upper.indexOf(']', after);
            if (end >= 0) after = end + 1;
        }
        // Skip spaces
        while (after < block.size() && block[after] == ' ') ++after;
        if (after >= block.size()) break;

        char ch = block[after];
        if (ch == '{') {
            // Inlined literal: {N}\n<data>
            int brace = block.indexOf('}', after);
            if (brace < 0) break;
            bool ok;
            int len = block.mid(after + 1, brace - after - 1).toInt(&ok);
            if (!ok || len < 0) break;
            int dataStart = brace + 1;
            if (dataStart < block.size() && block[dataStart] == '\n') ++dataStart;
            return block.mid(dataStart, len);
        } else if (ch == '(') {
            int depth = 0, start = after;
            for (int i = after; i < block.size(); ++i) {
                if (block[i] == '(') ++depth;
                else if (block[i] == ')') {
                    if (--depth == 0) return block.mid(start, i - start + 1);
                }
            }
        } else if (ch == '"') {
            int end = after + 1;
            while (end < block.size()) {
                if (block[end] == '\\') ++end;
                else if (block[end] == '"') break;
                ++end;
            }
            return block.mid(after + 1, end - after - 1);
        } else {
            int end = after;
            while (end < block.size() && block[end] != ' ' &&
                   block[end] != ')' && block[end] != '\n') ++end;
            return block.mid(after, end - after);
        }
        ++pos;
    }
    return {};
}

} // namespace

// ────────────────────────────────────────────────────────────────────────────
//  Construction / destruction
// ────────────────────────────────────────────────────────────────────────────

ImapClient::ImapClient(QObject *parent)
    : QObject(parent)
{
}

ImapClient::~ImapClient()
{
    disconnectFromHost();
    if (m_cancelPipe[0] != -1) ::close(m_cancelPipe[0]);
    if (m_cancelPipe[1] != -1) ::close(m_cancelPipe[1]);
    m_cancelPipe[0] = m_cancelPipe[1] = -1;
}

// ────────────────────────────────────────────────────────────────────────────
//  Low-level I/O
// ────────────────────────────────────────────────────────────────────────────

QByteArray ImapClient::readPhysicalLine()
{
    QByteArray line;
    while (true) {
        while (m_socket->bytesAvailable() == 0) {
            if (!m_socket->waitForReadyRead(kReadTimeoutMs)) {
                setError(QStringLiteral("Read timeout"), m_socket->errorString());
                return {};
            }
        }
        char c;
        if (m_socket->read(&c, 1) != 1) break;
        if (c == '\n') break;
        if (c != '\r') line += c;
    }
    return line;
}

QByteArray ImapClient::readBytes(int n)
{
    QByteArray data;
    data.reserve(n);
    while (data.size() < n) {
        if (m_socket->bytesAvailable() == 0) {
            if (!m_socket->waitForReadyRead(kReadTimeoutMs)) {
                setError(QStringLiteral("Read timeout (literal)"), m_socket->errorString());
                return data;
            }
        }
        QByteArray chunk = m_socket->read(n - data.size());
        if (chunk.isEmpty()) break;
        data += chunk;
    }
    return data;
}

// ────────────────────────────────────────────────────────────────────────────
//  Response reader
// ────────────────────────────────────────────────────────────────────────────

ImapClient::ImapResponse ImapClient::readResponse(const QByteArray &tag)
{
    ImapResponse resp;
    const QByteArray tagPrefix = tag + ' ';

    while (true) {
        QByteArray line = readPhysicalLine();
        if (line.isEmpty() && (!m_socket || !m_socket->isOpen())) {
            resp.ok = false;
            return resp;
        }

        if (line.startsWith(tagPrefix)) {
            resp.tagLine = line;
            resp.ok = line.mid(tagPrefix.size()).startsWith("OK");
            return resp;
        }

        // Inline literal data: a line ending with {N} is immediately followed
        // by N raw bytes, then another "continuation" physical line.
        // We may need several rounds (chained literals are rare but possible).
        while (line.endsWith('}')) {
            int open = line.lastIndexOf('{');
            if (open < 0) break;
            bool numOk;
            int litLen = line.mid(open + 1, line.size() - open - 2).toInt(&numOk);
            if (!numOk || litLen < 0) break;
            QByteArray literal = readBytes(litLen);
            line += '\n';
            line += literal;
            QByteArray cont = readPhysicalLine();
            line += '\n';
            line += cont;
        }

        resp.untagged.append(line);
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  Command helpers
// ────────────────────────────────────────────────────────────────────────────

QByteArray ImapClient::nextTag()
{
    return QByteArrayLiteral("A") + QByteArray::number(++m_tagCounter);
}

ImapClient::ImapResponse ImapClient::runCommand(const QByteArray &cmdWithoutTag)
{
    const QByteArray tag  = nextTag();
    const QByteArray line = tag + ' ' + cmdWithoutTag + "\r\n";
    m_socket->write(line);
    if (!m_socket->waitForBytesWritten(kWriteTimeoutMs)) {
        setError(QStringLiteral("Write"), m_socket->errorString());
        ImapResponse err;
        return err;
    }
    return readResponse(tag);
}

ImapClient::ImapResponse ImapClient::runAppend(const QString &folder,
                                                const QByteArray &rawMessage,
                                                quint32 *newUid)
{
    const QByteArray tag = nextTag();
    const QByteArray cmd = tag + " APPEND " + quotedMailbox(folder)
                           + " {" + QByteArray::number(rawMessage.size()) + "}\r\n";
    m_socket->write(cmd);
    if (!m_socket->waitForBytesWritten(kWriteTimeoutMs)) {
        setError(QStringLiteral("APPEND header write"), m_socket->errorString());
        ImapResponse err; return err;
    }

    // Wait for server continuation "+"
    QByteArray cont = readPhysicalLine();
    if (!cont.startsWith('+')) {
        ImapResponse err;
        err.tagLine = cont;
        err.ok = false;
        return err;
    }

    m_socket->write(rawMessage);
    m_socket->write("\r\n");
    if (!m_socket->waitForBytesWritten(kWriteTimeoutMs)) {
        setError(QStringLiteral("APPEND literal write"), m_socket->errorString());
        ImapResponse err; return err;
    }

    ImapResponse resp = readResponse(tag);

    if (resp.ok && newUid) {
        *newUid = 0;
        const QByteArray bc = bracketContent(resp.tagLine);
        if (bc.toUpper().startsWith("APPENDUID")) {
            const QList<QByteArray> parts = bc.split(' ');
            if (parts.size() >= 3) {
                bool ok;
                quint32 u = parts[2].trimmed().toUInt(&ok);
                if (ok) *newUid = u;
            }
        }
    }
    return resp;
}

// ────────────────────────────────────────────────────────────────────────────
//  Error recording
// ────────────────────────────────────────────────────────────────────────────

void ImapClient::setError(const QString &context, const QString &detail)
{
    m_lastError = detail.isEmpty()
                  ? context
                  : context + QStringLiteral(": ") + detail;
    emit error(m_lastError);
    qWarning() << "ImapClient:" << m_lastError;
}

// ────────────────────────────────────────────────────────────────────────────
//  Connection
// ────────────────────────────────────────────────────────────────────────────

bool ImapClient::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

bool ImapClient::connectToHost(const QString &host, int port, Security security)
{
    if (m_socket) disconnectFromHost();

    m_socket = new QSslSocket(this);
    // Allow self-signed / internal CA certs; add proper pinning as needed.
    m_socket->setPeerVerifyMode(QSslSocket::VerifyNone);

    if (security == ImplicitSsl) {
        m_socket->connectToHostEncrypted(host, static_cast<quint16>(port));
        if (!m_socket->waitForEncrypted(kConnectTimeoutMs)) {
            setError(QStringLiteral("SSL connect to %1:%2").arg(host).arg(port),
                     m_socket->errorString());
            delete m_socket; m_socket = nullptr;
            return false;
        }
    } else {
        m_socket->connectToHost(host, static_cast<quint16>(port));
        if (!m_socket->waitForConnected(kConnectTimeoutMs)) {
            setError(QStringLiteral("connect to %1:%2").arg(host).arg(port),
                     m_socket->errorString());
            delete m_socket; m_socket = nullptr;
            return false;
        }
    }

    // Read server greeting
    QByteArray greeting = readPhysicalLine();
    if (!greeting.startsWith("* OK") && !greeting.startsWith("* PREAUTH")) {
        setError(QStringLiteral("Unexpected greeting"), QString::fromLatin1(greeting));
        delete m_socket; m_socket = nullptr;
        return false;
    }

    if (security == StartTls) {
        ImapResponse r = runCommand("STARTTLS");
        if (!r.ok) {
            setError(QStringLiteral("STARTTLS"), QString::fromLatin1(r.tagLine));
            delete m_socket; m_socket = nullptr;
            return false;
        }
        m_socket->startClientEncryption();
        if (!m_socket->waitForEncrypted(kConnectTimeoutMs)) {
            setError(QStringLiteral("TLS handshake"), m_socket->errorString());
            delete m_socket; m_socket = nullptr;
            return false;
        }
    }

    return true;
}

bool ImapClient::login(const QString &username, const QString &password)
{
    if (!m_socket) { m_lastError = QStringLiteral("Not connected"); return false; }

    auto escape = [](const QString &s) -> QByteArray {
        QByteArray out;
        out += '"';
        for (const QChar &c : s) {
            if (c == QLatin1Char('"') || c == QLatin1Char('\\')) out += '\\';
            out += c.toLatin1();
        }
        out += '"';
        return out;
    };

    ImapResponse r = runCommand("LOGIN " + escape(username) + " " + escape(password));
    if (!r.ok) {
        setError(QStringLiteral("LOGIN"), QString::fromLatin1(r.tagLine));
        return false;
    }
    return refreshCapabilities();
}

bool ImapClient::refreshCapabilities()
{
    m_capabilities.clear();
    ImapResponse r = runCommand("CAPABILITY");
    for (const QByteArray &line : r.untagged) {
        if (!line.toUpper().startsWith("* CAPABILITY")) continue;
        const QList<QByteArray> tokens = line.split(' ');
        for (int i = 2; i < tokens.size(); ++i)
            m_capabilities.insert(QString::fromLatin1(tokens[i].trimmed()).toUpper());
    }
    if (r.ok) {
        const QByteArray bc = bracketContent(r.tagLine);
        if (bc.toUpper().startsWith("CAPABILITY")) {
            const QList<QByteArray> tokens = bc.split(' ');
            for (int i = 1; i < tokens.size(); ++i)
                m_capabilities.insert(QString::fromLatin1(tokens[i].trimmed()).toUpper());
        }
    }
    return true;
}

void ImapClient::disconnectFromHost()
{
    if (!m_socket) return;
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        const QByteArray tag = nextTag();
        m_socket->write(tag + " LOGOUT\r\n");
        m_socket->waitForBytesWritten(kWriteTimeoutMs);
        m_socket->waitForReadyRead(3000);
        m_socket->disconnectFromHost();
    }
    m_socket->deleteLater();
    m_socket = nullptr;
    m_selectedFolder.clear();
    m_capabilities.clear();
    emit disconnected();
}

bool ImapClient::hasCapability(const QString &name) const
{
    return m_capabilities.contains(name.toUpper());
}

// ────────────────────────────────────────────────────────────────────────────
//  Folder operations
// ────────────────────────────────────────────────────────────────────────────

bool ImapClient::listFolders(const QString &reference,
                              const QString &pattern,
                              QList<FolderInfo> *out)
{
    if (!m_socket || !out) return false;

    ImapResponse r = runCommand("LIST " + quotedMailbox(reference)
                                + " " + quotedMailbox(pattern));
    if (!r.ok) {
        setError(QStringLiteral("LIST"), QString::fromLatin1(r.tagLine));
        return false;
    }

    for (const QByteArray &line : r.untagged) {
        if (!line.toUpper().startsWith("* LIST")) continue;

        FolderInfo info;

        const int flagsOpen  = line.indexOf('(');
        const int flagsClose = line.indexOf(')', flagsOpen);
        if (flagsOpen >= 0 && flagsClose > flagsOpen) {
            const QByteArray flags =
                line.mid(flagsOpen + 1, flagsClose - flagsOpen - 1).toUpper();
            if (flags.contains("\\NOSELECT") || flags.contains("NOSELECT"))
                info.selectable = false;
        }

        QByteArray rest = line.mid(flagsClose + 1).trimmed();

        // Delimiter
        QByteArray delim;
        if (rest.startsWith("NIL")) {
            delim = m_hierarchyDelimiter.toLatin1();
            rest  = rest.mid(3).trimmed();
        } else if (rest.startsWith('"')) {
            int end = rest.indexOf('"', 1);
            if (end > 0) { delim = rest.mid(1, end - 1); rest = rest.mid(end + 1).trimmed(); }
        }

        if (!delim.isEmpty()) {
            info.delimiter     = QString::fromLatin1(delim);
            m_hierarchyDelimiter = info.delimiter;
        } else {
            info.delimiter = m_hierarchyDelimiter;
        }

        // Name
        if (rest.startsWith('"')) {
            int end = 1;
            while (end < rest.size()) {
                if (rest[end] == '\\') ++end;
                else if (rest[end] == '"') break;
                ++end;
            }
            info.fullPath = QString::fromUtf8(rest.mid(1, end - 1));
        } else {
            info.fullPath = QString::fromUtf8(rest);
        }

        if (!info.fullPath.isEmpty()) out->append(info);
    }
    return true;
}

bool ImapClient::createFolder(const QString &path)
{
    if (!m_socket) return false;
    ImapResponse r = runCommand("CREATE " + quotedMailbox(path));
    if (!r.ok) { setError(QStringLiteral("CREATE"), QString::fromLatin1(r.tagLine)); return false; }
    return true;
}

bool ImapClient::deleteFolder(const QString &path)
{
    if (!m_socket) return false;
    ImapResponse r = runCommand("DELETE " + quotedMailbox(path));
    if (!r.ok) { setError(QStringLiteral("DELETE"), QString::fromLatin1(r.tagLine)); return false; }
    return true;
}

bool ImapClient::renameFolder(const QString &oldPath, const QString &newPath)
{
    if (!m_socket) return false;
    ImapResponse r = runCommand("RENAME " + quotedMailbox(oldPath)
                                + " " + quotedMailbox(newPath));
    if (!r.ok) { setError(QStringLiteral("RENAME"), QString::fromLatin1(r.tagLine)); return false; }
    return true;
}

// ────────────────────────────────────────────────────────────────────────────
//  Message operations
// ────────────────────────────────────────────────────────────────────────────

bool ImapClient::selectFolder(const QString &path, SelectResult *out)
{
    if (!m_socket) return false;
    ImapResponse r = runCommand("SELECT " + quotedMailbox(path));
    if (!r.ok) {
        setError(QStringLiteral("SELECT"), QString::fromLatin1(r.tagLine));
        return false;
    }
    m_selectedFolder = path;
    if (out) {
        *out = SelectResult{};
        for (const QByteArray &line : r.untagged) {
            if (line.endsWith(" EXISTS")) {
                bool ok;
                quint32 n = line.mid(2, line.indexOf(' ', 2) - 2).toUInt(&ok);
                if (ok) out->exists = n;
            }
            const QByteArray bc = bracketContent(line);
            if (!bc.isEmpty()) {
                const QList<QByteArray> parts = bc.split(' ');
                const QByteArray kw = parts.value(0).toUpper();
                bool ok;
                if (kw == "UIDVALIDITY" && parts.size() > 1) {
                    quint32 v = parts[1].toUInt(&ok);
                    if (ok) out->uidValidity = v;
                } else if (kw == "UIDNEXT" && parts.size() > 1) {
                    quint32 v = parts[1].toUInt(&ok);
                    if (ok) out->uidNext = v;
                }
            }
        }
    }
    return true;
}

bool ImapClient::fetchAllUids(QList<quint32> *out)
{
    return fetchUidsSince(1, out);
}

bool ImapClient::fetchUidsSince(quint32 sinceUid, QList<quint32> *out)
{
    if (!m_socket || !out) return false;

    const QByteArray range = QByteArray::number(sinceUid) + ":*";
    ImapResponse r = runCommand("UID FETCH " + range + " (UID)");
    if (!r.ok) {
        setError(QStringLiteral("UID FETCH (UID)"), QString::fromLatin1(r.tagLine));
        return false;
    }

    for (const QByteArray &line : r.untagged) {
        if (!line.toUpper().contains(" FETCH ")) continue;
        const int paren = line.indexOf('(');
        if (paren < 0) continue;
        const QByteArray body = line.mid(paren);
        const QByteArray val  = fetchAttrValue(body, "UID");
        if (!val.isEmpty()) {
            bool ok;
            quint32 uid = val.toUInt(&ok);
            if (ok && uid >= sinceUid) out->append(uid);
        }
    }
    return true;
}

bool ImapClient::fetchFullMessage(quint32 uid, QByteArray *out)
{
    if (!m_socket || !out) return false;

    ImapResponse r = runCommand("UID FETCH " + QByteArray::number(uid)
                                + " (BODY.PEEK[])");
    if (!r.ok) {
        setError(QStringLiteral("UID FETCH BODY.PEEK[]"), QString::fromLatin1(r.tagLine));
        return false;
    }

    for (const QByteArray &line : r.untagged) {
        if (!line.toUpper().contains(" FETCH ")) continue;
        const int paren = line.indexOf('(');
        if (paren < 0) continue;
        const QByteArray body = line.mid(paren);
        QByteArray val = fetchAttrValue(body, "BODY[]");
        if (val.isEmpty()) val = fetchAttrValue(body, "RFC822");
        if (!val.isEmpty()) { *out = val; return true; }
    }

    setError(QStringLiteral("UID FETCH BODY.PEEK[]"),
             QStringLiteral("Empty body for UID %1").arg(uid));
    return false;
}

bool ImapClient::fetchHeader(quint32 uid, const QString &headerName, QString *out)
{
    if (!m_socket || !out) return false;

    const QByteArray hdr = headerName.toLatin1();
    ImapResponse r = runCommand(
        "UID FETCH " + QByteArray::number(uid)
        + " (BODY.PEEK[HEADER.FIELDS (" + hdr + ")])");
    if (!r.ok) {
        setError(QStringLiteral("UID FETCH HEADER"), QString::fromLatin1(r.tagLine));
        return false;
    }

    for (const QByteArray &line : r.untagged) {
        if (!line.toUpper().contains(" FETCH ")) continue;
        // The raw block may contain "BODY[HEADER.FIELDS (Name)] {N}\n<data>\n)"
        const int litOpen  = line.indexOf('{');
        const int litClose = line.indexOf('}', litOpen);
        if (litOpen < 0 || litClose <= litOpen) continue;
        bool ok;
        int litLen = line.mid(litOpen + 1, litClose - litOpen - 1).toInt(&ok);
        if (!ok || litLen <= 0) { *out = QString(); return true; }
        int dataStart = litClose + 1;
        if (dataStart < line.size() && line[dataStart] == '\n') ++dataStart;
        const QByteArray raw = line.mid(dataStart, litLen);

        const QByteArray needle = hdr + ':';
        const int idx = raw.toLower().indexOf(needle.toLower());
        if (idx < 0) { *out = QString(); return true; }
        int end = raw.indexOf('\n', idx);
        if (end < 0) end = raw.size();
        *out = QString::fromUtf8(
                   raw.mid(idx + needle.size(), end - idx - needle.size())
               ).trimmed();
        return true;
    }

    *out = QString();
    return true;
}

bool ImapClient::appendMessage(const QString &folder,
                                const QByteArray &rawMessage,
                                quint32 *newUid)
{
    if (!m_socket) return false;
    ImapResponse r = runAppend(folder, rawMessage, newUid);
    if (!r.ok) {
        setError(QStringLiteral("APPEND"), QString::fromLatin1(r.tagLine));
        return false;
    }
    return true;
}

bool ImapClient::markDeletedAndExpunge(const QList<quint32> &uids)
{
    if (!m_socket) return uids.isEmpty();
    if (uids.isEmpty()) return true;

    ImapResponse r = runCommand("UID STORE " + uidSet(uids)
                                + " +FLAGS.SILENT (\\Deleted)");
    if (!r.ok) {
        setError(QStringLiteral("UID STORE \\Deleted"), QString::fromLatin1(r.tagLine));
        return false;
    }
    ImapResponse er = runCommand("EXPUNGE");
    if (!er.ok) {
        setError(QStringLiteral("EXPUNGE"), QString::fromLatin1(er.tagLine));
        return false;
    }
    return true;
}

bool ImapClient::moveMessage(quint32 uid, const QString &destinationFolder)
{
    if (!m_socket) return false;
    const QByteArray us = QByteArray::number(uid);

    if (hasCapability(QStringLiteral("MOVE"))) {
        ImapResponse r = runCommand("UID MOVE " + us + " " + quotedMailbox(destinationFolder));
        if (!r.ok) {
            setError(QStringLiteral("UID MOVE"), QString::fromLatin1(r.tagLine));
            return false;
        }
        return true;
    }

    // Fallback: COPY + STORE \Deleted + EXPUNGE
    ImapResponse rc = runCommand("UID COPY " + us + " " + quotedMailbox(destinationFolder));
    if (!rc.ok) { setError(QStringLiteral("UID COPY"), QString::fromLatin1(rc.tagLine)); return false; }

    ImapResponse rs = runCommand("UID STORE " + us + " +FLAGS.SILENT (\\Deleted)");
    if (!rs.ok) { setError(QStringLiteral("UID STORE (move)"), QString::fromLatin1(rs.tagLine)); return false; }

    ImapResponse re = runCommand("EXPUNGE");
    if (!re.ok) { setError(QStringLiteral("EXPUNGE (move)"), QString::fromLatin1(re.tagLine)); return false; }

    return true;
}

// ────────────────────────────────────────────────────────────────────────────
//  IDLE  (RFC 2177)
// ────────────────────────────────────────────────────────────────────────────

bool ImapClient::waitForActivity(int maxSeconds)
{
    if (!m_socket) return false;
    if (!hasCapability(QStringLiteral("IDLE"))) {
        m_lastError = QStringLiteral("Server does not advertise IDLE");
        return false;
    }

    if (m_cancelPipe[0] == -1) {
        if (::pipe(m_cancelPipe) != 0) {
            m_lastError = QStringLiteral("Cannot create cancel pipe");
            return false;
        }
        ::fcntl(m_cancelPipe[0], F_SETFL, O_NONBLOCK);
        ::fcntl(m_cancelPipe[1], F_SETFL, O_NONBLOCK);
    }

    const QByteArray tag = nextTag();
    m_socket->write(tag + " IDLE\r\n");
    if (!m_socket->waitForBytesWritten(kWriteTimeoutMs)) {
        setError(QStringLiteral("IDLE write"), m_socket->errorString());
        return false;
    }

    // Read the "+" continuation
    QByteArray plus = readPhysicalLine();
    if (!plus.startsWith('+') && !plus.startsWith('*')) {
        setError(QStringLiteral("IDLE"),
                 QStringLiteral("Expected '+', got: ") + QString::fromLatin1(plus));
        return false;
    }

    const int imapFd   = static_cast<int>(m_socket->socketDescriptor());
    const int cancelFd = m_cancelPipe[0];
    const int nfds     = qMax(imapFd, cancelFd) + 1;

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(imapFd,   &rfds);
    FD_SET(cancelFd, &rfds);

    struct timeval tv;
    tv.tv_sec  = maxSeconds;
    tv.tv_usec = 0;

    const int sel = ::select(nfds, &rfds, nullptr, nullptr, &tv);

    if (sel > 0 && FD_ISSET(cancelFd, &rfds)) {
        char drain[64];
        while (::read(cancelFd, drain, sizeof(drain)) > 0) {}
    }

    m_socket->write("DONE\r\n");
    m_socket->waitForBytesWritten(kWriteTimeoutMs);

    ImapResponse r = readResponse(tag);
    if (!r.ok) {
        setError(QStringLiteral("IDLE DONE"), QString::fromLatin1(r.tagLine));
        return false;
    }

    if (sel < 0) { m_lastError = QStringLiteral("select() failed during IDLE"); return false; }
    if (sel == 0) return false; // timeout

    return FD_ISSET(imapFd, &rfds) != 0;
}

void ImapClient::cancelIdle()
{
    if (m_cancelPipe[1] == -1) return;
    const char b = 'x';
    // Suppress warn_unused_result: a failed write just means the IDLE loop
    // will time out naturally instead of being cancelled early — acceptable.
    if (::write(m_cancelPipe[1], &b, 1) < 0) {
        qWarning() << "ImapClient: cancelIdle write failed";
    }
}
