#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>

class TypeScriptHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    TypeScriptHighlighter(QTextDocument *parent = nullptr);

protected:
    void highlightBlock(const QString &text) override;

private:
    void setupHighlightingRules();
    
    struct HighlightingRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QVector<HighlightingRule> highlightingRules;

    QTextCharFormat keywordFormat;
    QTextCharFormat stringFormat;
    QTextCharFormat commentFormat;
};
