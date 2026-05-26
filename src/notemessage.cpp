#include "notemessage.h"

#include "note.h"

#include <QLocale>
#include <QRegularExpression>
#include <QStringList>
#include <QUuid>
#include <QtDebug>

namespace {

// RFC 5322 date format. Apple uses GMT in iOS notes, so we follow.
QString rfc2822Date(const QDateTime &dt)
{
    const QDateTime utc = dt.toUTC();
    // QLocale::c() guarantees English month/weekday names regardless of the
    // user's locale, which is required by RFC 5322.
    return QLocale::c().toString(utc, QStringLiteral("ddd, dd MMM yyyy hh:mm:ss")) + QStringLiteral(" +0000");
}

QString iso8601(const QDateTime &dt)
{
    return dt.toUTC().toString(Qt::ISODate);
}

// Encode an arbitrary UTF-8 string as RFC 2047 encoded-word (Q-encoded) when
// it contains non-ASCII bytes. Used for Subject and other header fields.
QByteArray encodeHeaderValue(const QString &value)
{
    const QByteArray utf8 = value.toUtf8();
    bool needsEncoding = false;
    for (char c : utf8) {
        if (static_cast<unsigned char>(c) > 0x7F || c == '\r' || c == '\n') {
            needsEncoding = true;
            break;
        }
    }
    if (!needsEncoding) {
        return utf8;
    }

    QByteArray encoded = "=?UTF-8?Q?";
    for (char c : utf8) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u == ' ') {
            encoded += '_';
        } else if (u < 0x21 || u > 0x7E || u == '=' || u == '?' || u == '_') {
            encoded += '=';
            encoded += QStringLiteral("%1").arg(u, 2, 16, QLatin1Char('0')).toUpper().toUtf8();
        } else {
            encoded += static_cast<char>(c);
        }
    }
    encoded += "?=";
    return encoded;
}

QByteArray base64Wrap(const QByteArray &raw)
{
    const QByteArray b64 = raw.toBase64();
    QByteArray out;
    out.reserve(b64.size() + b64.size() / 76 + 8);
    for (int i = 0; i < b64.size(); i += 76) {
        out += b64.mid(i, 76);
        out += "\r\n";
    }
    return out;
}

QString sanitizedUuid()
{
    QString s = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return s.toUpper();
}

// Headers are case-insensitive. Lower-case lookup helper.
QString headerValue(const QList<QPair<QString, QString>> &headers, const QString &name)
{
    const QString needle = name.toLower();
    for (const auto &h : headers) {
        if (h.first.toLower() == needle) {
            return h.second;
        }
    }
    return {};
}

// Parse an RFC 2047 encoded-word value. Falls back to the input string when no
// encoded-word is detected — the common case for ASCII-only headers.
QString decodeEncodedWord(const QString &in)
{
    static const QRegularExpression re(QStringLiteral(
        R"(=\?([^?]+)\?([QqBb])\?([^?]+)\?=)"));
    QString out;
    int pos = 0;
    auto it = re.globalMatch(in);
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        out.append(in.midRef(pos, m.capturedStart() - pos));
        const QString charset = m.captured(1);
        const QString encoding = m.captured(2).toUpper();
        QByteArray payload = m.captured(3).toLatin1();
        QByteArray decoded;
        if (encoding == QStringLiteral("Q")) {
            for (int i = 0; i < payload.size(); ++i) {
                const char c = payload.at(i);
                if (c == '_') {
                    decoded += ' ';
                } else if (c == '=' && i + 2 < payload.size()) {
                    bool ok = false;
                    const int v = payload.mid(i + 1, 2).toInt(&ok, 16);
                    if (ok) {
                        decoded += static_cast<char>(v);
                        i += 2;
                    }
                } else {
                    decoded += c;
                }
            }
        } else {
            decoded = QByteArray::fromBase64(payload);
        }
        if (charset.compare(QStringLiteral("UTF-8"), Qt::CaseInsensitive) == 0) {
            out.append(QString::fromUtf8(decoded));
        } else {
            // Other charsets are rare for iOS-emitted notes. Fall back to
            // latin-1 so we at least decode reversibly.
            out.append(QString::fromLatin1(decoded));
        }
        pos = m.capturedEnd();
    }
    out.append(in.midRef(pos));
    return out;
}

