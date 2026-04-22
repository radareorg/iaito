#include "SettingCategory.h"

SettingCategory::SettingCategory(const QString &name, const QIcon &icon)
    : name(name)
    , icon(icon)
    , widget(nullptr)
    , children{}
{}

SettingCategory::SettingCategory(const QString &name, QWidget *widget, const QIcon &icon)
    : name(name)
    , icon(icon)
    , widget(widget)
    , children{}
{}

SettingCategory::SettingCategory(
    const QString &name,
    QWidget *widget,
    const QIcon &icon,
    const QList<SettingCategory> &children)
    : name(name)
    , icon(icon)
    , widget(widget)
    , children(children)
{}

SettingCategory::SettingCategory(
    const QString &name, const QIcon &icon, const QList<SettingCategory> &children)
    : name(name)
    , icon(icon)
    , widget(nullptr)
    , children(children)
{}

void SettingCategory::addItem(QTreeWidget &tree, QStackedWidget &panel)
{
    QTreeWidgetItem *w = new QTreeWidgetItem({name});

    tree.addTopLevelItem(w);
    for (auto &c : children)
        c.addItem(*w, panel);

    w->setExpanded(true);
    w->setIcon(0, icon);

    if (widget) {
        panel.addWidget(widget);
        w->setData(0, Qt::UserRole, panel.count());
    }
}

void SettingCategory::addItem(QTreeWidgetItem &tree, QStackedWidget &panel)
{
    QTreeWidgetItem *w = new QTreeWidgetItem({name});

    tree.addChild(w);
    for (auto &c : children)
        c.addItem(*w, panel);

    w->setExpanded(true);
    w->setIcon(0, icon);

    if (widget) {
        panel.addWidget(widget);
        w->setData(0, Qt::UserRole, panel.count());
    }
}
