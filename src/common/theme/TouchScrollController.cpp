#include "TouchScrollController.h"
#include "common/Configuration.h"

#include <algorithm>
#include <cmath>
#include <QApplication>
#include <QContextMenuEvent>
#include <QGestureEvent>
#include <QGestureRecognizer>
#include <QLineF>
#include <QMouseEvent>
#include <QPinchGesture>
#include <QPlainTextEdit>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QScrollEvent>
#include <QScrollPrepareEvent>
#include <QScroller>
#include <QTapAndHoldGesture>
#include <QTextEdit>
#include <QTimer>
#include <QTouchEvent>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QInputDevice>
#else
#include <QTouchDevice>
#endif

namespace {
constexpr const char *releasedGestureProperty = "iaitoReleasedScrollGesture";

class TouchTransform : public QGesture
{
public:
    QPointF previousCenter;
    QPointF center;
    qreal distance = 0;
    qreal scale = 1;
    int firstId = -1;
    int secondId = -1;
    bool tracking = false;
};

class TouchTransformRecognizer : public QGestureRecognizer
{
public:
    QGesture *create(QObject *target) override
    {
        if (auto *widget = qobject_cast<QWidget *>(target)) {
            widget->setAttribute(Qt::WA_AcceptTouchEvents);
        }
        auto *gesture = new TouchTransform;
        gesture->setGestureCancelPolicy(QGesture::CancelAllInContext);
        return gesture;
    }

    Result recognize(QGesture *state, QObject *, QEvent *event) override
    {
        auto *gesture = static_cast<TouchTransform *>(state);
        if (event->type() == QEvent::TouchCancel) {
            return CancelGesture;
        }
        if (event->type() == QEvent::TouchEnd) {
            return gesture->tracking ? FinishGesture : CancelGesture;
        }
        if (event->type() != QEvent::TouchBegin && event->type() != QEvent::TouchUpdate) {
            return Ignore;
        }
        auto *touch = static_cast<QTouchEvent *>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        if (touch->pointingDevice()->type() != QInputDevice::DeviceType::TouchScreen) {
            return Ignore;
        }
        const auto points = touch->points();
#else
        if (touch->device()->type() != QTouchDevice::TouchScreen) {
            return Ignore;
        }
        const auto points = touch->touchPoints();
#endif
        QList<QPair<int, QPointF>> fingers;
        for (const auto &point : points) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            if (point.state() != QEventPoint::State::Released) {
                fingers.append({point.id(), point.globalPosition()});
            }
#else
            if (point.state() != Qt::TouchPointReleased) {
                fingers.append({point.id(), point.screenPos()});
            }
#endif
        }
        if (fingers.isEmpty()) {
            return gesture->tracking ? FinishGesture : CancelGesture;
        }
        if (fingers.size() < 2 && !gesture->tracking) {
            return MayBeGesture;
        }
        // Stable IDs prevent a point-order change from moving the content. Rebase
        // when fingers are added/lifted; the remaining finger can keep panning.
        std::sort(fingers.begin(), fingers.end(), [](const auto &a, const auto &b) {
            return a.first < b.first;
        });
        const int firstId = fingers[0].first;
        const int secondId = fingers.size() > 1 ? fingers[1].first : -1;
        const QPointF center = secondId >= 0 ? (fingers[0].second + fingers[1].second) / 2
                                             : fingers[0].second;
        const qreal distance = secondId >= 0 ? QLineF(fingers[0].second, fingers[1].second).length()
                                             : 0;
        gesture->scale = 1;
        if (firstId == gesture->firstId && secondId == gesture->secondId) {
            gesture->previousCenter = gesture->center;
            if (gesture->distance > 0 && distance > 0) {
                gesture->scale = distance / gesture->distance;
            }
        } else {
            gesture->previousCenter = center;
        }
        gesture->center = center;
        gesture->distance = distance;
        gesture->firstId = firstId;
        gesture->secondId = secondId;
        gesture->tracking = true;
        gesture->setHotSpot(center);
        return TriggerGesture;
    }

    void reset(QGesture *state) override
    {
        auto *gesture = static_cast<TouchTransform *>(state);
        gesture->previousCenter = gesture->center = QPointF();
        gesture->distance = 0;
        gesture->scale = 1;
        gesture->firstId = gesture->secondId = -1;
        gesture->tracking = false;
        QGestureRecognizer::reset(state);
    }
};

