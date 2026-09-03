#ifndef ADDRESSSCROLLBAR_H
#define ADDRESSSCROLLBAR_H

#include "common/Configuration.h"
#include "core/IaitoCommon.h"

#include <QElapsedTimer>
#include <QScrollBar>
#include <QTimer>

class QVariantAnimation;

/**
 * @brief Vertical scrollbar for views that browse the whole 64 bit address space.
 *
 * Spring mode: the handle rests where the current seek lies within the
 * navigation bar range. Dragging it scrolls with a speed that grows with the
 * displacement and with the time it is held, and the handle bounces back to
 * the rest position on release.
 * Bounded mode: a regular scrollbar mapped over the navigation bar range.
 * Hidden mode: no scrollbar at all.
 * The mode follows Configuration::getMemoryScrollBarMode().
 */
class AddressScrollBar : public QScrollBar
{
    Q_OBJECT

public:
    explicit AddressScrollBar(QWidget *parent = nullptr);

    /// Lines shown per screen and the bytes they cover, used for the steps
    void setViewport(int lines, RVA bytes);
    /// Sync the handle with the top address of the view without emitting anything
    void setAddress(RVA address);
    /// Current seek of the view, the rest position of the spring handle
    void setSeekAddress(RVA address);
    /// Re-fetch the address range of the bounded mode from the core
    void updateRange();
    bool isHiddenMode() const { return mode == Configuration::MemoryScrollBarMode::Hidden; }

signals:
    /// Spring mode: scroll the view by `lines` (positive scrolls down)
    void scrollRequested(int lines);
    /// Bounded mode: show `address` at the top of the view
    void addressRequested(RVA address);

protected:
    void wheelEvent(QWheelEvent *event) override;

private:
    Configuration::MemoryScrollBarMode mode;
    int pageLines = 1;
    RVA pageBytes = 1;
    RVA address = 0;
    RVA seekAddress = 0;
    int restValue = 0;
    RVA rangeStart = 0;
    RVA rangeEnd = 0;
    /// Bytes per slider unit in bounded mode, so any span fits in an int
    RVA unit = 1;
    bool syncing = false;
    QTimer springTimer;
    QElapsedTimer holdTimer;
    QElapsedTimer tickTimer;
    qreal springRemainder = 0;
    QVariantAnimation *bounce;

    bool isSpring() const { return mode == Configuration::MemoryScrollBarMode::Spring; }
    void applyMode();
    void applyRange();
    void updateRest();
    void springTick();
    void startSpring();
    void releaseSpring();
    void onActionTriggered(int action);
    void onValueChanged(int value);
};

#endif // ADDRESSSCROLLBAR_H
