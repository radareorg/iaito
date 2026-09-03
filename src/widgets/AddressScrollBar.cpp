#include "AddressScrollBar.h"

#include "common/TempConfig.h"
#include "core/Iaito.h"

#include <QApplication>
#include <QEasingCurve>
#include <QJsonObject>
#include <QVariantAnimation>
#include <QWheelEvent>

namespace {
// Half of the travel of the spring handle, zero is the middle of the trough
constexpr int SpringRange = 1000;
constexpr int SpringPageStep = 400;
// Travel kept on both sides of the rest position so the view can always scroll
constexpr int SpringMargin = 100;
// Largest span the bounded mode maps one byte per slider unit
constexpr RVA MaxSliderSpan = RVA(1) << 30;
constexpr int SpringTickMs = 16;
constexpr int BounceMs = 700;
// Screens per second when the handle is pulled all the way, once accelerated
constexpr qreal MaxPagesPerSecond = 6.0;
// Seconds the handle must be held before reaching full speed
constexpr qreal AccelerationSeconds = 3.0;
} // namespace

AddressScrollBar::AddressScrollBar(QWidget *parent)
    : QScrollBar(Qt::Vertical, parent)
    , mode(Config()->getMemoryScrollBarMode())
    , bounce(new QVariantAnimation(this))
{
    springTimer.setInterval(SpringTickMs);
    springTimer.setTimerType(Qt::PreciseTimer);
    bounce->setDuration(BounceMs);
    bounce->setEasingCurve(QEasingCurve::OutElastic);
    connect(bounce, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        syncing = true;
        setValue(v.toInt());
        syncing = false;
    });
    connect(&springTimer, &QTimer::timeout, this, &AddressScrollBar::springTick);
    connect(this, &QAbstractSlider::sliderPressed, this, &AddressScrollBar::startSpring);
    connect(this, &QAbstractSlider::sliderReleased, this, &AddressScrollBar::releaseSpring);
    connect(this, &QAbstractSlider::actionTriggered, this, &AddressScrollBar::onActionTriggered);
    connect(this, &QAbstractSlider::valueChanged, this, &AddressScrollBar::onValueChanged);
    connect(
        Config(),
        &Configuration::memoryScrollBarModeChanged,
        this,
        [this](Configuration::MemoryScrollBarMode newMode) {
            mode = newMode;
            applyMode();
        });
    connect(Core(), &IaitoCore::refreshAll, this, &AddressScrollBar::updateRange);
    applyMode();
}

void AddressScrollBar::setViewport(int lines, RVA bytes)
{
    pageLines = qMax(1, lines);
    pageBytes = qMax<RVA>(1, bytes);
    if (!isSpring() && !isHiddenMode()) {
        applyRange();
    }
}

void AddressScrollBar::setAddress(RVA newAddress)
{
    address = newAddress;
    if (isSpring() || isHiddenMode()) {
        return;
    }
    int v = 0;
    if (address >= rangeEnd) {
        v = maximum();
    } else if (address > rangeStart) {
        v = int((address - rangeStart) / unit);
    }
    syncing = true;
    setValue(v);
    syncing = false;
}

void AddressScrollBar::setSeekAddress(RVA newAddress)
{
    seekAddress = newAddress;
    updateRest();
}

void AddressScrollBar::updateRange()
{
    if (isHiddenMode()) {
        return;
    }
    RVA from = 0;
    RVA to = 0;
    // Same boundaries the navigation bar shows, the whole file when unmapped
    for (const char *where : {"bin.sections", "io.maps"}) {
        TempConfig tempConfig;
        tempConfig.set(QStringLiteral("search.in"), QString::fromLatin1(where));
        const QJsonObject stats = Core()->cmdj(QStringLiteral("p-j 1")).object();
        from = stats[QStringLiteral("from")].toVariant().toULongLong();
        to = stats[QStringLiteral("to")].toVariant().toULongLong();
        if (to > from) {
            break;
        }
    }
    if (to <= from) {
        from = 0;
        to = RVA_MAX;
    }
    rangeStart = from;
    rangeEnd = to;
    if (isSpring()) {
        updateRest();
    } else {
        applyRange();
    }
}

