#ifndef TOUCHTABBARCONTROLLER_H
#define TOUCHTABBARCONTROLLER_H

#include <QObject>
#include <QPointer>
#include <QTabBar>
#include <QTimer>

class TouchTabBarController : public QObject
{
    Q_OBJECT
public:
    explicit TouchTabBarController(QTabBar *tabs);
    ~TouchTabBarController() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QPointer<QTabBar> tabs;
    QTimer holdTimer;
    QPointF start;
    qreal lastStep = 0;
    bool moved = false;
    bool held = false;
    bool acceptedTouch = false;
};

#endif
