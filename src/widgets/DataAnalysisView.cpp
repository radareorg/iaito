#include "DataAnalysisView.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QGestureEvent>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPinchGesture>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using namespace DataAnalysis;

DataAnalysisView::DataAnalysisView(bool overview, QWidget *parent)
    : QWidget(parent)
    , overview(overview)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
#ifndef Q_OS_MACOS
    // On macOS, handle native trackpad events directly to retain their position.
    grabGesture(Qt::PinchGesture);
#endif
    setMinimumSize(220, overview ? 70 : 180);
    setAccessibleName(overview ? tr("Data overview") : tr("Data analysis graph"));
    setAccessibleDescription(
        overview
            ? tr("Drag the window or use the wheel to pan; drag its edges to resize; pinch to zoom."
                 " Double-click to fit.")
            : tr("Click to seek; drag to select bytes; wheel to pan; pinch to zoom."
                 " Right-click for comments, flags and range actions."));
    tooltipTimer.setSingleShot(true);
    tooltipTimer.setInterval(650);
    connect(&tooltipTimer, &QTimer::timeout, this, &DataAnalysisView::showTooltip);
}

QString DataAnalysisView::metricName(int metric)
{
    static const char *names[]
        = {QT_TR_NOOP("Entropy"),
           QT_TR_NOOP("Printable ASCII"),
           QT_TR_NOOP("0x00 bytes"),
           QT_TR_NOOP("0xFF bytes"),
           QT_TR_NOOP("Unique bytes"),
           QT_TR_NOOP("Mean byte"),
           QT_TR_NOOP("ASCII letters"),
           QT_TR_NOOP("ASCII digits"),
           QT_TR_NOOP("ASCII whitespace"),
           QT_TR_NOOP("ASCII control"),
           QT_TR_NOOP("High bytes")};
    return metric >= 0 && metric < Count ? tr(names[metric]) : QString();
}

QString DataAnalysisView::metricValue(int metric, double value)
{
    if (metric == Entropy) {
        return tr("%1 bits/byte").arg(value * 8, 0, 'f', 3);
    }
    if (metric == Unique) {
        return tr("%1 / 256").arg(qRound(value * 256));
    }
    if (metric == Mean) {
        return QString::number(value * 255, 'f', 2);
    }
    return tr("%1%").arg(value * 100, 0, 'f', 2);
}

QString DataAnalysisView::metricDescription(int metric)
{
    static const char *descriptions[]
        = {QT_TR_NOOP(
               "Shannon entropy: 0–8 bits per byte. Higher values indicate a more varied byte "
               "distribution. Overlaid values are divided by 8."),
           QT_TR_NOOP("Percentage of printable ASCII bytes (0x20–0x7e)."),
           QT_TR_NOOP("Percentage of zero bytes (0x00), often padding or uninitialized data."),
           QT_TR_NOOP("Percentage of 0xff bytes, often padding or erased storage."),
           QT_TR_NOOP("Number of distinct byte values observed, out of 256 possible values."),
           QT_TR_NOOP("Average byte value (0–255). Overlaid values are divided by 255."),
           QT_TR_NOOP("Percentage of ASCII letters (A–Z and a–z)."),
           QT_TR_NOOP("Percentage of ASCII decimal digits (0–9)."),
           QT_TR_NOOP(
               "Percentage of ASCII whitespace (space, tab, newline, vertical tab, form feed and "
               "carriage return)."),
           QT_TR_NOOP("Percentage of ASCII control bytes (0x00–0x1f and 0x7f)."),
           QT_TR_NOOP("Percentage of bytes with the high bit set (0x80–0xff).")};
    return metric >= 0 && metric < Count ? tr(descriptions[metric]) : QString();
}

QString DataAnalysisView::markerName(MarkerType type)
{
    static const char *names[]
        = {QT_TR_NOOP("Section"),
           QT_TR_NOOP("Segment"),
           QT_TR_NOOP("Map"),
           QT_TR_NOOP("Flag"),
           QT_TR_NOOP("Comment")};
    return tr(names[type]);
}

