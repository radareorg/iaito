#ifndef DECOMPILER_HIGHLIGHTER_H
#define DECOMPILER_HIGHLIGHTER_H

#include "IaitoCommon.h"
#include <array>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextDocument>

/**
 * \brief SyntaxHighlighter based on annotations from decompiled code.
 * Can be only used in combination with DecompilerWidget.
 */
class IAITO_EXPORT DecompilerHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    DecompilerHighlighter(QTextDocument *parent = nullptr);
    virtual ~DecompilerHighlighter() = default;

    /**
     * @brief Set the code with annotations to be used for highlighting.
     *
     * It is callers responsibility to ensure that it is synchronized with
     * currentTextDocument and has sufficiently long lifetime.
     *
     * @param code
     */
    void setAnnotations(RCodeMeta *code);

protected:
    void highlightBlock(const QString &text) override;

private:
    void setupTheme();

    static const int HIGHLIGHT_COUNT = R_SYNTAX_HIGHLIGHT_TYPE_GLOBAL_VARIABLE + 1;
    std::array<QTextCharFormat, HIGHLIGHT_COUNT> format;
    RCodeMeta *code = nullptr;
};

#endif