QDateTime parseRfc2822Date(const QString &value)
{
    QDateTime dt = QDateTime::fromString(value.left(31), Qt::RFC2822Date);
    if (!dt.isValid()) {
        // Some servers send slight variations. Try ISO 8601 as a fallback.
        dt = QDateTime::fromString(value, Qt::ISODate);
    }
    return dt;
}

// Split raw bytes into (headers, body). Header section ends at the first
// CRLFCRLF (or LFLF as a tolerant fallback).
bool splitHeadersBody(const QByteArray &raw, QByteArray *headers, QByteArray *body)
{
    int idx = raw.indexOf("\r\n\r\n");
    int sepLen = 4;
    if (idx < 0) {
        idx = raw.indexOf("\n\n");
        sepLen = 2;
    }
    if (idx < 0) {
        *headers = raw;
        body->clear();
        return true;
    }
    *headers = raw.left(idx);
    *body = raw.mid(idx + sepLen);
    return true;
}

QList<QPair<QString, QString>> parseHeaderBlock(const QByteArray &headerBlock)
{
    QList<QPair<QString, QString>> out;
    // Unfold continuation lines (lines starting with whitespace continue the
    // previous header) per RFC 5322 section 2.2.3.
    QByteArray unfolded;
    for (int i = 0; i < headerBlock.size(); ++i) {
        const char c = headerBlock.at(i);
        if (c == '\r' && i + 1 < headerBlock.size() && headerBlock.at(i + 1) == '\n') {
            if (i + 2 < headerBlock.size()
                && (headerBlock.at(i + 2) == ' ' || headerBlock.at(i + 2) == '\t')) {
                unfolded += ' ';
                i += 2;
                while (i + 1 < headerBlock.size()
                       && (headerBlock.at(i + 1) == ' ' || headerBlock.at(i + 1) == '\t')) {
                    ++i;
                }
                continue;
            }
            unfolded += '\n';
            ++i;
        } else if (c == '\n') {
            if (i + 1 < headerBlock.size()
                && (headerBlock.at(i + 1) == ' ' || headerBlock.at(i + 1) == '\t')) {
                unfolded += ' ';
                while (i + 1 < headerBlock.size()
                       && (headerBlock.at(i + 1) == ' ' || headerBlock.at(i + 1) == '\t')) {
                    ++i;
                }
                continue;
            }
            unfolded += '\n';
        } else {
            unfolded += c;
        }
    }

    for (const QByteArray &line : unfolded.split('\n')) {
        if (line.isEmpty()) continue;
        const int colon = line.indexOf(':');
        if (colon < 0) continue;
        const QString name = QString::fromLatin1(line.left(colon)).trimmed();
        const QString value = decodeEncodedWord(QString::fromUtf8(line.mid(colon + 1)).trimmed());
        out.append({ name, value });
    }
    return out;
}

QByteArray decodeQuotedPrintable(const QByteArray &in)
{
    QByteArray out;
    out.reserve(in.size());
    for (int i = 0; i < in.size(); ++i) {
        const char c = in.at(i);
        if (c == '=' && i + 1 < in.size()) {
            // Soft line break: =CRLF or =LF
            if (in.at(i + 1) == '\r' && i + 2 < in.size() && in.at(i + 2) == '\n') {
                i += 2;
                continue;
            }
            if (in.at(i + 1) == '\n') {
                i += 1;
                continue;
            }
            if (i + 2 < in.size()) {
                bool ok = false;
                const int v = in.mid(i + 1, 2).toInt(&ok, 16);
                if (ok) {
                    out += static_cast<char>(v);
                    i += 2;
                    continue;
                }
            }
        }
        out += c;
    }
    return out;
}

QByteArray decodeBody(const QByteArray &body, const QString &transferEncoding)
{
    const QString te = transferEncoding.trimmed().toLower();
    if (te == QStringLiteral("base64")) {
        return QByteArray::fromBase64(body);
    }
    if (te == QStringLiteral("quoted-printable")) {
        return decodeQuotedPrintable(body);
    }
    return body;
}

// Extract `name="..."` parameter from a Content-Type or Content-Disposition.
QString mimeParam(const QString &header, const QString &name)
{
    const QString needle = name.toLower();
    for (const QString &raw : header.split(QLatin1Char(';'))) {
        QString part = raw.trimmed();
        const int eq = part.indexOf(QLatin1Char('='));
        if (eq < 0) continue;
        if (part.left(eq).trimmed().toLower() != needle) continue;
        QString value = part.mid(eq + 1).trimmed();
        if (value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"'))) {
            value = value.mid(1, value.size() - 2);
        }
        return value;
    }
    return {};
}

