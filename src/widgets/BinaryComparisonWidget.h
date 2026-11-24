#ifndef BINARYCOMPARISONWIDGET_H
#define BINARYCOMPARISONWIDGET_H

#include <QtGlobal>

#if QT_CONFIG(process)
#include "IaitoDockWidget.h"
#include "core/Iaito.h"

#include <functional>
#include <memory>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcess>

namespace Ui {
class BinaryComparisonWidget;
}

class ComparisonGraphView;
class QTreeWidgetItem;

class BinaryComparisonWidget : public IaitoDockWidget
{
    Q_OBJECT

public:
    explicit BinaryComparisonWidget(MainWindow *main);
    ~BinaryComparisonWidget() override;
    void openComparison(bool diskImages);

private:
    void reset();
    void browse(bool left);
    void compare();
    void showMatches(const QJsonArray &data);
    void showRanges(const QJsonArray &data);
    void inspect(QTreeWidgetItem *item);
    void showFunction(const QJsonObject &data);
    void showDiskPage();
    void updateHexFilter();
    void generatePatch();
    void savePatch();
    void clearPatch();
    void importNames();
    void updateControls();
    void runDiff(
        const QStringList &args,
        const QString &message,
        std::function<void(const QJsonDocument &)> receive);
    void runDiffOutput(
        const QStringList &args,
        const QString &message,
        std::function<bool(const QByteArray &)> receive);

    std::unique_ptr<Ui::BinaryComparisonWidget> ui;
    MainWindow *main;
    QProcess process;
    std::function<bool(const QByteArray &)> receive;
    QByteArray patchData;
    ComparisonGraphView *leftGraph;
    ComparisonGraphView *rightGraph;
    QFileInfo leftFile;
    QFileInfo rightFile;
    RVA rangeStart = 0;
    RVA rangeEnd = 0;
    RVA page = 0;
    bool diskImages = false;
    bool cancelled = false;
};

#endif
#endif
