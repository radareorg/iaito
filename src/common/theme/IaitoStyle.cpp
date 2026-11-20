#include "IaitoStyle.h"
#include "TouchScrollController.h"
#include "TouchTabBarController.h"
#include <QDialog>

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QHeaderView>
#include <QIcon>
#include <QImage>
#include <QLineEdit>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QSplitterHandle>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTabBar>
#include <QTimer>
#include <QToolTip>
#include <QWidget>
#include <QtMath>

static void drawBevel(QPainter *p, const QRect &r, bool raised, const Theme &t);

namespace {

bool mobilePlatform()
{
    return QGuiApplication::platformName() == QLatin1String("android")
           || QGuiApplication::platformName() == QLatin1String("ios")
           || qEnvironmentVariableIntValue("IAITO_MOBILE_UI") == 1;
}

int touchControlHeight(const Theme &theme, const QStyleOption *option)
{
    const int minimum = qMax(theme.metrics.controlHeight, mobilePlatform() ? 44 : 0);
    return option ? qMax(minimum, option->fontMetrics.height() + 8) : minimum;
}

int touchItemHeight(const Theme &theme)
{
    return qMax(theme.metrics.controlHeight, mobilePlatform() ? 44 : 48);
}

bool isDockTabBar(const QWidget *widget)
{
    return widget && widget->property("iaitoDockTabBarConfigured").toBool();
}

// Paint above the window contents so toolbars and dock panels cannot cover the
// outline. Native title bars remain under the window system's control.
class WindowOutline : public QWidget
{
public:
    explicit WindowOutline(QWidget *window)
        : QWidget(window)
    {
        setObjectName(QStringLiteral("iaitoWindowOutline"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        window->installEventFilter(this);
        setGeometry(window->rect());
        show();
        raise();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == parentWidget()) {
            switch (event->type()) {
            case QEvent::Resize:
                setGeometry(parentWidget()->rect());
                break;
            case QEvent::Show:
            case QEvent::ChildAdded:
                raise();
                break;
            case QEvent::WindowStateChange:
                update();
                break;
            default:
                break;
            }
        }
        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent *) override
    {
        const auto *style = IaitoStyle::instance();
        const bool dialog = qobject_cast<QDialog *>(parentWidget());
        if (!style || (!dialog && !style->theme().windowBorder.isValid())
            || !parentWidget()->isWindow() || parentWidget()->isMaximized()
            || parentWidget()->isFullScreen()) {
            return;
        }
        QPainter painter(this);
        if (style->theme().skin == Skin::Windows) {
            drawBevel(&painter, rect(), true, style->theme());
            return;
        }
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor border = style->theme().windowBorder.isValid()
                                  ? style->theme().windowBorder
                                  : palette().color(QPalette::Highlight);
        painter.setPen(QPen(border, dialog ? 2 : 1));
        painter.setBrush(Qt::NoBrush);
        const qreal radius = style->theme().metrics.borderRadius;
        const qreal inset = dialog ? 1 : 0.5;
        painter
            .drawRoundedRect(QRectF(rect()).adjusted(inset, inset, -inset, -inset), radius, radius);
    }
};

} // namespace

static void drawTriangle(QPainter *p, QPointF c, const QColor &color, bool up, int unit = 1)
{
    const qreal s = 3.5 * unit;
    QPainterPath path;
    if (up) {
        path.moveTo(c.x() - s, c.y() + s / 2);
        path.lineTo(c.x() + s, c.y() + s / 2);
        path.lineTo(c.x(), c.y() - s / 2 - unit);
    } else {
        path.moveTo(c.x() - s, c.y() - s / 2);
        path.lineTo(c.x() + s, c.y() - s / 2);
        path.lineTo(c.x(), c.y() + s / 2 + unit);
    }
    path.closeSubpath();
    p->setPen(Qt::NoPen);
    p->setBrush(color);
    p->drawPath(path);
}

static void drawBevel(QPainter *p, const QRect &r, bool raised, const Theme &t)
{
    QColor tlOuter, tlInner, brInner, brOuter;
    if (raised) {
        tlOuter = t.bevelHighlight;
        tlInner = t.bevelLight;
        brInner = t.bevelDark;
        brOuter = t.bevelShadow;
    } else {
        tlOuter = t.bevelDark;
        tlInner = t.bevelShadow;
        brInner = t.bevelLight;
        brOuter = t.bevelHighlight;
    }
    const int unit = qMax(1, t.metrics.borderWidth);
    auto layer = [&](const QRect &rect, const QColor &topLeft, const QColor &bottomRight) {
        p->fillRect(rect.left(), rect.top(), rect.width() - unit, unit, topLeft);
        p->fillRect(rect.left(), rect.top(), unit, rect.height() - unit, topLeft);
        p->fillRect(rect.left(), rect.bottom() - unit + 1, rect.width(), unit, bottomRight);
        p->fillRect(rect.right() - unit + 1, rect.top(), unit, rect.height(), bottomRight);
    };
    layer(r, tlOuter, brOuter);
    layer(r.adjusted(unit, unit, -unit, -unit), tlInner, brInner);
}

static QColor bevelFace(const QStyleOption *option, const Theme &t)
{
    if (t.skin != Skin::Windows && (option->state & QStyle::State_Enabled)
        && (option->state & QStyle::State_MouseOver)) {
        return t.surface.hovered;
    }
    return t.surface.normal;
}

IaitoStyle *IaitoStyle::s_instance = nullptr;

IaitoStyle::IaitoStyle()
    : QProxyStyle()
{
    s_instance = this;
    m_nativeStyleName = baseStyle()->objectName();
    m_theme = Theme::iaitoDefault();
    m_busyTimer = new QTimer(this);
    m_busyTimer->setInterval(40);
    connect(m_busyTimer, &QTimer::timeout, this, [this]() {
        ++m_busyPhase;
        const QList<QPointer<QWidget>> bars = m_busyBars;
        m_busyBars.clear();
        bool any = false;
        for (const QPointer<QWidget> &w : bars) {
            if (w) {
                w->update();
                any = true;
            }
        }
        if (!any) {
            m_busyTimer->stop();
        }
    });
}

IaitoStyle *IaitoStyle::instance()
{
    return s_instance;
}

void IaitoStyle::setTheme(const Theme &theme, const QPalette &palette)
{
    const bool changeBase = (m_theme.skin == Skin::Windows) != (theme.skin == Skin::Windows)
                            || (m_theme.skin == Skin::Native) != (theme.skin == Skin::Native);
    QList<QPointer<QWidget>> widgets;
    for (QWidget *w : qApp->allWidgets()) {
        widgets.append(w);
    }
    for (const auto &w : widgets) {
        if (w && w->testAttribute(Qt::WA_WState_Polished)) {
            unpolish(w);
        }
    }
    if (changeBase) {
        baseStyle()->unpolish(qApp);
    }
    m_theme = theme;
    if (changeBase) {
        if (theme.skin == Skin::Native) {
            // A null factory result lets QProxyStyle choose the platform default.
            setBaseStyle(QStyleFactory::create(m_nativeStyleName));
        } else {
            QStyle *base = theme.skin == Skin::Windows ? QStyleFactory::create("windows") : nullptr;
            setBaseStyle(base ? base : new QCommonStyle);
        }
    }
    qApp->setPalette(palette);
    if (changeBase) {
        baseStyle()->polish(qApp);
    }
    if (theme.skin == Skin::Native) {
        const QPalette p = qApp->palette();
        m_theme = Theme::fromPalette(p, p.color(QPalette::Window).lightnessF() < 0.5);
    }
    // Clear the per-widget palettes left by previous themes. Let Qt resolve
    // class palettes and inactive/disabled roles through normal inheritance.
    for (const auto &w : widgets) {
        if (w) {
            w->setPalette(QPalette());
        }
    }
    for (const auto &w : widgets) {
        if (!w || !w->testAttribute(Qt::WA_WState_Polished)) {
            continue;
        }
        polish(w);
        QEvent styleChange(QEvent::StyleChange);
        QApplication::sendEvent(w, &styleChange);
        w->update();
        w->updateGeometry();
    }
    // QToolTip keeps a separate palette, including for already visible tips.
    QToolTip::setPalette(qApp->palette());
}

int IaitoStyle::styleHint(
    StyleHint hint,
    const QStyleOption *option,
    const QWidget *widget,
    QStyleHintReturn *returnData) const
{
    if ((m_theme.touch || mobilePlatform())
        && (hint == SH_ComboBox_Popup || hint == SH_ComboBox_UseNativePopup)) {
        // Use a bounded, scrollable list with the same delegate on every platform.
        return 0;
    }
    if (hint == SH_Menu_Scrollable && (m_theme.touch || mobilePlatform())) {
        return 1;
    }
    if (hint == SH_ScrollBar_Transient && (m_theme.touch || mobilePlatform())) {
        return 0;
    }
    if (m_theme.touch && hint == SH_ItemView_ScrollMode) {
        return QAbstractItemView::ScrollPerPixel;
    }
    if (m_theme.skin == Skin::Windows || m_theme.skin == Skin::Native) {
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
    switch (hint) {
    case SH_ComboBox_Popup:
        return 0;
    case SH_Menu_MouseTracking:
    case SH_MenuBar_MouseTracking:
    case SH_ComboBox_ListMouseTracking:
        return 1;
    case SH_EtchDisabledText:
        return 0;
    default:
        return QCommonStyle::styleHint(hint, option, widget, returnData);
    }
}

void IaitoStyle::drawPrimitive(
    PrimitiveElement element,
    const QStyleOption *option,
    QPainter *painter,
    const QWidget *widget) const
{
    if (m_theme.skin == Skin::Windows) {
        const bool sunken = option->state & (State_Sunken | State_On);
        const int unit = qMax(1, m_theme.metrics.borderWidth);
        switch (element) {
        case PE_PanelButtonCommand:
        case PE_PanelButtonBevel:
        case PE_PanelButtonTool:
            painter->fillRect(option->rect, option->palette.button());
            if (option->state & (State_Raised | State_Sunken | State_On)) {
                drawBevel(painter, option->rect, !sunken, m_theme);
            }
            return;
        case PE_FrameDefaultButton: {
            const QRect rect = option->rect;
            const QColor color = option->palette.shadow().color();
            painter->fillRect(rect.left(), rect.top(), rect.width(), unit, color);
            painter->fillRect(rect.left(), rect.top(), unit, rect.height(), color);
            painter->fillRect(rect.right() - unit + 1, rect.top(), unit, rect.height(), color);
            painter->fillRect(rect.left(), rect.bottom() - unit + 1, rect.width(), unit, color);
            return;
        }
        case PE_Frame:
            if (const auto *frame = qstyleoption_cast<const QStyleOptionFrame *>(option)) {
                if (frame->frameShape == QFrame::HLine || frame->frameShape == QFrame::VLine
                    || frame->frameShape == QFrame::NoFrame) {
                    QProxyStyle::drawPrimitive(element, option, painter, widget);
                    return;
                }
            }
            Q_FALLTHROUGH();
        case PE_FrameMenu:
        case PE_FrameTabWidget:
        case PE_FrameGroupBox:
        case PE_FrameWindow:
        case PE_FrameDockWidget:
            drawBevel(painter, option->rect, !sunken, m_theme);
            return;
        case PE_PanelMenu:
            painter->fillRect(option->rect, option->palette.window());
            drawBevel(painter, option->rect, true, m_theme);
            return;
        case PE_PanelStatusBar:
        case PE_FrameStatusBarItem:
            painter->fillRect(option->rect, option->palette.window());
            drawBevel(painter, option->rect, false, m_theme);
            return;
        case PE_PanelScrollAreaCorner:
            painter->fillRect(option->rect, option->palette.window());
            return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
        case PE_FrameFocusRect:
            painter->save();
            painter->setRenderHint(QPainter::NonCosmeticBrushPatterns);
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            painter->restore();
            return;
#endif
        case PE_FrameLineEdit:
        case PE_PanelLineEdit:
            if (element == PE_PanelLineEdit) {
                painter->fillRect(option->rect, option->palette.base());
            }
            if (const auto *frame = qstyleoption_cast<const QStyleOptionFrame *>(option)) {
                if (frame->lineWidth > 0) {
                    drawBevel(painter, option->rect, false, m_theme);
                }
            }
            return;
        case PE_IndicatorCheckBox:
        case PE_IndicatorItemViewItemCheck: {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, false);
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            painter->restore();
            return;
        }
        case PE_IndicatorArrowUp:
        case PE_IndicatorArrowDown:
        case PE_IndicatorArrowLeft:
        case PE_IndicatorArrowRight:
        case PE_IndicatorSpinUp:
        case PE_IndicatorSpinDown: {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, false);
            painter->translate(QRectF(option->rect).center());
            if (element == PE_IndicatorArrowLeft || element == PE_IndicatorArrowRight) {
                painter->rotate(element == PE_IndicatorArrowLeft ? 90 : -90);
            }
            const QColor color = option->palette.color(
                option->state & State_Enabled ? QPalette::Active : QPalette::Disabled,
                QPalette::ButtonText);
            drawTriangle(
                painter,
                QPointF(),
                color,
                element == PE_IndicatorArrowUp || element == PE_IndicatorSpinUp,
                unit);
            painter->restore();
            return;
        }
        default:
            break;
        }
    }
    if ((m_theme.touch || mobilePlatform())
        && (element == PE_IndicatorArrowUp || element == PE_IndicatorArrowDown
            || element == PE_IndicatorArrowLeft || element == PE_IndicatorArrowRight)) {
        // QCommonStyle scales arrows to their entire rect, including touch targets.
        QStyleOption arrow = *option;
        const int extent = qMin(12, qMin(option->rect.width(), option->rect.height()));
        arrow.rect
            = alignedRect(option->direction, Qt::AlignCenter, QSize(extent, extent), option->rect);
        if (m_theme.skin == Skin::Windows || m_theme.skin == Skin::Native) {
            QProxyStyle::drawPrimitive(element, &arrow, painter, widget);
        } else {
            QCommonStyle::drawPrimitive(element, &arrow, painter, widget);
        }
        return;
    }
    if (m_theme.skin == Skin::Windows || m_theme.skin == Skin::Native) {
        QProxyStyle::drawPrimitive(element, option, painter, widget);
        return;
    }
    const qreal radius = m_theme.metrics.borderRadius;
    const QRectF rect = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
    const bool focused = option->state & QStyle::State_HasFocus;

