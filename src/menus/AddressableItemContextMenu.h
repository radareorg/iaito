#ifndef ADDRESSABLEITEMCONTEXTMENU_H
#define ADDRESSABLEITEMCONTEXTMENU_H

#include "core/Iaito.h"
#include <QColor>
#include <QIcon>
#include <QKeySequence>
#include <QMenu>

class MainWindow;
class QAction;

class IAITO_EXPORT AddressableItemContextMenu : public QMenu
{
    Q_OBJECT

public:
    enum class MenuIcon {
        Navigation,
        Copy,
        XRefs,
        Comment,
        Rename,
        Delete,
        Color,
        Pin,
    };

    AddressableItemContextMenu(QWidget *parent, MainWindow *mainWindow);
    ~AddressableItemContextMenu();

    void setActionIcon(QAction *action, MenuIcon icon, const QColor &color);
    void setMenuIcon(QMenu *menu, MenuIcon icon, const QColor &color);

    /**
     * @brief Configure if addressable item refers to whole function or specific
     * address
     * @param wholeFunciton
     */
    void setWholeFunction(bool wholeFunciton);
public slots:
    void setOffset(RVA offset);
    void setTarget(RVA offset, QString name = QString());
    void clearTarget();
signals:
    void xrefsTriggered();

private:
    QIcon makeMenuIcon(MenuIcon icon, const QColor &color) const;

    void onActionCopyAddress();
    void onActionShowXrefs();
    void onActionAddComment();

    virtual void aboutToShowSlot();

    QMenu *pluginMenu;
    QAction *pluginMenuAction;
    MainWindow *mainWindow;

    RVA offset;
    bool hasTarget = false;

protected:
    void setHasTarget(bool hasTarget);
    QAction *actionShowInMenu;
    QAction *actionCopyAddress;
    QAction *actionShowXrefs;
    QAction *actionAddcomment;

    QString name;
    bool wholeFunction = false;
};
#endif // ADDRESSABLEITEMCONTEXTMENU_H
