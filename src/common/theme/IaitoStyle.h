#ifndef IAITO_STYLE_H
#define IAITO_STYLE_H

#include "Theme.h"

#include <QCommonStyle>
#include <QList>
#include <QPointer>

class QTimer;

class IaitoStyle : public QCommonStyle
{
    Q_OBJECT

public:
    explicit IaitoStyle();

    static IaitoStyle *instance();

    void setTheme(const Theme &theme);
    const Theme &theme() const { return m_theme; }

    int styleHint(
        StyleHint hint,
        const QStyleOption *option = nullptr,
        const QWidget *widget = nullptr,
        QStyleHintReturn *returnData = nullptr) const override;

    void drawPrimitive(
        PrimitiveElement element,
        const QStyleOption *option,
        QPainter *painter,
        const QWidget *widget = nullptr) const override;

    void drawControl(
        ControlElement element,
        const QStyleOption *option,
        QPainter *painter,
        const QWidget *widget = nullptr) const override;

    void drawComplexControl(
        ComplexControl control,
        const QStyleOptionComplex *option,
        QPainter *painter,
        const QWidget *widget = nullptr) const override;

    int pixelMetric(
        PixelMetric metric,
        const QStyleOption *option = nullptr,
        const QWidget *widget = nullptr) const override;

    QSize sizeFromContents(
        ContentsType type,
        const QStyleOption *option,
        const QSize &contentsSize,
        const QWidget *widget = nullptr) const override;

    void polish(QWidget *widget) override;
    void unpolish(QWidget *widget) override;

private:
    static IaitoStyle *s_instance;
    Theme m_theme;
    QTimer *m_busyTimer;
    mutable QList<QPointer<QWidget>> m_busyBars;
    int m_busyPhase = 0;
};

#endif