    switch (element) {
    case PE_PanelButtonCommand: {
        if (m_theme.skin == Skin::Bevel) {
            const bool sunken = option->state & (QStyle::State_Sunken | QStyle::State_On);
            painter->save();
            painter->fillRect(option->rect, bevelFace(option, m_theme));
            drawBevel(painter, option->rect, !sunken, m_theme);
            painter->restore();
            return;
        }
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QColor bd = focused ? m_theme.accent.normal : m_theme.border.forState(option->state);
        painter->setPen(QPen(bd, m_theme.metrics.borderWidth));
        painter->setBrush(m_theme.surface.forState(option->state));
        painter->drawRoundedRect(rect, radius, radius);
        painter->restore();
        return;
    }
    case PE_FrameLineEdit:
    case PE_PanelLineEdit: {
        if (widget && qobject_cast<QWidget *>(widget->parentWidget())
            && widget->parentWidget()->inherits("QComboBox")) {
            break;
        }
        if (m_theme.skin == Skin::Bevel) {
            painter->save();
            if (element == PE_PanelLineEdit) {
                painter->fillRect(option->rect, m_theme.panel.normal);
            }
            drawBevel(painter, option->rect, false, m_theme);
            painter->restore();
            return;
        }
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QColor bd = focused ? m_theme.accent.normal : m_theme.border.normal;
        painter->setPen(QPen(bd, m_theme.metrics.borderWidth));
        painter->setBrush(m_theme.panel.normal);
        painter->drawRoundedRect(rect, radius, radius);
        painter->restore();
        return;
    }
    case PE_IndicatorCheckBox: {
        const bool on = option->state & QStyle::State_On;
        const bool partial = option->state & QStyle::State_NoChange;
        if (m_theme.skin == Skin::Bevel) {
            const bool enabled = option->state & QStyle::State_Enabled;
            painter->save();
            painter
                ->fillRect(option->rect, enabled ? m_theme.panel.normal : m_theme.background.normal);
            drawBevel(painter, option->rect, false, m_theme);
            if (on || partial) {
                QPen mark(enabled ? m_theme.text.normal : m_theme.mutedText.normal, 1.6);
                mark.setJoinStyle(Qt::RoundJoin);
                mark.setCapStyle(Qt::RoundCap);
                painter->setPen(mark);
                painter->setBrush(Qt::NoBrush);
                const QRectF cr = QRectF(option->rect).adjusted(3, 3, -3, -3);
                if (on) {
                    QPainterPath path;
                    path.moveTo(cr.left(), cr.top() + cr.height() * 0.55);
                    path.lineTo(cr.left() + cr.width() * 0.35, cr.bottom());
                    path.lineTo(cr.right(), cr.top());
                    painter->drawPath(path);
                } else {
                    const qreal y = cr.center().y();
                    painter->drawLine(QPointF(cr.left(), y), QPointF(cr.right(), y));
                }
            }
            painter->restore();
            return;
        }
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QColor bd = focused ? m_theme.accent.normal : m_theme.border.forState(option->state);
        painter->setPen(QPen(bd, m_theme.metrics.borderWidth));
        painter->setBrush(
            (on || partial) ? m_theme.accent.forState(option->state) : m_theme.panel.normal);
        const qreal cbr = m_theme.metrics.checkBoxRadius;
        painter->drawRoundedRect(rect, cbr, cbr);
        if (on) {
            QPen check(m_theme.accentText.normal, 1.6);
            check.setJoinStyle(Qt::RoundJoin);
            check.setCapStyle(Qt::RoundCap);
            painter->setPen(check);
            painter->setBrush(Qt::NoBrush);
            const qreal w = rect.width();
            const qreal h = rect.height();
            QPainterPath path;
            path.moveTo(rect.left() + w * 0.26, rect.top() + h * 0.52);
            path.lineTo(rect.left() + w * 0.44, rect.top() + h * 0.70);
            path.lineTo(rect.left() + w * 0.74, rect.top() + h * 0.32);
            painter->drawPath(path);
        } else if (partial) {
            QPen dash(m_theme.accentText.normal, 1.6);
            dash.setCapStyle(Qt::RoundCap);
            painter->setPen(dash);
            const qreal y = rect.center().y();
            painter->drawLine(
                QPointF(rect.left() + rect.width() * 0.28, y),
                QPointF(rect.right() - rect.width() * 0.28, y));
        }
        painter->restore();
        return;
    }
    case PE_IndicatorRadioButton: {
        const bool on = option->state & QStyle::State_On;
        if (m_theme.skin == Skin::Bevel) {
            const bool enabled = option->state & QStyle::State_Enabled;
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            const QRectF er = QRectF(option->rect).adjusted(1, 1, -1, -1);
            painter->setPen(Qt::NoPen);
            painter->setBrush(enabled ? m_theme.panel.normal : m_theme.background.normal);
            painter->drawEllipse(er);
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(m_theme.bevelDark, 1));
            painter->drawArc(er, 45 * 16, 180 * 16);
            painter->setPen(QPen(m_theme.bevelHighlight, 1));
            painter->drawArc(er, 225 * 16, 180 * 16);
            if (on) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(enabled ? m_theme.text.normal : m_theme.mutedText.normal);
                painter->drawEllipse(er.center(), er.width() * 0.18, er.height() * 0.18);
            }
            painter->restore();
            return;
        }
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QColor bd = focused ? m_theme.accent.normal : m_theme.border.forState(option->state);
        painter->setPen(QPen(bd, m_theme.metrics.borderWidth));
        painter->setBrush(m_theme.panel.normal);
        painter->drawEllipse(rect);
        if (on) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(m_theme.accent.forState(option->state));
            painter->drawEllipse(rect.center(), rect.width() * 0.22, rect.height() * 0.22);
        }
        painter->restore();
        return;
    }
    case PE_PanelMenu: {
        if (m_theme.skin == Skin::Bevel) {
            painter->save();
            painter->fillRect(option->rect, m_theme.surface.normal);
            drawBevel(painter, option->rect, true, m_theme);
            painter->restore();
            return;
        }
        painter->save();
        painter->fillRect(option->rect, m_theme.panel.normal);
        painter->setPen(QPen(m_theme.border.normal, m_theme.metrics.borderWidth));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(rect);
        painter->restore();
        return;
    }
    case PE_IndicatorBranch: {
        if (option->state & State_Children) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            const QPointF c = QRectF(option->rect).center();
            if (option->state & State_Open) {
                drawTriangle(painter, c, m_theme.mutedText.normal, false);
            } else {
                const qreal s = 3.5;
                QPainterPath path;
                path.moveTo(c.x() - s / 2, c.y() - s);
                path.lineTo(c.x() + s / 2 + 1, c.y());
                path.lineTo(c.x() - s / 2, c.y() + s);
                path.closeSubpath();
                painter->setPen(Qt::NoPen);
                painter->setBrush(m_theme.mutedText.normal);
                painter->drawPath(path);
            }
            painter->restore();
        }
        return;
    }
    case PE_PanelItemViewItem: {
        const bool selected = option->state & QStyle::State_Selected;
        const bool hover = (option->state & QStyle::State_MouseOver)
                           && (option->state & QStyle::State_Enabled);
        if (!selected && !hover) {
            break;
        }
        painter->save();
        if (m_theme.skin == Skin::Bevel) {
            painter
                ->fillRect(option->rect, selected ? m_theme.accent.normal : m_theme.surface.hovered);
            painter->restore();
            return;
        }
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QRectF rr = QRectF(option->rect).adjusted(1, 0.5, -1, -0.5);
        const qreal ir = qMin<qreal>(m_theme.metrics.borderRadius, rr.height() / 2);
        painter->setPen(Qt::NoPen);
        painter->setBrush(selected ? m_theme.accent.normal : m_theme.surface.hovered);
        painter->drawRoundedRect(rr, ir, ir);
        painter->restore();
        return;
    }
    case PE_Frame: {
        if (m_theme.skin == Skin::Bevel) {
            painter->save();
            drawBevel(painter, option->rect, false, m_theme);
            painter->restore();
            return;
        }
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const qreal fr = m_theme.metrics.borderRadius;
        painter->setPen(QPen(m_theme.border.normal, m_theme.metrics.borderWidth));
        painter->setBrush(
            qobject_cast<const QAbstractScrollArea *>(widget)
                ? option->palette.brush(QPalette::Base)
                : QBrush(Qt::NoBrush));
        painter->drawRoundedRect(rect, fr, fr);
        painter->restore();
        return;
    }
    case PE_FrameTabWidget: {
        if (m_theme.skin == Skin::Bevel) {
            painter->save();
            drawBevel(painter, option->rect, true, m_theme);
            painter->restore();
            return;
        }
        painter->save();
        painter->setPen(QPen(m_theme.border.normal, m_theme.metrics.borderWidth));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(rect);
        painter->restore();
        return;
    }
    case PE_PanelTipLabel: {
        painter->save();
        painter->fillRect(option->rect, m_theme.tooltipBase);
        const QColor tb = m_theme.skin == Skin::Bevel ? m_theme.bevelShadow : m_theme.border.normal;
        painter->setPen(QPen(tb, m_theme.metrics.borderWidth));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(rect);
        painter->restore();
        return;
    }
    case PE_FrameFocusRect: {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(m_theme.accent.normal, m_theme.metrics.focusWidth));
        painter->setBrush(Qt::NoBrush);
        const qreal fr = m_theme.metrics.borderRadius;
        painter->drawRoundedRect(rect, fr, fr);
        painter->restore();
        return;
    }
    case PE_FrameDefaultButton:
        return;
    case PE_IndicatorToolBarSeparator: {
        painter->save();
        painter->setPen(m_theme.border.normal);
        const QRect br = option->rect;
        if (br.width() < br.height()) {
            const int x = br.center().x();
            painter->drawLine(x, br.top() + 4, x, br.bottom() - 4);
        } else {
            const int y = br.center().y();
            painter->drawLine(br.left() + 4, y, br.right() - 4, y);
        }
        painter->restore();
        return;
    }
    case PE_PanelButtonTool:
    case PE_PanelButtonBevel: {
        const bool active = option->state
                            & (QStyle::State_MouseOver | QStyle::State_Sunken | QStyle::State_On);
        if (!(option->state & QStyle::State_Enabled) || !active) {
            return;
        }
        if (m_theme.skin == Skin::Bevel) {
            const bool sunken = option->state & (QStyle::State_Sunken | QStyle::State_On);
            painter->save();
            painter->fillRect(option->rect, m_theme.surface.normal);
            drawBevel(painter, option->rect, !sunken, m_theme);
            painter->restore();
            return;
        }
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QColor bg = (option->state & QStyle::State_On)
                              ? m_theme.accent.normal
                              : m_theme.surface.forState(option->state);
        painter->setPen(Qt::NoPen);
        painter->setBrush(bg);
        painter->drawRoundedRect(rect, radius, radius);
        painter->restore();
        return;
    }
    case PE_IndicatorHeaderArrow: {
        if (const auto *h = qstyleoption_cast<const QStyleOptionHeader *>(option)) {
            if (h->sortIndicator == QStyleOptionHeader::None) {
                return;
            }
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            drawTriangle(
                painter,
                QRectF(option->rect).center(),
                m_theme.mutedText.normal,
                h->sortIndicator == QStyleOptionHeader::SortUp);
            painter->restore();
        }
        return;
    }
    default:
        break;
    }

    QCommonStyle::drawPrimitive(element, option, painter, widget);
}

