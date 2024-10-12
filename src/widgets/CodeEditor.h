#pragma once

#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QCompleter>
#include <QStringListModel>
#include <QTextCharFormat>

class CodeEditor : public QPlainTextEdit {
    Q_OBJECT

public:
    CodeEditor(QWidget *parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent *e) override;

private slots:
    void insertCompletion(const QString &completion);

private:
    QCompleter *completer;
    QString textUnderCursor() const;
    void setupCompleter();
    void autoIndentation();  // Add this method to handle auto-indentation
};
