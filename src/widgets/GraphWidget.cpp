#include "GraphWidget.h"
#include "DisassemblerGraphView.h"
#include "common/ShortcutManager.h"
#include "core/MainWindow.h"
#include <QShortcut>
#include <QVBoxLayout>

GraphWidget::GraphWidget(MainWindow *main)
    : MemoryDockWidget(MemoryWidgetType::Graph, main)
{
    setObjectName(main ? main->getUniqueObjectName(getWidgetType()) : getWidgetType());

    setAllowedAreas(Qt::AllDockWidgetAreas);

    auto *layoutWidget = new QWidget(this);
    setWidget(layoutWidget);
    auto *layout = new QVBoxLayout(layoutWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layoutWidget->setLayout(layout);

    header = new QLineEdit(this);
    header->setReadOnly(true);
    layout->addWidget(header);

    graphView = new DisassemblerGraphView(layoutWidget, seekable, main, {&syncAction});
    layout->addWidget(graphView);

    // Title needs to get set after graphView is defined
    updateWindowTitle();

    QShortcut *toggle_shortcut = ShortcutMgr()->registerShortcut("widget.toggleGraph", main);
    connect(toggle_shortcut, &QShortcut::activated, this, [this]() { toggleDockWidget(true); });

    connect(
        graphView, &DisassemblerGraphView::nameChanged, this, &MemoryDockWidget::updateWindowTitle);

    connect(this, &QDockWidget::visibilityChanged, this, [this, main](bool visibility) {
        main->toggleOverview(visibility, this);
        if (visibility) {
            graphView->onSeekChanged(Core()->getOffset());
        }
    });

    QAction *switchAction = new QAction(this);
    ShortcutMgr()->bindAction("graph.switchView", switchAction);
    switchAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(switchAction);
    connect(switchAction, &QAction::triggered, this, [this] {
        mainWindow->showMemoryWidget(MemoryWidgetType::Disassembly);
    });

    connect(graphView, &DisassemblerGraphView::graphMoved, this, [this, main]() {
        main->toggleOverview(true, this);
    });
    connect(seekable, &IaitoSeekable::seekableSeekChanged, this, &GraphWidget::prepareHeader);
    connect(Core(), &IaitoCore::functionRenamed, this, &GraphWidget::prepareHeader);
    graphView->installEventFilter(this);
}

QWidget *GraphWidget::widgetToFocusOnRaise()
{
    return graphView;
}

void GraphWidget::closeEvent(QCloseEvent *event)
{
    IaitoDockWidget::closeEvent(event);
    emit graphClosed();
}

QString GraphWidget::getWindowTitle() const
{
    return graphView->windowTitle;
}

DisassemblerGraphView *GraphWidget::getGraphView() const
{
    return graphView;
}

QString GraphWidget::getWidgetType()
{
    return "Graph";
}

void GraphWidget::prepareHeader()
{
    QString afcf = Core()->cmdRawAt("afcf", seekable->getOffset()).trimmed();
    if (afcf.isEmpty()) {
        header->hide();
        return;
    }
    header->show();
    header->setText(afcf);
}
