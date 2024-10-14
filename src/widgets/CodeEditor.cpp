#include "CodeEditor.h"
#include <QAbstractItemView>
#include <QKeyEvent>
#include <QRegularExpression> // Include QRegularExpression
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocument>

CodeEditor::CodeEditor(QWidget *parent)
    : QPlainTextEdit(parent)
    , completer(nullptr)
{
    setupCompleter();
}

void CodeEditor::setupCompleter()
{
    QStringList keywords
        = {"let",
           "const",
           "var",
           "function",
           "return",
           "if",
           "else",
           "for",
           "while",
           "class",
           "interface",
           "enum",
           "type",
           "import",
           "from",
           "as",
           "this",
           "new"};

    completer = new QCompleter(keywords, this);
    completer->setWidget(this);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setCaseSensitivity(Qt::CaseInsensitive);

    connect(
        completer,
        QOverload<const QString &>::of(&QCompleter::activated),
        this,
        &CodeEditor::insertCompletion);
}

void CodeEditor::keyPressEvent(QKeyEvent *e)
{
    if (completer && completer->popup()->isVisible()) {
        switch (e->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Escape:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            e->ignore();
            return;
        default:
            break;
        }
    }

    // Call auto-indentation logic when Enter/Return is pressed
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        autoIndentation();
        return;
    }

    QPlainTextEdit::keyPressEvent(e);

    const QString completionPrefix = textUnderCursor();
    if (completionPrefix.length() < 2) {
        completer->popup()->hide();
        return;
    }

    if (completionPrefix != completer->completionPrefix()) {
        completer->setCompletionPrefix(completionPrefix);
        completer->popup()->setCurrentIndex(completer->completionModel()->index(0, 0));
    }

    QRect cr = cursorRect();
    cr.setWidth(
        completer->popup()->sizeHintForColumn(0)
        + completer->popup()->verticalScrollBar()->sizeHint().width());
    completer->complete(cr);
}

void CodeEditor::insertCompletion(const QString &completion)
{
    QTextCursor tc = textCursor();
    int extra = completion.length() - completer->completionPrefix().length();
    tc.movePosition(
        QTextCursor::Left, QTextCursor::KeepAnchor, completer->completionPrefix().length());
    tc.insertText(completion);
    setTextCursor(tc);
}

QString CodeEditor::textUnderCursor() const
{
    QTextCursor tc = textCursor();
    tc.select(QTextCursor::WordUnderCursor);
    return tc.selectedText();
}

// Auto-indentation method
void CodeEditor::autoIndentation()
{
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();

    // Get the current line text
    QTextBlock block = cursor.block();
    QString currentText = block.text();

    // Get the leading spaces of the current line (preserve the same indentation
    // level)
    QRegularExpression leadingSpaces("^(\\s+)"); // Change to QRegularExpression
    QRegularExpressionMatch match = leadingSpaces.match(currentText);
    QString leadingWhitespace;
    if (match.hasMatch()) {
        leadingWhitespace = match.captured(1);
    }

    // Insert new line and maintain the leading whitespace
    cursor.insertText("\n" + leadingWhitespace);

    // Auto-increase indentation after certain symbols (e.g., '{')
    if (currentText.trimmed().endsWith("{")) {
        cursor.insertText("    "); // Indent with 4 spaces (you can adjust this
                                   // to tabs if necessary)
    }

    cursor.endEditBlock();
    setTextCursor(cursor);
}