QString mimeType(const QString &contentType)
{
    return contentType.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
}

void parsePart(const QByteArray &part, NoteMessage::Parsed *out)
{
    QByteArray headerBlock;
    QByteArray body;
    splitHeadersBody(part, &headerBlock, &body);
    const auto headers = parseHeaderBlock(headerBlock);

    const QString contentType = headerValue(headers, QStringLiteral("Content-Type"));
    const QString transferEncoding = headerValue(headers, QStringLiteral("Content-Transfer-Encoding"));
    const QString disposition = headerValue(headers, QStringLiteral("Content-Disposition"));
    const QByteArray decoded = decodeBody(body, transferEncoding);
    const QString type = mimeType(contentType);

    if (type.startsWith(QStringLiteral("text/html"))) {
        out->bodyHtml = QString::fromUtf8(decoded);
        return;
    }
    if (type.startsWith(QStringLiteral("text/plain")) && out->bodyHtml.isEmpty()) {
        // Plaintext-only notes still display in iOS — wrap them back in HTML.
        out->bodyHtml = QStringLiteral("<pre>%1</pre>")
                            .arg(QString::fromUtf8(decoded).toHtmlEscaped());
        return;
    }

    NoteMessage::Attachment att;
    att.filename = mimeParam(disposition, QStringLiteral("filename"));
    if (att.filename.isEmpty()) {
        att.filename = mimeParam(contentType, QStringLiteral("name"));
    }
    att.mimeType = type;
    att.data = decoded;
    if (att.filename.isEmpty()) {
        att.filename = QStringLiteral("attachment");
    }
    out->attachments.append(att);
}

void splitMultipart(const QByteArray &body, const QString &boundary, QList<QByteArray> *parts)
{
    const QByteArray delim = "--" + boundary.toUtf8();
    int idx = body.indexOf(delim);
    while (idx >= 0) {
        // Skip over the boundary line.
        int partStart = idx + delim.size();
        if (partStart < body.size() && body.at(partStart) == '-' && body.at(partStart + 1) == '-') {
            break; // closing boundary
        }
        // Move past the trailing CRLF on the boundary line.
        while (partStart < body.size() && (body.at(partStart) == '\r' || body.at(partStart) == '\n')) {
            ++partStart;
        }
        const int nextIdx = body.indexOf(delim, partStart);
        if (nextIdx < 0) break;
        // Strip the CRLF that precedes the next boundary marker.
        int partEnd = nextIdx;
        while (partEnd > partStart && (body.at(partEnd - 1) == '\r' || body.at(partEnd - 1) == '\n')) {
            --partEnd;
        }
        parts->append(body.mid(partStart, partEnd - partStart));
        idx = nextIdx;
    }
}

} // namespace

