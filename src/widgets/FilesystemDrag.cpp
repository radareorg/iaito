#include "widgets/FilesystemDrag.h"

#include <QDir>
#include <QDrag>
#include <QFileInfo>
#include <QMimeData>
#include <QStyle>
#include <QUrl>

QString FilesystemDrag::uniquePath(const QDir &directory, QString name)
{
    const QString forbidden = QStringLiteral("/\\:*?\"<>|");
    for (int i = 0; i < name.size(); ++i) {
        if (name.at(i).unicode() < 32 || forbidden.contains(name.at(i))) {
            name[i] = QLatin1Char('_');
        }
    }
    name = name.left(120).trimmed();
    while (name.endsWith(QLatin1Char('.'))) {
        name.chop(1);
    }
    if (name.isEmpty() || name.startsWith(QLatin1Char('.'))) {
        name.prepend(QStringLiteral("recovered-"));
    }
#ifdef Q_OS_WIN
    const auto base = name.section(QLatin1Char('.'), 0, 0).toUpper();
    if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL"
        || (base.size() == 4 && (base.startsWith("COM") || base.startsWith("LPT"))
            && base.at(3).isDigit())) {
        name.prepend(QLatin1Char('_'));
    }
#endif
    QString path = directory.filePath(name);
    for (int suffix = 2; QFileInfo(path).exists() || QFileInfo(path).isSymLink(); ++suffix) {
        path = directory.filePath(QStringLiteral("%1 (%2)").arg(name).arg(suffix));
    }
    return path;
}

Qt::DropAction FilesystemDrag::copyFiles(QWidget *widget, const QStringList &paths)
{
    if (paths.isEmpty()) {
        return Qt::IgnoreAction;
    }
    QList<QUrl> urls;
    for (const auto &path : paths) {
        urls.append(QUrl::fromLocalFile(path));
    }
    auto *mime = new QMimeData;
    mime->setUrls(urls);
    auto *drag = new QDrag(widget);
    drag->setMimeData(mime);
    drag->setPixmap(widget->style()->standardIcon(QStyle::SP_FileIcon).pixmap(24, 24));
    const auto action = drag->exec(Qt::CopyAction);
    drag->deleteLater();
    return action;
}