void IaitoStyle::drawControl(
    ControlElement element,
    const QStyleOption *option,
    QPainter *painter,
    const QWidget *widget) const
{
    if (m_theme.skin == Skin::Windows
        && (element == CE_ScrollBarAddLine || element == CE_ScrollBarSubLine
            || (element == CE_ScrollBarSlider && (option->state & State_Enabled))
            || element == CE_HeaderSection)) {
        painter->fillRect(option->rect, option->palette.button());
        drawBevel(painter, option->rect, !(option->state & State_Sunken), m_theme);
        if (element == CE_ScrollBarAddLine || element == CE_ScrollBarSubLine) {
            const bool add = element == CE_ScrollBarAddLine;
            PrimitiveElement arrow = add ? PE_IndicatorArrowDown : PE_IndicatorArrowUp;
            if (option->state & State_Horizontal) {
                arrow = add == (option->direction == Qt::LeftToRight) ? PE_IndicatorArrowRight
                                                                      : PE_IndicatorArrowLeft;
            }
            drawPrimitive(arrow, option, painter, widget);
        }
        return;
    }
    if (m_theme.skin == Skin::Windows
        && (element == CE_ScrollBarAddPage || element == CE_ScrollBarSubPage
            || (element == CE_ScrollBarSlider && !(option->state & State_Enabled)))) {
        const bool pressed = option->state & State_Sunken;
        const QColor foreground = option->palette.color(
            pressed ? QPalette::Shadow : QPalette::Light);
        const QColor background = option->palette.color(pressed ? QPalette::Dark : QPalette::Window);
        // Texture pixels are logical pixels; Qt's Dense4Pattern uses device pixels
        // and makes the Windows checkerboard too fine on Retina displays.
        const int cell = qMax(1, m_theme.metrics.borderWidth);
        QImage pattern(2 * cell, 2 * cell, QImage::Format_RGB32);
        pattern.fill(background);
        for (int y = 0; y < 2 * cell; ++y) {
            for (int x = 0; x < 2 * cell; ++x) {
                if ((x / cell) == (y / cell)) {
                    pattern.setPixelColor(x, y, foreground);
                }
            }
        }
        painter->save();
        painter->setBrushOrigin(0, 0);
        painter->fillRect(option->rect, QBrush(pattern));
        painter->restore();
        return;
    }
    if (element == CE_TabBarTabLabel && isDockTabBar(widget)) {
        if (const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option)) {
            const QRect textRect = subElementRect(SE_TabBarTabText, tab, widget);
            QString title = tab->text;
            if (const auto *tabs = qobject_cast<const QTabBar *>(widget)) {
                const int index = tabs->tabAt(tab->rect.center());
                if (index >= 0) {
                    title = tabs->tabText(index);
                }
            }
            const QString text
                = tab->fontMetrics.elidedText(title, Qt::ElideRight, qMax(0, textRect.width()));
            painter->save();
            if (widget) {
                painter->setFont(widget->font());
            }
            painter->setClipRect(textRect, Qt::IntersectClip);
            drawItemText(
                painter,
                textRect,
                Qt::AlignCenter | Qt::TextShowMnemonic,
                tab->palette,
                tab->state & State_Enabled,
                text,
                QPalette::WindowText);
            painter->restore();
            return;
        }
    }
    if (m_theme.skin == Skin::Windows || m_theme.skin == Skin::Native) {
        if (element == CE_TabBarTab && isDockTabBar(widget)) {
            QProxyStyle::drawControl(CE_TabBarTabShape, option, painter, widget);
            drawControl(CE_TabBarTabLabel, option, painter, widget);
            return;
        }
        QProxyStyle::drawControl(element, option, painter, widget);
        return;
    }
    if (element == CE_MenuBarEmptyArea) {
        painter->fillRect(
            option->rect,
            m_theme.chromeStyle == ChromeStyle::Accent ? m_theme.accent.normal
                                                       : m_theme.background.normal);
        return;
    }

    if (element == CE_MenuBarItem) {
        if (const auto *mbi = qstyleoption_cast<const QStyleOptionMenuItem *>(option)) {
            const bool accent = m_theme.chromeStyle == ChromeStyle::Accent;
            painter->save();
            const bool active = option->state & (State_Selected | State_Sunken);
            if (accent) {
                painter
                    ->fillRect(option->rect, active ? m_theme.accent.pressed : m_theme.accent.normal);
            } else if (active) {
                painter->fillRect(option->rect, m_theme.accent.normal);
            }
            const QColor fg = (accent || active) ? m_theme.accentText.normal
                                                 : m_theme.text.forState(option->state);
            painter->setPen(fg);
            const int flags = Qt::AlignCenter | Qt::TextShowMnemonic | Qt::TextDontClip
                              | Qt::TextSingleLine;
            painter->drawText(option->rect, flags, mbi->text);
            painter->restore();
            return;
        }
    }

    if (element == CE_ToolBar) {
        painter->fillRect(
            option->rect,
            m_theme.chromeStyle == ChromeStyle::Accent ? m_theme.accent.normal
                                                       : m_theme.background.normal);
        return;
    }

    if (element == CE_MenuItem) {
        if (const auto *mi = qstyleoption_cast<const QStyleOptionMenuItem *>(option)) {
            painter->save();
            painter->setFont(mi->font);
            const QRect r = option->rect;
            if (mi->menuItemType == QStyleOptionMenuItem::Separator) {
                const int y = r.center().y();
                if (m_theme.skin == Skin::Bevel) {
                    painter->setPen(m_theme.bevelDark);
                    painter->drawLine(r.left() + 4, y, r.right() - 4, y);
                    painter->setPen(m_theme.bevelHighlight);
                    painter->drawLine(r.left() + 4, y + 1, r.right() - 4, y + 1);
                } else {
                    painter->setPen(m_theme.border.normal);
                    painter->drawLine(r.left() + 8, y, r.right() - 8, y);
                }
                painter->restore();
                return;
            }
            const bool enabled = option->state & State_Enabled;
            const bool active = (option->state & State_Selected) && enabled;
            if (active) {
                painter->fillRect(r, m_theme.accent.normal);
            }
            const QColor fg = !enabled ? m_theme.mutedText.normal
                              : active ? m_theme.accentText.normal
                                       : m_theme.text.normal;
            painter->setPen(fg);

            const int leftMargin = m_theme.metrics.menuItemPadding;
            const int iconCol = qMax(mi->maxIconWidth, m_theme.metrics.iconSize + 8);
            const bool checkable = mi->checkType != QStyleOptionMenuItem::NotCheckable;
            if (!mi->icon.isNull()) {
                const int sz = m_theme.metrics.iconSize;
                QRect ir(r.left() + leftMargin, r.center().y() - sz / 2, sz, sz);
                if (checkable && mi->checked) {
                    painter->save();
                    painter->setRenderHint(QPainter::Antialiasing, true);
                    QColor hl = active ? m_theme.accentText.normal : m_theme.accent.normal;
                    hl.setAlphaF(0.22);
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(hl);
                    const qreal rr = m_theme.metrics.checkBoxRadius;
                    painter->drawRoundedRect(QRectF(ir).adjusted(-2, -2, 2, 2), rr, rr);
                    painter->restore();
                }
                mi->icon
                    .paint(painter, ir, Qt::AlignCenter, enabled ? QIcon::Normal : QIcon::Disabled);
            } else if (checkable && mi->checked) {
                QRectF cb(r.left() + leftMargin, r.center().y() - 5, 10, 10);
                QPen check(fg, 1.6);
                check.setJoinStyle(Qt::RoundJoin);
                check.setCapStyle(Qt::RoundCap);
                painter->setRenderHint(QPainter::Antialiasing, true);
                painter->setPen(check);
                QPainterPath p;
                p.moveTo(cb.left() + cb.width() * 0.1, cb.top() + cb.height() * 0.55);
                p.lineTo(cb.left() + cb.width() * 0.42, cb.bottom());
                p.lineTo(cb.right(), cb.top());
                painter->drawPath(p);
                painter->setPen(fg);
            }

            QString text = mi->text;
            QString shortcut;
            const int tab = text.indexOf(QLatin1Char('\t'));
            if (tab >= 0) {
                shortcut = text.mid(tab + 1);
                text = text.left(tab);
            }
            const QRect textRect = r.adjusted(leftMargin + iconCol, 0, -22, 0);
            painter->drawText(
                textRect,
                Qt::AlignVCenter | Qt::AlignLeft | Qt::TextShowMnemonic | Qt::TextSingleLine,
                text);
            if (!shortcut.isEmpty()) {
                painter->drawText(
                    textRect, Qt::AlignVCenter | Qt::AlignRight | Qt::TextSingleLine, shortcut);
            }
            if (mi->menuItemType == QStyleOptionMenuItem::SubMenu) {
                painter->setRenderHint(QPainter::Antialiasing, true);
                const qreal as = 4;
                const qreal ax = r.right() - 13;
                const qreal ay = r.center().y();
                QPainterPath arrow;
                arrow.moveTo(ax - as / 2, ay - as);
                arrow.lineTo(ax + as / 2, ay);
                arrow.lineTo(ax - as / 2, ay + as);
                painter->setPen(QPen(fg, 1.4));
                painter->setBrush(Qt::NoBrush);
                painter->drawPath(arrow);
            }
            painter->restore();
            return;
        }
    }

    if (element == CE_Splitter) {
        if (m_theme.skin == Skin::Bevel) {
            painter->fillRect(option->rect, m_theme.surface.normal);
            return;
        }
        painter->save();
        if ((option->state & State_MouseOver) && (option->state & State_Enabled)) {
            painter->fillRect(option->rect, m_theme.surface.hovered);
        }
        painter->setPen(Qt::NoPen);
        painter->setBrush(m_theme.border.normal);
        const QPointF c = QRectF(option->rect).center();
        const bool wide = option->rect.width() > option->rect.height();
        for (int i = -1; i <= 1; ++i) {
            const QPointF p = wide ? QPointF(c.x() + i * 4, c.y()) : QPointF(c.x(), c.y() + i * 4);
            painter->drawEllipse(p, 1, 1);
        }
        painter->restore();
        return;
    }

    if (element == CE_PushButtonBevel) {
        if (m_theme.skin == Skin::Bevel) {
            const bool sunken = option->state & (State_Sunken | State_On);
            painter->save();
            painter->fillRect(option->rect, bevelFace(option, m_theme));
            drawBevel(painter, option->rect, !sunken, m_theme);
            painter->restore();
            return;
        }
        const bool enabled = option->state & State_Enabled;
        const bool checked = option->state & State_On;
        const bool focused = option->state & State_HasFocus;
        const bool primary = checked && enabled;
        const QColor fill = primary ? m_theme.accent.forState(option->state)
                                    : m_theme.surface.forState(option->state);
        const QColor bd = (focused || primary) ? m_theme.accent.normal
                                               : m_theme.border.forState(option->state);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(bd, m_theme.metrics.borderWidth));
        painter->setBrush(fill);
        const qreal r = m_theme.metrics.borderRadius;
        painter->drawRoundedRect(QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5), r, r);
        painter->restore();
        return;
    }

    if (element == CE_PushButton) {
        if (const auto *btn = qstyleoption_cast<const QStyleOptionButton *>(option)) {
            drawControl(CE_PushButtonBevel, option, painter, widget);
            QStyleOptionButton label = *btn;
            if ((option->state & State_On) && (option->state & State_Enabled)) {
                label.palette.setColor(QPalette::ButtonText, m_theme.accentText.normal);
            }
            QCommonStyle::drawControl(CE_PushButtonLabel, &label, painter, widget);
            return;
        }
    }

    if (element == CE_TabBarTab) {
        if (const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option)) {
            if (m_theme.skin == Skin::Bevel) {
                const QRect r = tab->rect;
                const QColor hi = m_theme.bevelHighlight;
                const QColor dk = m_theme.bevelDark;
                const QColor sh = m_theme.bevelShadow;
                const bool selected = tab->state & State_Selected;
                painter->save();
                painter->fillRect(r, selected ? m_theme.surface.normal : bevelFace(tab, m_theme));
                switch (tab->shape) {
                case QTabBar::RoundedSouth:
                case QTabBar::TriangularSouth:
                    painter->setPen(hi);
                    painter->drawLine(r.left(), r.top(), r.left(), r.bottom() - 2);
                    painter->setPen(dk);
                    painter->drawLine(r.left() + 1, r.bottom() - 1, r.right() - 1, r.bottom() - 1);
                    painter->drawLine(r.right() - 1, r.top(), r.right() - 1, r.bottom() - 1);
                    painter->setPen(sh);
                    painter->drawLine(r.left(), r.bottom(), r.right(), r.bottom());
                    painter->drawLine(r.right(), r.top(), r.right(), r.bottom());
                    break;
                case QTabBar::RoundedWest:
                case QTabBar::TriangularWest:
                case QTabBar::RoundedEast:
                case QTabBar::TriangularEast:
                    drawBevel(painter, r, true, m_theme);
                    break;
                default:
                    painter->setPen(hi);
                    painter->drawLine(r.left(), r.top() + 2, r.left(), r.bottom());
                    painter->drawLine(r.left(), r.top() + 2, r.left() + 2, r.top());
                    painter->drawLine(r.left() + 2, r.top(), r.right() - 2, r.top());
                    painter->setPen(dk);
                    painter->drawLine(r.right() - 1, r.top() + 2, r.right() - 1, r.bottom());
                    painter->setPen(sh);
                    painter->drawLine(r.right(), r.top() + 3, r.right(), r.bottom());
                    break;
                }
                painter->restore();
                drawControl(CE_TabBarTabLabel, tab, painter, widget);
                return;
            }
            const bool accent = m_theme.chromeStyle == ChromeStyle::Accent;
            const bool accentTabs = m_theme.chromeStyle == ChromeStyle::AccentTabs;
            painter->save();
            const bool selected = tab->state & State_Selected;
            const bool hovered = (tab->state & State_MouseOver) && !selected
                                 && (tab->state & State_Enabled);
            QColor bg;
            QColor fg = m_theme.text.normal;
            if (accentTabs) {
                bg = selected  ? m_theme.accent.normal
                     : hovered ? m_theme.surface.hovered
                               : m_theme.background.normal;
                fg = selected ? m_theme.accentText.normal : m_theme.text.normal;
            } else if (accent) {
                bg = selected  ? m_theme.background.normal
                     : hovered ? m_theme.accent.hovered
                               : m_theme.accent.normal;
                fg = selected ? m_theme.text.normal : m_theme.accentText.normal;
            } else if (m_theme.metrics.tabBorderWidth > 0) {
                bg = selected  ? m_theme.panel.normal
                     : hovered ? m_theme.surface.hovered
                               : m_theme.surface.normal;
            } else {
                bg = selected  ? m_theme.panel.normal
                     : hovered ? m_theme.surface.hovered
                               : m_theme.background.normal;
            }
            painter->fillRect(tab->rect, bg);
            if (m_theme.metrics.tabBorderWidth > 0 && (!accent || selected)) {
                const QRect r = tab->rect;
                painter->setPen(m_theme.border.normal);
                switch (tab->shape) {
                case QTabBar::RoundedSouth:
                case QTabBar::TriangularSouth:
                    painter->drawLine(r.topLeft(), r.bottomLeft());
                    painter->drawLine(r.topRight(), r.bottomRight());
                    painter->drawLine(r.bottomLeft(), r.bottomRight());
                    break;
                case QTabBar::RoundedWest:
                case QTabBar::TriangularWest:
                case QTabBar::RoundedEast:
                case QTabBar::TriangularEast:
                    painter->drawRect(r.adjusted(0, 0, -1, -1));
                    break;
                default:
                    painter->drawLine(r.topLeft(), r.topRight());
                    painter->drawLine(r.topLeft(), r.bottomLeft());
                    painter->drawLine(r.topRight(), r.bottomRight());
                    break;
                }
            }
            if (!accent && (selected || m_theme.metrics.tabBorderWidth == 0)) {
                const int t = selected ? m_theme.metrics.tabIndicatorWidth : 1;
                const QColor lineColor = selected ? m_theme.accent.normal : m_theme.border.normal;
                const QRect r = tab->rect;
                QRect bar;
                switch (tab->shape) {
                case QTabBar::RoundedSouth:
                case QTabBar::TriangularSouth:
                    bar = QRect(r.left(), r.top(), r.width(), t);
                    break;
                case QTabBar::RoundedWest:
                case QTabBar::TriangularWest:
                    bar = QRect(r.right() - t + 1, r.top(), t, r.height());
                    break;
                case QTabBar::RoundedEast:
                case QTabBar::TriangularEast:
                    bar = QRect(r.left(), r.top(), t, r.height());
                    break;
                default:
                    bar = QRect(r.left(), r.bottom() - t + 1, r.width(), t);
                    break;
                }
                painter->fillRect(bar, lineColor);
            }
            if ((!accent
                 || (!selected && tab->selectedPosition != QStyleOptionTab::PreviousIsSelected))
                && tab->position != QStyleOptionTab::Beginning
                && tab->position != QStyleOptionTab::OnlyOneTab) {
                QColor sep = accent ? m_theme.accent.pressed : m_theme.border.normal;
                sep.setAlphaF(accent ? 0.7 : 0.45);
                painter->setPen(sep);
                const QRect r = tab->rect;
                const bool vertical = tab->shape == QTabBar::RoundedWest
                                      || tab->shape == QTabBar::TriangularWest
                                      || tab->shape == QTabBar::RoundedEast
                                      || tab->shape == QTabBar::TriangularEast;
                if (vertical) {
                    const int inset = r.width() / 4;
                    painter->drawLine(r.left() + inset, r.top(), r.right() - inset, r.top());
                } else {
                    const int inset = r.height() / 4;
                    const int x = tab->direction == Qt::RightToLeft ? r.right() : r.left();
                    painter->drawLine(x, r.top() + inset, x, r.bottom() - inset);
                }
            }
            painter->restore();
            QStyleOptionTab label = *tab;
            label.palette.setColor(QPalette::WindowText, fg);
            label.palette.setColor(QPalette::Text, fg);
            label.palette.setColor(QPalette::ButtonText, fg);
            drawControl(CE_TabBarTabLabel, &label, painter, widget);
            return;
        }
    }

    if (element == CE_HeaderSection) {
        if (m_theme.skin == Skin::Bevel) {
            const bool sunken = option->state & (State_Sunken | State_On);
            painter->save();
            painter->fillRect(option->rect, m_theme.surface.normal);
            drawBevel(painter, option->rect, !sunken, m_theme);
            painter->restore();
            return;
        }
        painter->save();
        const bool hovered = (option->state & State_MouseOver) && (option->state & State_Enabled);
        painter->fillRect(option->rect, hovered ? m_theme.surface.hovered : m_theme.surface.normal);
        painter->setPen(m_theme.border.normal);
        painter->drawLine(option->rect.bottomLeft(), option->rect.bottomRight());
        painter->drawLine(option->rect.topRight(), option->rect.bottomRight());
        painter->restore();
        return;
    }

    if (element == CE_ProgressBar) {
        if (const auto *pb = qstyleoption_cast<const QStyleOptionProgressBar *>(option)) {
            const bool busy = pb->maximum == pb->minimum;
            if (m_theme.skin == Skin::Bevel) {
                painter->save();
                painter->fillRect(option->rect, m_theme.panel.normal);
                drawBevel(painter, option->rect, false, m_theme);
                const QRect inner = option->rect.adjusted(2, 2, -2, -2);
                const int blockW = 8, gap = 2, step = blockW + gap;
                const int cells = qMax(1, inner.width() / step);
                auto block = [&](int i) {
                    painter->fillRect(
                        QRect(inner.left() + i * step, inner.top(), blockW, inner.height()),
                        m_theme.accent.normal);
                };
                if (busy) {
                    const int chunk = qMax(3, cells / 3);
                    const int head = m_busyPhase % (cells + chunk);
                    for (int i = 0; i < chunk; ++i) {
                        const int idx = head - i;
                        if (idx >= 0 && idx < cells) {
                            block(idx);
                        }
                    }
                } else {
                    const qreal frac = qreal(pb->progress - pb->minimum)
                                       / qreal(pb->maximum - pb->minimum);
                    const int n = int(cells * qBound(qreal(0), frac, qreal(1)) + 0.5);
                    for (int i = 0; i < n; ++i) {
                        block(i);
                    }
                }
                if (pb->textVisible) {
                    painter->setPen(m_theme.text.normal);
                    painter->drawText(option->rect, Qt::AlignCenter, pb->text);
                }
                painter->restore();
                if (busy && widget) {
                    QWidget *w = const_cast<QWidget *>(widget);
                    if (!m_busyBars.contains(w)) {
                        m_busyBars.append(w);
                    }
                    if (!m_busyTimer->isActive()) {
                        m_busyTimer->start();
                    }
                }
                return;
            }
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            const qreal r = m_theme.metrics.borderRadius;
            const QRectF groove = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
            painter->setPen(QPen(m_theme.border.normal, m_theme.metrics.borderWidth));
            painter->setBrush(m_theme.panel.normal);
            painter->drawRoundedRect(groove, r, r);
            if (busy) {
                const qreal chunk = groove.width() / 3.0;
                const qreal travel = groove.width() + chunk;
                const qreal pos = qreal((m_busyPhase * 6) % qMax(1, int(travel))) - chunk;
                QRectF bar(groove.left() + pos, groove.top(), chunk, groove.height());
                QPainterPath clip;
                clip.addRoundedRect(groove, r, r);
                painter->setClipPath(clip);
                painter->setPen(Qt::NoPen);
                painter->setBrush(m_theme.accent.normal);
                painter->drawRect(bar);
                painter->setClipping(false);
            } else if (pb->maximum > pb->minimum) {
                const qreal frac = qreal(pb->progress - pb->minimum)
                                   / qreal(pb->maximum - pb->minimum);
                QRectF fill = groove;
                fill.setWidth(groove.width() * qBound(qreal(0), frac, qreal(1)));
                if (fill.width() > 0) {
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(m_theme.accent.normal);
                    painter->setClipRect(fill);
                    painter->drawRoundedRect(groove, r, r);
                    painter->setClipping(false);
                }
            }
            if (pb->textVisible) {
                painter->setPen(m_theme.text.normal);
                painter->drawText(option->rect, Qt::AlignCenter, pb->text);
            }
            painter->restore();
            if (busy && widget) {
                QWidget *w = const_cast<QWidget *>(widget);
                if (!m_busyBars.contains(w)) {
                    m_busyBars.append(w);
                }
                if (!m_busyTimer->isActive()) {
                    m_busyTimer->start();
                }
            }
            return;
        }
    }

    if (element == CE_DockWidgetTitle) {
        if (const auto *dw = qstyleoption_cast<const QStyleOptionDockWidget *>(option)) {
            const bool bevel = m_theme.skin == Skin::Bevel;
            const bool accent = bevel || m_theme.chromeStyle == ChromeStyle::Accent;
            painter->save();
            if (bevel) {
                const QColor a = m_theme.accent.normal;
                const QColor hi = m_theme.bevelHighlight;
                const QColor
                    end((a.red() * 2 + hi.red() * 3) / 5,
                        (a.green() * 2 + hi.green() * 3) / 5,
                        (a.blue() * 2 + hi.blue() * 3) / 5);
                QLinearGradient g(option->rect.topLeft(), option->rect.topRight());
                g.setColorAt(0, a);
                g.setColorAt(1, end);
                painter->fillRect(option->rect, g);
            } else {
                painter
                    ->fillRect(option->rect, accent ? m_theme.accent.normal : m_theme.panel.normal);
            }
            painter->setPen(accent ? m_theme.accent.pressed : m_theme.border.normal);
            painter->drawLine(option->rect.bottomLeft(), option->rect.bottomRight());
            const QRect tr = subElementRect(SE_DockWidgetTitleBarText, dw, widget);
            painter->setPen(accent ? m_theme.accentText.normal : m_theme.text.normal);
            const QString txt
                = painter->fontMetrics().elidedText(dw->title, Qt::ElideRight, tr.width());
            painter->drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, txt);
            painter->restore();
            return;
        }
    }

    QCommonStyle::drawControl(element, option, painter, widget);
}

