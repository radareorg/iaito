#ifndef CONTEXTMENUICONS_H
#define CONTEXTMENUICONS_H

#include <QAction>
#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QVector>
#include <QWidget>

namespace ContextMenuIcons {

enum class Icon {
    Copy,
    Navigation,
    XRefs,
    Comment,
    Rename,
    Delete,
    Edit,
    View,
    Breakpoint,
    Debug,
    Fold,
    Collapse,
    Expand,
    Sync
};

inline void setIcon(const QWidget *widget, QAction *action, Icon icon, const QColor &color)
{
    const qreal ratio = widget->devicePixelRatioF();
    const int size = 16;
    QPixmap pixmap(size * ratio, size * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor accent = color;
    accent.setAlpha(205);
    painter.setPen(QPen(accent, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    switch (icon) {
    case Icon::Copy:
        painter.drawRect(QRectF(5, 3.5, 7, 9));
        painter.drawRect(QRectF(3, 5.5, 7, 7));
        break;
    case Icon::Navigation:
        painter.drawLine(QPointF(4, 8), QPointF(11, 8));
        painter.drawPolyline(
            QPolygonF(QVector<QPointF>{QPointF(8, 5), QPointF(11, 8), QPointF(8, 11)}));
        break;
    case Icon::XRefs:
        painter.drawEllipse(QPointF(5, 8), 2.0, 2.0);
        painter.drawEllipse(QPointF(11, 5), 2.0, 2.0);
        painter.drawEllipse(QPointF(11, 11), 2.0, 2.0);
        painter.drawLine(QPointF(6.7, 7), QPointF(9.3, 5.8));
        painter.drawLine(QPointF(6.7, 9), QPointF(9.3, 10.2));
        break;
    case Icon::Comment:
        painter.drawRoundedRect(QRectF(3.5, 4, 9, 7), 1.5, 1.5);
        painter.drawLine(QPointF(6, 11), QPointF(4.5, 13));
        break;
    case Icon::Rename:
        painter.drawLine(QPointF(4, 12), QPointF(12, 4));
        painter.drawLine(QPointF(10, 4), QPointF(12, 6));
        painter.drawLine(QPointF(4, 12), QPointF(7, 11));
        break;
    case Icon::Delete:
        painter.drawLine(QPointF(4, 5), QPointF(12, 5));
        painter.drawLine(QPointF(6, 3.5), QPointF(10, 3.5));
        painter.drawRoundedRect(QRectF(5, 6, 6, 7), 1.0, 1.0);
        painter.drawLine(QPointF(7, 7.5), QPointF(7, 11.5));
        painter.drawLine(QPointF(9, 7.5), QPointF(9, 11.5));
        break;
    case Icon::Edit:
        painter.drawRect(QRectF(4, 4, 8, 8));
        painter.drawLine(QPointF(6, 7), QPointF(10, 7));
        painter.drawLine(QPointF(6, 9), QPointF(9, 9));
        break;
    case Icon::View:
        painter.drawEllipse(QRectF(3, 5, 10, 6));
        painter.drawEllipse(QPointF(8, 8), 1.8, 1.8);
        break;
    case Icon::Breakpoint:
        painter.setBrush(accent);
        painter.drawEllipse(QPointF(8, 8), 4.0, 4.0);
        break;
    case Icon::Debug:
        painter.drawLine(QPointF(8, 3), QPointF(8, 13));
        painter.drawLine(QPointF(4, 5), QPointF(12, 5));
        painter.drawLine(QPointF(4, 11), QPointF(12, 11));
        painter.drawRoundedRect(QRectF(5, 4, 6, 8), 1.5, 1.5);
        break;
    case Icon::Fold:
        painter.drawPolyline(
            QPolygonF(QVector<QPointF>{QPointF(4, 3), QPointF(8, 7), QPointF(12, 3)}));
        painter.drawPolyline(
            QPolygonF(QVector<QPointF>{QPointF(4, 13), QPointF(8, 9), QPointF(12, 13)}));
        break;
    case Icon::Collapse:
    case Icon::Expand:
        painter.drawRoundedRect(QRectF(3, 3, 10, 10), 1.5, 1.5);
        painter.drawLine(QPointF(5.5, 8), QPointF(10.5, 8));
        if (icon == Icon::Expand) {
            painter.drawLine(QPointF(8, 5.5), QPointF(8, 10.5));
        }
        break;
    case Icon::Sync:
        painter.drawLine(QPointF(3, 5), QPointF(12, 5));
        painter.drawPolyline(
            QPolygonF(QVector<QPointF>{QPointF(10, 3), QPointF(12, 5), QPointF(10, 7)}));
        painter.drawLine(QPointF(13, 11), QPointF(4, 11));
        painter.drawPolyline(
            QPolygonF(QVector<QPointF>{QPointF(6, 9), QPointF(4, 11), QPointF(6, 13)}));
        break;
    }

    painter.end();
    action->setIcon(QIcon(pixmap));
    action->setIconVisibleInMenu(true);
}

} // namespace ContextMenuIcons

#endif // CONTEXTMENUICONS_H
