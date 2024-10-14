
#include "DecompilerHighlighter.h"
#include "common/Configuration.h"

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
    size_t start = block.position();
    size_t end = block.position() + block.length();

    auto cm = r_codemeta_in(code, start, end);
    if (cm == nullptr) {
        return;
    }
    std::unique_ptr<RPVector, decltype(&r_pvector_free)> annotations(cm, &r_pvector_free);
    void **iter;
    r_pvector_foreach(annotations.get(), iter)
    {
        RCodeMetaItem *annotation = static_cast<RCodeMetaItem *>(*iter);
        if (annotation->type != R_CODEMETA_TYPE_SYNTAX_HIGHLIGHT) {
            continue;
        }
        auto type = annotation->syntax_highlight.type;
        if (size_t(type) >= HIGHLIGHT_COUNT) {
            continue;
        }
        auto annotationStart = annotation->start;
        if (annotationStart < start) {
            annotationStart = 0;
        } else {
            annotationStart -= start;
        }
        auto annotationEnd = annotation->end - start;

        setFormat(annotationStart, annotationEnd - annotationStart, format[type]);
    }
}
