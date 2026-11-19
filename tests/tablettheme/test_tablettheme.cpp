#include "common/Configuration.h"
#include "common/theme/IaitoStyle.h"
#include "common/theme/TouchScrollController.h"
#include "core/MainWindow.h"
#include "dialogs/settings/SettingsDialog.h"
#include "widgets/AddressScrollBar.h"
#include "widgets/DataAnalysisView.h"
#include "widgets/DecompilerWidget.h"
#include "widgets/DisassemblyWidget.h"
#include "widgets/GraphView.h"
#include "widgets/HexWidget.h"
#include "widgets/HexdumpWidget.h"
#include "widgets/ZoomWidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QLineF>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QScroller>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStyleOption>
#include <QTabBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

class TabletGraph : public GraphView
{
public:
    TabletGraph()
        : GraphView(nullptr)
    {
        width = height = 2000;
    }
    void drawBlock(QPainter &, GraphBlock &, bool) override {}
};

class TabletThemeTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QPointingDevice *device = QTest::createTouchDevice();
#else
    QTouchDevice *device = QTest::createTouchDevice();
#endif

    static void selectTheme(const QString &name)
    {
        const auto &themes = Configuration::iaitoInterfaceThemesList();
        for (int i = 0; i < themes.size(); ++i) {
            if (themes.at(i).name == name) {
                Config()->setInterfaceTheme(i);
                return;
            }
        }
        QFAIL("Theme is missing from Appearance settings");
    }

    static void populate(QListWidget &list)
    {
        for (int i = 0; i < 100; ++i) {
            list.addItem(QStringLiteral("Function %1").arg(i));
        }
        list.resize(400, 400);
        list.show();
        QVERIFY(QTest::qWaitForWindowExposed(&list));
    }

    void swipe(QWidget *viewport, const QPoint &delta)
    {
        const QPoint start = viewport->rect().center();
        QTest::touchEvent(viewport, device).press(0, start, viewport);
        for (int step = 1; step <= 6; ++step) {
            QTest::qWait(20);
            QTest::touchEvent(viewport, device).move(0, start + delta * step / 6, viewport);
        }
        QTest::touchEvent(viewport, device).release(0, start + delta, viewport);
    }

    void pinch(QWidget *viewport, qreal scale, const QPoint &pan = QPoint())
    {
        const QPoint center = viewport->rect().center();
        const QPoint first = center - QPoint(40, 0);
        const QPoint second = center + QPoint(40, 0);
        QTest::touchEvent(viewport, device).press(0, first, viewport).press(1, second, viewport);
        for (int step = 1; step <= 8; ++step) {
            QTest::qWait(25);
            const int radius = qRound(40 * (1 + (scale - 1) * step / 8));
            const QPoint movingCenter = center + pan * step / 8;
            QTest::touchEvent(viewport, device)
                .move(0, movingCenter - QPoint(radius, 0), viewport)
                .move(1, movingCenter + QPoint(radius, 0), viewport);
        }
        const int radius = qRound(40 * scale);
        QTest::touchEvent(viewport, device)
            .release(0, center + pan - QPoint(radius, 0), viewport)
            .release(1, center + pan + QPoint(radius, 0), viewport);
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("tablettheme");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        qApp->setStyle(new IaitoStyle);
        Core()->initialize(false);
        Config()->loadInitial();
        Config()->setOutputRedirectionEnabled(false);
    }

    void init() { selectTheme("Tablet"); }
    void cleanup()
    {
        qunsetenv("IAITO_MOBILE_UI");
        selectTheme("Dark");
    }

    void mobileTargetsAcrossThemes()
    {
        qputenv("IAITO_MOBILE_UI", "1");
        for (const QString &theme : {QString("Native"), QString("Dark")}) {
            selectTheme(theme);
            QLineEdit address;
            QComboBox selector;
            selector.addItem("Functions");
            QPushButton open("Open");
            for (QWidget *widget : QList<QWidget *>{&address, &selector, &open}) {
                widget->ensurePolished();
                QVERIFY(widget->sizeHint().height() >= 44);
            }
            QListWidget list;
            populate(list);
            QVERIFY(list.visualItemRect(list.item(0)).height() >= 44);
            QTableWidget table(2, 2);
            table.ensurePolished();
            QVERIFY(table.rowHeight(0) >= 44);
            QMenu menu;
            auto *action = menu.addAction("Analyze function");
            menu.show();
            QVERIFY(QTest::qWaitForWindowExposed(&menu));
            QVERIFY(menu.actionGeometry(action).height() >= 44);
        }
    }

    void touchTargets()
    {
        QVERIFY(IaitoStyle::instance()->theme().touch);
        QVERIFY(!Configuration::interfaceThemeIsDark("Tablet"));
        QPushButton button("Open");
        QToolButton tool;
        QCheckBox check("Enable analysis");
        QComboBox combo;
        combo.addItem("Automatic");
        QSpinBox spin;
        QTabBar tabs;
        tabs.addTab("Disassembly");
        for (QWidget *widget : QList<QWidget *>{&button, &tool, &check, &combo, &spin, &tabs}) {
            widget->ensurePolished();
            QVERIFY2(widget->sizeHint().height() >= 48, widget->metaObject()->className());
        }
        spin.resize(spin.sizeHint());
        QStyleOptionSpinBox option;
        option.initFrom(&spin);
        const QRect up
            = spin.style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, &spin);
        const QRect down
            = spin.style()
                  ->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxDown, &spin);
        QVERIFY(up.width() >= 48 && up.height() >= 48);
        QVERIFY(down.width() >= 48 && down.height() >= 48);
        QVERIFY(!up.intersects(down));
        QTest::mouseClick(&spin, Qt::LeftButton, Qt::NoModifier, up.center());
        QCOMPARE(spin.value(), 1);
        QTest::mouseClick(&spin, Qt::LeftButton, Qt::NoModifier, down.center());
        QCOMPARE(spin.value(), 0);

        QListWidget list;
        populate(list);
        QVERIFY(list.visualItemRect(list.item(0)).height() >= 48);
        QTableWidget table(2, 2);
        table.ensurePolished();
        QVERIFY(table.rowHeight(0) >= 48);
        QMenu menu;
        auto *action = menu.addAction("Analyze function");
        menu.show();
        QVERIFY(QTest::qWaitForWindowExposed(&menu));
        QVERIFY(menu.actionGeometry(action).height() >= 48);
    }

    void readableComboPopups()
    {
        qputenv("IAITO_MOBILE_UI", "1");
        for (const QString &theme : {QString("Native"), QString("Light"), QString("Tablet")}) {
            selectTheme(theme);
            for (bool editable : {false, true}) {
                QComboBox combo;
                combo.setEditable(editable);
                combo.addItems({"Light", "Dark", "A longer interface theme name"});
                combo.resize(65, 48);
                combo.show();
                QVERIFY(QTest::qWaitForWindowExposed(&combo));
                combo.showPopup();
                auto *view = combo.view();
                QVERIFY(QTest::qWaitForWindowExposed(view));
                const int textWidth = combo.fontMetrics().horizontalAdvance(combo.itemText(2));
                QVERIFY(view->viewport()->width() >= textWidth);
                QVERIFY(combo.screen()->availableGeometry().contains(view->window()->geometry()));
                QVERIFY(view->visualRect(view->model()->index(0, 0)).height() >= 44);
                QSignalSpy activated(&combo, QOverload<int>::of(&QComboBox::activated));
                const QPoint position = view->visualRect(view->model()->index(1, 0)).center();
                QTest::touchEvent(view->viewport(), device).press(0, position, view->viewport());
                QTest::touchEvent(view->viewport(), device).release(0, position, view->viewport());
                QTRY_COMPARE(combo.currentIndex(), 1);
                QCOMPARE(activated.size(), 1);
                QVERIFY(!view->isVisible());
            }
        }
    }

    void touchScrollingAcrossThemes()
    {
        selectTheme("Dark");
        QListWidget list;
        populate(list);
        const bool acceptsTouch = list.viewport()->testAttribute(Qt::WA_AcceptTouchEvents);
        const auto properties = QScroller::scroller(list.viewport())->scrollerProperties();
        QScrollArea existing;
        existing.ensurePolished();
        QScroller::grabGesture(existing.viewport(), QScroller::TouchGesture);
        const auto existingGesture = QScroller::grabbedGesture(existing.viewport());
        existing.resize(300, 200);
        existing.show();
        QVERIFY(QTest::qWaitForWindowExposed(&existing));
        const auto existingProperties
            = QScroller::scroller(existing.viewport())->scrollerProperties();
        for (const auto &theme : {"Tablet", "Dark", "Native", "Tablet", "Dark"}) {
            selectTheme(theme);
            QCOMPARE(list.verticalScrollMode(), QAbstractItemView::ScrollPerPixel);
            QVERIFY(list.findChild<TouchScrollController *>());
            QVERIFY(list.findChild<TouchTransformController *>());
            QCOMPARE(list.horizontalScrollMode(), QAbstractItemView::ScrollPerPixel);
            QCOMPARE(list.viewport()->testAttribute(Qt::WA_AcceptTouchEvents), acceptsTouch);
            QVERIFY(QScroller::scroller(list.viewport())->scrollerProperties() == properties);
            QCOMPARE(QScroller::grabbedGesture(existing.viewport()), existingGesture);
            QVERIFY(
                QScroller::scroller(existing.viewport())->scrollerProperties()
                == existingProperties);
        }
        existing.hide();
        QCoreApplication::processEvents();
        swipe(list.viewport(), QPoint(0, -100));
        QTRY_VERIFY(list.verticalScrollBar()->value() > 0);
    }

    void tapAndLongPress()
    {
        QListWidget list;
        populate(list);
        list.setContextMenuPolicy(Qt::CustomContextMenu);
        QSignalSpy menus(&list, &QWidget::customContextMenuRequested);
        QSignalSpy clicks(&list, &QAbstractItemView::clicked);
        QWidget *viewport = list.viewport();
        const QPoint tap = list.visualItemRect(list.item(1)).center();
        QTest::touchEvent(viewport, device).press(0, tap, viewport);
        QTest::qWait(40);
        QTest::touchEvent(viewport, device).release(0, tap, viewport);
        QTRY_COMPARE(clicks.size(), 1);
        QCOMPARE(list.currentRow(), 1);
        const QPoint hold = list.visualItemRect(list.item(3)).center();
        QTest::touchEvent(viewport, device).press(0, hold, viewport);
        QTRY_COMPARE_WITH_TIMEOUT(menus.size(), 1, 1800);
        QCOMPARE(menus.at(0).at(0).toPoint(), hold);
        QCOMPARE(list.currentRow(), 3);
        QTest::touchEvent(viewport, device).release(0, hold, viewport);
        QTest::qWait(100);
        QCOMPARE(clicks.size(), 1);
        QCOMPARE(menus.size(), 1);
    }

    void swipeDoesNotActivateOrOpenMenu()
    {
        QListWidget list;
        populate(list);
        list.setContextMenuPolicy(Qt::CustomContextMenu);
        QSignalSpy menus(&list, &QWidget::customContextMenuRequested);
        QSignalSpy clicks(&list, &QAbstractItemView::clicked);
        QWidget *viewport = list.viewport();
        QTest::touchEvent(viewport, device).press(0, QPoint(100, 300), viewport);
        for (int y = 280; y >= 100; y -= 20) {
            QTest::qWait(20);
            QTest::touchEvent(viewport, device).move(0, QPoint(100, y), viewport);
        }
        QTRY_VERIFY(list.verticalScrollBar()->value() > 0);
        QTest::qWait(800);
        QCOMPARE(menus.size(), 0);
        QTest::touchEvent(viewport, device).release(0, QPoint(100, 100), viewport);
        QTest::qWait(100);
        QCOMPARE(clicks.size(), 0);
    }

    void overshootReturnsToRest()
    {
        QScrollArea scroll;
        auto *content = new QLabel("Scroll down");
        content->setFixedSize(400, 2000);
        scroll.setWidget(content);
        scroll.resize(450, 400);
        scroll.show();
        QVERIFY(QTest::qWaitForWindowExposed(&scroll));
        QWidget *viewport = scroll.viewport();
        const QPoint rest = viewport->pos();
        QTest::touchEvent(viewport, device).press(0, QPoint(100, 100), viewport);
        for (int y = 120; y <= 260; y += 20) {
            QTest::qWait(20);
            QTest::touchEvent(viewport, device).move(0, QPoint(100, y), viewport);
        }
        QTRY_VERIFY(viewport->pos() != rest);
        QTest::touchEvent(viewport, device).release(0, QPoint(100, 260), viewport);
        QTRY_COMPARE_WITH_TIMEOUT(viewport->pos(), rest, 3000);
        QCOMPARE(scroll.verticalScrollBar()->value(), 0);
    }

    void savedThemeKeepsTouchBehavior()
    {
        const QString path = settings.filePath("tablet-copy.theme");
        IaitoStyle::instance()->theme().save(path);
        const Theme copy = Theme::load(path, true);
        QVERIFY(copy.touch);
        QCOMPARE(copy.metrics.controlHeight, 48);
        QVERIFY(!copy.dark);
    }

    void plainTextSwipeUsesPixels()
    {
        QPlainTextEdit edit;
        edit.setPlainText(QStringLiteral("A line of code\n").repeated(1000));
        edit.resize(500, 400);
        edit.show();
        QVERIFY(QTest::qWaitForWindowExposed(&edit));
        swipe(edit.viewport(), QPoint(0, -120));
        QScroller::scroller(edit.viewport())->stop();
        QVERIFY(edit.verticalScrollBar()->value() > 0);
        QVERIFY(edit.verticalScrollBar()->value() < 20);
    }

    void graphSwipe()
    {
        TabletGraph graph;
        graph.resize(500, 400);
        graph.show();
        QVERIFY(QTest::qWaitForWindowExposed(&graph));
        graph.setViewOffset(QPoint(500, 500));
        const QPoint before = graph.getViewOffset();
        swipe(graph.viewport(), QPoint(-120, -80));
        QTRY_VERIFY(graph.getViewOffset().x() > before.x());
        QTRY_VERIFY(graph.getViewOffset().y() > before.y());
    }

    void hexSwipe()
    {
        Core()->cmd("o+ malloc://4096");
        HexWidget hex;
        hex.resize(600, 400);
        hex.show();
        QVERIFY(QTest::qWaitForWindowExposed(&hex));
        auto *bar = hex.findChild<AddressScrollBar *>();
        QVERIFY(bar);
        const int before = bar->value();
        swipe(hex.viewport(), QPoint(0, -120));
        QTRY_VERIFY(bar->value() > before);
    }

    void swipeTabs()
    {
        QTabBar tabs;
        for (int i = 0; i < 12; ++i) {
            tabs.addTab(QString("Panel %1").arg(i));
        }
        tabs.setUsesScrollButtons(true);
        tabs.resize(500, 52);
        tabs.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tabs));
        tabs.setCurrentIndex(0);
        QSignalSpy selections(&tabs, &QTabBar::currentChanged);
        const int first = tabs.tabRect(0).left();
        swipe(&tabs, QPoint(-180, 0));
        QCOMPARE(tabs.currentIndex(), 0);
        QTRY_VERIFY(tabs.tabRect(0).left() < first);
        const int scrolled = tabs.tabRect(0).left();
        swipe(&tabs, QPoint(120, 0));
        QTRY_VERIFY(tabs.tabRect(0).left() > scrolled);
        const int beforeVertical = tabs.tabRect(0).left();
        swipe(&tabs, QPoint(0, 100));
        QCOMPARE(tabs.tabRect(0).left(), beforeVertical);
        QCOMPARE(selections.size(), 0);
        QPoint target;
        for (int i = 1; i < tabs.count(); ++i) {
            if (tabs.rect().contains(tabs.tabRect(i))) {
                target = tabs.tabRect(i).center();
                break;
            }
        }
        QVERIFY(!target.isNull());
        const int tapped = tabs.tabAt(target);
        QTest::touchEvent(&tabs, device).press(0, target, &tabs);
        QTest::touchEvent(&tabs, device).release(0, target, &tabs);
        QTRY_COMPARE(tabs.currentIndex(), tapped);
    }

    void pinchTextViews()
    {
        Core()->cmd("o+ malloc://4096");
        Config()->setDecompilerAutoRefreshEnabled(false);
        MainWindow main;
        main.finalizeOpen();
        QTRY_VERIFY(main.isUiReady());
        main.showNormal();
        main.resize(1024, 768);
        auto *disasm = main.findChild<DisassemblyWidget *>();
        auto *hex = main.findChild<HexdumpWidget *>();
        auto *decompiler = main.findChild<DecompilerWidget *>();
        QVERIFY(disasm && hex && decompiler);
        for (auto *dock : QList<IaitoDockWidget *>{disasm, hex, decompiler}) {
            dock->raiseMemoryWidget();
            QCoreApplication::processEvents();
            QAbstractScrollArea *area
                = dock == hex
                      ? static_cast<QAbstractScrollArea *>(hex->findChild<HexWidget *>())
                      : static_cast<QAbstractScrollArea *>(dock->findChild<QPlainTextEdit *>());
            QVERIFY(area);
            QTRY_VERIFY(area->viewport()->isVisible());
            QVERIFY(area->property("iaitoTextZoom").toBool());
            Config()->setZoomFactor(1.0);
            pinch(area->viewport(), 2.0);
            QTRY_VERIFY(Config()->getZoomFactor() > 1.3);
            const qreal enlarged = Config()->getZoomFactor();
            pinch(area->viewport(), 0.5);
            QTRY_VERIFY(Config()->getZoomFactor() < enlarged);
            QCOMPARE(QScroller::scroller(area->viewport())->state(), QScroller::Inactive);
        }
        Config()->setZoomFactor(1.0);
    }

    void combinedGraphTransform_data()
    {
        QTest::addColumn<QString>("theme");
        for (const auto &theme : {"Tablet", "Native", "Dark"}) {
            QTest::newRow(theme) << QString(theme);
        }
    }

    void combinedGraphTransform()
    {
        QFETCH(QString, theme);
        selectTheme(theme);
        QVERIFY(!qEnvironmentVariableIsSet("IAITO_MOBILE_UI"));
        TabletGraph graph;
        graph.resize(600, 500);
        graph.move(137, 91);
        graph.show();
        QVERIFY(QTest::qWaitForWindowExposed(&graph));
        graph.setViewOffset(QPoint(500, 500));
        const QPoint center = graph.viewport()->rect().center();
        const QPointF anchor = QPointF(graph.getViewOffset()) + center;
        const QPoint pan(-60, -40);
        pinch(graph.viewport(), 2.0, pan);
        QVERIFY(graph.getViewScale() > 1.8);
        const QPointF movedAnchor = QPointF(graph.getViewOffset())
                                    + QPointF(center + pan) / graph.getViewScale();
        QVERIFY2(
            QLineF(anchor, movedAnchor).length() < 5,
            "The content under the moving pinch center must remain anchored");
        auto *transform = graph.viewport()->findChild<TouchTransformController *>();
        QVERIFY(transform && !transform->isActive());
        QCOMPARE(QScroller::scroller(graph.viewport())->state(), QScroller::Inactive);
    }

    void combinedTextTransform()
    {
        selectTheme("Native");
        QPlainTextEdit edit;
        edit.setLineWrapMode(QPlainTextEdit::NoWrap);
        edit.setPlainText((QString(200, QLatin1Char('x')) + '\n').repeated(1000));
        connect(Config(), &Configuration::fontsUpdated, &edit, [&edit]() {
            edit.setFont(Config()->getFont());
        });
        Config()->setZoomFactor(1.0);
        edit.setFont(Config()->getFont());
        TouchScrollController::enableTextZoom(&edit);
        edit.resize(650, 500);
        edit.show();
        QVERIFY(QTest::qWaitForWindowExposed(&edit));
        edit.verticalScrollBar()->setValue(100);
        edit.horizontalScrollBar()->setValue(300);
        const QPoint center = edit.viewport()->rect().center();
        const qreal line = edit.verticalScrollBar()->value()
                           + qreal(center.y()) / edit.fontMetrics().lineSpacing();
        const qreal column = qreal(edit.horizontalScrollBar()->value() + center.x())
                             / edit.fontMetrics().horizontalAdvance('M');
        const QPoint pan(-80, -100);
        pinch(edit.viewport(), 1.6, pan);
        QVERIFY(Config()->getZoomFactor() > 1.4);
        const qreal movedLine = edit.verticalScrollBar()->value()
                                + qreal((center + pan).y()) / edit.fontMetrics().lineSpacing();
        QVERIFY(qAbs(movedLine - line) < 2);
        const qreal movedColumn = qreal(edit.horizontalScrollBar()->value() + (center + pan).x())
                                  / edit.fontMetrics().horizontalAdvance('M');
        QVERIFY(qAbs(movedColumn - column) < 2);
        const int beforePan = edit.verticalScrollBar()->value();
        const qreal beforeZoom = Config()->getZoomFactor();
        pinch(edit.viewport(), 1.0, QPoint(0, -100));
        QCOMPARE(Config()->getZoomFactor(), beforeZoom);
        QVERIFY(edit.verticalScrollBar()->value() > beforePan);
        Config()->setZoomFactor(1.0);
    }

    void transformFingerTransitions()
    {
        QWidget target;
        target.resize(600, 500);
        auto *transform = TouchTransformController::install(&target);
        QSignalSpy transforms(transform, &TouchTransformController::transformed);
        target.show();
        QVERIFY(QTest::qWaitForWindowExposed(&target));
        QTest::touchEvent(&target, device).press(0, QPoint(200, 200), &target);
        QTest::touchEvent(&target, device)
            .move(0, QPoint(200, 200), &target)
            .press(1, QPoint(300, 200), &target);
        QTest::touchEvent(&target, device)
            .move(0, QPoint(170, 180), &target)
            .move(1, QPoint(330, 180), &target);
        QVERIFY(transform->isActive());
        QVERIFY(!transforms.isEmpty());
        const auto values = transforms.last();
        QVERIFY(values[0].toReal() > 1);
        QVERIFY(values[2].toPointF().y() < values[1].toPointF().y());
        QTest::touchEvent(&target, device)
            .release(0, QPoint(170, 180), &target)
            .move(1, QPoint(330, 180), &target);
        QVERIFY(transform->isActive());
        QTest::touchEvent(&target, device).move(1, QPoint(350, 210), &target);
        QCOMPARE(transforms.last()[0].toReal(), qreal(1));
        QCOMPARE(transforms.last()[2].toPointF() - transforms.last()[1].toPointF(), QPointF(20, 30));
        QTest::touchEvent(&target, device).release(1, QPoint(350, 210), &target);
        QVERIFY(!transform->isActive());
        QTest::touchEvent(&target, device)
            .press(0, QPoint(200, 200), &target)
            .press(1, QPoint(300, 200), &target);
        QTest::touchEvent(&target, device)
            .move(0, QPoint(180, 180), &target)
            .move(1, QPoint(320, 180), &target);
        QVERIFY(transform->isActive());
        QTouchEvent cancel(QEvent::TouchCancel, device);
        QApplication::sendEvent(&target, &cancel);
        QVERIFY(!transform->isActive());
        QTest::touchEvent(&target, device)
            .release(0, QPoint(180, 180), &target)
            .release(1, QPoint(320, 180), &target);
        pinch(&target, 1.0, QPoint(0, -40));
        QVERIFY(!transform->isActive());
        QCOMPARE(transforms.last()[0].toReal(), qreal(1));
    }

    void binaryZoomAndPan()
    {
        ZoomView view;
        QVector<ZoomView::BlockEntry> blocks;
        for (int i = 0; i < 4096; ++i) {
            blocks.append({RVA(i), i % 256});
        }
        view.setData(blocks);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QSignalSpy zooms(&view, &ZoomView::blocksChangeRequested);
        QSignalSpy pans(&view, &ZoomView::panRequested);
        pinch(&view, 2.0, QPoint(-60, -40));
        QVERIFY(!zooms.isEmpty());
        QPointF pan;
        for (const auto &values : pans) {
            pan += values[0].toPointF();
        }
        QCOMPARE(pan, QPointF(-60, -40));
    }

    void combinedDataTransform_data()
    {
        QTest::addColumn<bool>("overview");
        QTest::newRow("plot") << false;
        QTest::newRow("overview") << true;
    }

    void combinedDataTransform()
    {
        QFETCH(bool, overview);
        DataAnalysisView view(overview);
        view.domain = {RVA_MAX - 10000, RVA_MAX};
        const DataAnalysis::Range initial{RVA_MAX - 7000, RVA_MAX - 3000};
        view.visibleRange = initial;
        connect(&view, &DataAnalysisView::rangeRequested, &view, [&view](RVA start, RVA end) {
            view.visibleRange = {start, end};
        });
        view.resize(860, 400);
        view.move(137, 91);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        pinch(&view, 2.0);
        const auto zoomOnly = view.visibleRange;
        view.visibleRange = initial;
        pinch(&view, 2.0, QPoint(80, 0));
        QVERIFY(view.visibleRange.size() < initial.size() * 3 / 4);
        QVERIFY(qAbs(qint64(view.visibleRange.size()) - qint64(zoomOnly.size())) < 10);
        if (overview) {
            QVERIFY(view.visibleRange.start > zoomOnly.start + 100);
        } else {
            QVERIFY(view.visibleRange.start < zoomOnly.start - 100);
        }
        QVERIFY(view.visibleRange.start >= view.domain.start);
        QVERIFY(view.visibleRange.end <= view.domain.end);
    }

    void tabletWorkspace()
    {
        Core()->cmd("o+ malloc://4096");
        Core()->cmd("e asm.arch=x86");
        Core()->cmd("e asm.bits=64");
        Core()->cmd("wx " + QStringLiteral("90").repeated(4096));
        MainWindow main;
        main.finalizeOpen();
        main.showNormal();
        main.resize(1280, 800);
        QVERIFY(QTest::qWaitForWindowExposed(&main));
        QTRY_COMPARE(main.width(), 1280);
        auto *disassembly = main.findChild<DisassemblyWidget *>();
        QVERIFY(disassembly);
        disassembly->raiseMemoryWidget();
        QCoreApplication::processEvents();
        main.resize(1280, 800);
        QTRY_COMPARE(main.height(), 800);
        auto *edit = disassembly->findChild<DisassemblyTextEdit *>();
        QVERIFY(edit);
        QVERIFY(edit->findChild<TouchScrollController *>());
        auto *titleBar = disassembly->titleBarWidget();
        QVERIFY(titleBar);
        auto *close = titleBar->findChild<QToolButton *>("dockHandleCloseButton");
        QVERIFY(close);
        QCOMPARE(close->size(), QSize(48, 48));
        QVERIFY(!disassembly->getLines().isEmpty());
        const RVA initial = disassembly->getLines().first().offset;
        QWidget *viewport = edit->viewport();
        QVERIFY(viewport->isVisible());
        swipe(viewport, QPoint(0, -120));
        if (qEnvironmentVariableIsSet("IAITO_TABLET_PREVIEW")) {
            main.grab().save(qEnvironmentVariable("IAITO_TABLET_PREVIEW"));
            SettingsDialog dialog(&main);
            dialog.showSection(SettingsDialog::Section::Appearance);
            dialog.show();
            QCoreApplication::processEvents();
            dialog.grab().save(qEnvironmentVariable("IAITO_TABLET_PREVIEW") + ".settings.png");
        }
        QTRY_VERIFY(disassembly->getLines().first().offset > initial);
        selectTheme("Dark");
        QCOMPARE(QScroller::scroller(viewport)->state(), QScroller::Inactive);
        QCOMPARE(disassembly->titleBarWidget()->height(), 8);
    }
};

QTEST_MAIN(TabletThemeTests)
#include "test_tablettheme.moc"
