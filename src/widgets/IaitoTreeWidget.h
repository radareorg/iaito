#ifndef CUTTERTREEWIDGET_H
#define CUTTERTREEWIDGET_H

#include "core/IaitoCommon.h"

#include <QStatusBar>
#include <QVBoxLayout>

class MainWindow;

class IAITO_EXPORT IaitoTreeWidget : public QObject
{

    Q_OBJECT

public:
    explicit IaitoTreeWidget(QObject *parent = nullptr);
    ~IaitoTreeWidget();
    void addStatusBar(QVBoxLayout *pos);
    void showItemsNumber(int count);
    void showStatusBar(bool show);

private:
    QStatusBar *bar;

};
#endif // CUTTERTREEWIDGET_H