Qt::GestureType touchTransformType()
{
    static const auto type = QGestureRecognizer::registerRecognizer(new TouchTransformRecognizer);
    return type;
}
} // namespace

TouchTransformController::TouchTransformController(QWidget *target)
    : QObject(target)
{
    target->grabGesture(touchTransformType());
    target->installEventFilter(this);
}

TouchTransformController *TouchTransformController::install(QWidget *target)
{
    auto *controller
        = target->findChild<TouchTransformController *>(QString(), Qt::FindDirectChildrenOnly);
    return controller ? controller : new TouchTransformController(target);
}

bool TouchTransformController::hasTouchScreen()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    for (const auto *device : QInputDevice::devices()) {
        if (device->type() == QInputDevice::DeviceType::TouchScreen) {
#else
    for (const auto *device : QTouchDevice::devices()) {
        if (device->type() == QTouchDevice::TouchScreen) {
#endif
            return true;
        }
    }
    return false;
}

bool TouchTransformController::eventFilter(QObject *watched, QEvent *event)
{
    if (active && (event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchUpdate)) {
        // Let QApplication record the accepted TouchBegin on the viewport so it
        // keeps routing subsequent samples here, including QAbstractScrollArea.
        event->accept();
        return true;
    }
    if (event->type() == QEvent::Gesture) {
        auto *gestures = static_cast<QGestureEvent *>(event);
        if (auto *gesture = static_cast<TouchTransform *>(gestures->gesture(touchTransformType()))) {
            QPointer<TouchTransformController> guard(this);
            if (gesture->state() == Qt::GestureStarted) {
                active = true;
                emit started();
            }
            if (!guard) {
                return true;
            }
            if ((gesture->state() == Qt::GestureStarted || gesture->state() == Qt::GestureUpdated)
                && (!qFuzzyCompare(gesture->scale, 1.0)
                    || gesture->previousCenter != gesture->center)) {
                auto *widget = static_cast<QWidget *>(watched);
                auto local = [widget](const QPointF &global) {
                    return QPointF(widget->mapFromGlobal(global.toPoint())) + global
                           - QPointF(global.toPoint());
                };
                emit transformed(
                    gesture->scale, local(gesture->previousCenter), local(gesture->center));
            }
            if (!guard) {
                return true;
            }
            if (gesture->state() == Qt::GestureFinished || gesture->state() == Qt::GestureCanceled) {
                active = false;
                emit finished();
            }
            gestures->accept(gesture);
            return true;
        }
    } else if (active && (event->type() == QEvent::Hide || event->type() == QEvent::TouchCancel)) {
        active = false;
        emit finished();
    }
    return false;
}

TouchScrollController::TouchScrollController(QAbstractScrollArea *area, QWidget *target)
    : QObject(target ? target : area->viewport())
    , area(area)
    , viewport(target ? target : area->viewport())
{
    acceptedTouch = viewport->testAttribute(Qt::WA_AcceptTouchEvents);
    scroller = QScroller::scroller(viewport);
    originalProperties = scroller->scrollerProperties();
    const int gesture = int(QScroller::grabbedGesture(viewport));
    // Qt initializes this to CustomGesture, and ungrabGesture() leaves the old
    // type behind. Remember our released type so repeated theme switches work.
    ownsScrollGesture = gesture <= int(Qt::CustomGesture)
                        || gesture == scroller->property(releasedGestureProperty).toInt();
    // Custom address/graph views already consume raw touch events. Feed their
    // scroller here as well, without depending on platform gesture arbitration.
    directTouch = ownsScrollGesture
                  && area->findChild<TouchScrollAdapter *>(QString(), Qt::FindDirectChildrenOnly);
    if (directTouch) {
        viewport->setAttribute(Qt::WA_AcceptTouchEvents);
    } else if (ownsScrollGesture) {
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
        }
    });
    if (viewport != area->viewport()) {
        auto *contentScroller = QScroller::scroller(area->viewport());
        auto stopOther = [](QScroller *other, QScroller::State state) {
            if (state == QScroller::Pressed || state == QScroller::Dragging) {
                other->stop();
            }
        };
        // The gutter and content move one listing; touching either must stop
        // momentum left over from a flick on the other surface.
        connect(
            scroller,
            &QScroller::stateChanged,
            contentScroller,
            [contentScroller, stopOther](QScroller::State state) {
                stopOther(contentScroller, state);
            });
        connect(
            contentScroller,
            &QScroller::stateChanged,
            this,
            [this, stopOther](QScroller::State state) { stopOther(scroller, state); });
    }
    viewport->grabGesture(Qt::TapAndHoldGesture);
    if (textZoomEnabled()) {
        viewport->grabGesture(Qt::PinchGesture);
    }
    auto *transform = TouchTransformController::install(viewport);
    connect(transform, &TouchTransformController::started, this, [this]() {
        pinching = true;
        scrolled = true;
        scroller->stop();
        if (selecting) {
            cancelMousePress();
            selecting = false;
        }
    });
    connect(transform, &TouchTransformController::finished, this, [this]() { pinching = false; });
    connect(
        transform,
        &TouchTransformController::transformed,
        this,
        [this](qreal, const QPointF &previous, const QPointF &center) {
            if (!textZoomEnabled() && !this->area->property("iaitoCustomTouchTransform").toBool()) {
                scrollBy(this->area, center - previous);
            }
        });
    viewport->installEventFilter(this);
    if (viewport == area->viewport()) {
        area->installEventFilter(this);
    }
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
        if (ownsScrollGesture && !directTouch && scroller) {
            scroller->setProperty(releasedGestureProperty, int(QScroller::grabbedGesture(viewport)));
            QScroller::ungrabGesture(viewport);
        }
        viewport->ungrabGesture(Qt::TapAndHoldGesture);
        if (textZoomEnabled()) {
            viewport->ungrabGesture(Qt::PinchGesture);
        }
        viewport->setAttribute(Qt::WA_AcceptTouchEvents, acceptedTouch);
    }
    if (auto *view = qobject_cast<QAbstractItemView *>(area.data())) {
        view->setHorizontalScrollMode(horizontalMode);
        view->setVerticalScrollMode(verticalMode);
    }
}

