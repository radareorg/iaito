
#include "SelectionHighlight.h"
#include "Configuration.h"

#include <QColor>
#include <QList>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QTextEdit>

QList<QTextEdit::ExtraSelection> createSameWordsSelections(
    QPlainTextEdit *textEdit, const QString &word)
{
    QList<QTextEdit::ExtraSelection> selections;
    QTextEdit::ExtraSelection highlightSelection;
    QTextDocument *document = textEdit->document();
    QColor highlightWordColor = ConfigColor("wordHighlight");

    if (word.isEmpty()) {
        return QList<QTextEdit::ExtraSelection>();
    }

    highlightSelection.cursor = textEdit->textCursor();
    highlightSelection.cursor.movePosition(QTextCursor::Start, QTextCursor::MoveAnchor);

    while (!highlightSelection.cursor.isNull() && !highlightSelection.cursor.atEnd()) {
        highlightSelection.cursor
            = document->find(word, highlightSelection.cursor, QTextDocument::FindWholeWords);

        if (!highlightSelection.cursor.isNull()) {
            highlightSelection.format.setBackground(highlightWordColor);

            selections.append(highlightSelection);
        }
    }
    return selections;
}

QList<QTextEdit::ExtraSelection> createHighlightSelections(
    QPlainTextEdit *textEdit, const QString &text)
{
    QList<QTextEdit::ExtraSelection> selections;
    QTextEdit::ExtraSelection highlightSelection;
    QTextDocument *document = textEdit->document();
    if (text.isEmpty()) {
        return selections;
    }

    highlightSelection.cursor = textEdit->textCursor();
    highlightSelection.cursor.movePosition(QTextCursor::Start, QTextCursor::MoveAnchor);

    while (!highlightSelection.cursor.isNull() && !highlightSelection.cursor.atEnd()) {
        highlightSelection.cursor = document->find(text, highlightSelection.cursor);

        if (!highlightSelection.cursor.isNull()) {
            highlightSelection.format.setBackground(QColor(0xe8, 0xbb, 0x32, 0xc0));
            highlightSelection.format.setForeground(QColor(Qt::black));
            selections.append(highlightSelection);
        }
    }
    return selections;
}

QTextEdit::ExtraSelection createLineHighlight(const QTextCursor &cursor, QColor highlightColor)
{
    QTextEdit::ExtraSelection highlightSelection;
    highlightSelection.cursor = cursor;
    highlightSelection.format.setBackground(highlightColor);
    highlightSelection.format.setProperty(QTextFormat::FullWidthSelection, true);
    highlightSelection.cursor.clearSelection();
    return highlightSelection;
}

QTextEdit::ExtraSelection createLineHighlightSelection(const QTextCursor &cursor)
{
    QColor highlightColor = ConfigColor("lineHighlight");
    return createLineHighlight(cursor, highlightColor);
}

QTextEdit::ExtraSelection createLineHighlightPC(const QTextCursor &cursor)
{
    QColor highlightColor = ConfigColor("highlightPC");
    return createLineHighlight(cursor, highlightColor);
}

QTextEdit::ExtraSelection createLineHighlightBP(const QTextCursor &cursor)
{
    QColor highlightColor = ConfigColor("gui.breakpoint_background");
    return createLineHighlight(cursor, highlightColor);
}