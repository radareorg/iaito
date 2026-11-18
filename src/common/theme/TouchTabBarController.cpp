#include "TouchTabBarController.h"

#include <QApplication>
#include <QTouchEvent>

TouchTabBarController::TouchTabBarController(QTabBar *tabs)
    : QObject(tabs)
    , tabs(tabs)
    , acceptedTouch(tabs->testAttribute(Qt::WA_AcceptTouchEvents))
{
    tabs->setAttribute(Qt::WA_AcceptTouchEvents);
    tabs->installEventFilter(this);
}

TouchTabBarController::~TouchTabBarController()
{
    if (tabs) {
        tabs->setAttribute(Qt::WA_AcceptTouchEvents, acceptedTouch);
    }
}

bool TouchTabBarController::eventFilter(QObject *, QEvent *event)
{
    if (!tabs
        || (event->type() != QEvent::TouchBegin && event->type() != QEvent::TouchUpdate
            && event->type() != QEvent::TouchEnd && event->type() != QEvent::TouchCancel)) {
        return false;
    }
    auto *touch = static_cast<QTouchEvent *>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const auto points = touch->points();
    const QPointF position = points.isEmpty() ? start : points.first().position();
#else
    const auto points = touch->touchPoints();
    const QPointF position = points.isEmpty() ? start : points.first().pos();
#endif
    if (event->type() == QEvent::TouchBegin) {
        start = position;
        lastStep = 0;
        moved = false;
    } else if (event->type() == QEvent::TouchUpdate) {
        const QPointF delta = position - start;
        moved |= delta.manhattanLength() > QApplication::startDragDistance();
        if (points.size() == 1 && qAbs(delta.x()) > qAbs(delta.y())) {
            const int threshold = qMax(48, tabs->fontMetrics().height() * 3);
            const int steps = int((delta.x() - lastStep) / threshold);
            const int direction = (steps < 0 ? 1 : -1)
                                  * (tabs->layoutDirection() == Qt::RightToLeft ? -1 : 1);
            for (int step = 0; step < qAbs(steps); ++step) {
                int next = tabs->currentIndex() + direction;
                while (next >= 0 && next < tabs->count() && !tabs->isTabEnabled(next)) {
                    next += direction;
                }
                if (next >= 0 && next < tabs->count()) {
                    tabs->setCurrentIndex(next);
                }
            }
            lastStep += steps * threshold;
        }
    } else if (event->type() == QEvent::TouchEnd && !moved) {
        const int index = tabs->tabAt(position.toPoint());
        if (index >= 0 && tabs->isTabEnabled(index)) {
            tabs->setCurrentIndex(index);
        }
    }
    event->accept();
    return true;
}
