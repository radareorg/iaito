#include "TouchScrollController.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QGestureEvent>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QScrollEvent>
#include <QScrollPrepareEvent>
#include <QScroller>
#include <QTapAndHoldGesture>
#include <QTimer>

namespace {
constexpr const char *releasedGestureProperty = "iaitoReleasedScrollGesture";
} // namespace

TouchScrollController::TouchScrollController(QAbstractScrollArea *area)
    : QObject(area->viewport())
    , area(area)
    , viewport(area->viewport())
{
    acceptedTouch = viewport->testAttribute(Qt::WA_AcceptTouchEvents);
    scroller = QScroller::scroller(viewport);
    originalProperties = scroller->scrollerProperties();
    const int gesture = int(QScroller::grabbedGesture(viewport));
    // Qt initializes this to CustomGesture, and ungrabGesture() leaves the old
    // type behind. Remember our released type so repeated theme switches work.
    ownsScrollGesture = gesture <= int(Qt::CustomGesture)
                        || gesture == scroller->property(releasedGestureProperty).toInt();
    if (ownsScrollGesture) {
        QScroller::grabGesture(viewport, QScroller::TouchGesture);
    }
    auto properties = originalProperties;
    properties.setScrollMetric(QScrollerProperties::DragStartDistance, 0.003);
    properties.setScrollMetric(QScrollerProperties::AxisLockThreshold, 0.3);
    properties.setScrollMetric(QScrollerProperties::DecelerationFactor, 0.15);
    properties.setScrollMetric(QScrollerProperties::MaximumVelocity, 0.8);
    properties.setScrollMetric(QScrollerProperties::OvershootDragResistanceFactor, 0.35);
    properties.setScrollMetric(QScrollerProperties::OvershootDragDistanceFactor, 0.15);
    properties.setScrollMetric(QScrollerProperties::OvershootScrollDistanceFactor, 0.1);
    properties.setScrollMetric(QScrollerProperties::OvershootScrollTime, 0.3);
    properties.setScrollMetric(
        QScrollerProperties::HorizontalOvershootPolicy,
        QScrollerProperties::OvershootWhenScrollable);
    properties.setScrollMetric(
        QScrollerProperties::VerticalOvershootPolicy, QScrollerProperties::OvershootWhenScrollable);
    scroller->setScrollerProperties(properties);
    connect(scroller, &QScroller::stateChanged, this, [this](QScroller::State state) {
        if (!scrolled && (state == QScroller::Dragging || state == QScroller::Scrolling)) {
            scrolled = true;
            if (touching) {
                cancelMousePress();
            }
        }
    });
    viewport->grabGesture(Qt::TapAndHoldGesture);
    viewport->installEventFilter(this);
    area->installEventFilter(this);
    if (auto *view = qobject_cast<QAbstractItemView *>(area)) {
        horizontalMode = view->horizontalScrollMode();
        verticalMode = view->verticalScrollMode();
        view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    }
}

TouchScrollController::~TouchScrollController()
{
    if (viewport) {
        if (scroller) {
            scroller->stop();
            scroller->setScrollerProperties(originalProperties);
        }
        if (ownsScrollGesture && scroller) {
            scroller->setProperty(releasedGestureProperty, int(QScroller::grabbedGesture(viewport)));
            QScroller::ungrabGesture(viewport);
        }
        viewport->ungrabGesture(Qt::TapAndHoldGesture);
        viewport->setAttribute(Qt::WA_AcceptTouchEvents, acceptedTouch);
    }
    if (auto *view = qobject_cast<QAbstractItemView *>(area.data())) {
        view->setHorizontalScrollMode(horizontalMode);
        view->setVerticalScrollMode(verticalMode);
    }
}

TouchScrollAdapter *TouchScrollController::pixelScrollAdapter(QAbstractScrollArea *area)
{
    auto *adapter = area->findChild<TouchScrollAdapter *>(QString(), Qt::FindDirectChildrenOnly);
    return adapter ? adapter : new TouchScrollAdapter(area);
}

void TouchScrollController::cancelMousePress()
{
    if (!viewport) {
        return;
    }
    // End any synthesized press outside the content, clearing selection/drag
    // state without activating the item under the finger on release.
    const QPoint position(-QWIDGETSIZE_MAX, -QWIDGETSIZE_MAX);
    QMouseEvent release(
        QEvent::MouseButtonRelease,
        position,
        viewport->mapToGlobal(position),
        Qt::LeftButton,
        Qt::NoButton,
        Qt::NoModifier);
    QApplication::sendEvent(viewport, &release);
}

