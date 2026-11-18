#include "FilePaths.h"

#include <QDir>

QString Iaito::fileNameFromUrl(const QUrl &url)
{
    return url.isLocalFile() ? QDir::toNativeSeparators(url.toLocalFile())
                             : url.toString(QUrl::FullyEncoded);
}

QString Iaito::fileUriScheme(const QString &name)
{
    const int separator = name.indexOf(QStringLiteral("://"));
    return separator > 0 ? name.left(separator + 3) : QStringLiteral("file://");
}

QString Iaito::fileNameForIO(const QString &name, const QString &scheme)
{
    if (name.startsWith(QStringLiteral("file://"))) {
        return fileNameFromUrl(QUrl(name));
    }
    if (name.contains(QStringLiteral("://"))) {
        return name;
    }
    if (scheme == QStringLiteral("file://")
        || (scheme == QStringLiteral("content://") && QDir::isAbsolutePath(name))) {
        return QDir::toNativeSeparators(name);
    }
    return scheme + name;
}
