#include "checklistparser.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>
#include <QStringList>
#include <QTextDocumentFragment>

namespace {

const char *kOpenGlyph  = "\xE2\x98\x90"; // U+2610 BALLOT BOX
const char *kDoneGlyph  = "\xE2\x98\x91"; // U+2611 BALLOT BOX WITH CHECK

// Regex for "<ul ... class=...imap-checklist... > ... </ul>".
//
// Notes:
//   * QRegularExpression supports inline flags with (?...) at the start of
//     the pattern. We need DOTALL ((?s)) so .* spans newlines and
//     CASE-INSENSITIVE ((?i)) so QTextDocument-normalised markup (which
//     sometimes upper-cases tags) still matches.
//   * Non-greedy .*? ensures we stop at the *first* </ul>, since checklists
//     are flat per requirements §5.3.
QRegularExpression checklistRegex()
{
    static const QRegularExpression re(
        QStringLiteral("(?is)<ul\\b[^>]*\\bclass\\s*=\\s*[\"']?[^\"'>]*imap-checklist[^\"'>]*[\"']?[^>]*>(.*?)</ul>"));
    return re;
}

QRegularExpression checklistItemRegex()
{
    static const QRegularExpression re(
        QStringLiteral("(?is)<li\\b[^>]*\\bclass\\s*=\\s*[\"']?(open|done)[\"']?[^>]*>(.*?)</li>"));
    return re;
}

// Reduce inner-<li> HTML to plain text for editor display. We deliberately do
// not preserve nested formatting inside a checklist item — per requirements
// items are flat plain text.
QString itemTextFromHtml(const QString &innerHtml)
{
    QString text = QTextDocumentFragment::fromHtml(innerHtml).toPlainText();
    return text.trimmed();
}

QString escapeForHtml(const QString &s)
{
    return s.toHtmlEscaped();
}

QString renderChecklistFromItems(const QVariantList &items, bool sortDoneToBottom)
{
    QStringList opens;
    QStringList dones;
    QStringList combined;
    for (const QVariant &v : items) {
        const QVariantMap m = v.toMap();
        const QString text = m.value(QStringLiteral("text")).toString();
        const bool done = m.value(QStringLiteral("done")).toBool();
        const QString rendered = QStringLiteral("  <li class=\"%1\">%2</li>")
                                     .arg(done ? QStringLiteral("done") : QStringLiteral("open"),
                                          escapeForHtml(text));
        if (sortDoneToBottom) {
            if (done) dones.append(rendered);
            else opens.append(rendered);
        } else {
            combined.append(rendered);
        }
    }
    QString body;
    if (sortDoneToBottom) {
        body = opens.join(QLatin1Char('\n'));
        if (!opens.isEmpty() && !dones.isEmpty()) body += QLatin1Char('\n');
        body += dones.join(QLatin1Char('\n'));
    } else {
        body = combined.join(QLatin1Char('\n'));
    }
    return QStringLiteral("<ul class=\"imap-checklist\">\n%1\n</ul>").arg(body);
}

} // namespace

ChecklistParser::ChecklistParser(QObject *parent)
    : QObject(parent)
{
}

