#pragma once

#include <memory>
#include <QEvent>
#include <QGridLayout>
#include <QJsonObject>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>

#include "IaitoDockWidget.h"
#include "core/Iaito.h"
#include "menus/AddressableItemContextMenu.h"

class MainWindow;

namespace Ui {
class RegistersWidget;
}

class RegistersWidget : public IaitoDockWidget
{
    Q_OBJECT

public:
    explicit RegistersWidget(MainWindow *main);
    ~RegistersWidget();

private slots:
    void updateContents();
    void setRegisterGrid();
    void openContextMenu(QPoint point, QString address);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    std::unique_ptr<Ui::RegistersWidget> ui;
    QGridLayout *registerLayout = new QGridLayout;
    AddressableItemContextMenu addressContextMenu;
    int numCols = 2;
    int registerLen = 0;
    RefreshDeferrer *refreshDeferrer;
    // Cache previous register values to detect changes and avoid redundant updates
    QHash<QString, QString> previousValues;
    // Map register labels to their value widgets for click-to-seek
    QHash<QObject *, QLineEdit *> labelToValue;

    void setTooltipStylesheet();
    QString buildRichTooltip(RVA address);
};
