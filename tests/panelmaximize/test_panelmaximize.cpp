#include "common/Configuration.h"
#include "core/DockManager.h"
#include "core/MainWindow.h"
#include "widgets/IaitoDockWidget.h"

#include <QApplication>
#include <QLineEdit>
#include <QSettings>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>

class PanelMaximizeTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;

#ifdef Q_OS_MACOS
    const Qt::KeyboardModifier panelModifier = Qt::ControlModifier;
#else
    const Qt::KeyboardModifier panelModifier = Qt::MetaModifier;
#endif

    void hideBuiltinPanels(MainWindow &main)
    {
        main.setAnimated(false);
        main.resize(1100, 700);
        for (auto *dock : main.findChildren<IaitoDockWidget *>()) {
            dock->hide();
        }
    }

    IaitoDockWidget *addPanel(MainWindow &main, const QString &name)
    {
        auto *dock = new IaitoDockWidget(&main);
        dock->setObjectName(name);
        dock->setWindowTitle(name);
        dock->setWidget(new QLineEdit(dock));
        dock->widget()->setObjectName(name + "-input");
        main.addWidget(dock);
        main.addDockWidget(Qt::TopDockWidgetArea, dock);
        return dock;
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("panelmaximize");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        Core()->initialize(false);
        qRegisterMetaType<RVA>("RVA");
        Config()->loadInitial();
        Config()->setOutputRedirectionEnabled(false);
        Config()->setDecompilerAutoRefreshEnabled(false);
    }

    void restoreTiledLayout_data()
    {
        QTest::addColumn<int>("key");
        QTest::newRow("return") << int(Qt::Key_Return);
        QTest::newRow("keypad-enter") << int(Qt::Key_Enter);
    }

    void restoreTiledLayout()
    {
        QFETCH(int, key);
        MainWindow main;
        hideBuiltinPanels(main);
        auto *target = addPanel(main, "target");
        auto *tab = addPanel(main, "tab");
        auto *side = addPanel(main, "side");
        auto *hidden = addPanel(main, "hidden");
        main.splitDockWidget(target, side, Qt::Horizontal);
        main.tabifyDockWidget(target, tab);
        hidden->hide();
        target->setMaximumSize(450, 500);
        main.show();
        QVERIFY(QTest::qWaitForWindowExposed(&main));
        target->raise();
        target->widget()->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), target->widget());
        QCoreApplication::processEvents();
        const QByteArray state = main.saveState();
        const QRect targetGeometry = target->geometry();
        const QRect sideGeometry = side->geometry();
        const QRect panelArea = targetGeometry.united(sideGeometry);
        const QSize maximumSize = target->maximumSize();

        const auto modifiers = Qt::KeyboardModifiers(panelModifier)
                               | (key == Qt::Key_Enter ? Qt::KeypadModifier : Qt::NoModifier);
        QTest::keyClick(target->widget(), Qt::Key(key), modifiers);
        QTRY_VERIFY(side->isHidden());
        QVERIFY(tab->isHidden());
        QVERIFY(hidden->isHidden());
        QTRY_COMPARE(target->x(), panelArea.x());
        QTRY_COMPARE(target->width(), panelArea.width());
        QVERIFY(target->height() > targetGeometry.height());
        QCOMPARE(QApplication::focusWidget(), target->widget());
        auto *manager = main.findChild<DockManager *>();
        QVERIFY(manager);
        QCOMPARE(manager->saveState(), state);

        // Saving while expanded must persist the original tiled layout.
        main.saveSettings();
        QSettings saved;
        const int count = saved.beginReadArray("layouts");
        QVERIFY(count > 0);
        saved.setArrayIndex(0);
        QCOMPARE(saved.value("state").toByteArray(), state);
        saved.endArray();

        QTest::keyClick(target->widget(), Qt::Key_Return, panelModifier);
        QTRY_VERIFY(!side->isHidden());
        QVERIFY(!tab->isHidden());
        QVERIFY(hidden->isHidden());
        QCOMPARE(target->maximumSize(), maximumSize);
        QTRY_COMPARE(target->geometry(), targetGeometry);
        QTRY_COMPARE(side->geometry(), sideGeometry);
        QVERIFY(main.tabifiedDockWidgets(target).contains(tab));
        QCOMPARE(QApplication::focusWidget(), target->widget());
    }

    void closingExpandedPanelRestoresOthers()
    {
        MainWindow main;
        hideBuiltinPanels(main);
        auto *target = addPanel(main, "target");
        auto *side = addPanel(main, "side");
        main.splitDockWidget(target, side, Qt::Horizontal);
        target->setTransient(true);
        main.show();
        QVERIFY(QTest::qWaitForWindowExposed(&main));
        target->widget()->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), target->widget());
        QTest::keyClick(target->widget(), Qt::Key_Return, panelModifier);
        QTRY_VERIFY(side->isHidden());
        QPointer<IaitoDockWidget> guardedTarget(target);
        target->close();
        QTRY_VERIFY(!side->isHidden());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!guardedTarget);
    }

    void openingAnotherPanelRestoresLayout()
    {
        MainWindow main;
        hideBuiltinPanels(main);
        auto *target = addPanel(main, "target");
        auto *side = addPanel(main, "side");
        auto *hidden = addPanel(main, "hidden");
        main.splitDockWidget(target, side, Qt::Horizontal);
        hidden->hide();
        main.show();
        QVERIFY(QTest::qWaitForWindowExposed(&main));
        target->widget()->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), target->widget());
        QTest::keyClick(target->widget(), Qt::Key_Return, panelModifier);
        QTRY_VERIFY(side->isHidden());
        hidden->show();
        QTRY_VERIFY(!side->isHidden());
        QVERIFY(!hidden->isHidden());
        QVERIFY(target->width() < main.width());
    }

    void selectedTabsSurviveLayoutChanges_data()
    {
        QTest::addColumn<QString>("mutation");
        QTest::newRow("resize") << QString("resize");
        QTest::newRow("add-remove-panel") << QString("add-remove-panel");
        QTest::newRow("save-restore") << QString("save-restore");
        QTest::newRow("maximize-restore") << QString("maximize-restore");
    }

    void selectedTabsSurviveLayoutChanges()
    {
        QFETCH(QString, mutation);
        MainWindow main;
        hideBuiltinPanels(main);
        auto *first = addPanel(main, "first");
        auto *selected = addPanel(main, "selected");
        auto *last = addPanel(main, "last");
        auto *target = addPanel(main, "target");
        auto *otherFirst = addPanel(main, "other-first");
        auto *otherSelected = addPanel(main, "other-selected");
        auto *otherLast = addPanel(main, "other-last");
        main.splitDockWidget(first, target, Qt::Horizontal);
        main.splitDockWidget(target, otherFirst, Qt::Horizontal);
        main.tabifyDockWidget(first, selected);
        main.tabifyDockWidget(first, last);
        main.tabifyDockWidget(otherFirst, otherSelected);
        main.tabifyDockWidget(otherFirst, otherLast);
        main.show();
        QVERIFY(QTest::qWaitForWindowExposed(&main));
        selected->raise();
        otherSelected->raise();
        target->widget()->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), target->widget());
        QTRY_VERIFY(!selected->visibleRegion().isEmpty());
        QTRY_VERIFY(!otherSelected->visibleRegion().isEmpty());

        auto *manager = main.findChild<DockManager *>();
        QVERIFY(manager);
        // Chrome reconciliation must leave Qt's selected tab identities intact.
        manager->updateDockTabBars();
        if (mutation == "resize") {
            main.resize(1200, 750);
        } else if (mutation == "add-remove-panel") {
            auto *added = addPanel(main, "added");
            QCoreApplication::processEvents();
            added->setTransient(true);
            added->close();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        } else if (mutation == "save-restore") {
            const QByteArray state = main.saveState();
            QVERIFY(main.restoreState(state));
        } else {
            for (int i = 0; i < 3; ++i) {
                QTest::keyClick(target->widget(), Qt::Key_Return, panelModifier);
                QTRY_VERIFY(selected->isHidden());
                QTest::keyClick(target->widget(), Qt::Key_Return, panelModifier);
                QTRY_VERIFY(!selected->isHidden());
                QCoreApplication::processEvents();
            }
        }
        QTest::qWait(50);
        QTRY_VERIFY(!selected->visibleRegion().isEmpty());
        QTRY_VERIFY(!otherSelected->visibleRegion().isEmpty());
        QVERIFY(first->visibleRegion().isEmpty());
        QVERIFY(last->visibleRegion().isEmpty());
        QVERIFY(otherFirst->visibleRegion().isEmpty());
        QVERIFY(otherLast->visibleRegion().isEmpty());
    }

    void closeCorrectDockWithDuplicateTitles()
    {
        MainWindow main;
        hideBuiltinPanels(main);
        auto *first = addPanel(main, "first");
        auto *second = addPanel(main, "second");
        first->setWindowTitle("Shared panel");
        second->setWindowTitle("Shared panel");
        // Deliberately put the panels in the opposite order to their registration.
        main.tabifyDockWidget(second, first);
        main.show();
        QVERIFY(QTest::qWaitForWindowExposed(&main));
        first->raise();
        QTRY_VERIFY(!first->visibleRegion().isEmpty());

        auto *manager = main.findChild<DockManager *>();
        QVERIFY(manager);
        manager->updateDockTabBars();
        QTabBar *tabs = nullptr;
        for (auto *bar : main.findChildren<QTabBar *>()) {
            if (bar->isVisible() && bar->count() == 2 && bar->tabText(0) == "Shared panel"
                && bar->tabText(1) == "Shared panel") {
                tabs = bar;
                break;
            }
        }
        QVERIFY(tabs);
        tabs->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), tabs);
        QVERIFY(manager->closeCurrentTab());
        QTRY_VERIFY(first->isHidden());
        QVERIFY(!second->isHidden());
        QTRY_VERIFY(!second->visibleRegion().isEmpty());
    }

    void numberedTabShortcuts_data()
    {
        QTest::addColumn<bool>("maximized");
        QTest::newRow("tiled") << false;
        QTest::newRow("maximized") << true;
    }

    void numberedTabShortcuts()
    {
        QFETCH(bool, maximized);
        MainWindow main;
        hideBuiltinPanels(main);
        QList<IaitoDockWidget *> panels;
        for (int i = 0; i < 5; ++i) {
            panels.append(addPanel(main, QStringLiteral("tab-%1").arg(i + 1)));
        }
        auto *otherFirst = addPanel(main, "other-first");
        auto *otherSelected = addPanel(main, "other-selected");
        main.splitDockWidget(panels.first(), otherFirst, Qt::Horizontal);
        for (int i = 1; i < panels.size(); ++i) {
            main.tabifyDockWidget(panels.first(), panels.at(i));
        }
        main.tabifyDockWidget(otherFirst, otherSelected);
        main.show();
        QVERIFY(QTest::qWaitForWindowExposed(&main));
        otherSelected->raise();
        panels.last()->raise();
        panels.last()->widget()->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), panels.last()->widget());
        if (maximized) {
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return, panelModifier);
            QTRY_VERIFY(otherSelected->isHidden());
        }

        for (int i = 0; i < panels.size(); ++i) {
            QTest::keyClick(QApplication::focusWidget(), Qt::Key(Qt::Key_1 + i), panelModifier);
            auto *selected = panels.at(i);
            QTRY_VERIFY(!selected->visibleRegion().isEmpty());
            QVERIFY(
                selected == QApplication::focusWidget()
                || selected->isAncestorOf(QApplication::focusWidget()));
            if (maximized) {
                QVERIFY(otherSelected->isHidden());
                for (auto *panel : panels) {
                    QCOMPARE(panel->isHidden(), panel != selected);
                }
            } else {
                QVERIFY(!otherSelected->visibleRegion().isEmpty());
            }
        }

        if (maximized) {
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return, panelModifier);
            QTRY_VERIFY(!otherSelected->isHidden());
        }
        // Moving focus to another group makes the same keys operate on that group.
        otherSelected->widget()->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), otherSelected->widget());
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_1, panelModifier);
        QTRY_VERIFY(!otherFirst->visibleRegion().isEmpty());
        QVERIFY(!panels.last()->visibleRegion().isEmpty());
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_2, panelModifier);
        QTRY_VERIFY(!otherSelected->visibleRegion().isEmpty());
        QVERIFY(!panels.last()->visibleRegion().isEmpty());
    }

    void numberedShortcutsFollowVisibleTabOrder()
    {
        MainWindow main;
        hideBuiltinPanels(main);
        QList<IaitoDockWidget *> panels;
        for (int i = 0; i < 5; ++i) {
            panels.append(addPanel(main, QStringLiteral("tab-%1").arg(i + 1)));
        }
        auto *standalone = addPanel(main, "standalone");
        main.splitDockWidget(panels.first(), standalone, Qt::Horizontal);
        for (int i = 1; i < panels.size(); ++i) {
            main.tabifyDockWidget(panels.first(), panels.at(i));
        }
        main.show();
        QVERIFY(QTest::qWaitForWindowExposed(&main));
        panels.last()->raise();
        panels.last()->widget()->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), panels.last()->widget());
        QTest::qWait(50);
        QTabBar *tabs = nullptr;
        for (auto *bar : main.findChildren<QTabBar *>()) {
            if (!bar->visibleRegion().isEmpty() && bar->count() == 5 && bar->tabText(0) == "tab-1") {
                tabs = bar;
                break;
            }
        }
        QVERIFY(tabs);
        tabs->moveTab(4, 3);
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_4, panelModifier);
        QTRY_VERIFY(!panels.last()->visibleRegion().isEmpty());
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_5, panelModifier);
        QTRY_VERIFY(!panels.at(3)->visibleRegion().isEmpty());

        panels.at(1)->hide();
        QTRY_COMPARE(tabs->count(), 4);
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_3, panelModifier);
        QTRY_VERIFY(!panels.last()->visibleRegion().isEmpty());
        // Missing tab numbers and standalone panels leave the current view alone.
        QWidget *focus = QApplication::focusWidget();
        QTest::keyClick(focus, Qt::Key_5, panelModifier);
        QCOMPARE(QApplication::focusWidget(), focus);
        QVERIFY(!panels.last()->visibleRegion().isEmpty());
        standalone->widget()->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), standalone->widget());
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_2, panelModifier);
        QCOMPARE(QApplication::focusWidget(), standalone->widget());
        QVERIFY(!panels.last()->visibleRegion().isEmpty());
    }

    void frameShortcutsPreserveSelectedTabs_data()
    {
        QTest::addColumn<bool>("maximized");
        QTest::newRow("tiled") << false;
        QTest::newRow("maximized") << true;
    }

    void frameShortcutsPreserveSelectedTabs()
    {
        QFETCH(bool, maximized);
        MainWindow main;
        hideBuiltinPanels(main);
        // Registration order deliberately differs from the frame order on screen.
        auto *bottomRight = addPanel(main, "bottom-right");
        auto *rightFirst = addPanel(main, "right-first");
        auto *rightSelected = addPanel(main, "right-selected");
        auto *rightLast = addPanel(main, "right-last");
        auto *bottomLeft = addPanel(main, "bottom-left");
        auto *leftFirst = addPanel(main, "left-first");
        auto *leftSelected = addPanel(main, "left-selected");
        auto *leftLast = addPanel(main, "left-last");
        auto *hidden = addPanel(main, "hidden");
        auto *floating = addPanel(main, "floating");
        main.splitDockWidget(leftFirst, rightFirst, Qt::Horizontal);
        main.splitDockWidget(leftFirst, bottomLeft, Qt::Vertical);
        main.splitDockWidget(rightFirst, bottomRight, Qt::Vertical);
        main.tabifyDockWidget(leftFirst, leftSelected);
        main.tabifyDockWidget(leftFirst, leftLast);
        main.tabifyDockWidget(rightFirst, rightSelected);
        main.tabifyDockWidget(rightFirst, rightLast);
        hidden->hide();
        floating->setAttribute(Qt::WA_ShowWithoutActivating);
        floating->setFloating(true);
        main.show();
        main.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&main));
        QTest::qWait(50);
        QApplication::setActiveWindow(&main);
        leftSelected->raise();
        rightSelected->raise();
        const QList<IaitoDockWidget *> frames
            = {leftSelected, rightSelected, bottomLeft, bottomRight};
        for (auto *frame : frames) {
            frame->raiseMemoryWidget();
            frame->widget()->setFocus();
            QTRY_COMPARE(QApplication::focusWidget(), frame->widget());
        }
        leftSelected->widget()->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), leftSelected->widget());
        QTest::qWait(50);
        QVERIFY(leftSelected->geometry().left() < rightSelected->geometry().left());
        QVERIFY(leftSelected->geometry().top() < bottomLeft->geometry().top());
        QVERIFY(bottomLeft->geometry().left() < bottomRight->geometry().left());
        if (maximized) {
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return, panelModifier);
            QTRY_VERIFY(rightSelected->isHidden());
        }

        int current = 0;
        for (Qt::Key key : {Qt::Key_J, Qt::Key_K}) {
            // Traverse twice in each direction to verify wraparound and stable selection.
            for (int step = 0; step < frames.size() * 2; ++step) {
                current = (current + (key == Qt::Key_J ? 1 : frames.size() - 1)) % frames.size();
                QTest::keyClick(QApplication::focusWidget(), key, panelModifier);
                QTRY_COMPARE(QApplication::focusWidget(), frames.at(current)->widget());
                QVERIFY(!frames.at(current)->visibleRegion().isEmpty());
                QVERIFY(leftFirst->visibleRegion().isEmpty());
                QVERIFY(leftLast->visibleRegion().isEmpty());
                QVERIFY(rightFirst->visibleRegion().isEmpty());
                QVERIFY(rightLast->visibleRegion().isEmpty());
                QVERIFY(hidden->isHidden());
                QVERIFY(floating->isFloating());
                if (maximized) {
                    for (int i = 0; i < frames.size(); ++i) {
                        QCOMPARE(frames.at(i)->isHidden(), i != current);
                    }
                }
            }
        }
        if (maximized) {
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return, panelModifier);
            QTRY_VERIFY(!rightSelected->isHidden());
        }
        QVERIFY(!leftSelected->visibleRegion().isEmpty());
        QVERIFY(!rightSelected->visibleRegion().isEmpty());
    }

    void frameShortcutsSkipClosedFrames()
    {
        MainWindow main;
        hideBuiltinPanels(main);
        auto *left = addPanel(main, "left");
        auto *rightFirst = addPanel(main, "right-first");
        auto *rightSelected = addPanel(main, "right-selected");
        main.splitDockWidget(left, rightFirst, Qt::Horizontal);
        main.tabifyDockWidget(rightFirst, rightSelected);
        main.show();
        QVERIFY(QTest::qWaitForWindowExposed(&main));
        rightSelected->raise();
        rightSelected->widget()->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), rightSelected->widget());
        left->setTransient(true);
        left->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QTest::qWait(50);
        for (Qt::Key key : {Qt::Key_J, Qt::Key_K}) {
            QTest::keyClick(QApplication::focusWidget(), key, panelModifier);
            QCOMPARE(QApplication::focusWidget(), rightSelected->widget());
            QVERIFY(!rightSelected->visibleRegion().isEmpty());
            QVERIFY(rightFirst->visibleRegion().isEmpty());
        }
    }
};

QTEST_MAIN(PanelMaximizeTests)
#include "test_panelmaximize.moc"
