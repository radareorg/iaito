#ifndef ADDRESSABLE_DOCK_WIDGET_H
#define ADDRESSABLE_DOCK_WIDGET_H

#include "IaitoDockWidget.h"
#include "core/Iaito.h"

#include <QAction>

class IaitoSeekable;
class QLabel;
class QToolBar;

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
    void updateSyncActionText();

protected:
    IaitoSeekable *seekable = nullptr;
    QAction syncAction;
    QMenu *dockMenu = nullptr;

    void setupSeekHistoryBar(bool alwaysVisible = false);
    void setSeekHistoryBarEnabled(bool enabled);

    virtual QString getWindowTitle() const = 0;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    QToolBar *seekHistoryBar = nullptr;
    QLabel *seekAddressLabel = nullptr;
    QAction *seekBackAction = nullptr;
    QAction *seekForwardAction = nullptr;
    bool historyBarAlwaysVisible = false;
    bool historyBarEnabled = true;

    void updateSeekHistoryBar();
};

#endif // ADDRESSABLE_DOCK_WIDGET_H
