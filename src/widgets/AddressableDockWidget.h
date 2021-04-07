#ifndef ADDRESSABLE_DOCK_WIDGET_H
#define ADDRESSABLE_DOCK_WIDGET_H

#include "IaitoDockWidget.h"
#include "core/Iaito.h"

#include <QAction>

class IaitoSeekable;

class AddressableDockWidget : public IaitoDockWidget
{
    Q_OBJECT
public:
    AddressableDockWidget(MainWindow *parent);
    ~AddressableDockWidget() override {}

    IaitoSeekable *getSeekable() const;

    QVariantMap serializeViewProprties() override;
    void deserializeViewProperties(const QVariantMap &properties) override;
public slots:
    void updateWindowTitle();

protected:
    IaitoSeekable *seekable = nullptr;
    QAction syncAction;
    QMenu *dockMenu = nullptr;

    virtual QString getWindowTitle() const = 0;
    void contextMenuEvent(QContextMenuEvent *event) override;
};

#endif // ADDRESSABLE_DOCK_WIDGET_H