void TouchScrollController::showContextMenu(const QPoint &globalPosition)
{
    if (!viewport || !viewport->isVisible() || !viewport->isEnabled()) {
        return;
    }
    QPointer<QWidget> target = viewport->childAt(viewport->mapFromGlobal(globalPosition));
    if (!target) {
        target = viewport;
    }
    const QPoint position = target->mapFromGlobal(globalPosition);
    // Right-press selects the row or instruction under the finger without
    // activating links or triggering a left-click action.
    QMouseEvent press(
        QEvent::MouseButtonPress,
        position,
        globalPosition,
        Qt::RightButton,
        Qt::RightButton,
        Qt::NoModifier);
    QApplication::sendEvent(target, &press);
    if (!target) {
        return;
    }
    QMouseEvent release(
        QEvent::MouseButtonRelease,
        position,
        globalPosition,
        Qt::RightButton,
        Qt::NoButton,
        Qt::NoModifier);
    QApplication::sendEvent(target, &release);
    if (target) {
        QContextMenuEvent context(QContextMenuEvent::Mouse, position, globalPosition);
        QApplication::sendEvent(target, &context);
    }
}

bool TouchScrollController::eventFilter(QObject *watched, QEvent *event)
{
    if (!area || !viewport || forwardingScroll) {
        return false;
    }
    switch (event->type()) {
    case QEvent::TouchBegin:
        touching = true;
        held = false;
        scrolled = false;
        break;
    case QEvent::TouchEnd:
        touching = false;
        break;
    case QEvent::TouchCancel:
    case QEvent::Hide:
        touching = false;
        scroller->stop();
        break;
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseMove:
        if ((held || scrolled)
            && static_cast<QMouseEvent *>(event)->source() != Qt::MouseEventNotSynthesized) {
            return true;
        }
        break;
    case QEvent::Gesture: {
        auto *gestures = static_cast<QGestureEvent *>(event);
        auto *hold = static_cast<QTapAndHoldGesture *>(gestures->gesture(Qt::TapAndHoldGesture));
        if (hold && touching && !held && !scrolled && hold->state() == Qt::GestureFinished
            && scroller->state() != QScroller::Dragging
            && scroller->state() != QScroller::Scrolling) {
            held = true;
            scroller->stop();
            cancelMousePress();
            gestures->accept(hold);
            const QPoint globalPosition = hold->position().toPoint();
            QTimer::singleShot(0, this, [this, globalPosition]() {
                showContextMenu(globalPosition);
            });
            return true;
        }
        break;
    }
    case QEvent::ScrollPrepare:
        if (watched == viewport
            && area->findChild<TouchScrollAdapter *>(QString(), Qt::FindDirectChildrenOnly)) {
            auto *prepare = static_cast<QScrollPrepareEvent *>(event);
            // Relative scrolling keeps 64-bit addresses out of QScroller's pixel range.
            lastScrollPosition = QPoint();
            prepare->setViewportSize(viewport->size());
            prepare->setContentPosRange(QRectF(-1e9, -1e9, 2e9, 2e9));
            prepare->setContentPos(QPointF());
            prepare->accept();
            return true;
        }
        if (watched == viewport && qobject_cast<QPlainTextEdit *>(area.data())) {
            auto *prepare = static_cast<QScrollPrepareEvent *>(event);
            const int lineHeight = qMax(1, area->fontMetrics().lineSpacing());
            auto *horizontal = area->horizontalScrollBar();
            auto *vertical = area->verticalScrollBar();
            prepare->setViewportSize(viewport->size());
            prepare->setContentPosRange(QRectF(
                horizontal->minimum(),
                qreal(vertical->minimum()) * lineHeight,
                horizontal->maximum() - horizontal->minimum(),
                qreal(vertical->maximum() - vertical->minimum()) * lineHeight));
            prepare->setContentPos(
                QPointF(horizontal->value(), qreal(vertical->value()) * lineHeight));
            prepare->accept();
            return true;
        }
        break;
    case QEvent::Scroll: {
        auto *adapter = area->findChild<TouchScrollAdapter *>(QString(), Qt::FindDirectChildrenOnly);
        if (watched == viewport && adapter) {
            const QPoint position = static_cast<QScrollEvent *>(event)->contentPos().toPoint();
            const QPoint delta = lastScrollPosition - position;
            lastScrollPosition = position;
            if (!delta.isNull()) {
                // QScroller consumes wheel events while active; dispatch directly.
                emit adapter->scrollRequested(delta);
            }
            event->accept();
            return true;
        }
        if (watched == viewport && qobject_cast<QPlainTextEdit *>(area.data())) {
            // QPlainTextEdit's vertical scrollbar counts lines. Keep QScroller
            // coordinates and overshoot in pixels, and let Qt handle the bounce.
            const auto *scroll = static_cast<QScrollEvent *>(event);
            QPointF position = scroll->contentPos();
            position.ry() /= qMax(1, area->fontMetrics().lineSpacing());
            QScrollEvent translated(position, scroll->overshootDistance(), scroll->scrollState());
            QScopedValueRollback<bool> guard(forwardingScroll, true);
            QApplication::sendEvent(viewport, &translated);
            event->accept();
            return true;
        }
        break;
    }
    default:
        break;
    }
    return false;
}
