#ifndef MARKDOWN_H
#define MARKDOWN_H

#include <QString>

class QTextBrowser;

namespace Markdown {

void configureBrowser(QTextBrowser *browser);
void render(QTextBrowser *browser, const QString &markdown);

} // namespace Markdown

#endif // MARKDOWN_H