bool TouchScrollController::textZoomEnabled() const
{
    return area && viewport == area->viewport() && area->property("iaitoTextZoom").toBool();
}

TouchScrollAdapter *TouchScrollController::pixelScrollAdapter(QAbstractScrollArea *area)
{
    auto *adapter = area->findChild<TouchScrollAdapter *>(QString(), Qt::FindDirectChildrenOnly);
    return adapter ? adapter : new TouchScrollAdapter(area);
}

void TouchScrollController::enableTextZoom(QAbstractScrollArea *area)
{
    if (area->property("iaitoTextZoom").toBool()) {
        return;
    }
    area->setProperty("iaitoTextZoom", true);
    auto *transform = TouchTransformController::install(area->viewport());
    connect(transform, &TouchTransformController::started, area, [area]() {
        area->setProperty("iaitoTouchScrollRemainder", QPointF());
    });
    connect(
        transform,
        &TouchTransformController::transformed,
        area,
        [area](qreal scale, const QPointF &previous, const QPointF &center) {
            const qreal oldZoom = Config()->getZoomFactor();
            const qreal zoom = qBound(0.3, oldZoom * scale, 4.0);
            const int oldLineHeight = qMax(1, area->fontMetrics().lineSpacing());
            const int oldCharacterWidth = qMax(1, area->fontMetrics().horizontalAdvance('M'));
            const int horizontal = area->horizontalScrollBar()->value();
            const int vertical = area->verticalScrollBar()->value();
            if (!qFuzzyCompare(zoom, oldZoom)) {
                Config()->setZoomFactor(zoom);
            }
            const qreal verticalRatio = qreal(qMax(1, area->fontMetrics().lineSpacing()))
                                        / oldLineHeight;
            const qreal horizontalRatio = qreal(qMax(1, area->fontMetrics().horizontalAdvance('M')))
                                          / oldCharacterWidth;
            const QPointF delta
                = center - QPointF(previous.x() * horizontalRatio, previous.y() * verticalRatio);
            if (area->findChild<TouchScrollAdapter *>(QString(), Qt::FindDirectChildrenOnly)) {
                scrollBy(area, delta);
            } else {
                // QPlainTextEdit scrolls vertically in lines. Preserve the document
                // position under the fingers even if changing the font moved its cursor.
                area->horizontalScrollBar()->setValue(qRound(horizontal * horizontalRatio));
                area->verticalScrollBar()->setValue(vertical);
                scrollBy(area, delta);
            }
        });
    if (area->viewport()->findChild<TouchScrollController *>()) {
        area->viewport()->grabGesture(Qt::PinchGesture);
    }
}