namespace NoteMessage {

QByteArray serialize(const Note &note, const QString &fromAddress, const QList<Attachment> &attachments)
{
    QByteArray uuid = note.uuid.toUtf8();
    if (uuid.isEmpty()) {
        uuid = sanitizedUuid().toUtf8();
    }

    const QDateTime created = note.created.isValid() ? note.created : QDateTime::currentDateTimeUtc();
    const QDateTime modified = note.lastModified.isValid() ? note.lastModified : created;

    QByteArray subject = encodeHeaderValue(note.title.isEmpty() ? QStringLiteral("(no title)") : note.title);
    QByteArray from = encodeHeaderValue(fromAddress);

    QByteArray msg;
    msg.reserve(note.bodyHtml.toUtf8().size() + 1024);

    auto appendHeader = [&msg](const QByteArray &name, const QByteArray &value) {
        msg += name;
        msg += ": ";
        msg += value;
        msg += "\r\n";
    };

    appendHeader("From", from);
    appendHeader("Subject", subject);
    appendHeader("Date", rfc2822Date(created).toUtf8());
    appendHeader("X-Universally-Unique-Identifier", uuid);
    appendHeader("X-Uniform-Type-Identifier", "com.apple.mail-note");
    appendHeader("X-Mail-Created-Date", rfc2822Date(created).toUtf8());
    appendHeader("X-Last-Modified", iso8601(modified).toUtf8());
    appendHeader("X-ImapNotes-Format", note.format == QStringLiteral("plain") ? "plain" : "rich");
    appendHeader("Mime-Version", "1.0");

    if (attachments.isEmpty()) {
        appendHeader("Content-Type", "text/html; charset=utf-8");
        appendHeader("Content-Transfer-Encoding", "8bit");
        msg += "\r\n";
        msg += note.bodyHtml.toUtf8();
        return msg;
    }

    // multipart/mixed: HTML first, then attachments as base64.
    const QByteArray boundary = "harbour-imap-notes-" + uuid;
    appendHeader("Content-Type", "multipart/mixed; boundary=\"" + boundary + "\"");
    msg += "\r\n";
    msg += "This is a multi-part message in MIME format.\r\n";

    msg += "--";
    msg += boundary;
    msg += "\r\n";
    appendHeader("Content-Type", "text/html; charset=utf-8");
    appendHeader("Content-Transfer-Encoding", "8bit");
    msg += "\r\n";
    msg += note.bodyHtml.toUtf8();
    msg += "\r\n";

    for (const Attachment &att : attachments) {
        msg += "--";
        msg += boundary;
        msg += "\r\n";
        const QByteArray ct = att.mimeType.toUtf8()
            + "; name=\"" + att.filename.toUtf8() + "\"";
        const QByteArray cd = "attachment; filename=\"" + att.filename.toUtf8() + "\"";
        appendHeader("Content-Type", ct);
        appendHeader("Content-Transfer-Encoding", "base64");
        appendHeader("Content-Disposition", cd);
        msg += "\r\n";
        msg += base64Wrap(att.data);
        msg += "\r\n";
    }

    msg += "--";
    msg += boundary;
    msg += "--\r\n";
    return msg;
}

bool parse(const QByteArray &raw, Parsed *out)
{
    if (!out) return false;

    QByteArray headerBlock;
    QByteArray body;
    if (!splitHeadersBody(raw, &headerBlock, &body)) {
        return false;
    }
    const auto headers = parseHeaderBlock(headerBlock);

    out->uuid = headerValue(headers, QStringLiteral("X-Universally-Unique-Identifier"));
    out->title = headerValue(headers, QStringLiteral("Subject"));
    out->format = headerValue(headers, QStringLiteral("X-ImapNotes-Format"));
    if (out->format.isEmpty()) {
        out->format = QStringLiteral("rich");
    }

    out->created = parseRfc2822Date(headerValue(headers, QStringLiteral("X-Mail-Created-Date")));
    if (!out->created.isValid()) {
        out->created = parseRfc2822Date(headerValue(headers, QStringLiteral("Date")));
    }

    const QString lastMod = headerValue(headers, QStringLiteral("X-Last-Modified"));
    if (!lastMod.isEmpty()) {
        out->lastModified = QDateTime::fromString(lastMod, Qt::ISODate);
    }
    if (!out->lastModified.isValid()) {
        out->lastModified = out->created;
    }

    const QString contentType = headerValue(headers, QStringLiteral("Content-Type"));
    const QString transferEncoding = headerValue(headers, QStringLiteral("Content-Transfer-Encoding"));
    const QString type = mimeType(contentType);

    if (type.startsWith(QStringLiteral("multipart/"))) {
        const QString boundary = mimeParam(contentType, QStringLiteral("boundary"));
        QList<QByteArray> parts;
        splitMultipart(body, boundary, &parts);
        for (const QByteArray &part : parts) {
            parsePart(part, out);
        }
    } else if (type.startsWith(QStringLiteral("text/html"))) {
        out->bodyHtml = QString::fromUtf8(decodeBody(body, transferEncoding));
    } else if (type.startsWith(QStringLiteral("text/plain")) || type.isEmpty()) {
        out->bodyHtml = QStringLiteral("<pre>%1</pre>")
                            .arg(QString::fromUtf8(decodeBody(body, transferEncoding)).toHtmlEscaped());
    } else {
        // Unknown top-level: treat the whole body as the only attachment.
        Attachment att;
        att.filename = QStringLiteral("body.bin");
        att.mimeType = type;
        att.data = decodeBody(body, transferEncoding);
        out->attachments.append(att);
    }

    // No UUID and no Apple type identifier — accept it anyway because we are
    // operating inside the Notes/ subtree, but log it so foreign drops are
    // visible in diagnostics.
    if (out->uuid.isEmpty()) {
        out->uuid = sanitizedUuid();
        qWarning() << "NoteMessage: synthesised UUID for message without X-Universally-Unique-Identifier";
    }

    return true;
}

}
