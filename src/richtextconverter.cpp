#include "richtextconverter.h"

#include <QTextDocumentFragment>

RichTextConverter::RichTextConverter(QObject *parent)
    : QObject(parent)
{
}

QString RichTextConverter::htmlToPlain(const QString &html)
{
    return QTextDocumentFragment::fromHtml(html).toPlainText();
}

QString RichTextConverter::plainToHtml(const QString &plain)
{
    QString escaped = plain.toHtmlEscaped();
    return QStringLiteral("<!DOCTYPE html><html><body><pre>%1</pre></body></html>").arg(escaped);
}

QString RichTextConverter::previewFromHtml(const QString &html)
{
    QString plain = htmlToPlain(html).simplified();
    constexpr int kMax = 120;
    if (plain.size() > kMax) {
        plain.truncate(kMax);
        plain += QStringLiteral("…");
    }
    return plain;
}