void TouchScrollController::scrollBy(QAbstractScrollArea *area, const QPointF &delta)
{
    auto *adapter = area->findChild<TouchScrollAdapter *>(QString(), Qt::FindDirectChildrenOnly);
    QPointF motion = delta;
    if (!adapter && qobject_cast<QPlainTextEdit *>(area)) {
        motion.ry() /= qMax(1, area->fontMetrics().lineSpacing());
    }
    motion += area->property("iaitoTouchScrollRemainder").toPointF();
    const QPoint pixels = motion.toPoint();
    area->setProperty("iaitoTouchScrollRemainder", motion - QPointF(pixels));
    if (pixels.isNull()) {
        return;
    }
    if (adapter) {
        emit adapter->scrollRequested(pixels);
    } else {
        area->horizontalScrollBar()->setValue(area->horizontalScrollBar()->value() - pixels.x());
        area->verticalScrollBar()->setValue(area->verticalScrollBar()->value() - pixels.y());
    }
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

bool TouchScrollController::sendMouseEvent(QEvent::Type type, const QPoint &globalPosition)
{
    QPointer<QWidget> target = viewport->childAt(viewport->mapFromGlobal(globalPosition));
    if (!target) {
        target = viewport;
    }
    const bool release = type == QEvent::MouseButtonRelease;
    QMouseEvent mouse(
        type,
        target->mapFromGlobal(globalPosition),
        globalPosition,
        type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
        release ? Qt::NoButton : Qt::LeftButton,
        Qt::NoModifier);
    QPointer<TouchScrollController> guard(this);
    forwardingMouse = true;
    QApplication::sendEvent(target, &mouse);
    if (!guard) {
        return false;
    }
    forwardingMouse = false;
    return true;
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
    if (!area || !viewport || forwardingScroll || forwardingMouse) {
        return false;
    }
    if (event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchUpdate
        || event->type() == QEvent::TouchEnd) {
        auto *touch = static_cast<QTouchEvent *>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        if (touch->pointingDevice()->type() != QInputDevice::DeviceType::TouchScreen) {
            return false;
        }
        const auto points = touch->points();
        const QPoint globalPosition = points.isEmpty() ? pressGlobalPosition
                                                       : points.first().globalPosition().toPoint();
#else
        if (touch->device()->type() != QTouchDevice::TouchScreen) {
            return false;
        }
        const auto points = touch->touchPoints();
        const QPoint globalPosition = points.isEmpty() ? pressGlobalPosition
                                                       : points.first().screenPos().toPoint();
#endif
        event->accept();
        if (event->type() == QEvent::TouchBegin) {
            touching = true;
            pinching = false;
            held = false;
            scrolled = false;
            selecting = false;
            selectionDragged = false;
            pressGlobalPosition = globalPosition;
            if (directTouch) {
                scroller->handleInput(
                    QScroller::InputPress,
                    viewport->mapFromGlobal(globalPosition),
                    touch->timestamp());
            }
        } else if (event->type() == QEvent::TouchUpdate) {
            if (points.size() > 1) {
                scroller->stop();
                scrolled = true;
            } else if (selecting) {
                selectionDragged |= (globalPosition - pressGlobalPosition).manhattanLength()
                                    >= QApplication::startDragDistance();
                sendMouseEvent(QEvent::MouseMove, globalPosition);
            } else if (!pinching) {
                if (directTouch) {
                    scroller->handleInput(
                        QScroller::InputMove,
                        viewport->mapFromGlobal(globalPosition),
                        touch->timestamp());
                }
                if ((globalPosition - pressGlobalPosition).manhattanLength()
                    >= QApplication::startDragDistance()) {
                    // A drag on content without a scroll range is still not a tap.
                    scrolled = true;
                }
            }
        } else {
            touching = false;
            if (directTouch && !held && !pinching) {
                scroller->handleInput(
                    QScroller::InputRelease,
                    viewport->mapFromGlobal(globalPosition),
                    touch->timestamp());
            }
            if (selecting) {
                selecting = false;
                if (!sendMouseEvent(QEvent::MouseButtonRelease, globalPosition)) {
                    return true;
                }
                if (!selectionDragged) {
                    QTimer::singleShot(0, this, [this, globalPosition]() {
                        showContextMenu(globalPosition);
                    });
                }
            } else if (!held && !scrolled && !pinching) {
                // Delay clicks until release: pressing into a text view seeks,
                // changes its cursor and can scroll horizontally before a swipe
                // has crossed QScroller's drag threshold.
                QPointer<TouchScrollController> guard(this);
                sendMouseEvent(QEvent::MouseButtonPress, pressGlobalPosition);
                if (guard && viewport) {
                    sendMouseEvent(QEvent::MouseButtonRelease, globalPosition);
                }
            }
        }
        return true;
    }
    switch (event->type()) {
    case QEvent::TouchCancel:
    case QEvent::Hide:
        touching = false;
        pinching = false;
        if (selecting) {
            cancelMousePress();
            selecting = false;
        }
        scroller->stop();
        break;
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseMove:
        if ((touching || held || scrolled)
            && static_cast<QMouseEvent *>(event)->source() != Qt::MouseEventNotSynthesized) {
            return true;
        }
        break;
    case QEvent::Gesture: {
        auto *gestures = static_cast<QGestureEvent *>(event);
        auto *pinch = static_cast<QPinchGesture *>(gestures->gesture(Qt::PinchGesture));
        if (pinch && textZoomEnabled()) {
            if (pinch->state() == Qt::GestureStarted) {
                pinchStartZoom = Config()->getZoomFactor();
                pinching = true;
                scrolled = true;
                if (selecting) {
                    cancelMousePress();
                    selecting = false;
                }
            }
            scroller->stop();
            const qreal scale = pinch->totalScaleFactor();
            if (pinching && std::isfinite(scale) && scale > 0
                && pinch->state() != Qt::GestureCanceled) {
                const qreal zoom = qBound(0.3, qRound(pinchStartZoom * scale * 100) / 100.0, 4.0);
                if (!qFuzzyCompare(zoom, Config()->getZoomFactor())) {
                    Config()->setZoomFactor(zoom);
                }
            }
            if (pinch->state() == Qt::GestureFinished || pinch->state() == Qt::GestureCanceled) {
                pinching = false;
            }
            gestures->accept(pinch);
            return true;
        }
        auto *hold = static_cast<QTapAndHoldGesture *>(gestures->gesture(Qt::TapAndHoldGesture));
        if (hold && touching && !held && !scrolled && hold->state() == Qt::GestureFinished
            && scroller->state() != QScroller::Dragging
            && scroller->state() != QScroller::Scrolling) {
            held = true;
            scroller->stop();
            gestures->accept(hold);
            const QPoint globalPosition = hold->position().toPoint();
            const bool onContent = viewport == area->viewport();
            auto *plainText = onContent ? qobject_cast<QPlainTextEdit *>(area.data()) : nullptr;
            auto *richText = onContent ? qobject_cast<QTextEdit *>(area.data()) : nullptr;
            const bool hasSelection = (plainText && plainText->textCursor().hasSelection())
                                      || (richText && richText->textCursor().hasSelection())
                                      || area->property("iaitoHasTouchSelection").toBool();
            if (!hasSelection
                && (plainText || richText || area->property("iaitoTouchSelection").toBool())) {
                selecting = true;
                sendMouseEvent(QEvent::MouseButtonPress, pressGlobalPosition);
                return true;
            }
            QTimer::singleShot(0, this, [this, globalPosition]() {
                showContextMenu(globalPosition);
            });
            return true;
        }
        break;
    }
    case QEvent::ScrollPrepare:
        if (pinching) {
            return true;
        }
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
        if (pinching) {
            event->accept();
            return true;
        }
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
