#include "TouchTabBarController.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QStyleHints>
#include <QTouchEvent>

TouchTabBarController::TouchTabBarController(QTabBar *tabs)
    : QObject(tabs)
    , tabs(tabs)
    , acceptedTouch(tabs->testAttribute(Qt::WA_AcceptTouchEvents))
{
    tabs->setAttribute(Qt::WA_AcceptTouchEvents);
    tabs->installEventFilter(this);
    holdTimer.setSingleShot(true);
    connect(&holdTimer, &QTimer::timeout, this, [this]() {
        if (!this->tabs || !this->tabs->isVisible() || moved) {
            return;
        }
        held = true;
        const QPoint position = start.toPoint();
        QContextMenuEvent
            context(QContextMenuEvent::Mouse, position, this->tabs->mapToGlobal(position));
        QApplication::sendEvent(this->tabs, &context);
    });
}

TouchTabBarController::~TouchTabBarController()
{
    if (tabs) {
        tabs->setAttribute(Qt::WA_AcceptTouchEvents, acceptedTouch);
    }
}

bool TouchTabBarController::eventFilter(QObject *, QEvent *event)
{
    if (event->type() == QEvent::Hide || event->type() == QEvent::WindowDeactivate) {
        holdTimer.stop();
        moved = true;
    }
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
        holdTimer.stop();
        start = position;
        lastStep = 0;
        moved = points.size() != 1;
        held = false;
        const int index = tabs->tabAt(start.toPoint());
        if (!moved && index >= 0 && tabs->isTabEnabled(index)) {
            holdTimer.start(QGuiApplication::styleHints()->mousePressAndHoldInterval());
        }
    } else if (event->type() == QEvent::TouchUpdate) {
        const QPointF delta = position - start;
        moved |= points.size() != 1 || delta.manhattanLength() > QApplication::startDragDistance();
        if (moved) {
            holdTimer.stop();
        }
        if (!held && points.size() == 1 && qAbs(delta.x()) > qAbs(delta.y())) {
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
    } else {
        holdTimer.stop();
        if (event->type() == QEvent::TouchEnd && !moved && !held) {
            const int index = tabs->tabAt(position.toPoint());
            if (index >= 0 && tabs->isTabEnabled(index)) {
                tabs->setCurrentIndex(index);
            }
        }
    }
    event->accept();
    return true;
}
