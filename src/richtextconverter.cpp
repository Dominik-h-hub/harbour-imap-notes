#include "richtextconverter.h"

#include "checklistparser.h"

#include <QTextDocumentFragment>

RichTextConverter::RichTextConverter(QObject *parent)
    : QObject(parent)
{
}

QString RichTextConverter::htmlToPlain(const QString &html)
{
    // Route through the block parser so checklist items survive the round-trip
    // as ☐/☑ lines instead of being collapsed by QTextDocumentFragment.
    const QVariantList blocks = ChecklistParser::parseBlocks(html);
    if (blocks.isEmpty()) {
        return QTextDocumentFragment::fromHtml(html).toPlainText();
    }
    return ChecklistParser::plaintextFromBlocks(blocks);
}

QString RichTextConverter::plainToHtml(const QString &plain)
{
    QString escaped = plain.toHtmlEscaped();
    return QStringLiteral("<!DOCTYPE html><html><body><pre>%1</pre></body></html>").arg(escaped);
}

QString RichTextConverter::previewFromHtml(const QString &html)
{
    // Render checklists as inline "☐ item · ☑ item" so the preview line still
    // shows what's on the list rather than dropping the structured part.
    const QString withGlyphs = ChecklistParser::stripChecklistMarkup(html);
    QString plain = QTextDocumentFragment::fromHtml(withGlyphs).toPlainText().simplified();
    constexpr int kMax = 120;
    if (plain.size() > kMax) {
        plain.truncate(kMax);
        plain += QStringLiteral("…");
    }
    return plain;
}
