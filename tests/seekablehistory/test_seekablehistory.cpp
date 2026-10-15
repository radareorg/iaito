#include "common/Configuration.h"
#include "common/IaitoSeekable.h"
#include "core/MainWindow.h"
#include "widgets/CallGraph.h"
#include "widgets/DataAnalysisWidget.h"
#include "widgets/DecompilerWidget.h"
#include "widgets/DisassemblyWidget.h"

#include <QMenu>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <QToolButton>

class SeekableHistoryTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("seekablehistory");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        Core()->initialize(false);
        qRegisterMetaType<RVA>("RVA");
        Config()->loadInitial();
        Config()->setOutputRedirectionEnabled(false);
        Config()->setDecompilerAutoRefreshEnabled(false);
        Core()->cmd("o malloc://16384");
    }

    void init()
    {
        Core()->seek(0x100);
        auto core = Core()->core();
        r_io_sundo_reset(core->io);
    }

    void detachAtCurrentAddress()
    {
        IaitoSeekable view;
        Core()->seek(0x200);
        view.setSynchronization(false);
        QCOMPARE(view.getOffset(), RVA(0x200));
        QVERIFY(!view.canSeekPrev());
        QVERIFY(!view.canSeekNext());
        view.seekPrev();
        view.seekNext();
        view.seek(RVA_INVALID);
        QCOMPARE(view.getOffset(), RVA(0x200));
    }

    void backForwardAndBranch()
    {
        IaitoSeekable view;
        view.setSynchronization(false);
        view.seek(0x200);
        view.seek(0x300);
        QVERIFY(view.canSeekPrev());
        QVERIFY(!view.canSeekNext());
        view.seekPrev();
        QCOMPARE(view.getOffset(), RVA(0x200));
        view.seekPrev();
        QCOMPARE(view.getOffset(), RVA(0x100));
        QVERIFY(!view.canSeekPrev());
        view.seekPrev();
        QCOMPARE(view.getOffset(), RVA(0x100));
        view.seekNext();
        QCOMPARE(view.getOffset(), RVA(0x200));
        view.seekNext();
        QCOMPARE(view.getOffset(), RVA(0x300));
        view.seekNext();
        QCOMPARE(view.getOffset(), RVA(0x300));
        view.seekPrev();
        view.seek(0x400);
        QVERIFY(!view.canSeekNext());
        view.seekPrev();
        QCOMPARE(view.getOffset(), RVA(0x200));
        view.seekNext();
        QCOMPARE(view.getOffset(), RVA(0x400));
        QCOMPARE(Core()->getOffset(), RVA(0x100));
    }

    void repeatedSeekKeepsForwardHistory()
    {
        IaitoSeekable view;
        view.setSynchronization(false);
        view.seek(0x200);
        view.seekPrev();
        QSignalSpy seeks(&view, &IaitoSeekable::seekableSeekChanged);
        view.seek(0x100);
        view.seek(RVA_INVALID);
        QCOMPARE(seeks.count(), 0);
        QVERIFY(view.canSeekNext());
        view.seekNext();
        QCOMPARE(view.getOffset(), RVA(0x200));
        view.seekPrev();
        QVERIFY(!view.canSeekPrev());
    }

    void independentViewsAndGlobalHistory()
    {
        IaitoSeekable first;
        IaitoSeekable second;
        first.setSynchronization(false);
        second.setSynchronization(false);
        first.seek(0x200);
        second.seek(0x300);
        Core()->seek(0x400);
        first.seekPrev();
        QCOMPARE(first.getOffset(), RVA(0x100));
        QCOMPARE(second.getOffset(), RVA(0x300));
        QCOMPARE(Core()->getOffset(), RVA(0x400));
        Core()->seekPrev();
        QCOMPARE(Core()->getOffset(), RVA(0x100));
        QCOMPARE(second.getOffset(), RVA(0x300));
        first.seekNext();
        QCOMPARE(first.getOffset(), RVA(0x200));
    }

    void reattachAndDetach()
    {
        IaitoSeekable view;
        view.setSynchronization(false);
        view.seek(0x200);
        Core()->seek(0x300);
        view.setSynchronization(true);
        QCOMPARE(view.getOffset(), RVA(0x300));
        view.seekPrev();
        QCOMPARE(Core()->getOffset(), RVA(0x100));
        view.seekNext();
        QCOMPARE(Core()->getOffset(), RVA(0x300));
        view.setSynchronization(false);
        QCOMPARE(view.getOffset(), RVA(0x300));
        QVERIFY(!view.canSeekPrev());
        view.seek(0x400);
        view.setSynchronization(false);
        view.seekPrev();
        QCOMPARE(view.getOffset(), RVA(0x300));
        QVERIFY(!view.canSeekPrev());
    }

    void historyAvailabilitySignals()
    {
        IaitoSeekable view;
        view.setSynchronization(false);
        QSignalSpy changes(&view, &IaitoSeekable::historyChanged);
        view.seek(0x200);
        QCOMPARE(changes.count(), 1);
        view.seekPrev();
        QCOMPARE(changes.count(), 2);
        view.seekNext();
        QCOMPARE(changes.count(), 3);
        view.seekNext();
        view.seek(0x200);
        QCOMPARE(changes.count(), 3);
    }

    void viewHistoryBars_data()
    {
        QTest::addColumn<int>("type");
        QTest::newRow("disassembly") << int(MemoryWidgetType::Disassembly);
        QTest::newRow("graph") << int(MemoryWidgetType::Graph);
        QTest::newRow("hex") << int(MemoryWidgetType::Hexdump);
        QTest::newRow("decompiler") << int(MemoryWidgetType::Decompiler);
        QTest::newRow("data-analysis") << 4;
        QTest::newRow("call-graph") << 5;
    }

    void viewHistoryBars()
    {
        QFETCH(int, type);
        MainWindow main;
        AddressableDockWidget *dock;
        if (type == 4) {
            dock = new DataAnalysisWidget(&main);
        } else if (type == 5) {
            dock = new CallGraphWidget(&main, false);
        } else {
            dock = main.addNewMemoryWidget(MemoryWidgetType(type), 0x100);
        }
        dock->getSeekable()->setSynchronization(false);
        dock->setParent(nullptr);
        dock->resize(1000, 500);
        dock->show();
        QVERIFY(QTest::qWaitForWindowExposed(dock));
        auto *bar = dock->findChild<QToolBar *>("seekHistoryBar");
        QVERIFY(bar && bar->isVisible());
        auto *back = bar->findChild<QAction *>("seekBackAction");
        auto *forward = bar->findChild<QAction *>("seekForwardAction");
        QVERIFY(back && forward);
        QVERIFY(!back->isEnabled());
        QVERIFY(!forward->isEnabled());
        dock->getSeekable()->seek(0x200);
        dock->getSeekable()->seek(0x300);
        QVERIFY(back->isEnabled());
        QTest::mouseClick(bar->widgetForAction(back), Qt::LeftButton);
        QCOMPARE(dock->getSeekable()->getOffset(), RVA(0x200));
        QVERIFY(forward->isEnabled());
        QTest::mouseClick(bar->widgetForAction(forward), Qt::LeftButton);
        QCOMPARE(dock->getSeekable()->getOffset(), RVA(0x300));
        QCOMPARE(Core()->getOffset(), RVA(0x100));
        dock->getSeekable()->setSynchronization(true);
        QCOMPARE(bar->isVisible(), type == int(MemoryWidgetType::Decompiler));
        main.removeWidget(dock);
        delete dock;
    }

    void decompilerBarAboveCode()
    {
        MainWindow main;
        DecompilerWidget dock(&main);
        dock.setParent(nullptr);
        dock.resize(1000, 500);
        dock.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dock));
        auto *bar = dock.findChild<QToolBar *>("seekHistoryBar");
        auto *edit = dock.findChild<QPlainTextEdit *>("textEdit");
        QVERIFY(bar && edit);
        QVERIFY(bar->isVisible());
        QVERIFY(bar->height() < 50);
        QVERIFY(bar->mapTo(&dock, QPoint(0, bar->height())).y() <= edit->mapTo(&dock, QPoint()).y());
        auto *back = bar->findChild<QAction *>("seekBackAction");
        auto *forward = bar->findChild<QAction *>("seekForwardAction");
        QVERIFY(back && forward);
        QVERIFY(!back->isEnabled());
        Core()->seek(0x200);
        QVERIFY(back->isEnabled());
        back->trigger();
        QCOMPARE(Core()->getOffset(), RVA(0x100));
        QVERIFY(forward->isEnabled());
        forward->trigger();
        QCOMPARE(Core()->getOffset(), RVA(0x200));
    }

    void localShortcutsAndMainHistory()
    {
        MainWindow main;
        auto *dock = main.addNewMemoryWidget(MemoryWidgetType::Decompiler, 0x100, false);
        dock->setParent(nullptr);
        dock->resize(1000, 500);
        dock->show();
        QVERIFY(QTest::qWaitForWindowExposed(dock));
        auto *edit = dock->findChild<QPlainTextEdit *>("textEdit");
        QVERIFY(edit);
        edit->setFocus();
        QTRY_VERIFY(edit->hasFocus());
        auto *view = dock->getSeekable();
        view->seek(0x200);
        view->seek(0x300);
        QVERIFY(QMetaObject::invokeMethod(&main, "on_actionBackward_triggered"));
        QCOMPARE(view->getOffset(), RVA(0x200));
        QVERIFY(QMetaObject::invokeMethod(&main, "on_actionForward_triggered"));
        QCOMPARE(view->getOffset(), RVA(0x300));
        QTest::keyClick(edit, Qt::Key_Escape);
        QCOMPARE(view->getOffset(), RVA(0x200));
        QTest::mouseClick(edit->viewport(), Qt::ForwardButton);
        QCOMPARE(view->getOffset(), RVA(0x300));
        QTest::mouseClick(edit->viewport(), Qt::BackButton);
        QCOMPARE(view->getOffset(), RVA(0x200));
        auto *menu = main.findChild<QMenu *>("historyMenu");
        QVERIFY(menu);
        QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow"));
        QCOMPARE(menu->actions().size(), 2);
        QVERIFY(!menu->actions().first()->isEnabled());
        menu->actions().last()->trigger();
        QCOMPARE(view->getOffset(), RVA(0x100));
        auto *forwardMenu = main.findChild<QMenu *>("forwardHistoryMenu");
        QVERIFY(forwardMenu);
        QVERIFY(QMetaObject::invokeMethod(forwardMenu, "aboutToShow"));
        QCOMPARE(forwardMenu->actions().size(), 3);
        forwardMenu->actions().last()->trigger();
        QCOMPARE(view->getOffset(), RVA(0x300));
        QCOMPARE(Core()->getOffset(), RVA(0x100));
        main.removeWidget(dock);
        delete dock;
    }

    void disassemblyEscapeUsesLocalHistory()
    {
        DisassemblyWidget dock(nullptr);
        auto *view = dock.getSeekable();
        view->setSynchronization(false);
        view->seek(0x200);
        view->seek(0x300);
        dock.seekPrev();
        QCOMPARE(view->getOffset(), RVA(0x200));
        QCOMPARE(Core()->getOffset(), RVA(0x100));
    }
};

QTEST_MAIN(SeekableHistoryTests)
#include "test_seekablehistory.moc"
