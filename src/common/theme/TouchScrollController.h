#ifndef IAITO_TOUCH_SCROLL_CONTROLLER_H
#define IAITO_TOUCH_SCROLL_CONTROLLER_H

#include <QAbstractItemView>
#include <QEvent>
#include <QPointer>
#include <QScrollerProperties>

class QScroller;

// One touchscreen gesture carries both scale and translation. It is independent
// of native trackpad recognizers and remains installed across theme changes.
class TouchTransformController : public QObject
{
    Q_OBJECT
public:
    static TouchTransformController *install(QWidget *target);
    static bool hasTouchScreen();
    bool isActive() const { return active; }

signals:
    void started();
    void transformed(qreal scale, const QPointF &previousCenter, const QPointF &center);
    void finished();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit TouchTransformController(QWidget *target);
    bool active = false;
};

// Custom views translate pixel motion into their own address or graph coordinates.
class TouchScrollAdapter : public QObject
{
    Q_OBJECT
public:
    explicit TouchScrollAdapter(QAbstractScrollArea *area)
        : QObject(area)
    {}

signals:
    void scrollRequested(const QPoint &delta);
};

// Installed by the touch style, or on a custom view's companion surface. Style-owned
// controllers restore the original settings on unpolish. Mouse and trackpad input stay native.
class TouchScrollController : public QObject
{
    Q_OBJECT

public:
    explicit TouchScrollController(QAbstractScrollArea *area, QWidget *target = nullptr);
    ~TouchScrollController() override;

    // Memory and graph views scroll logical addresses instead of pixel scrollbars.
    static TouchScrollAdapter *pixelScrollAdapter(QAbstractScrollArea *area);
    static void enableTextZoom(QAbstractScrollArea *area);
    static void scrollBy(QAbstractScrollArea *area, const QPointF &delta);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QPointer<QAbstractScrollArea> area;
    QPointer<QWidget> viewport;
    QPointer<QScroller> scroller;
    QScrollerProperties originalProperties;
    QAbstractItemView::ScrollMode horizontalMode = QAbstractItemView::ScrollPerItem;
    QAbstractItemView::ScrollMode verticalMode = QAbstractItemView::ScrollPerItem;
    bool acceptedTouch = false;
    bool ownsScrollGesture = false;
    bool directTouch = false;
    bool touching = false;
    bool held = false;
    bool scrolled = false;
    bool forwardingScroll = false;
    bool forwardingMouse = false;
    bool selecting = false;
    bool selectionDragged = false;
    bool pinching = false;
    qreal pinchStartZoom = 1;
    QPoint lastScrollPosition;
    QPoint pressGlobalPosition;

    bool textZoomEnabled() const;
    void cancelMousePress();
    bool sendMouseEvent(QEvent::Type type, const QPoint &globalPosition);
    void showContextMenu(const QPoint &globalPosition);
};

#endif