// Rests the spring handle where the seek lies within the range
void AddressScrollBar::updateRest()
{
    if (!isSpring()) {
        return;
    }
    const RVA span = rangeEnd - rangeStart;
    qreal position = 0.5;
    if (span > 0 && seekAddress > rangeStart) {
        position = seekAddress >= rangeEnd ? 1.0 : qreal(seekAddress - rangeStart) / span;
    }
    const int travel = SpringRange - SpringMargin;
    restValue = qRound(-travel + 2 * travel * position);
    if (!isSliderDown() && bounce->state() != QAbstractAnimation::Running) {
        syncing = true;
        setValue(restValue);
        syncing = false;
    }
}

void AddressScrollBar::applyMode()
{
    springTimer.stop();
    bounce->stop();
    springRemainder = 0;
    setVisible(!isHiddenMode());
    syncing = true;
    if (isSpring()) {
        setRange(-SpringRange, SpringRange);
        setPageStep(SpringPageStep);
        setSingleStep(1);
        setToolTip(tr("Drag away from the seek to scroll, further and longer is faster"));
    } else {
        setToolTip(QString());
    }
    syncing = false;
    updateRange();
}

void AddressScrollBar::applyRange()
{
    const RVA span = rangeEnd - rangeStart;
    unit = span > MaxSliderSpan ? (span + MaxSliderSpan - 1) / MaxSliderSpan : 1;
    syncing = true;
    setRange(0, int(span / unit));
    setPageStep(qMax<int>(1, int(pageBytes / unit)));
    setSingleStep(qMax<int>(1, int(pageBytes / pageLines / unit)));
    syncing = false;
    setAddress(address);
}

void AddressScrollBar::startSpring()
{
    if (!isSpring()) {
        return;
    }
    bounce->stop();
    springRemainder = 0;
    holdTimer.start();
    tickTimer.start();
    springTimer.start();
}

void AddressScrollBar::releaseSpring()
{
    if (!isSpring()) {
        return;
    }
    springTimer.stop();
    bounce->stop();
    bounce->setStartValue(value());
    bounce->setEndValue(restValue);
    bounce->start();
}

void AddressScrollBar::springTick()
{
    const qreal dt = tickTimer.restart() / 1000.0;
    const int offset = value() - restValue;
    // Normalized by the travel left on that side, so the edge is always full speed
    const int travel = qMax(1, offset > 0 ? SpringRange - restValue : SpringRange + restValue);
    const qreal pull = qBound(-1.0, offset / qreal(travel), 1.0);
    if (qFuzzyIsNull(pull)) {
        springRemainder = 0;
        return;
    }
    const qreal hold = holdTimer.elapsed() / 1000.0;
    const qreal ramp = qMin(1.0, 0.1 + hold / AccelerationSeconds);
    // Quadratic so that small pulls stay slow enough to read the listing
    const qreal pagesPerSecond = pull * qAbs(pull) * MaxPagesPerSecond * ramp;
    springRemainder += pagesPerSecond * pageLines * dt;
    const int lines = int(springRemainder);
    if (lines != 0) {
        springRemainder -= lines;
        emit scrollRequested(lines);
    }
}

void AddressScrollBar::onActionTriggered(int action)
{
    if (!isSpring()) {
        return;
    }
    int lines = 0;
    switch (action) {
    case SliderSingleStepAdd:
        lines = 1;
        break;
    case SliderSingleStepSub:
        lines = -1;
        break;
    case SliderPageStepAdd:
        lines = pageLines;
        break;
    case SliderPageStepSub:
        lines = -pageLines;
        break;
    case SliderMove:
        return;
    default:
        break;
    }
    // Steps scroll the view but never move the handle away from its rest
    setSliderPosition(restValue);
    if (lines != 0) {
        emit scrollRequested(lines);
    }
}

void AddressScrollBar::onValueChanged(int value)
{
    if (syncing || isSpring()) {
        return;
    }
    const RVA target = rangeStart + RVA(value) * unit;
    if (target != address) {
        address = target;
        emit addressRequested(target);
    }
}

void AddressScrollBar::wheelEvent(QWheelEvent *event)
{
    if (!isSpring()) {
        QScrollBar::wheelEvent(event);
        return;
    }
    const int lines = -(event->angleDelta().y() / 120) * QApplication::wheelScrollLines();
    if (lines != 0) {
        emit scrollRequested(qBound(-pageLines, lines, pageLines));
    }
    event->accept();
}
