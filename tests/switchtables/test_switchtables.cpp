#include "common/Configuration.h"
#include "core/MainWindow.h"
#include "dialogs/SwitchJumpTableDialog.h"
#include "widgets/DisassemblyWidget.h"
#include "widgets/SwitchTablesWidget.h"

#include <QAbstractItemModelTester>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>

class SwitchTablesTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;

    static RAnalSwitchOp *addSwitch(
        RVA function, RVA block, RVA dispatcher, RVA table, const QList<SwitchCaseDescription> &cases)
    {
        auto core = Core()->core();
        auto fcn = r_anal_get_function_at(core->anal, function);
        if (!fcn) {
            return nullptr;
        }
        auto bb = r_anal_create_block(core->anal, block, 16);
        if (!bb) {
            return nullptr;
        }
        r_anal_function_add_block(fcn, bb);
        bb->switch_op = r_anal_switch_op_new(dispatcher, 0, 255, UT64_MAX);
        bb->switch_op->daddr = table;
        bb->switch_op->dsize = 8;
        bb->switch_op->amount = cases.size();
        int index = 0;
        for (const auto &entry : cases) {
            r_anal_switch_op_add_case(bb->switch_op, table + 8 * index++, entry.value, entry.target);
        }
        const auto op = bb->switch_op;
        r_unref(bb);
        return op;
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("switchtables");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        Core()->initialize(false);
        qRegisterMetaType<RVA>("RVA");
        Config()->loadInitial();
        Config()->setOutputRedirectionEnabled(false);
        Config()->setDecompilerAutoRefreshEnabled(false);
        Core()->cmd("o malloc://16384;e asm.arch=x86;e asm.bits=64");
    }

    void init()
    {
        Core()->cmd("af-*;f-*;wx c3 @ 0x500;wx c3 @ 0x600");
        Core()->seek(0);
    }

    void allAnalyzedTables()
    {
        Core()->cmd(
            "af+ 0x100 sym.parse;af+ 0x200 sym.shared;"
            "af+ 0x8000000000000000 sym.high");
        const auto first = addSwitch(0x100, 0x100, 0x104, 0x800, {{65, 0x500}, {90, 0x600}});
        QVERIFY(first);
        QVERIFY(addSwitch(0x100, 0x120, 0x125, 0x900, {}));
        const RVA high = 0x8000000000000000;
        QVERIFY(addSwitch(high, high, high + 4, high + 0x800, {{UT64_MAX, high + 0x500}}));
        {
            auto core = Core()->core();
            r_anal_function_add_block(
                r_anal_get_function_at(core->anal, 0x200), r_anal_get_block_at(core->anal, 0x100));
        }
        Core()->seek(0x40);
        const auto tables = Core()->getAllSwitchTables();
        QCOMPARE(tables.size(), 3); // The shared block appears only once.
        for (const auto &table : tables) {
            if (table.address == 0x104) {
                QCOMPARE(table.tableAddress, RVA(0x800));
                QCOMPARE(table.functionName, QString("sym.parse"));
                QCOMPARE(table.cases.size(), 2);
                QCOMPARE(table.cases[1].value, ut64(90));
                QCOMPARE(table.cases[1].target, RVA(0x600));
            } else if (table.address == 0x125) {
                QVERIFY(table.cases.isEmpty());
            } else {
                QCOMPARE(table.address, high + 4);
                QCOMPARE(table.tableAddress, high + 0x800);
                QCOMPARE(table.cases[0].value, ut64(UT64_MAX));
                QCOMPARE(table.cases[0].target, high + 0x500);
            }
        }
        QCOMPARE(Core()->getOffset(), RVA(0x40));
    }

    void sortingFilteringAndAscii()
    {
        SwitchTablesModel model;
        QAbstractItemModelTester
            tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setTables(
            {{0x8000000000000001, 0x900, "sym.z", {{65, 0x500}}},
             {0, 0x800, "sym.a", {{10, 0x600}, {127, 0x700}}}});
        AddressableFilterProxyModel proxy(&model, nullptr);
        proxy.setSortRole(Qt::EditRole);
        proxy.setFilterKeyColumn(-1);
        proxy.sort(SwitchTablesModel::AddressColumn);
        QCOMPARE(proxy.address(proxy.index(0, 0)), RVA(0));
        QCOMPARE(proxy.address(proxy.index(1, 0)), RVA(0x8000000000000001));
        proxy.sort(SwitchTablesModel::CasesColumn);
        QCOMPARE(proxy.name(proxy.index(0, 0)), QString("sym.z"));
        proxy.setFilterWildcard("*sym.a*");
        QCOMPARE(proxy.rowCount(), 1);

        SwitchCasesModel cases;
        QAbstractItemModelTester
            casesTester(&cases, QAbstractItemModelTester::FailureReportingMode::QtTest);
        cases.setCases(
            {{65, 0x8000000000000001},
             {10, 0},
             {127, 0x500},
             {128, 0x600},
             {UT64_MAX, 0x700},
             {'\'', 0x800},
             {'\\', 0x900}});
        const auto ascii = [&](int row) {
            return cases.index(row, SwitchCasesModel::AsciiColumn).data().toString();
        };
        QCOMPARE(ascii(0), QString("'A'"));
        QCOMPARE(ascii(1), QString("'\\n'"));
        QCOMPARE(ascii(2), QString("'\\x7f'"));
        QVERIFY(ascii(3).isEmpty());
        QVERIFY(ascii(4).isEmpty());
        QCOMPARE(ascii(5), QString("'\\\''"));
        QCOMPARE(ascii(6), QString("'\\\\'"));
        QCOMPARE(cases.address(cases.index(0, 0)), RVA(0x8000000000000001));
        QCOMPARE(cases.address(cases.index(1, 0)), RVA(0));
        AddressableFilterProxyModel caseProxy(&cases, nullptr);
        caseProxy.setSortRole(Qt::EditRole);
        caseProxy.sort(SwitchCasesModel::ValueColumn);
        QCOMPARE(caseProxy.index(0, 0).data().toLongLong(), qint64(-1));
        QVERIFY(!model.data({}).isValid());
        QVERIFY(!cases.data({}).isValid());
    }

    void browserWorkflow()
    {
        Core()->cmd("af+ 0x100 sym.parse;af+ 0x200 sym.other");
        QVERIFY(addSwitch(0x100, 0x100, 0x104, 0x800, {{65, 0x500}, {90, 0x600}}));
        QVERIFY(addSwitch(0x200, 0x200, 0x204, 0x900, {{10, 0x500}}));
        MainWindow main;
        main.setAnimated(false);
        main.resize(1200, 800);
        QVERIFY(QMetaObject::invokeMethod(&main, "on_actionDefault_triggered"));
        for (auto panel : main.findChildren<QDockWidget *>()) {
            panel->hide();
        }
        main.show();
        QVERIFY(QTest::qWaitForWindowExposed(&main));
        auto dock = main.findChild<SwitchTablesWidget *>("SwitchTablesWidget");
        QVERIFY(dock);
        dock->show();
        dock->raise();
        QTRY_VERIFY(dock->isVisibleToUser());
        auto tables = dock->findChild<QTreeView *>("treeView");
        auto cases = dock->findChild<QTreeView *>("casesView");
        auto edit = dock->findChild<QPushButton *>("editButton");
        auto refresh = dock->findChild<QAction *>("actionRefreshSwitchTables");
        QVERIFY(tables && cases && edit && refresh);
        QTRY_COMPARE(tables->model()->rowCount(), 2);
        QVERIFY(!edit->isEnabled());
        QCOMPARE(cases->model()->rowCount(), 0);
        const auto first = tables->model()->index(0, 0);
        QTest::mouseClick(
            tables->viewport(), Qt::LeftButton, Qt::NoModifier, tables->visualRect(first).center());
        QCOMPARE(Core()->getOffset(), RVA(0x104));
        QCOMPARE(cases->model()->rowCount(), 2);
        QVERIFY(edit->isEnabled());
        const auto target = cases->model()->index(1, SwitchCasesModel::TargetColumn);
        QTest::mouseClick(
            cases->viewport(), Qt::LeftButton, Qt::NoModifier, cases->visualRect(target).center());
        QCOMPARE(Core()->getOffset(), RVA(0x600));
        const auto disassembly = main.findChild<DisassemblyWidget *>();
        QVERIFY(disassembly);
        QTRY_VERIFY(disassembly->isVisibleToUser());
        QVERIFY(dock->isVisibleToUser());
        QCOMPARE(tables->currentIndex().row(), 0);
        const QString screenshot = qEnvironmentVariable("IAITO_SWITCHTABLES_SCREENSHOT");
        if (!screenshot.isEmpty()) {
            QCoreApplication::processEvents();
            QVERIFY(main.grab().save(screenshot));
        }

        // Editing must use the selected switch, even after navigating to a case.
        bool openedSelected = false;
        QTimer::singleShot(0, this, [&]() {
            auto dialog = qobject_cast<SwitchJumpTableDialog *>(QApplication::activeModalWidget());
            if (!dialog) {
                return;
            }
            for (auto label : dialog->findChildren<QLabel *>()) {
                openedSelected |= label->text() == QStringLiteral("0x104");
            }
            {
                auto core = Core()->core();
                auto op = r_anal_get_block_at(core->anal, 0x100)->switch_op;
                r_anal_switch_op_add_case(op, 0x810, 97, 0x500);
            }
            emit Core() -> functionsChanged();
            dialog->reject();
        });
        edit->click();
        QVERIFY(openedSelected);
        QCOMPARE(tables->currentIndex().data().toString(), RAddressString(0x104));
        QCOMPARE(cases->model()->rowCount(), 3);
        QCOMPARE(cases->currentIndex().row(), 1);
        QCOMPARE(Core()->getOffset(), RVA(0x600));
        Core()->renameFunction(0x100, "sym.renamed");
        QCOMPARE(
            tables->model()->index(0, SwitchTablesModel::FunctionColumn).data().toString(),
            QString("sym.renamed"));
        QCOMPARE(cases->model()->rowCount(), 3);

        auto proxy = dynamic_cast<AddressableFilterProxyModel *>(tables->model());
        QVERIFY(proxy);
        proxy->setFilterWildcard("*missing*");
        QCOMPARE(tables->model()->rowCount(), 0);
        QCOMPARE(cases->model()->rowCount(), 0);
        QVERIFY(!edit->isEnabled());
        proxy->setFilterWildcard({});
        tables->setCurrentIndex(tables->model()->index(1, 0));
        QCOMPARE(cases->model()->rowCount(), 1);
        Core()->cmd("af- @ 0x200");
        refresh->trigger();
        QCOMPARE(tables->model()->rowCount(), 1);
        QCOMPARE(cases->model()->rowCount(), 0);
        QVERIFY(!edit->isEnabled());
        dock->hide();
        Core()->cmd("af- @ 0x100");
        emit Core() -> functionsChanged();
        dock->show();
        dock->raise();
        QTRY_VERIFY(dock->isVisibleToUser());
        QTRY_COMPARE(tables->model()->rowCount(), 0);
    }
};

QTEST_MAIN(SwitchTablesTests)
#include "test_switchtables.moc"