Range DataAnalysisView::axisRange() const
{
    return overview ? domain : visibleRange;
}

QRectF DataAnalysisView::plotRect() const
{
    return QRectF(
        46, overview ? 5 : 30, qMax(1, width() - 60), qMax(1, height() - (overview ? 26 : 59)));
}

qreal DataAnalysisView::xForAddress(RVA address) const
{
    const Range range = axisRange();
    if (!range.valid()) {
        return plotRect().left();
    }
    const long double distance = address >= range.start ? (long double) (address - range.start)
                                                        : -(long double) (range.start - address);
    return plotRect().left() + distance / range.size() * plotRect().width();
}

RVA DataAnalysisView::addressAt(qreal x) const
{
    const Range range = axisRange();
    if (!range.valid()) {
        return RVA_INVALID;
    }
    const long double fraction = (x - plotRect().left()) / plotRect().width();
    if (fraction <= 0) {
        return range.start;
    }
    if (fraction >= 1) {
        return range.end - 1;
    }
    return range.start + qMin<RVA>(RVA(fraction * range.size()), range.size() - 1);
}

void DataAnalysisView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().color(QPalette::Base));
    const QRectF plot = plotRect();
    const Range range = axisRange();
    const QColor text = palette().color(QPalette::Text);
    QColor grid = text;
    grid.setAlpha(35);
    p.setPen(grid);
    p.drawRect(plot);
    if (!range.valid()) {
        p.setPen(text);
        p.drawText(plot, Qt::AlignCenter, tr("Open a file to analyze its data"));
        return;
    }

    const bool split = !overview && separateTracks;
    const int tracks = split ? qMax(1, int(metrics.size())) : 1;
    for (int track = 0; track < tracks; ++track) {
        const qreal top = plot.top() + track * plot.height() / tracks;
        const qreal bottom = plot.top() + (track + 1) * plot.height() / tracks;
        const qreal graphTop = top + (split ? 19 : 0);
        const qreal graphBottom = bottom - (split ? 5 : 0);
        const qreal graphHeight = qMax(1.0, graphBottom - graphTop);
        if (!overview) {
            for (int tick = 0; tick <= 4; ++tick) {
                const qreal y = graphBottom - graphHeight * tick / 4;
                p.setPen(grid);
                p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
                if (tracks == 1 || tick == 0 || tick == 4) {
                    int maximum = 100;
                    if ((separateTracks || metrics.size() == 1) && !metrics.isEmpty()) {
                        const int metric = metrics[track];
                        maximum = metric == Entropy  ? 8
                                  : metric == Mean   ? 255
                                  : metric == Unique ? 256
                                                     : 100;
                    }
                    p.setPen(text);
                    QString label = QString::number(maximum * tick / 4.0, 'g', 3);
                    if (maximum == 100 && tick == 4) {
                        label += '%';
                    }
                    p.drawText(
                        QRectF(0, y - 8, plot.left() - 7, 16),
                        Qt::AlignRight | Qt::AlignVCenter,
                        label);
                }
            }
            if (split && !metrics.isEmpty()) {
                p.setPen(colors[metrics[track]]);
                p.drawText(
                    QRectF(plot.left() + 5, top, plot.width() - 10, 18),
                    Qt::AlignLeft | Qt::AlignVCenter,
                    metricName(metrics[track]));
            }
        }
        p.save();
        p.setClipRect(QRectF(plot.left(), graphTop, plot.width(), graphHeight));
        const QVector<int> drawn = overview                      ? QVector<int>{Entropy}
                                   : split && !metrics.isEmpty() ? QVector<int>{metrics[track]}
                                                                 : metrics;
        for (int series = 0; series < drawn.size(); ++series) {
            const int metric = drawn[series];
            const QColor color = colors[metric];
            QPainterPath path;
            qreal firstX = 0, lastX = 0;
            auto flush = [&]() {
                if (path.isEmpty()) {
                    return;
                }
                if (overview || style == Areas) {
                    QPainterPath area = path;
                    area.lineTo(lastX, graphBottom);
                    area.lineTo(firstX, graphBottom);
                    area.closeSubpath();
                    QLinearGradient gradient(0, graphTop, 0, graphBottom);
                    QColor fill = color;
                    fill.setAlpha(overview ? 110 : 85);
                    gradient.setColorAt(0, fill);
                    fill.setAlpha(12);
                    gradient.setColorAt(1, fill);
                    p.fillPath(area, gradient);
                }
                p.setPen(QPen(color, overview ? 1 : 1.5));
                p.drawPath(path);
                path = QPainterPath();
            };
            for (const auto &sample : samples) {
                if (!sample.bytes) {
                    flush();
                    continue;
                }
                const qreal left = xForAddress(sample.range.start);
                const qreal right = xForAddress(sample.range.end);
                const qreal y = graphBottom - qBound(0.0, sample.values[metric], 1.0) * graphHeight;
                if (!overview && style == Bars) {
                    const qreal barWidth = (right - left) / qMax(1, int(drawn.size()));
                    QColor fill = color;
                    fill.setAlpha(190);
                    p.fillRect(
                        QRectF(left + series * barWidth, y, qMax(0.6, barWidth), graphBottom - y),
                        fill);
                } else {
                    if (path.isEmpty()) {
                        firstX = left;
                        path.moveTo(left, y);
                    }
                    path.lineTo((left + right) / 2, y);
                    lastX = right;
                    // Retain single-bucket signals and reach both ends of the range.
                    path.lineTo(right, y);
                }
            }
            flush();
        }
        p.restore();
    }

    // Markers are coalesced by pixel and category to keep dense flag sets cheap to paint.
    std::array<int, MarkerCount> lastPixel;
    lastPixel.fill(-1000);
    qreal labelEnd = plot.left();
    for (const auto &marker : markers) {
        if (!markerEnabled[marker.type]) {
            continue;
        }
        const QColor color = colors[(marker.type + 4) % Count];
        for (RVA address : {marker.range.start, marker.range.end}) {
            if (address == marker.range.end && marker.type >= Flag) {
                continue;
            }
            if (address < range.start || address > range.end) {
                continue;
            }
            const qreal x = xForAddress(address);
            const int pixel = qRound(x);
            if (std::abs(pixel - lastPixel[marker.type]) < 2) {
                continue;
            }
            lastPixel[marker.type] = pixel;
            QColor line = color;
            line.setAlpha(95);
            p.setPen(QPen(line, 1, marker.type >= Flag ? Qt::DotLine : Qt::DashLine));
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            p.setPen(color);
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.top() + 5));
        }
        if (!overview && marker.type <= Map && intersect(marker.range, range).valid()) {
            const qreal left = qMax(plot.left(), xForAddress(marker.range.start));
            const qreal right = qMin(plot.right(), xForAddress(marker.range.end));
            if (left >= labelEnd && right - left > 38) {
                p.setPen(color);
                const QString label
                    = fontMetrics().elidedText(marker.name, Qt::ElideRight, int(right - left - 6));
                p.drawText(QRectF(left + 3, 4, right - left - 6, 20), Qt::AlignLeft, label);
                labelEnd = left + fontMetrics().horizontalAdvance(label) + 10;
            }
        }
    }

    auto highlight = [&](Range selected, bool window) {
        selected = intersect(selected, range);
        if (!selected.valid()) {
            return;
        }
        const qreal left = xForAddress(selected.start), right = xForAddress(selected.end);
        QColor fill = palette().color(QPalette::Highlight);
        fill.setAlpha(window ? 35 : 55);
        p.fillRect(QRectF(left, plot.top(), qMax(1.0, right - left), plot.height()), fill);
        p.setPen(QPen(palette().color(QPalette::Highlight), window ? 2 : 1));
        p.drawRect(QRectF(left, plot.top(), qMax(1.0, right - left), plot.height()));
        if (window) {
            p.fillRect(QRectF(left - 2, plot.center().y() - 7, 4, 14), text);
            p.fillRect(QRectF(right - 2, plot.center().y() - 7, 4, 14), text);
        }
    };
    if (overview) {
        const QColor shade(0, 0, 0, 65);
        const qreal left = xForAddress(visibleRange.start), right = xForAddress(visibleRange.end);
        p.fillRect(
            QRectF(plot.left(), plot.top(), qMax(0.0, left - plot.left()), plot.height()), shade);
        p.fillRect(QRectF(right, plot.top(), qMax(0.0, plot.right() - right), plot.height()), shade);
        highlight(visibleRange, true);
    }
    highlight(drag == Select && dragSelection.valid() ? dragSelection : selection, false);
    if (range.contains(seekAddress)) {
        const qreal x = xForAddress(seekAddress);
        p.setPen(QPen(palette().color(QPalette::Highlight), 2));
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        p.setBrush(palette().color(QPalette::Highlight));
        p.drawPolygon(QPolygonF(
            QVector<QPointF>{
                QPointF(x - 4, plot.top()), QPointF(x + 4, plot.top()), QPointF(x, plot.top() + 6)}));
    }
    if (!overview && hoverX >= plot.left() && hoverX <= plot.right()) {
        p.setPen(QPen(text, 1, Qt::DotLine));
        p.drawLine(QPointF(hoverX, plot.top()), QPointF(hoverX, plot.bottom()));
    }
    p.setPen(text);
    const int addressWidth = fontMetrics().horizontalAdvance(RAddressString(range.end - 1));
    const int labels = qBound(1, int(plot.width() / (addressWidth + 24)), 4);
    for (int i = 0; i <= labels; ++i) {
        const qreal x = plot.left() + plot.width() * i / labels;
        const QString label = fontMetrics().elidedText(
            RAddressString(addressAt(x)), Qt::ElideMiddle, int(plot.width() / (labels + 1)));
        const qreal width = fontMetrics().horizontalAdvance(label) + 2;
        const qreal left
            = qBound(plot.left(), x - width / 2, qMax(plot.left(), plot.right() - width));
        p.drawText(QRectF(left, plot.bottom() + 4, width, 20), Qt::AlignLeft, label);
    }
    const bool hasData = std::any_of(samples.cbegin(), samples.cend(), [](const Sample &sample) {
        return sample.bytes > 0;
    });
    if (!overview && !hasData) {
        p.drawText(plot, Qt::AlignCenter, loading ? tr("Analyzing…") : tr("No readable data"));
    }
}