void IaitoStyle::drawComplexControl(
    ComplexControl control,
    const QStyleOptionComplex *option,
    QPainter *painter,
    const QWidget *widget) const
{
    if (m_theme.skin == Skin::Windows && control == CC_ComboBox) {
        if (const auto *combo = qstyleoption_cast<const QStyleOptionComboBox *>(option)) {
            painter->save();
            painter->fillRect(option->rect, option->palette.button());
            if (combo->frame && (combo->subControls & SC_ComboBoxFrame)) {
                drawBevel(painter, option->rect, false, m_theme);
            }
            if (combo->subControls & SC_ComboBoxArrow) {
                QStyleOption arrow = *option;
                arrow.rect = subControlRect(CC_ComboBox, combo, SC_ComboBoxArrow, widget);
                const bool pressed = (combo->activeSubControls & SC_ComboBoxArrow)
                                     && (option->state & State_Sunken);
                painter->fillRect(arrow.rect, option->palette.button());
                drawBevel(painter, arrow.rect, !pressed, m_theme);
                drawPrimitive(PE_IndicatorArrowDown, &arrow, painter, widget);
            }
            if (!combo->editable && (combo->state & State_HasFocus)
                && (combo->subControls & SC_ComboBoxEditField)) {
                painter->fillRect(
                    subControlRect(CC_ComboBox, combo, SC_ComboBoxEditField, widget),
                    combo->palette.highlight());
            }
            painter->restore();
            return;
        }
    }
    if ((m_theme.skin == Skin::Windows && control != CC_SpinBox && control != CC_Slider)
        || m_theme.skin == Skin::Native) {
        QProxyStyle::drawComplexControl(control, option, painter, widget);
        return;
    }
    if (control == CC_ToolButton) {
        if (const auto *tb = qstyleoption_cast<const QStyleOptionToolButton *>(option)) {
            const bool enabled = option->state & State_Enabled;
            const bool active = option->state & (State_MouseOver | State_Sunken | State_On);
            if (enabled && active && m_theme.skin == Skin::Bevel) {
                const bool sunken = option->state & (State_Sunken | State_On);
                painter->save();
                painter->fillRect(option->rect, m_theme.surface.normal);
                drawBevel(painter, option->rect, !sunken, m_theme);
                painter->restore();
            } else if (enabled && active) {
                painter->save();
                painter->setRenderHint(QPainter::Antialiasing, true);
                const QColor bg = (option->state & State_On)
                                      ? m_theme.accent.normal
                                      : m_theme.surface.forState(option->state);
                painter->setPen(Qt::NoPen);
                painter->setBrush(bg);
                const qreal r = m_theme.metrics.borderRadius;
                painter->drawRoundedRect(QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5), r, r);
                painter->restore();
            }
            QStyleOptionToolButton label = *tb;
            label.rect = subControlRect(CC_ToolButton, tb, SC_ToolButton, widget);
            QCommonStyle::drawControl(CE_ToolButtonLabel, &label, painter, widget);
            if (tb->subControls & SC_ToolButtonMenu) {
                const QRect mr = subControlRect(CC_ToolButton, tb, SC_ToolButtonMenu, widget);
                painter->save();
                painter->setRenderHint(QPainter::Antialiasing, true);
                const qreal as = 3;
                const QPointF c = mr.center();
                QPainterPath a;
                a.moveTo(c.x() - as, c.y() - as / 2);
                a.lineTo(c.x() + as, c.y() - as / 2);
                a.lineTo(c.x(), c.y() + as / 2 + 1);
                a.closeSubpath();
                painter->setPen(Qt::NoPen);
                painter->setBrush(m_theme.text.forState(option->state));
                painter->drawPath(a);
                painter->restore();
            }
            return;
        }
    }

    if (control == CC_GroupBox) {
        if (const auto *gb = qstyleoption_cast<const QStyleOptionGroupBox *>(option)) {
            if (m_theme.skin == Skin::Bevel) {
                painter->save();
                if (gb->subControls & SC_GroupBoxFrame) {
                    const QRect fr = subControlRect(CC_GroupBox, gb, SC_GroupBoxFrame, widget);
                    drawBevel(painter, fr, false, m_theme);
                }
                if (gb->subControls & SC_GroupBoxCheckBox) {
                    const QRect cr = subControlRect(CC_GroupBox, gb, SC_GroupBoxCheckBox, widget);
                    QStyleOptionButton cbopt;
                    cbopt.rect = cr;
                    cbopt.state = gb->state;
                    drawPrimitive(PE_IndicatorCheckBox, &cbopt, painter, widget);
                }
                if ((gb->subControls & SC_GroupBoxLabel) && !gb->text.isEmpty()) {
                    const QRect tr = subControlRect(CC_GroupBox, gb, SC_GroupBoxLabel, widget);
                    painter->fillRect(tr.adjusted(-2, 0, 2, 0), m_theme.background.normal);
                    painter->setPen(
                        (gb->state & State_Enabled) ? m_theme.text.normal
                                                    : m_theme.mutedText.normal);
                    painter->drawText(
                        tr, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextShowMnemonic, gb->text);
                }
                painter->restore();
                return;
            }
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            if (gb->subControls & SC_GroupBoxFrame) {
                const QRect fr = subControlRect(CC_GroupBox, gb, SC_GroupBoxFrame, widget);
                painter->setPen(QPen(m_theme.border.normal, m_theme.metrics.borderWidth));
                painter->setBrush(Qt::NoBrush);
                const qreal r = m_theme.metrics.borderRadius;
                painter->drawRoundedRect(QRectF(fr).adjusted(0.5, 0.5, -0.5, -0.5), r, r);
            }
            if (gb->subControls & SC_GroupBoxCheckBox) {
                const QRect cr = subControlRect(CC_GroupBox, gb, SC_GroupBoxCheckBox, widget);
                QStyleOptionButton cbopt;
                cbopt.rect = cr;
                cbopt.state = gb->state;
                drawPrimitive(PE_IndicatorCheckBox, &cbopt, painter, widget);
            }
            if ((gb->subControls & SC_GroupBoxLabel) && !gb->text.isEmpty()) {
                const QRect tr = subControlRect(CC_GroupBox, gb, SC_GroupBoxLabel, widget);
                painter->fillRect(tr.adjusted(-2, 0, 2, 0), m_theme.background.normal);
                painter->setPen(
                    (gb->state & State_Enabled) ? m_theme.text.normal : m_theme.mutedText.normal);
                painter
                    ->drawText(tr, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextShowMnemonic, gb->text);
            }
            painter->restore();
            return;
        }
    }

    if (control == CC_ComboBox) {
        if (const auto *cb = qstyleoption_cast<const QStyleOptionComboBox *>(option)) {
            if (m_theme.skin == Skin::Bevel) {
                painter->save();
                if (cb->editable) {
                    painter->fillRect(option->rect, m_theme.panel.normal);
                    drawBevel(painter, option->rect, false, m_theme);
                } else {
                    painter->fillRect(option->rect, bevelFace(option, m_theme));
                    drawBevel(painter, option->rect, true, m_theme);
                }
                const QRect ar = subControlRect(CC_ComboBox, cb, SC_ComboBoxArrow, widget);
                const bool sunken = (cb->activeSubControls & SC_ComboBoxArrow)
                                    && (option->state & State_Sunken);
                painter->fillRect(ar, bevelFace(option, m_theme));
                drawBevel(painter, ar, !sunken, m_theme);
                drawTriangle(
                    painter, QRectF(ar).center(), m_theme.text.forState(option->state), false);
                painter->restore();
                return;
            }
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            const bool focused = option->state & State_HasFocus;
            const QColor bd = focused ? m_theme.accent.normal : m_theme.border.normal;
            const qreal r = m_theme.metrics.borderRadius;
            const QRectF fr = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
            painter->setPen(QPen(bd, m_theme.metrics.borderWidth));
            painter->setBrush(
                cb->editable ? m_theme.panel.normal : m_theme.surface.forState(option->state));
            painter->drawRoundedRect(fr, r, r);
            const QRect ar = subControlRect(CC_ComboBox, cb, SC_ComboBoxArrow, widget);
            drawTriangle(painter, QRectF(ar).center(), m_theme.text.forState(option->state), false);
            painter->restore();
            return;
        }
    }

    if (control == CC_SpinBox) {
        if (const auto *spin = qstyleoption_cast<const QStyleOptionSpinBox *>(option)) {
            if (m_theme.skin == Skin::Bevel || m_theme.skin == Skin::Windows) {
                painter->save();
                painter->fillRect(option->rect, m_theme.panel.normal);
                drawBevel(painter, option->rect, false, m_theme);
                auto stepBtn = [&](QStyle::SubControl sc, bool up, bool enabled) {
                    const QRect br = subControlRect(CC_SpinBox, spin, sc, widget);
                    if (!br.isValid()) {
                        return;
                    }
                    const bool sunken = (spin->activeSubControls & sc)
                                        && (option->state & State_Sunken);
                    painter->fillRect(br, m_theme.surface.normal);
                    drawBevel(painter, br, !sunken, m_theme);
                    drawTriangle(
                        painter,
                        QRectF(br).center(),
                        enabled ? m_theme.text.normal : m_theme.mutedText.normal,
                        up,
                        m_theme.skin == Skin::Windows ? m_theme.metrics.borderWidth : 1);
                };
                stepBtn(SC_SpinBoxUp, true, spin->stepEnabled & QAbstractSpinBox::StepUpEnabled);
                stepBtn(SC_SpinBoxDown, false, spin->stepEnabled & QAbstractSpinBox::StepDownEnabled);
                painter->restore();
                return;
            }
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            const bool focused = option->state & State_HasFocus;
            const QColor bd = focused ? m_theme.accent.normal : m_theme.border.normal;
            const qreal r = m_theme.metrics.borderRadius;
            const QRectF fr = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
            painter->setPen(QPen(bd, m_theme.metrics.borderWidth));
            painter->setBrush(m_theme.panel.normal);
            painter->drawRoundedRect(fr, r, r);
            if (spin->subControls & SC_SpinBoxUp) {
                const QRect ur = subControlRect(CC_SpinBox, spin, SC_SpinBoxUp, widget);
                const bool en = spin->stepEnabled & QAbstractSpinBox::StepUpEnabled;
                if (en && (spin->activeSubControls & SC_SpinBoxUp)
                    && (option->state & State_MouseOver)) {
                    painter->fillRect(ur, m_theme.surface.hovered);
                }
                drawTriangle(
                    painter,
                    QRectF(ur).center(),
                    en ? m_theme.text.normal : m_theme.mutedText.normal,
                    true);
            }
            if (spin->subControls & SC_SpinBoxDown) {
                const QRect dr = subControlRect(CC_SpinBox, spin, SC_SpinBoxDown, widget);
                const bool en = spin->stepEnabled & QAbstractSpinBox::StepDownEnabled;
                if (en && (spin->activeSubControls & SC_SpinBoxDown)
                    && (option->state & State_MouseOver)) {
                    painter->fillRect(dr, m_theme.surface.hovered);
                }
                drawTriangle(
                    painter,
                    QRectF(dr).center(),
                    en ? m_theme.text.normal : m_theme.mutedText.normal,
                    false);
            }
            painter->restore();
            return;
        }
    }

    if (control == CC_Slider) {
        if (const auto *sl = qstyleoption_cast<const QStyleOptionSlider *>(option)) {
            if (m_theme.skin == Skin::Bevel || m_theme.skin == Skin::Windows) {
                const QRect groove = subControlRect(CC_Slider, sl, SC_SliderGroove, widget);
                const QRect handle = subControlRect(CC_Slider, sl, SC_SliderHandle, widget);
                const bool horiz = sl->orientation == Qt::Horizontal;
                const int gh = m_theme.metrics.sliderGrooveHeight;
                const QRect gr
                    = horiz
                          ? QRect(groove.left(), groove.center().y() - gh / 2, groove.width(), gh)
                          : QRect(groove.center().x() - gh / 2, groove.top(), gh, groove.height());
                painter->save();
                painter->fillRect(gr, m_theme.panel.normal);
                drawBevel(painter, gr, false, m_theme);
                painter->fillRect(handle, m_theme.surface.normal);
                drawBevel(painter, handle, true, m_theme);
                painter->restore();
                return;
            }
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            const QRect groove = subControlRect(CC_Slider, sl, SC_SliderGroove, widget);
            const QRect handle = subControlRect(CC_Slider, sl, SC_SliderHandle, widget);
            const bool horiz = sl->orientation == Qt::Horizontal;
            const qreal gh = m_theme.metrics.sliderGrooveHeight;
            const qreal gr_r = gh / 2;
            QRectF gr
                = horiz ? QRectF(groove.left(), groove.center().y() - gh / 2, groove.width(), gh)
                        : QRectF(groove.center().x() - gh / 2, groove.top(), gh, groove.height());
            painter->setPen(Qt::NoPen);
            painter->setBrush(m_theme.panel.normal);
            painter->drawRoundedRect(gr, gr_r, gr_r);
            QRectF fill = gr;
            if (horiz) {
                fill.setRight(handle.center().x());
            } else {
                fill.setTop(handle.center().y());
            }
            painter->setBrush(m_theme.accent.normal);
            painter->drawRoundedRect(fill, gr_r, gr_r);
            const bool hov = (sl->activeSubControls & SC_SliderHandle)
                             && (option->state & State_MouseOver);
            painter->setBrush(hov ? m_theme.surface.hovered : m_theme.surface.normal);
            painter->setPen(QPen(m_theme.border.normal, 1));
            painter->drawEllipse(QRectF(handle).adjusted(1, 1, -1, -1));
            painter->restore();
            return;
        }
    }

    if (control == CC_ScrollBar) {
        if (const auto *sb = qstyleoption_cast<const QStyleOptionSlider *>(option)) {
            if (m_theme.skin == Skin::Bevel) {
                const bool horiz = sb->orientation == Qt::Horizontal;
                const QRect slider = subControlRect(CC_ScrollBar, sb, SC_ScrollBarSlider, widget);
                const QRect addLine = subControlRect(CC_ScrollBar, sb, SC_ScrollBarAddLine, widget);
                const QRect subLine = subControlRect(CC_ScrollBar, sb, SC_ScrollBarSubLine, widget);
                painter->save();
                painter->fillRect(option->rect, m_theme.background.normal);
                if (slider.isValid()) {
                    painter->fillRect(slider, m_theme.surface.normal);
                    drawBevel(painter, slider, true, m_theme);
                }
                auto arrowBtn = [&](const QRect &br, bool up, bool left, bool horizArrow) {
                    if (!br.isValid()) {
                        return;
                    }
                    painter->fillRect(br, m_theme.surface.normal);
                    drawBevel(painter, br, true, m_theme);
                    const QPointF c = QRectF(br).center();
                    const qreal s = 3;
                    QPainterPath tri;
                    if (horizArrow) {
                        if (left) {
                            tri.moveTo(c.x() + s / 2, c.y() - s);
                            tri.lineTo(c.x() + s / 2, c.y() + s);
                            tri.lineTo(c.x() - s, c.y());
                        } else {
                            tri.moveTo(c.x() - s / 2, c.y() - s);
                            tri.lineTo(c.x() - s / 2, c.y() + s);
                            tri.lineTo(c.x() + s, c.y());
                        }
                    } else {
                        if (up) {
                            tri.moveTo(c.x() - s, c.y() + s / 2);
                            tri.lineTo(c.x() + s, c.y() + s / 2);
                            tri.lineTo(c.x(), c.y() - s);
                        } else {
                            tri.moveTo(c.x() - s, c.y() - s / 2);
                            tri.lineTo(c.x() + s, c.y() - s / 2);
                            tri.lineTo(c.x(), c.y() + s);
                        }
                    }
                    tri.closeSubpath();
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(m_theme.text.normal);
                    painter->drawPath(tri);
                };
                arrowBtn(subLine, true, true, horiz);
                arrowBtn(addLine, false, false, horiz);
                painter->restore();
                return;
            }
            painter->save();
            painter->fillRect(option->rect, m_theme.panel.normal);
            const QRect handle = subControlRect(CC_ScrollBar, sb, SC_ScrollBarSlider, widget);
            if (handle.isValid()) {
                const bool act = sb->activeSubControls & SC_ScrollBarSlider;
                const QColor hc = (act && (option->state & State_Sunken)) ? m_theme.border.pressed
                                  : (act && (option->state & State_MouseOver))
                                      ? m_theme.border.hovered
                                      : m_theme.border.normal;
                QRectF hr = QRectF(handle).adjusted(2, 2, -2, -2);
                painter->setRenderHint(QPainter::Antialiasing, true);
                painter->setPen(Qt::NoPen);
                painter->setBrush(hc);
                const qreal r = qMin(hr.width(), hr.height()) / 2;
                painter->drawRoundedRect(hr, r, r);
            }
            painter->restore();
            return;
        }
    }

    QCommonStyle::drawComplexControl(control, option, painter, widget);
}

