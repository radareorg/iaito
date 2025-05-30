#ifndef CUSTOMCOMMANDWIDGET_H
#define CUSTOMCOMMANDWIDGET_H

#include "common/CommandTask.h"
#include "widgets/IaitoDockWidget.h"
#include <memory>
#include <QSharedPointer>
#include <QString>
#include <QTimer>

namespace Ui {
class CustomCommandWidget;
}
class RefreshDeferrer;
class MainWindow;

class CustomCommandWidget : public IaitoDockWidget
{
    Q_OBJECT

public:
    explicit CustomCommandWidget(MainWindow *main);
    ~CustomCommandWidget() override;

private slots:
    void runCommand();
    void onCommandFinished(const QString &result);
    void setWrap(bool wrap);
    void onIntervalChanged(int index);

private:
    std::unique_ptr<Ui::CustomCommandWidget> ui;
    QSharedPointer<CommandTask> commandTask;
    RefreshDeferrer *refreshDeferrer;
    QTimer *autoRunTimer;
};

#endif // CUSTOMCOMMANDWIDGET_H