QVariantList ChecklistParser::parseBlocks(const QString &html)
{
    QVariantList blocks;
    if (html.isEmpty()) {
        return blocks;
    }

    const QRegularExpression ulRe = checklistRegex();
    int pos = 0;
    QRegularExpressionMatchIterator it = ulRe.globalMatch(html);
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        const int start = m.capturedStart();
        const int end = m.capturedEnd();

        if (start > pos) {
            const QString chunk = html.mid(pos, start - pos);
            if (!chunk.trimmed().isEmpty()) {
                QVariantMap tb;
                tb.insert(QStringLiteral("type"), QStringLiteral("text"));
                tb.insert(QStringLiteral("html"), chunk);
                blocks.append(tb);
            }
        }

        QVariantList items;
        const QString listInner = m.captured(1);
        const QRegularExpression liRe = checklistItemRegex();
        QRegularExpressionMatchIterator lit = liRe.globalMatch(listInner);
        while (lit.hasNext()) {
            QRegularExpressionMatch lm = lit.next();
            QVariantMap item;
            item.insert(QStringLiteral("text"), itemTextFromHtml(lm.captured(2)));
            item.insert(QStringLiteral("done"), lm.captured(1).compare(QStringLiteral("done"), Qt::CaseInsensitive) == 0);
            items.append(item);
        }

        QVariantMap cb;
        cb.insert(QStringLiteral("type"), QStringLiteral("checklist"));
        cb.insert(QStringLiteral("items"), items);
        blocks.append(cb);

        pos = end;
    }

    if (pos < html.size()) {
        const QString chunk = html.mid(pos);
        if (!chunk.trimmed().isEmpty()) {
            QVariantMap tb;
            tb.insert(QStringLiteral("type"), QStringLiteral("text"));
            tb.insert(QStringLiteral("html"), chunk);
            blocks.append(tb);
        }
    }

    // Empty input → emit a single empty text block so the editor has something
    // to focus on. Same when the HTML was checklist-only but the editor still
    // wants a trailing text caret.
    if (blocks.isEmpty()) {
        QVariantMap tb;
        tb.insert(QStringLiteral("type"), QStringLiteral("text"));
        tb.insert(QStringLiteral("html"), QString());
        blocks.append(tb);
    }
    return blocks;
}

QString ChecklistParser::serializeBlocks(const QVariantList &blocks, bool sortDoneToBottom)
{
    QStringList parts;
    parts.reserve(blocks.size());
    for (const QVariant &v : blocks) {
        const QVariantMap m = v.toMap();
        const QString type = m.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("checklist")) {
            const QVariantList items = m.value(QStringLiteral("items")).toList();
            // Drop fully-empty items — the editor often leaves a trailing
            // empty row after the user pressed Enter to terminate the list.
            QVariantList filtered;
            filtered.reserve(items.size());
            for (const QVariant &iv : items) {
                const QVariantMap im = iv.toMap();
                if (im.value(QStringLiteral("text")).toString().trimmed().isEmpty()) {
                    continue;
                }
                filtered.append(iv);
            }
            if (filtered.isEmpty()) {
                continue;
            }
            parts.append(renderChecklistFromItems(filtered, sortDoneToBottom));
        } else {
            const QString html = m.value(QStringLiteral("html")).toString();
            if (!html.isEmpty()) {
                parts.append(html);
            }
        }
    }
    return parts.join(QStringLiteral("\n"));
}

QString ChecklistParser::plaintextFromBlocks(const QVariantList &blocks)
{
    QStringList lines;
    for (const QVariant &v : blocks) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("type")).toString() == QStringLiteral("checklist")) {
            const QVariantList items = m.value(QStringLiteral("items")).toList();
            for (const QVariant &iv : items) {
                const QVariantMap im = iv.toMap();
                const QString text = im.value(QStringLiteral("text")).toString();
                const bool done = im.value(QStringLiteral("done")).toBool();
                lines.append(QStringLiteral("%1 %2")
                                 .arg(QString::fromUtf8(done ? kDoneGlyph : kOpenGlyph), text));
            }
        } else {
            const QString html = m.value(QStringLiteral("html")).toString();
            const QString plain = QTextDocumentFragment::fromHtml(html).toPlainText();
            if (!plain.isEmpty()) {
                lines.append(plain);
            }
        }
    }
    return lines.join(QLatin1Char('\n'));
}