bool DataAnalysisView::event(QEvent *event)
{
    if (event->type() == QEvent::Gesture) {
        auto *gestures = static_cast<QGestureEvent *>(event);
        if (auto *pinch = static_cast<QPinchGesture *>(gestures->gesture(Qt::PinchGesture))) {
            if (pinch->state() != Qt::GestureFinished && pinch->state() != Qt::GestureCanceled
                && (pinch->changeFlags() & QPinchGesture::ScaleFactorChanged)) {
                const QPoint position = mapFromGlobal(pinch->centerPoint().toPoint());
                pinchZoom(pinch->scaleFactor(), position.x());
            }
            gestures->accept(pinch);
            return true;
        }
    } else if (event->type() == QEvent::NativeGesture) {
        auto *gesture = static_cast<QNativeGestureEvent *>(event);
        if (gesture->gestureType() == Qt::ZoomNativeGesture) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            const qreal x = gesture->position().x();
#else
            const qreal x = gesture->localPos().x();
#endif
            pinchZoom(1.0 + gesture->value(), x);
            event->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

void DataAnalysisView::pinchZoom(qreal scaleFactor, qreal x)
{
    cancelTooltip();
    drag = None;
    dragSelection = {};
    if (std::isfinite(scaleFactor) && scaleFactor > 0 && scaleFactor != 1) {
        // Magnifying the graph narrows the address range around the gesture.
        zoom(1.0 / scaleFactor, addressAt(x));
    }
    update();
}

void DataAnalysisView::zoom(double factor, RVA anchor)
{
    if (!domain.valid() || !visibleRange.valid() || !std::isfinite(factor) || factor <= 0) {
        return;
    }
    long double scaled = (long double) visibleRange.size() * factor;
    if (factor > 1) {
        // Small pinch steps must still let the user zoom out from a one-byte range.
        scaled = std::ceil(scaled);
    }
    const RVA size = scaled >= domain.size() ? domain.size() : qMax<RVA>(1, RVA(scaled));
    anchor = qBound(visibleRange.start, anchor, visibleRange.end - 1);
    const long double ratio = (long double) (anchor - visibleRange.start) / visibleRange.size();
    const RVA before = qMin<RVA>(size - 1, RVA(ratio * size));
    const RVA start = anchor - qMin(anchor - domain.start, before);
    const RVA clamped = qMin(start, domain.end - size);
    emit rangeRequested(clamped, clamped + size);
}

void DataAnalysisView::pan(int direction)
{
    if (!visibleRange.valid() || !domain.valid()) {
        return;
    }
    const RVA step = qMax<RVA>(1, visibleRange.size() / 4);
    const RVA start = direction < 0
                          ? visibleRange.start - qMin(step, visibleRange.start - domain.start)
                          : visibleRange.start + qMin(step, domain.end - visibleRange.end);
    emit rangeRequested(start, start + visibleRange.size());
}

void DataAnalysisView::mousePressEvent(QMouseEvent *event)
{
    cancelTooltip();
    if (event->button() != Qt::LeftButton || !axisRange().valid()
        || !plotRect().contains(event->pos())) {
        return;
    }
    setFocus();
    pressPosition = event->pos();
    pressAddress = addressAt(event->pos().x());
    pressRange = visibleRange;
    dragSelection = {};
    if (overview) {
        if (std::abs(event->pos().x() - xForAddress(visibleRange.start)) < 7) {
            drag = LeftEdge;
        } else if (std::abs(event->pos().x() - xForAddress(visibleRange.end)) < 7) {
            drag = RightEdge;
        } else {
            drag = Move;
            if (!visibleRange.contains(pressAddress)) {
                pressRange = centered(domain, pressAddress, visibleRange.size());
                emit rangeRequested(pressRange.start, pressRange.end);
            }
        }
    } else {
        drag = Select;
    }
    event->accept();
}

void DataAnalysisView::updateDrag(QPoint position)
{
    const RVA address = addressAt(position.x());
    if (drag == Select) {
        dragSelection = {qMin(address, pressAddress), qMax(address, pressAddress) + 1};
        update();
    } else if (drag == LeftEdge) {
        emit rangeRequested(qMin(address, pressRange.end - 1), pressRange.end);
    } else if (drag == RightEdge) {
        emit rangeRequested(pressRange.start, qMax(address + 1, pressRange.start + 1));
    } else if (drag == Move) {
        const RVA start = address >= pressAddress
                              ? pressRange.start
                                    + qMin(address - pressAddress, domain.end - pressRange.end)
                              : pressRange.start
                                    - qMin(pressAddress - address, pressRange.start - domain.start);
        emit rangeRequested(start, start + pressRange.size());
    }
}

void DataAnalysisView::mouseMoveEvent(QMouseEvent *event)
{
    const bool moved = hoverPosition != event->pos();
    hoverPosition = event->pos();
    hoverX = event->pos().x();
    if (drag != None) {
        updateDrag(event->pos());
    } else if (plotRect().contains(event->pos())) {
        if (moved) {
            cancelTooltip();
            tooltipTimer.start();
        }
        const bool edge = overview
                          && (std::abs(hoverX - xForAddress(visibleRange.start)) < 7
                              || std::abs(hoverX - xForAddress(visibleRange.end)) < 7);
        setCursor(edge ? Qt::SizeHorCursor : overview ? Qt::OpenHandCursor : Qt::CrossCursor);
    } else {
        cancelTooltip();
    }
    update();
}

void DataAnalysisView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || drag == None) {
        return;
    }
    updateDrag(event->pos());
    if (drag == Select) {
        if ((event->pos() - pressPosition).manhattanLength() < QApplication::startDragDistance()) {
            emit selectionRequested(0, 0);
            emit seekRequested(nearestMarker(event->pos().x()));
        } else {
            emit selectionRequested(dragSelection.start, dragSelection.end);
        }
    }
    drag = None;
    dragSelection = {};
    update();
}

