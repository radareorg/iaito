#ifndef EXCEPTIONBAR_H
#define EXCEPTIONBAR_H

#include "common/ExceptionInfo.h"
#include <QWidget>

class QToolButton;
class QMenu;

// Shared, read-only exception navigator for code views.
class ExceptionBar : public QWidget
{
    Q_OBJECT
public:
    explicit ExceptionBar(QWidget *parent = nullptr);
    void setRegions(const QList<ExceptionRegion> &regions, quint64 address);
    void setAddress(quint64 address);
    void populateMenu(QMenu *menu, quint64 address, quint64 size = 1);

signals:
    void seekRequested(quint64 address);

private:
    QList<ExceptionRegion> regions;
    QToolButton *navigator;
    void addRegion(QMenu *menu, const ExceptionRegion &region);
};

#endif