int IaitoStyle::pixelMetric(
    PixelMetric metric, const QStyleOption *option, const QWidget *widget) const
{
    if (m_theme.touch || mobilePlatform()) {
        switch (metric) {
        case PM_ToolBarIconSize:
        case PM_SmallIconSize:
        case PM_ButtonIconSize:
            return m_theme.metrics.iconSize;
        case PM_TabBarScrollButtonWidth:
            return touchControlHeight(m_theme, option);
        case PM_ScrollBarExtent:
            return qMax(28, m_theme.metrics.scrollBarThickness);
        case PM_ScrollBarSliderMin:
        case PM_HeaderDefaultSectionSizeVertical:
        case PM_MenuScrollerHeight:
            return touchItemHeight(m_theme);
        case PM_SplitterWidth:
        case PM_DockWidgetSeparatorExtent:
        case PM_DockWidgetTitleMargin:
            return m_theme.metrics.spacing;
        default:
            break;
        }
    }
    if (mobilePlatform() && (m_theme.skin == Skin::Windows || m_theme.skin == Skin::Native)) {
        switch (metric) {
        case PM_ButtonMargin:
            return 6;
        case PM_DefaultFrameWidth:
        case PM_SpinBoxFrameWidth:
        case PM_ComboBoxFrameWidth:
            return 2;
        default:
            break;
        }
    }
    if (metric == PM_TabBarTabHSpace && isDockTabBar(widget)) {
        return m_theme.skin == Skin::Windows ? 3 * m_theme.metrics.spacing : 20;
    }
    if (m_theme.skin == Skin::Windows) {
        switch (metric) {
        case PM_DefaultFrameWidth:
        case PM_SpinBoxFrameWidth:
        case PM_ComboBoxFrameWidth:
        case PM_MenuPanelWidth:
            return 2 * m_theme.metrics.borderWidth;
        case PM_ButtonDefaultIndicator:
            return m_theme.metrics.borderWidth;
        case PM_ScrollBarExtent:
            return m_theme.metrics.scrollBarThickness;
        case PM_ScrollBarSliderMin:
            return m_theme.metrics.scrollBarThickness;
        case PM_TabBarScrollButtonWidth:
            return m_theme.metrics.controlHeight;
        case PM_IndicatorWidth:
        case PM_IndicatorHeight:
        case PM_ExclusiveIndicatorWidth:
        case PM_ExclusiveIndicatorHeight:
            return m_theme.metrics.checkBoxSize;
        case PM_SmallIconSize:
        case PM_ButtonIconSize:
        case PM_ToolBarIconSize:
        case PM_TabBarIconSize:
            return m_theme.metrics.iconSize;
        case PM_LayoutHorizontalSpacing:
        case PM_LayoutVerticalSpacing:
        case PM_SplitterWidth:
        case PM_DockWidgetSeparatorExtent:
        case PM_TabBarTabVSpace:
            return m_theme.metrics.spacing;
        case PM_ButtonMargin:
            return m_theme.metrics.menuItemPadding;
        default:
            return QProxyStyle::pixelMetric(metric, option, widget);
        }
    }
    if (m_theme.skin == Skin::Native) {
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
    switch (metric) {
    case PM_DefaultFrameWidth:
        if (m_theme.skin == Skin::Modern && qobject_cast<const QAbstractScrollArea *>(widget)) {
            // Keep the rectangular viewport inside the rounded frame, including
            // the diagonal of each corner, so it cannot paint over the outline.
            return qMax(
                1,
                qCeil(
                    m_theme.metrics.borderRadius * (1 - qSqrt(0.5)) + m_theme.metrics.borderWidth));
        }
        break;
    case PM_ScrollBarExtent:
        return m_theme.metrics.scrollBarThickness;
    case PM_IndicatorWidth:
    case PM_IndicatorHeight:
    case PM_ExclusiveIndicatorWidth:
    case PM_ExclusiveIndicatorHeight:
        return m_theme.metrics.checkBoxSize;
    case PM_SmallIconSize:
    case PM_ButtonIconSize:
        return m_theme.metrics.iconSize;
    case PM_LayoutHorizontalSpacing:
    case PM_LayoutVerticalSpacing:
        return m_theme.metrics.spacing;
    case PM_MenuBarPanelWidth:
    case PM_MenuBarHMargin:
    case PM_MenuBarVMargin:
        return 0;
    case PM_SliderLength:
    case PM_SliderThickness:
    case PM_SliderControlThickness:
        return m_theme.metrics.checkBoxSize;
    default:
        return QCommonStyle::pixelMetric(metric, option, widget);
    }
    return QCommonStyle::pixelMetric(metric, option, widget);
}

QRect IaitoStyle::subElementRect(
    SubElement element, const QStyleOption *option, const QWidget *widget) const
{
    if (m_theme.touch && (element == SE_CheckBoxClickRect || element == SE_RadioButtonClickRect)) {
        return option->rect;
    }
    if (isDockTabBar(widget)) {
        if (const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option)) {
            if (element == SE_TabBarTabLeftButton) {
                return QRect(
                    tab->rect.left() + 2,
                    tab->rect.center().y() - tab->leftButtonSize.height() / 2,
                    tab->leftButtonSize.width(),
                    tab->leftButtonSize.height());
            }
            if (element == SE_TabBarTabText) {
                // Equal margins center the title over the whole tab, with a small
                // gap from the shared icon/close slot even when the title is elided.
                const int margin = tab->leftButtonSize.width() + 4;
                return tab->rect.adjusted(margin, 0, -margin, 0);
            }
        }
    }
    return QProxyStyle::subElementRect(element, option, widget);
}

