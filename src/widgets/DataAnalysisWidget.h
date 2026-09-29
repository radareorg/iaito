#ifndef DATAANALYSISWIDGET_H
#define DATAANALYSISWIDGET_H

#include "AddressableDockWidget.h"
#include "DataAnalysisView.h"

#include <memory>
#include <QTimer>

namespace Ui {
class DataAnalysisWidget;
}
class QMenu;

class DataAnalysisWidget : public AddressableDockWidget
{
    Q_OBJECT

public:
    explicit DataAnalysisWidget(MainWindow *main);
    ~DataAnalysisWidget() override;
    QVariantMap serializeViewProprties() override;
    void deserializeViewProperties(const QVariantMap &properties) override;

    // Reusable entry point for other widgets. Both endpoints are inclusive.
    void showRange(RVA start, RVA end);

protected:
    QString getWindowTitle() const override { return tr("Data Analysis"); }
    QWidget *widgetToFocusOnRaise() override;

private:
    std::unique_ptr<Ui::DataAnalysisWidget> ui;
    DataAnalysisView *plot;
    DataAnalysisView *overview;
    RefreshDeferrer *refreshDeferrer;
    QTimer reloadTimer;
    QTimer sampleTimer;
    QTimer rangeTimer;
    QVector<DataAnalysis::Range> readable;
    QVector<DataAnalysis::Sample> pending;
    DataAnalysis::Range pendingRange;
    DataAnalysis::Range requestedRange;
    int nextBucket = 0;
    int resolution = 1024;
    int colorMode = 3;
    bool samplingOverview = false;
    bool virtualAddresses = true;
    bool metadataDirty = true;
    bool publishingSelection = false;
    QAction *followAction;
    QAction *tracksAction;
    QAction *selectionAction;
    QAction *exportAction;
    QMenu *optionsMenu;
    std::array<QAction *, DataAnalysis::Count> metricActions;
    std::array<QAction *, DataAnalysis::Count> legendActions;
    std::array<QColor, DataAnalysis::Count> customColors;

    void setupActions();
    void scheduleReload();
    void reload();
    void loadMetadata();
    void beginSampling(bool mini);
    void sampleBatch();
    void setRange(DataAnalysis::Range range);
    void updateLabels();
    void updateColors();
    void updateMetrics();
    void configureMetric(int metric);
    void onSeekChanged(RVA address);
    void selectRange(DataAnalysis::Range range, bool publish);
    void fitSelection();
    void editRange();
    void fitMarker(DataAnalysisView::MarkerType type);
    void findMarker();
    void showContextMenu(RVA address, QPoint position);
    void exportCsv();
};

#endif
