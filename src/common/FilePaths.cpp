#include "FilePaths.h"

#include <r_userconf.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>

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

QString Iaito::importFile(const QString &source, const QString &directory, QString *error)
{
    error->clear();
    if (directory.isEmpty() || !QDir().mkpath(directory)) {
        *error = QStringLiteral("Cannot create the imported files directory.");
        return {};
    }
    // Separate directories preserve the original name without overwriting a
    // previous selection with the same name (possibly from another provider).
    QTemporaryDir destination(QDir(directory).filePath(QStringLiteral("file-XXXXXX")));
    if (!destination.isValid()) {
        *error = destination.errorString();
        return {};
    }
    const QString path = QDir(destination.path()).filePath(QFileInfo(source).fileName());
    QFile file(source);
    if (!file.copy(path)) {
        *error = file.errorString();
        return {};
    }
    destination.setAutoRemove(false);
    return path;
}

QString Iaito::findRadareTool(const QString &name)
{
    QStringList directories{QCoreApplication::applicationDirPath(), QStringLiteral(R2_BINDIR)};
    const QString prefix = qEnvironmentVariable("R2_PREFIX");
    if (!prefix.isEmpty()) {
        directories.prepend(QDir(prefix).filePath("bin"));
    }
    QString tool = QStandardPaths::findExecutable(name, directories);
    return tool.isEmpty() ? QStandardPaths::findExecutable(name) : tool;
}
