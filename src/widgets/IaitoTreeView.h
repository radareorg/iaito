#ifndef IAITOTREEVIEW_H
#define IAITOTREEVIEW_H

#include "core/IaitoCommon.h"

#include <memory>
#include <QAbstractItemView>
#include <QTreeView>

namespace Ui {
class IaitoTreeView;
}

class IAITO_EXPORT IaitoTreeView : public QTreeView
{
    Q_OBJECT

public:
    explicit IaitoTreeView(QWidget *parent = nullptr);
    ~IaitoTreeView();

private:
    std::unique_ptr<Ui::IaitoTreeView> ui;
};

#endif // IAITOTREEVIEW_H
