#ifndef SYNTAXHIGHLIGHTER_H
#define SYNTAXHIGHLIGHTER_H

#include "IaitoCommon.h"
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QVector>

#ifdef IAITO_ENABLE_KSYNTAXHIGHLIGHTING

#include <KSyntaxHighlighting/SyntaxHighlighter>

class SyntaxHighlighter : public KSyntaxHighlighting::SyntaxHighlighter
{
    Q_OBJECT

public:
    SyntaxHighlighter(QTextDocument *document);

private slots:
    void updateTheme();
};

#endif

/**
 * SyntaxHighlighter to be used when KSyntaxHighlighting is not available
 */
class IAITO_EXPORT FallbackSyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    enum class Language { C, JavaScript, Python, Radare2 };
    FallbackSyntaxHighlighter(QTextDocument *parent = nullptr, Language language = Language::C);
    virtual ~FallbackSyntaxHighlighter() = default;

protected:
    void highlightBlock(const QString &text) override;

private:
    void updateTheme();
    Language language;

    struct HighlightingRule
    {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    QVector<HighlightingRule> highlightingRules;

    QTextCharFormat multiLineCommentFormat;
};

#endif