void DataAnalysisView::mouseDoubleClickEvent(QMouseEvent *event)
{
    cancelTooltip();
    if (event->button() == Qt::LeftButton && axisRange().valid()) {
        drag = None;
        if (overview) {
            emit rangeRequested(domain.start, domain.end);
        } else {
            zoom(0.5, addressAt(event->pos().x()));
        }
    }
}

void DataAnalysisView::wheelEvent(QWheelEvent *event)
{
    cancelTooltip();
    const QPoint delta = event->angleDelta();
    const int movement = delta.y() ? delta.y() : delta.x();
    if (!movement) {
        event->ignore();
        return;
    }
    pan(movement > 0 ? -1 : 1);
    event->accept();
}

void DataAnalysisView::keyPressEvent(QKeyEvent *event)
{
    cancelTooltip();
    const RVA center = visibleRange.start + visibleRange.size() / 2;
    switch (event->key()) {
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        zoom(0.5, center);
        break;
    case Qt::Key_Minus:
        zoom(2, center);
        break;
    case Qt::Key_Left:
        pan(-1);
        break;
    case Qt::Key_Right:
        pan(1);
        break;
    case Qt::Key_Home:
        emit rangeRequested(domain.start, domain.end);
        break;
    case Qt::Key_Escape:
        drag = None;
        emit selectionRequested(0, 0);
        update();
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}

void DataAnalysisView::leaveEvent(QEvent *)
{
    cancelTooltip();
    hoverX = -1;
    unsetCursor();
    QToolTip::hideText();
    update();
}

void DataAnalysisView::cancelTooltip()
{
    tooltipTimer.stop();
    QToolTip::hideText();
}

void DataAnalysisView::hideEvent(QHideEvent *event)
{
    cancelTooltip();
    QWidget::hideEvent(event);
}

RVA DataAnalysisView::nearestMarker(qreal x) const
{
    qreal distance = 5;
    RVA result = addressAt(x);
    for (const auto &marker : markers) {
        if (markerEnabled[marker.type] && axisRange().contains(marker.range.start)) {
            const qreal candidate = std::abs(x - xForAddress(marker.range.start));
            if (candidate < distance) {
                distance = candidate;
                result = marker.range.start;
            }
        }
    }
    return result;
}

void DataAnalysisView::showTooltip()
{
    if (!isVisible() || drag != None || !plotRect().contains(hoverPosition)) {
        return;
    }
    const RVA address = addressAt(hoverPosition.x());
    if (address == RVA_INVALID) {
        return;
    }
    QString tip = RAddressString(address);
    for (const auto &sample : samples) {
        if (!sample.range.contains(address)) {
            continue;
        }
        tip += tr("\nBucket: %1 – %2 (%3 bytes)\nRead %4 of %5 mapped bytes")
                   .arg(RAddressString(sample.range.start), RAddressString(sample.range.end - 1))
                   .arg(sample.range.size())
                   .arg(sample.bytes)
                   .arg(sample.covered);
        if (sample.bytes) {
            for (int metric : metrics) {
                tip += QString("\n%1: %2")
                           .arg(metricName(metric), metricValue(metric, sample.values[metric]));
            }
        } else {
            tip += tr("\nNo readable data");
        }
        break;
    }
    int found = 0;
    for (const auto &marker : markers) {
        if (markerEnabled[marker.type]
            && (marker.range.contains(address)
                || std::abs(xForAddress(marker.range.start) - hoverPosition.x()) < 5)) {
            tip += QString("\n%1: %2 @ %3")
                       .arg(
                           markerName(marker.type),
                           marker.name.left(160),
                           RAddressString(marker.range.start));
            if (++found == 12) {
                break;
            }
        }
    }
    QToolTip::showText(mapToGlobal(hoverPosition), tip.toHtmlEscaped().replace('\n', "<br>"), this);
}

void DataAnalysisView::contextMenuEvent(QContextMenuEvent *event)
{
    cancelTooltip();
    if (axisRange().valid()) {
        emit contextRequested(nearestMarker(event->pos().x()), event->globalPos());
        event->accept();
    }
}
