
#include "SyntaxHighlighter.h"
#include "Configuration.h"
#include "Helpers.h"
#include <QApplication>

#ifdef IAITO_ENABLE_KSYNTAXHIGHLIGHTING

#include <KSyntaxHighlighting/Theme>

SyntaxHighlighter::SyntaxHighlighter(QTextDocument *document)
    : KSyntaxHighlighting::SyntaxHighlighter(document)
{
    connect(
        Config(),
        &Configuration::kSyntaxHighlightingThemeChanged,
        this,
        &SyntaxHighlighter::updateTheme);
    updateTheme();
}

void SyntaxHighlighter::updateTheme()
{
    setTheme(Config()->getKSyntaxHighlightingTheme());
    rehighlight();
}

#endif

FallbackSyntaxHighlighter::FallbackSyntaxHighlighter(QTextDocument *parent, Language language)
    : QSyntaxHighlighter(parent)
    , language(language)
{
    updateTheme();
    connect(Config(), &Configuration::colorsUpdated, this, [this]() {
        updateTheme();
        rehighlight();
    });
}

void FallbackSyntaxHighlighter::updateTheme()
{
    highlightingRules.clear();
    const QColor background = qApp->palette().color(QPalette::Base);
    auto addRule = [this,
                    &background](const QString &pattern, const char *color, bool italic = false) {
        QTextCharFormat format;
        format.setForeground(qhelpers::readableColor(ConfigColor(color), background));
        format.setFontItalic(italic);
        highlightingRules.append({QRegularExpression(pattern), format});
    };
    QString keywords;
    switch (language) {
    case Language::C:
        keywords = QStringLiteral(
            "auto|double|int|struct|break|else|long|switch|case|enum|register|typedef|char|extern|"
            "return|union|const|float|short|unsigned|continue|for|signed|void|default|goto|sizeof|"
            "volatile|do|if|static|while");
        break;
    case Language::JavaScript:
        keywords = QStringLiteral(
            "let|const|var|function|return|if|else|for|while|class|interface|enum|type|import|from|"
            "as|new|async|await|try|catch");
        break;
    case Language::Python:
        keywords = QStringLiteral(
            "def|class|return|if|elif|else|for|while|try|except|finally|with|pass|import|from|as|"
            "yield|async|await");
        break;
    case Language::Radare2:
        keywords = QStringLiteral(
            "af|afl|afr|aaa|aae|aap|pd|pdf|px|izz|is|ii|f|fs|s|e|wx|wv|"
            "ood|oodf|db|dc|dr|afvd");
        addRule(QStringLiteral(R"(^\s*[.?]?[A-Za-z][A-Za-z0-9._-]*)"), "pop");
        break;
    }
    addRule(QStringLiteral("\\b(%1)\\b").arg(keywords), "pop");
    addRule(QStringLiteral(R"(\b[A-Za-z_][A-Za-z0-9_]*(?=\())"), "fname", true);
    addRule(QStringLiteral(R"(\b0x[0-9a-fA-F]+\b)"), "num");
    addRule(QStringLiteral(R"("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')"), "string");
    addRule(
        language == Language::Python || language == Language::Radare2 ? QStringLiteral("#[^\n]*")
                                                                      : QStringLiteral("//[^\n]*"),
        "comment");
    if (language == Language::Radare2) {
        addRule(QStringLiteral(";[^\n]*"), "comment");
    }
    multiLineCommentFormat.setForeground(
        qhelpers::readableColor(ConfigColor("comment"), background));
}

void FallbackSyntaxHighlighter::highlightBlock(const QString &text)
{
    for (const auto &it : highlightingRules) {
        auto matchIterator = it.pattern.globalMatch(text);
        while (matchIterator.hasNext()) {
            const auto match = matchIterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), it.format);
        }
    }

    setCurrentBlockState(0);
    if (language != Language::C) {
        return;
    }

    bool continuing = previousBlockState() == 1;
    int startIndex = continuing ? 0 : text.indexOf(QStringLiteral("/*"));
    while (startIndex >= 0) {
        const int endIndex = text.indexOf(QStringLiteral("*/"), startIndex + (continuing ? 0 : 2));
        if (endIndex < 0) {
            setCurrentBlockState(1);
            setFormat(startIndex, text.length() - startIndex, multiLineCommentFormat);
            break;
        }
        setFormat(startIndex, endIndex + 2 - startIndex, multiLineCommentFormat);
        continuing = false;
        startIndex = text.indexOf(QStringLiteral("/*"), endIndex + 2);
    }
}
