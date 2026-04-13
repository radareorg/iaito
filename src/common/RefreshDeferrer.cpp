
#include "RefreshDeferrer.h"
#include "widgets/IaitoDockWidget.h"

RefreshDeferrer::RefreshDeferrer(
    IaitoDockWidget *dockWidget, RefreshDeferrerAccumulator *acc, QObject *parent)
    : QObject(parent)
    , dockWidget(dockWidget)
    , acc(acc)
{
    Q_ASSERT(dockWidget);
    connect(dockWidget, &IaitoDockWidget::becameVisibleToUser, this, [this]() {
        if (dirty) {
            emit refreshNow(this->acc ? this->acc->result() : nullptr);
            if (this->acc) {
                this->acc->clear();
            }
            dirty = false;
        }
    });
}

RefreshDeferrer::~RefreshDeferrer()
{
    delete acc;
}

bool RefreshDeferrer::attemptRefresh(RefreshDeferrerParams params)
{
    if (dockWidget->isVisibleToUser()) {
        if (acc) {
            acc->ignoreParams(params);
        }
        return true;
    } else {
        dirty = true;
        if (acc) {
            acc->accumulate(params);
        }
        return false;
    }
}
