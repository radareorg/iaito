#ifndef CLICKABLESVGWIDGET_H
#define CLICKABLESVGWIDGET_H

#include <QMouseEvent>
#include <QSvgWidget>

class ClickableSvgWidget : public QSvgWidget
{
    Q_OBJECT

public:
    explicit ClickableSvgWidget(QWidget *parent = nullptr)
        : QSvgWidget(parent)
    {}

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            emit clicked();
        }
        // Call the base class implementation to ensure proper event handling
        QSvgWidget::mousePressEvent(event);
    }
};

#endif // CLICKABLESVGWIDGET_H
