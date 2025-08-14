
#include "SvgIconEngine.h"

#include <QFile>
#include <QPainter>
#include <QSvgRenderer>

#include "Helpers.h"

SvgIconEngine::SvgIconEngine(const QString &filename)
{
    QFile file(filename);
    file.open(QFile::ReadOnly);
    this->svgData = file.readAll();
}

SvgIconEngine::SvgIconEngine(const QString &filename, const QColor &tintColor)
    : SvgIconEngine(filename)
{
    this->svgData = qhelpers::applyColorToSvg(svgData, tintColor);
}

SvgIconEngine::SvgIconEngine(const QString &filename, QPalette::ColorRole colorRole)
    : SvgIconEngine(filename, QPalette().color(colorRole))
{}

void SvgIconEngine::paint(
    QPainter *painter, const QRect &rect, QIcon::Mode /*mode*/, QIcon::State /*state*/)
{
    QSvgRenderer renderer(svgData);
    renderer.render(painter, rect);
}

QIconEngine *SvgIconEngine::clone() const
{
    return new SvgIconEngine(*this);
}

#include <QCoreApplication>
#include <QDebug>
#include <QThread>

QPixmap SvgIconEngine::pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state)
{
    // QPixmap/QSvgRenderer must be used from the GUI thread. If called from a
    // worker thread, return a harmless empty pixmap and log a warning to avoid
    // crashing (this indicates a bug elsewhere where GUI icons are requested
    // off the main thread).
    if (QCoreApplication::instance()
        && QThread::currentThread() != QCoreApplication::instance()->thread()) {
        qWarning().nospace() << "SvgIconEngine::pixmap called from non-GUI thread: "
                             << (quintptr) QThread::currentThread();
        return QPixmap(size);
    }

    QImage img(size, QImage::Format_ARGB32);
    img.fill(qRgba(0, 0, 0, 0));
    QPixmap pix = QPixmap::fromImage(img, Qt::NoFormatConversion);
    {
        QPainter painter(&pix);
        QRect r(QPoint(0, 0), size);
        this->paint(&painter, r, mode, state);
    }
    return pix;
}
