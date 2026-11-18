#include "common/Configuration.h"
#include "common/theme/IaitoStyle.h"
#include "common/theme/TouchScrollController.h"
#include "core/MainWindow.h"
#include "dialogs/settings/SettingsDialog.h"
#include "widgets/AddressScrollBar.h"
#include "widgets/DisassemblyWidget.h"
#include "widgets/GraphView.h"
#include "widgets/HexWidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
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

    void init() { selectTheme("GNOME Tablet"); }
    void cleanup() { selectTheme("Dark"); }

    void touchTargets()
    {
        QVERIFY(IaitoStyle::instance()->theme().touch);
        QVERIFY(!Configuration::interfaceThemeIsDark("GNOME Tablet"));
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

    void switchingRestoresScrolling()
    {
        selectTheme("Dark");
        QListWidget list;
        populate(list);
        list.setHorizontalScrollMode(QAbstractItemView::ScrollPerItem);
        list.setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
        const bool acceptsTouch = list.viewport()->testAttribute(Qt::WA_AcceptTouchEvents);
        const auto properties = QScroller::scroller(list.viewport())->scrollerProperties();
        QScrollArea existing;
        existing.ensurePolished();
        QScroller::grabGesture(existing.viewport(), QScroller::TouchGesture);
        const auto existingGesture = QScroller::grabbedGesture(existing.viewport());
        const auto existingProperties
            = QScroller::scroller(existing.viewport())->scrollerProperties();
        existing.resize(300, 200);
        existing.show();
        QVERIFY(QTest::qWaitForWindowExposed(&existing));
        for (int i = 0; i < 2; ++i) {
            selectTheme("GNOME Tablet");
            QCOMPARE(list.verticalScrollMode(), QAbstractItemView::ScrollPerPixel);
            QVERIFY(list.findChild<TouchScrollController *>());
            selectTheme("Dark");
            QVERIFY(!list.findChild<TouchScrollController *>());
            QCOMPARE(list.verticalScrollMode(), QAbstractItemView::ScrollPerItem);
            QCOMPARE(list.horizontalScrollMode(), QAbstractItemView::ScrollPerItem);
            QCOMPARE(list.viewport()->testAttribute(Qt::WA_AcceptTouchEvents), acceptsTouch);
            QVERIFY(QScroller::scroller(list.viewport())->scrollerProperties() == properties);
            QCOMPARE(QScroller::grabbedGesture(existing.viewport()), existingGesture);
            QVERIFY(
                QScroller::scroller(existing.viewport())->scrollerProperties()
                == existingProperties);
        }
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