QRect IaitoStyle::subControlRect(
    ComplexControl control,
    const QStyleOptionComplex *option,
    SubControl subControl,
    const QWidget *widget) const
{
    if (m_theme.skin == Skin::Windows && !mobilePlatform() && option) {
        const int frame = 2 * m_theme.metrics.borderWidth;
        const QRect rect = option->rect;
        const QRect inner = rect.adjusted(frame, frame, -frame, -frame);
        const int width = qMin(m_theme.metrics.iconSize, inner.width());
        QRect logical;
        if (control == CC_ComboBox) {
            if (subControl == SC_ComboBoxArrow) {
                logical = QRect(inner.right() - width + 1, inner.top(), width, inner.height());
            } else if (subControl == SC_ComboBoxEditField) {
                logical = inner.adjusted(m_theme.metrics.borderWidth, 0, -width - frame, 0);
            }
        } else if (control == CC_SpinBox) {
            const auto *spin = qstyleoption_cast<const QStyleOptionSpinBox *>(option);
            if (spin && spin->buttonSymbols != QAbstractSpinBox::NoButtons) {
                const int height = inner.height() / 2;
                if (subControl == SC_SpinBoxUp) {
                    logical = QRect(inner.right() - width + 1, inner.top(), width, height);
                } else if (subControl == SC_SpinBoxDown) {
                    logical = QRect(
                        inner.right() - width + 1,
                        inner.top() + height,
                        width,
                        inner.height() - height);
                } else if (subControl == SC_SpinBoxEditField) {
                    logical = inner.adjusted(m_theme.metrics.borderWidth, 0, -width - frame, 0);
                }
            }
        }
        if (logical.isValid()) {
            return visualRect(option->direction, rect, logical);
        }
    }
    if ((m_theme.touch || mobilePlatform()) && control == CC_ComboBox
        && subControl == SC_ComboBoxListBoxPopup) {
        QRect rect = QCommonStyle::subControlRect(control, option, subControl, widget);
        if (const auto *combo = qobject_cast<const QComboBox *>(widget)) {
            // The popup must fit its items even when the closed control is narrow
            // (toolbars, compact forms and editable combos all share this path).
            const int contents = combo->view()->sizeHintForColumn(combo->modelColumn());
            const int extra = pixelMetric(PM_ScrollBarExtent, option, widget)
                              + 2 * combo->view()->frameWidth() + 16;
            const int available = combo->screen()->availableGeometry().width();
            rect.setWidth(qMin(available, qMax(rect.width(), contents + extra)));
        }
        return rect;
    }
    if (m_theme.touch && option) {
        const QRect rect = option->rect;
        const int buttonWidth = touchControlHeight(m_theme, option);
        QRect logical;
        if (control == CC_SpinBox) {
            const auto *spin = qstyleoption_cast<const QStyleOptionSpinBox *>(option);
            if (spin && spin->buttonSymbols != QAbstractSpinBox::NoButtons) {
                // Side-by-side buttons give each step a full-height touch target.
                if (subControl == SC_SpinBoxUp) {
                    logical = QRect(
                        rect.right() - buttonWidth + 1, rect.top(), buttonWidth, rect.height());
                } else if (subControl == SC_SpinBoxDown) {
                    logical = QRect(
                        rect.right() - 2 * buttonWidth + 1, rect.top(), buttonWidth, rect.height());
                } else if (subControl == SC_SpinBoxEditField) {
                    logical = rect.adjusted(8, 2, -2 * buttonWidth, -2);
                }
            }
        } else if (control == CC_ComboBox) {
            if (subControl == SC_ComboBoxArrow) {
                logical
                    = QRect(rect.right() - buttonWidth + 1, rect.top(), buttonWidth, rect.height());
            } else if (subControl == SC_ComboBoxEditField) {
                logical = rect.adjusted(12, 2, -buttonWidth, -2);
            }
        }
        if (logical.isValid()) {
            return visualRect(option->direction, rect, logical);
        }
    }
    return QProxyStyle::subControlRect(control, option, subControl, widget);
}

