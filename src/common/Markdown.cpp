#include "common/Markdown.h"

#include <QScrollBar>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextOption>
#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
#define IAITO_HAS_NATIVE_MARKDOWN_READER QT_CONFIG(textmarkdownreader)
#else
#define IAITO_HAS_NATIVE_MARKDOWN_READER 0
#endif

namespace {

QString markdownStyleSheet()
{
    return QStringLiteral(
        "h1 { margin-top: 0.45em; margin-bottom: 0.35em; font-size: 160%; }"
        "h2 { margin-top: 0.45em; margin-bottom: 0.30em; font-size: 140%; }"
        "h3 { margin-top: 0.40em; margin-bottom: 0.25em; font-size: 125%; }"
        "h4, h5, h6 { margin-top: 0.35em; margin-bottom: 0.20em; }"
        "p { margin-top: 0; margin-bottom: 0.65em; }"
        "ul, ol { margin-top: 0.20em; margin-bottom: 0.65em; }"
        "li { margin-top: 0.12em; margin-bottom: 0.12em; }"
        "blockquote { margin-left: 1em; padding-left: 0.6em; }"
        "pre { margin-top: 0.25em; margin-bottom: 0.75em; white-space: pre-wrap; }"
        "code { font-family: monospace; }"
        ".task-checkbox { font-family: sans-serif; }");
}

void setDocumentMarkdown(QTextDocument *document, const QString &markdown)
{
    if (!document) {
        return;
    }

    document->setDocumentMargin(8);
#ifndef QT_NO_TEXTHTMLPARSER
    document->setDefaultStyleSheet(markdownStyleSheet());
#endif

#if IAITO_HAS_NATIVE_MARKDOWN_READER
    document->setMarkdown(markdown, QTextDocument::MarkdownDialectGitHub);
#else
    document->setPlainText(markdown);
#endif
}

} // namespace

namespace Markdown {

void configureBrowser(QTextBrowser *browser)
{
    if (!browser) {
        return;
    }
    browser->setReadOnly(true);
    browser->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    browser->document()->setDocumentMargin(8);
}

void render(QTextBrowser *browser, const QString &markdown)
{
    if (!browser) {
        return;
    }

    QScrollBar *bar = browser->verticalScrollBar();
    const bool wasAtBottom = !bar || bar->value() >= bar->maximum() - 4;
    setDocumentMarkdown(browser->document(), markdown);
    if (bar && wasAtBottom) {
        bar->setValue(bar->maximum());
    }
}

} // namespace Markdown

#undef IAITO_HAS_NATIVE_MARKDOWN_READER
