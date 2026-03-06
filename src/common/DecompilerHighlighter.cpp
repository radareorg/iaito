
#include "DecompilerHighlighter.h"
#include "CodeMetaRange.h"
#include "common/Configuration.h"

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
}

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
}