QSize IaitoStyle::sizeFromContents(
    ContentsType type,
    const QStyleOption *option,
    const QSize &contentsSize,
    const QWidget *widget) const
{
    const bool native = m_theme.skin == Skin::Windows || m_theme.skin == Skin::Native;
    const bool nativeSize = native && !mobilePlatform();
    // Android's native style can impose large minimums before our own sizing runs.
    QSize s = nativeSize ? QProxyStyle::sizeFromContents(type, option, contentsSize, widget)
                         : QCommonStyle::sizeFromContents(type, option, contentsSize, widget);
    if (type == CT_TabBarTab && isDockTabBar(widget)) {
        if (const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option)) {
            // Match the symmetric icon/close margins used by SE_TabBarTabText.
            // Qt normally reserves a button margin only on the occupied side.
            const int margin = tab->leftButtonSize.width() + 4;
            s.setWidth(qMax(s.width(), tab->fontMetrics.horizontalAdvance(tab->text) + 2 * margin));
        }
    }
    if (nativeSize) {
        if (m_theme.skin == Skin::Windows) {
            switch (type) {
            case CT_ItemViewItem:
                s.setHeight(qMax(s.height(), m_theme.metrics.iconSize));
                break;
            case CT_MenuItem:
                if (const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option)) {
                    if (item->menuItemType == QStyleOptionMenuItem::Separator) {
                        break;
                    }
                }
                Q_FALLTHROUGH();
            case CT_PushButton:
            case CT_ToolButton:
            case CT_ComboBox:
            case CT_LineEdit:
            case CT_SpinBox:
            case CT_CheckBox:
            case CT_RadioButton:
            case CT_TabBarTab:
            case CT_HeaderSection:
            case CT_MenuBarItem:
                s.setHeight(qMax(s.height(), m_theme.metrics.controlHeight));
                break;
            default:
                break;
            }
        }
        return s;
    }
    switch (type) {
    case CT_PushButton:
    case CT_ComboBox:
    case CT_LineEdit:
    case CT_SpinBox:
        s.setHeight(qMax(s.height(), m_theme.metrics.controlHeight));
        break;
    case CT_MenuBarItem:
        s.setWidth(s.width() + 4 * m_theme.metrics.menuItemPadding);
        s.setHeight(qMax(s.height(), contentsSize.height() + 2 * m_theme.metrics.menuItemPadding));
        break;
    case CT_MenuItem:
        if (const auto *mi = qstyleoption_cast<const QStyleOptionMenuItem *>(option)) {
            if (mi->menuItemType != QStyleOptionMenuItem::Separator) {
                const QFontMetrics fm(mi->font);
                QString text = mi->text;
                QString shortcut;
                const int tab = text.indexOf(QLatin1Char('\t'));
                if (tab >= 0) {
                    shortcut = text.mid(tab + 1);
                    text = text.left(tab);
                }
                int w = 2 * m_theme.metrics.menuItemPadding
                        + qMax(mi->maxIconWidth, m_theme.metrics.iconSize + 8)
                        + fm.horizontalAdvance(text) + 22;
                if (!shortcut.isEmpty()) {
                    w += fm.horizontalAdvance(shortcut) + 20;
                }
                s.setWidth(qMax(s.width(), w));
            }
        }
        break;
    default:
        break;
    }
    if (m_theme.touch || mobilePlatform()) {
        const int height = touchControlHeight(m_theme, option);
        const int itemHeight = touchItemHeight(m_theme);
        switch (type) {
        case CT_SpinBox:
            s.setHeight(qMax(s.height(), height));
            if (const auto *spin = qstyleoption_cast<const QStyleOptionSpinBox *>(option)) {
                if (m_theme.touch && spin->buttonSymbols != QAbstractSpinBox::NoButtons) {
                    s.rwidth() += 2 * height;
                }
            }
            break;
        case CT_ComboBox:
            s.setHeight(qMax(s.height(), height));
            if (m_theme.touch) {
                s.rwidth() += height;
            }
            break;
        case CT_MenuItem:
            if (const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option)) {
                if (item->menuItemType == QStyleOptionMenuItem::Separator) {
                    break;
                }
            }
            s = s.expandedTo(QSize(itemHeight, itemHeight));
            break;
        case CT_ToolButton:
        case CT_TabBarTab:
        case CT_PushButton:
        case CT_LineEdit:
        case CT_CheckBox:
        case CT_RadioButton:
            s = s.expandedTo(QSize(height, height));
            break;
        case CT_ItemViewItem:
        case CT_HeaderSection:
        case CT_MenuBarItem:
            s = s.expandedTo(QSize(itemHeight, itemHeight));
            break;
        default:
            break;
        }
    }
    return s;
}

