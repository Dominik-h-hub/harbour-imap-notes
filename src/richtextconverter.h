#ifndef RICHTEXTCONVERTER_H
#define RICHTEXTCONVERTER_H

#include <QObject>
#include <QString>

class RichTextConverter : public QObject
{
    Q_OBJECT
public:
    explicit RichTextConverter(QObject *parent = nullptr);

    // Strip all HTML, keeping line breaks. Used when switching a note from
    // rich -> plain.
    Q_INVOKABLE static QString htmlToPlain(const QString &html);

    // Wrap plain text in a minimal HTML document so iOS still recognises the
    // note. Preserves whitespace via <pre>.
    Q_INVOKABLE static QString plainToHtml(const QString &plain);

    // Short (~120 char) plain-text preview for the notes list.
    static QString previewFromHtml(const QString &html);
};

#endif
