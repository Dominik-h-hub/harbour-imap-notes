#ifndef CHECKLISTPARSER_H
#define CHECKLISTPARSER_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

// Parses the note HTML into a flat sequence of blocks for the mixed-content
// editor and serialises blocks back to HTML. The on-disk schema for a
// checklist block is
//
//   <ul class="imap-checklist">
//     <li class="open">item text</li>
//     <li class="done">item text</li>
//   </ul>
//
// Anything else (free text, normal bullet lists from iOS without our marker
// class, headings, ...) is kept verbatim inside a single "text" block.
//
// QML side consumes blocks as a list of QVariantMaps with shape
//   { type: "text",       html: "..." }
//   { type: "checklist",  items: [ { text: "...", done: bool }, ... ] }
class ChecklistParser : public QObject
{
    Q_OBJECT
public:
    explicit ChecklistParser(QObject *parent = nullptr);

    Q_INVOKABLE static QVariantList parseBlocks(const QString &html);
    Q_INVOKABLE static QString serializeBlocks(const QVariantList &blocks,
                                               bool sortDoneToBottom = true);

    // Plain-text <-> blocks. Unicode glyph ☐ ("☐") marks open items,
    // ☑ ("☑") marks done items, one item per line. Plain-text-only notes
    // (X-ImapNotes-Format: plain) round-trip through these.
    Q_INVOKABLE static QString plaintextFromBlocks(const QVariantList &blocks);
    Q_INVOKABLE static QVariantList plaintextToBlocks(const QString &plain);

    // Convenience: reorder done items to the end of every imap-checklist <ul>
    // inside `html`. Used by NotesManager::updateNote so the on-server copy
    // matches the iOS-Notes UX of "checked items sink to the bottom".
    Q_INVOKABLE static QString sortDoneItems(const QString &html);

    // Convenience helpers for previews/search:
    Q_INVOKABLE static QString stripChecklistMarkup(const QString &html);
};

#endif
