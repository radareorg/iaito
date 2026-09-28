
#ifndef QUICKFILTERVIEW_H
#define QUICKFILTERVIEW_H

#include "core/IaitoCommon.h"

#include <memory>

#include <QPointer>
#include <QTimer>
#include <QWidget>

namespace Ui {
class QuickFilterView;
}

class QAction;
class AddressableFilterProxyModel;
class QAbstractItemModel;

class IAITO_EXPORT QuickFilterView : public QWidget
{
    Q_OBJECT

public:
    explicit QuickFilterView(QWidget *parent = nullptr, bool defaultOn = true);
    ~QuickFilterView();
    void addActionButton(QAction *action);
    void setTypeFilter(AddressableFilterProxyModel *model, int column);

public slots:
    void showFilter();
    void closeFilter();
    void clearFilter();

signals:
    void filterTextChanged(const QString &text);
    void filterTypeChanged(const QVariant &type);
    void filterClosed();

private:
    std::unique_ptr<Ui::QuickFilterView> ui;
    bool hasActions = false;
    QTimer filterTimer;
    int typeColumn = -1;
    QPointer<AddressableFilterProxyModel> typeModel;
    QPointer<QAbstractItemModel> typeSource;
    QList<QMetaObject::Connection> typeConnections;
    QMetaObject::Connection sourceChangedConnection;
    QMetaObject::Connection modelDestroyedConnection;

    void bindTypeSource();
    void updateTypes();
};

#endif // QUICKFILTERVIEW_H
