#ifndef COLUMNHEADERMENU_H
#define COLUMNHEADERMENU_H

#include <QObject>

// Apply column controls to all table and tree headers, including plugin views.
class ColumnHeaderMenu : public QObject
{
    Q_OBJECT

public:
    explicit ColumnHeaderMenu(QObject *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};

#endif // COLUMNHEADERMENU_H
