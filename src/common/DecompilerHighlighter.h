#ifndef DECOMPILER_HIGHLIGHTER_H
#define DECOMPILER_HIGHLIGHTER_H

// Define to cache and pre-sort syntax-highlight items once per setAnnotations()
// instead of calling r_codemeta_in() per text block. Disabled by default; the
// r_codemeta API is expected to be fast enough on its own.
// #define IAITO_DECOMPILER_CACHE_HIGHLIGHTS 1

#include "IaitoCommon.h"
#include <array>
#ifdef IAITO_DECOMPILER_CACHE_HIGHLIGHTS
#include <vector>
#endif
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

#ifdef IAITO_DECOMPILER_CACHE_HIGHLIGHTS
    struct HighlightItem
    {
        size_t start;
        size_t end;
        int type;
    };
    // Cached, start-sorted syntax-highlight items pulled from `code`.
    // Avoids calling r_codemeta_in() once per text block, whose tree walk
    // can degrade to O(annotations) per call and hang large documents.
    std::vector<HighlightItem> highlightItems;

    void rebuildHighlightItems();
#endif
};

#endif
