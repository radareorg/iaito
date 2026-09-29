#ifndef HEXPATWIDGET_H
#define HEXPATWIDGET_H

#include "IaitoDockWidget.h"

#include <memory>

class QJsonObject;
class QTreeWidgetItem;

namespace Ui {
class HexPatWidget;
}

class HexPatWidget : public IaitoDockWidget
{
    Q_OBJECT

public:
    explicit HexPatWidget(MainWindow *main);
    ~HexPatWidget() override;

public slots:
    void updateAvailability();

private slots:
    void openPattern();
    void evaluatePattern();
    void filterPatterns();

private:
    std::unique_ptr<Ui::HexPatWidget> ui;
    bool available = false;

    void addPattern(QTreeWidgetItem *parent, const QJsonObject &pattern);
    void seekToPattern(QTreeWidgetItem *item);
};

#endif // HEXPATWIDGET_H
