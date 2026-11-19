#ifndef IAITO_FILEDIALOG_H
#define IAITO_FILEDIALOG_H

#include <QString>
#include <QUrl>

class QWidget;

namespace Iaito {
// Retain Android document URIs and persist temporary iOS document imports.
QString getOpenFileName(
    QWidget *parent,
    const QString &caption,
    const QUrl &directory,
    const QString &filter = QString());
} // namespace Iaito

#endif
