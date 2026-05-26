#ifndef NOTEMESSAGE_H
#define NOTEMESSAGE_H

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

class Note;

// Encodes notes to and decodes notes from the RFC2822 wire format that iOS
// Mail/Notes writes into an IMAP "Notes" folder. The headers we produce match
// what the native iOS Notes app emits for IMAP-backed notes, with our own
// X-Last-Modified and X-ImapNotes-Format tacked on for conflict resolution.
//
// Notes with attachments are encoded as multipart/mixed with the HTML body as
// the first part and attachments base64-encoded as the remaining parts. Plain
// notes (no attachments) are emitted as single-part text/html so iOS displays
// them straight away.
namespace NoteMessage {

struct Attachment
{
    QString filename;
    QString mimeType;
    QByteArray data;
};

struct Parsed
{
    QString uuid;
    QString title;
    QString bodyHtml;
    QString format = QStringLiteral("rich");
    QDateTime created;
    QDateTime lastModified;
    QList<Attachment> attachments;
};

// Returns the raw RFC2822 bytes ready to be passed to IMAP APPEND. `note.uuid`
// must be set; if empty a fresh UUID is generated and assigned. `fromAddress`
// is the synthetic From: header (Apple uses something like
// "noreply@example.invalid").
QByteArray serialize(const Note &note,
                     const QString &fromAddress,
                     const QList<Attachment> &attachments = QList<Attachment>());

// Parse a fetched message. Returns true if the message looks like a note (has
// the Apple type-identifier header *or* the IMAP server allowed any message to
// land in the Notes folder). Sets fields in *out.
bool parse(const QByteArray &raw, Parsed *out);

}

#endif
