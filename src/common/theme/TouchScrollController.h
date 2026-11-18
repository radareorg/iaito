#ifndef IAITO_TOUCH_SCROLL_CONTROLLER_H
#define IAITO_TOUCH_SCROLL_CONTROLLER_H

#include <QAbstractItemView>
#include <QPointer>
#include <QScrollerProperties>

class QScroller;

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

// Installed by the touch theme, and removed (with the original settings restored)
// when the widget is unpolished. Mouse selection and trackpad wheel events stay native.
class TouchScrollController : public QObject
{
    Q_OBJECT

public:
    explicit TouchScrollController(QAbstractScrollArea *area);
    ~TouchScrollController() override;

    // Memory and graph views scroll logical addresses instead of pixel scrollbars.
    static TouchScrollAdapter *pixelScrollAdapter(QAbstractScrollArea *area);
    static void enableTextZoom(QAbstractScrollArea *area);

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
    bool touching = false;
    bool held = false;
    bool scrolled = false;
    bool forwardingScroll = false;
    bool pinching = false;
    qreal pinchStartZoom = 1;
    QPoint lastScrollPosition;

    void cancelMousePress();
    void showContextMenu(const QPoint &globalPosition);
};

#endif
