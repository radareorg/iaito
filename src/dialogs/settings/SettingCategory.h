#ifndef SETTINGCATEGORY_H
#define SETTINGCATEGORY_H

#include <QStackedWidget>
#include <QString>
#include <QTreeWidget>

class SettingCategory
{
public:
    SettingCategory(const QString &name, const QIcon &icon);
    SettingCategory(const QString &name, QWidget *widget, const QIcon &icon);
    SettingCategory(
        const QString &name,
        QWidget *widget,
        const QIcon &icon,
        const QList<SettingCategory> &children);
    SettingCategory(const QString &name, const QIcon &icon, const QList<SettingCategory> &children);

    void addItem(QTreeWidget &tree, QStackedWidget &panel);
    void addItem(QTreeWidgetItem &tree, QStackedWidget &panel);

private:
    QString name;
    QIcon icon;
    QWidget *widget;
    QList<SettingCategory> children;
};

#endif // SETTINGCATEGORY_H