QVariantList ChecklistParser::plaintextToBlocks(const QString &plain)
{
    QVariantList blocks;
    const QString openGlyph = QString::fromUtf8(kOpenGlyph);
    const QString doneGlyph = QString::fromUtf8(kDoneGlyph);

    QVariantList pendingItems;
    QStringList pendingText;

    auto flushText = [&]() {
        if (pendingText.isEmpty()) return;
        QString chunk = pendingText.join(QLatin1Char('\n')).trimmed();
        pendingText.clear();
        if (chunk.isEmpty()) return;
        QVariantMap tb;
        tb.insert(QStringLiteral("type"), QStringLiteral("text"));
        tb.insert(QStringLiteral("html"),
                  QStringLiteral("<p>%1</p>").arg(chunk.toHtmlEscaped().replace(QLatin1Char('\n'),
                                                                                QStringLiteral("<br/>"))));
        blocks.append(tb);
    };
    auto flushChecklist = [&]() {
        if (pendingItems.isEmpty()) return;
        QVariantMap cb;
        cb.insert(QStringLiteral("type"), QStringLiteral("checklist"));
        cb.insert(QStringLiteral("items"), pendingItems);
        blocks.append(cb);
        pendingItems.clear();
    };

    const QStringList lines = plain.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(openGlyph) || trimmed.startsWith(doneGlyph)) {
            flushText();
            const bool done = trimmed.startsWith(doneGlyph);
            QString text = trimmed.mid(1).trimmed();
            QVariantMap item;
            item.insert(QStringLiteral("text"), text);
            item.insert(QStringLiteral("done"), done);
            pendingItems.append(item);
        } else {
            flushChecklist();
            pendingText.append(line);
        }
    }
    flushChecklist();
    flushText();

    if (blocks.isEmpty()) {
        QVariantMap tb;
        tb.insert(QStringLiteral("type"), QStringLiteral("text"));
        tb.insert(QStringLiteral("html"), QString());
        blocks.append(tb);
    }
    return blocks;
}

QString ChecklistParser::sortDoneItems(const QString &html)
{
    if (html.isEmpty()) {
        return html;
    }
    const QRegularExpression ulRe = checklistRegex();
    const QRegularExpression liRe = checklistItemRegex();

    QString out;
    out.reserve(html.size());
    int pos = 0;
    QRegularExpressionMatchIterator it = ulRe.globalMatch(html);
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        out += html.midRef(pos, m.capturedStart() - pos);

        QVariantList items;
        const QString listInner = m.captured(1);
        QRegularExpressionMatchIterator lit = liRe.globalMatch(listInner);
        while (lit.hasNext()) {
            QRegularExpressionMatch lm = lit.next();
            QVariantMap item;
            item.insert(QStringLiteral("text"), itemTextFromHtml(lm.captured(2)));
            item.insert(QStringLiteral("done"),
                        lm.captured(1).compare(QStringLiteral("done"), Qt::CaseInsensitive) == 0);
            items.append(item);
        }
        out += renderChecklistFromItems(items, /*sortDoneToBottom*/ true);
        pos = m.capturedEnd();
    }
    out += html.midRef(pos);
    return out;
}

QString ChecklistParser::stripChecklistMarkup(const QString &html)
{
    // Used by preview/search: turn checklist <ul> into a text representation
    // with Unicode glyphs so previews remain readable instead of dropping the
    // checklist's content.
    QString out = html;
    const QRegularExpression ulRe = checklistRegex();
    const QRegularExpression liRe = checklistItemRegex();

    QString result;
    int pos = 0;
    QRegularExpressionMatchIterator it = ulRe.globalMatch(out);
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        result += out.midRef(pos, m.capturedStart() - pos);

        const QString listInner = m.captured(1);
        QRegularExpressionMatchIterator lit = liRe.globalMatch(listInner);
        QStringList parts;
        while (lit.hasNext()) {
            QRegularExpressionMatch lm = lit.next();
            const bool done = lm.captured(1).compare(QStringLiteral("done"), Qt::CaseInsensitive) == 0;
            const QString text = itemTextFromHtml(lm.captured(2));
            parts.append(QStringLiteral("%1 %2")
                             .arg(QString::fromUtf8(done ? kDoneGlyph : kOpenGlyph), text));
        }
        result += parts.join(QStringLiteral(" · "));
        pos = m.capturedEnd();
    }
    result += out.midRef(pos);
    return result;
}
