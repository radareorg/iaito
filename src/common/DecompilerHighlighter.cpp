
#include "DecompilerHighlighter.h"
#ifndef IAITO_DECOMPILER_CACHE_HIGHLIGHTS
#include "CodeMetaRange.h"
#endif
#include "common/Configuration.h"

#ifdef IAITO_DECOMPILER_CACHE_HIGHLIGHTS
#include <algorithm>
#include <cstring>
#endif
#include <limits>

DecompilerHighlighter::DecompilerHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    setupTheme();
    connect(Config(), &Configuration::colorsUpdated, this, [this]() {
        setupTheme();
        rehighlight();
    });
}

void DecompilerHighlighter::setAnnotations(RCodeMeta *code)
{
    this->code = code;
#ifdef IAITO_DECOMPILER_CACHE_HIGHLIGHTS
    rebuildHighlightItems();
#endif
}

#ifdef IAITO_DECOMPILER_CACHE_HIGHLIGHTS
void DecompilerHighlighter::rebuildHighlightItems()
{
    highlightItems.clear();
    if (!code || !code->code) {
        return;
    }
    const size_t len = strlen(code->code);
    auto cm = r_codemeta_in(code, 0, len + 1);
    if (!cm) {
        return;
    }
    auto process = [&](RCodeMetaItem *a) {
        if (!a || a->type != R_CODEMETA_TYPE_SYNTAX_HIGHLIGHT) {
            return;
        }
        const int t = static_cast<int>(a->syntax_highlight.type);
        if (t < 0 || t >= HIGHLIGHT_COUNT) {
            return;
        }
        if (a->end <= a->start) {
            return;
        }
        highlightItems.push_back({a->start, a->end, t});
    };
#if R2_ABIVERSION >= 40
    std::unique_ptr<RVecCodeMetaItemPtr, decltype(&RVecCodeMetaItemPtr_free)>
        annotations(cm, &RVecCodeMetaItemPtr_free);
    RCodeMetaItem **iter;
    R_VEC_FOREACH(annotations.get(), iter)
    {
        process(*iter);
    }
#else
    std::unique_ptr<RPVector, decltype(&r_pvector_free)> annotations(cm, &r_pvector_free);
    void **iter;
    r_pvector_foreach(annotations.get(), iter)
    {
        process(static_cast<RCodeMetaItem *>(*iter));
    }
#endif
    std::sort(
        highlightItems.begin(),
        highlightItems.end(),
        [](const HighlightItem &a, const HighlightItem &b) { return a.start < b.start; });
}
#endif

void DecompilerHighlighter::setupTheme()
{
    struct
    {
        RSyntaxHighlightType type;
        QString name;
    } mapping[] = {
        {R_SYNTAX_HIGHLIGHT_TYPE_KEYWORD, "pop"},
        {R_SYNTAX_HIGHLIGHT_TYPE_COMMENT, "comment"},
        {R_SYNTAX_HIGHLIGHT_TYPE_DATATYPE, "func_var_type"},
        {R_SYNTAX_HIGHLIGHT_TYPE_FUNCTION_NAME, "fname"},
        {R_SYNTAX_HIGHLIGHT_TYPE_FUNCTION_PARAMETER, "args"},
        {R_SYNTAX_HIGHLIGHT_TYPE_LOCAL_VARIABLE, "func_var"},
        {R_SYNTAX_HIGHLIGHT_TYPE_CONSTANT_VARIABLE, "num"},
        {R_SYNTAX_HIGHLIGHT_TYPE_GLOBAL_VARIABLE, "flag"},
    };
    for (const auto &pair : mapping) {
        assert(pair.type < format.size());
        format[pair.type].setForeground(Config()->getColor(pair.name));
    }
}

void DecompilerHighlighter::highlightBlock(const QString &)
{
#ifdef IAITO_DECOMPILER_CACHE_HIGHLIGHTS
    if (highlightItems.empty()) {
        return;
    }
    auto block = currentBlock();
    const size_t start = static_cast<size_t>(block.position());
    const size_t end = start + static_cast<size_t>(block.length());

    // Upper bound: first cached item whose start is >= block end.
    // Everything past this point cannot overlap the block.
    auto itEnd = std::lower_bound(
        highlightItems.begin(),
        highlightItems.end(),
        end,
        [](const HighlightItem &item, size_t value) { return item.start < value; });

    for (auto it = highlightItems.begin(); it != itEnd; ++it) {
        if (it->end <= start) {
            continue; // entirely before block
        }
        const size_t rangeStart = std::max(start, it->start);
        const size_t rangeEnd = std::min(end, it->end);
        if (rangeEnd <= rangeStart) {
            continue;
        }
        const size_t relStart = rangeStart - start;
        const size_t length = rangeEnd - rangeStart;
        if (relStart > static_cast<size_t>(std::numeric_limits<int>::max())
            || length > static_cast<size_t>(std::numeric_limits<int>::max())) {
            continue;
        }
        setFormat(static_cast<int>(relStart), static_cast<int>(length), format[it->type]);
    }
#else
    if (!code) {
        return;
    }
    auto block = currentBlock();
    size_t start = static_cast<size_t>(block.position());
    size_t end = start + static_cast<size_t>(block.length());

    auto cm = r_codemeta_in(code, start, end);
    if (cm == nullptr) {
        return;
    }
    auto processAnnotation = [&](RCodeMetaItem *annotation) {
        if (!annotation || annotation->type != R_CODEMETA_TYPE_SYNTAX_HIGHLIGHT) {
            return;
        }
        auto type = annotation->syntax_highlight.type;
        if (size_t(type) >= HIGHLIGHT_COUNT) {
            return;
        }
        auto range = intersectCodeMetaRange(annotation->start, annotation->end, start, end);
        if (!range) {
            return;
        }
        if (range->start > static_cast<size_t>(std::numeric_limits<int>::max())
            || range->length > static_cast<size_t>(std::numeric_limits<int>::max())) {
            return;
        }
        setFormat(static_cast<int>(range->start), static_cast<int>(range->length), format[type]);
    };
#if R2_ABIVERSION >= 40
    std::unique_ptr<RVecCodeMetaItemPtr, decltype(&RVecCodeMetaItemPtr_free)>
        annotations(cm, &RVecCodeMetaItemPtr_free);
    RCodeMetaItem **iter;
    R_VEC_FOREACH(annotations.get(), iter)
    {
        processAnnotation(*iter);
    }
#else
    std::unique_ptr<RPVector, decltype(&r_pvector_free)> annotations(cm, &r_pvector_free);
    void **iter;
    r_pvector_foreach(annotations.get(), iter)
    {
        processAnnotation(static_cast<RCodeMetaItem *>(*iter));
    }
#endif
#endif
}
