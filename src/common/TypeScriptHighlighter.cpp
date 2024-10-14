#include "TypeScriptHighlighter.h"

TypeScriptHighlighter::TypeScriptHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    setupHighlightingRules();
}

void TypeScriptHighlighter::setupHighlightingRules()
{
    keywordFormat.setForeground(Qt::blue);

    // Define patterns for TypeScript keywords
    QStringList keywordPatterns
        = {"\\blet\\b",
           "\\bconst\\b",
           "\\bvar\\b",
           "\\bfunction\\b",
           "\\breturn\\b",
           "\\bif\\b",
           "\\belse\\b",
           "\\bfor\\b",
           "\\bwhile\\b",
           "\\bclass\\b",
           "\\binterface\\b",
           "\\benum\\b",
           "\\btype\\b",
           "\\bimport\\b",
           "\\bfrom\\b",
           "\\bas\\b",
           "\\bnew\\b"};

    // Create highlighting rules for each keyword
    for (const QString &pattern : keywordPatterns) {
        HighlightingRule rule;
        rule.pattern = QRegularExpression(pattern); // Change to QRegularExpression
        rule.format = keywordFormat;
        highlightingRules.append(rule);
    }

    // String highlighting
    stringFormat.setForeground(Qt::darkGreen);
    HighlightingRule stringRule;
    stringRule.pattern = QRegularExpression("\".*\"|'.*'");
    stringRule.format = stringFormat;
    highlightingRules.append(stringRule);

    // Comment highlighting
    commentFormat.setForeground(Qt::gray);
    HighlightingRule commentRule;
    commentRule.pattern = QRegularExpression("//[^\n]*");
    commentRule.format = commentFormat;
    highlightingRules.append(commentRule);
}

void TypeScriptHighlighter::highlightBlock(const QString &text)
{
    // Apply each highlighting rule
    for (const HighlightingRule &rule : qAsConst(highlightingRules)) {
        QRegularExpressionMatchIterator matchIterator = rule.pattern.globalMatch(text);
        while (matchIterator.hasNext()) {
            QRegularExpressionMatch match = matchIterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
}