void IaitoStyle::polish(QWidget *widget)
{
    if (m_theme.touch || mobilePlatform() || TouchTransformController::hasTouchScreen()) {
        if (auto *tabs = qobject_cast<QTabBar *>(widget)) {
            if (!tabs->findChild<TouchTabBarController *>()) {
                new TouchTabBarController(tabs);
            }
        }
        if (auto *area = qobject_cast<QAbstractScrollArea *>(widget)) {
            if (!qobject_cast<QHeaderView *>(area)) {
                area->installEventFilter(this);
                if (area->isVisible() && !area->viewport()->findChild<TouchScrollController *>()) {
                    new TouchScrollController(area);
                }
            }
        }
    }
    if ((m_theme.windowBorder.isValid() || qobject_cast<QDialog *>(widget)) && widget->isWindow()
        && (widget->windowType() == Qt::Window || widget->windowType() == Qt::Dialog
            || widget->windowType() == Qt::Tool)) {
        if (!widget->findChild<QWidget *>(
                QStringLiteral("iaitoWindowOutline"), Qt::FindDirectChildrenOnly)) {
            new WindowOutline(widget);
        }
    }
    if (m_theme.skin == Skin::Windows || m_theme.skin == Skin::Native) {
        QProxyStyle::polish(widget);
        return;
    }
    QCommonStyle::polish(widget);
    if (qobject_cast<QAbstractButton *>(widget) || qobject_cast<QLineEdit *>(widget)
        || qobject_cast<QComboBox *>(widget) || qobject_cast<QTabBar *>(widget)
        || qobject_cast<QAbstractSlider *>(widget) || qobject_cast<QAbstractSpinBox *>(widget)
        || qobject_cast<QHeaderView *>(widget) || qobject_cast<QSplitterHandle *>(widget)) {
        widget->setAttribute(Qt::WA_Hover, true);
    }
    if (auto *view = qobject_cast<QAbstractItemView *>(widget)) {
        view->viewport()->setAttribute(Qt::WA_Hover, true);
    }
}

void IaitoStyle::unpolish(QWidget *widget)
{
    widget->removeEventFilter(this);
    delete widget->findChild<TouchTabBarController *>(QString(), Qt::FindDirectChildrenOnly);
    if (auto *area = qobject_cast<QAbstractScrollArea *>(widget)) {
        delete area->viewport()
            ->findChild<TouchScrollController *>(QString(), Qt::FindDirectChildrenOnly);
    }
    QProxyStyle::unpolish(widget);
}

bool IaitoStyle::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Show
        && (m_theme.touch || mobilePlatform() || TouchTransformController::hasTouchScreen())) {
        if (auto *area = qobject_cast<QAbstractScrollArea *>(watched)) {
            // Wait for a real pane or popup to appear. Qt keeps some hidden helper
            // widgets alive past QApplication, when its gesture manager is gone.
            if (!area->viewport()->findChild<TouchScrollController *>()) {
                new TouchScrollController(area);
            }
        }
    }
    return QProxyStyle::eventFilter(watched, event);
}
