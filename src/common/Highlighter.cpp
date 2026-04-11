#include "Highlighter.h"
#include "core/MainWindow.h"

#include <QtGui>

Highlighter::Highlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    HighlightingRule rule;

    core = Core();

    keywordFormat.setForeground(QColor(65, 131, 215));

    for (const QString &pattern : this->core->opcodes) {
        rule.pattern.setPattern("\\b" + pattern + "\\b");
        rule.pattern.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
        rule.format = keywordFormat;
        highlightingRules.append(rule);
    }

    regFormat.setForeground(QColor(236, 100, 75));

    for (const QString &pattern : this->core->regs) {
        rule.pattern.setPattern("\\b" + pattern + "\\b");
        rule.pattern.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
        rule.format = regFormat;
        highlightingRules.append(rule);
    }

    singleLineCommentFormat.setFontWeight(QFont::Bold);
    singleLineCommentFormat.setForeground(QColor(63, 195, 128));
    rule.pattern.setPattern(";[^\n]*");
    rule.format = singleLineCommentFormat;
    highlightingRules.append(rule);

    commentStartRegularExpression.setPattern("/\\*");
    commentEndRegularExpression.setPattern("\\*/");
}

void Highlighter::highlightBlock(const QString &text)
{
    for (const HighlightingRule &rule : highlightingRules) {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), rule.format);
        }
    }
    setCurrentBlockState(0);

    int startIndex = 0;
    if (previousBlockState() != 1) {
        startIndex = commentStartRegularExpression.match(text).capturedStart();
    }

    while (startIndex >= 0) {
        const QRegularExpressionMatch commentEndMatch
            = commentEndRegularExpression.match(text, startIndex);
        const int endIndex = commentEndMatch.capturedStart();
        int commentLength;
        if (endIndex == -1) {
            setCurrentBlockState(1);
            commentLength = text.length() - startIndex;
        } else {
            commentLength = endIndex - startIndex + commentEndMatch.capturedLength();
        }
        setFormat(startIndex, commentLength, multiLineCommentFormat);
        startIndex
            = commentStartRegularExpression.match(text, startIndex + commentLength).capturedStart();
    }
}
