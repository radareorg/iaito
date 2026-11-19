#ifndef IAITO_FILEPATHS_H
#define IAITO_FILEPATHS_H

#include <QString>
#include <QUrl>

namespace Iaito {
// Preserve encoded provider IDs; toLocalFile() is only valid for file URLs.
QString fileNameFromUrl(const QUrl &url);
QString fileUriScheme(const QString &name);
QString fileNameForIO(const QString &name, const QString &scheme);
// Keep a picker-provided temporary document for later analysis and recent files.
QString importFile(const QString &source, const QString &directory, QString *error);
} // namespace Iaito

#endif
