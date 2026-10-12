#include "common/Configuration.h"
#include "common/NameListModel.h"
#include "core/MainWindow.h"
#include "dialogs/XrefsDialog.h"
#include "widgets/GlobalVariablesWidget.h"

#include <QAbstractItemModelTester>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>

class GlobalVariablesTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;

private slots:
    void initTestCase()
    {
        QVERIFY(R2_ABIVERSION >= 154);
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("globalvariables");
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
        // Remove globals through avg- so their type links are also cleaned up.
        for (const auto &variable : Core()->getAllGlobalVariables()) {
            Core()->cmdRawAt("avg-", variable.address);
        }
        Core()->cmd("f--;C-*;ax-*");
        Core()->seek(0x100);
    }

    void listExternalGlobals()
    {
        Core()->cmd(
            "avg int count @ 0;avg char * text @ 0x20000000000001;"
            "avg int kernel @ 0x8000000000000001;fs scratch");
        const auto variables = Core()->getAllGlobalVariables();
        QCOMPARE(variables.size(), 3);
        QCOMPARE(variables[0].address, RVA(0));
        QCOMPARE(variables[0].name, QString("count"));
        QCOMPARE(variables[0].type, QString("int"));
        QCOMPARE(variables[0].size, RVA(4));
        QCOMPARE(variables[1].address, RVA(0x20000000000001));
        QCOMPARE(variables[1].type, QString("char *"));
        QCOMPARE(variables[2].address, RVA(0x8000000000000001));
        QCOMPARE(Core()->getOffset(), RVA(0x100));
        const auto spaces = Core()->cmdj("fsj").array();
        bool selected = false;
        for (const auto &space : spaces) {
            const auto object = space.toObject();
            selected |= object["name"].toString() == "scratch" && object["selected"].toBool();
        }
        QVERIFY(selected);
    }

    void registerEditAndDelete()
    {
        QSignalSpy flags(Core(), &IaitoCore::flagsChanged);
        QSignalSpy code(Core(), &IaitoCore::refreshCodeViews);
        QString error;
        QVERIFY2(
            Core()->setGlobalVariable(0x20, "counter", "unsigned int", {}, &error),
            qPrintable(error));
        auto variables = Core()->getAllGlobalVariables();
        QCOMPARE(variables.size(), 1);
        QCOMPARE(variables[0].type, QString("unsigned int"));
        QCOMPARE(variables[0].size, RVA(4));
        Core()->cmd("axd 0x20 0x80");
        QVERIFY2(
            Core()->setGlobalVariable(0x20, "app.counter", "int[4]", "counter", &error),
            qPrintable(error));
        variables = Core()->getAllGlobalVariables();
        QCOMPARE(variables.size(), 1);
        QCOMPARE(variables[0].name, QString("app.counter"));
        QCOMPARE(variables[0].type, QString("int[4]"));
        QCOMPARE(variables[0].size, RVA(16));
        QCOMPARE(Core()->cmdj("axtj @ 0x20").array().size(), 1);
        // An unrelated symbol at the same address must survive avg-.
        Core()->cmd("fs symbols;f sym.counter 4 @ 0x20");
        QVERIFY(Core()->deleteGlobalVariable(0x20, "app.counter"));
        QVERIFY(Core()->getAllGlobalVariables().isEmpty());
        QVERIFY(!Core()->cmdj("fj").array().isEmpty());
        QCOMPARE(Core()->cmdj("axtj @ 0x20").array().size(), 1);
        QCOMPARE(Core()->getOffset(), RVA(0x100));
        QCOMPARE(flags.count(), 3);
        QCOMPARE(code.count(), 3);
    }

    void rejectedEditsPreserveVariables()
    {
        QVERIFY(Core()->setGlobalVariable(0x20, "counter", "int"));
        Core()->cmd("fs symbols;f occupied 4 @ 0x40");
        QSignalSpy flags(Core(), &IaitoCore::flagsChanged);
        QString error;
        QVERIFY(!Core()->setGlobalVariable(0x20, "other", "char", {}, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!Core()->setGlobalVariable(0x40, "occupied", "int", {}, &error));
        QVERIFY(!Core()->setGlobalVariable(0x20, "occupied", "char", "counter", &error));
        QVERIFY(!Core()->setGlobalVariable(0x20, "renamed", "unknown_type", "counter", &error));
        QVERIFY(
            !Core()->setGlobalVariable(0x20, "counter", "int;f injected @ 0x70", "counter", &error));
        QVERIFY(!r_flag_get(Core()->core()->flags, "injected"));
        QVERIFY(!Core()->setGlobalVariable(0x20, "bad;name", "char", "counter", &error));
        QVERIFY(!Core()->setGlobalVariable(0x20, "renamed", "char", "missing", &error));
        QVERIFY(!Core()->setGlobalVariable(RVA_INVALID, "invalid", "int", {}, &error));
        QVERIFY(!Core()->deleteGlobalVariable(0x40, "occupied"));
        QVERIFY(!Core()->deleteGlobalVariable(0x40, "counter"));
        const auto variables = Core()->getAllGlobalVariables();
        QCOMPARE(variables.size(), 1);
        QCOMPARE(variables[0].name, QString("counter"));
        QCOMPARE(variables[0].type, QString("int"));
        QCOMPARE(variables[0].size, RVA(4));
        QCOMPARE(flags.count(), 0);
    }

    void modelSortingFilteringAndHierarchy()
    {
        GlobalVariablesModel model;
        QAbstractItemModelTester
            tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setVariables(
            {{0x8000000000000002, 16, "app.z", "int[4]"},
             {0x8000000000000001, 4, "app.a", "int"},
             {0, 1, "zero", "char"}});
        AddressableFilterProxyModel proxy(&model, nullptr);
        proxy.setSortRole(Qt::EditRole);
        proxy.setFilterKeyColumn(-1);
        proxy.sort(GlobalVariablesModel::AddressColumn);
        QCOMPARE(proxy.address(proxy.index(0, 0)), RVA(0));
        QCOMPARE(proxy.address(proxy.index(1, 0)), RVA(0x8000000000000001));
        QCOMPARE(proxy.address(proxy.index(2, 0)), RVA(0x8000000000000002));
        proxy.setTypeFilterColumn(GlobalVariablesModel::TypeColumn);
        proxy.setTypeFilter("int");
        QCOMPARE(proxy.rowCount(), 1);
        QCOMPARE(proxy.name(proxy.index(0, 0)), QString("app.a"));
        proxy.setTypeFilter({});
        proxy.setFilterWildcard("*app*");
        QCOMPARE(proxy.rowCount(), 2);
        NameListModel hierarchy(&proxy, GlobalVariablesModel::NameColumn);
        hierarchy.setHierarchy(true);
        const auto group = hierarchy.index(0, 0);
        QCOMPARE(hierarchy.address(group), RVA_INVALID);
        const auto leaf = hierarchy.index(0, GlobalVariablesModel::NameColumn, group);
        QCOMPARE(hierarchy.name(leaf), QString("app.a"));
        QCOMPARE(
            leaf.data(GlobalVariablesModel::DescriptionRole)
                .value<GlobalVariableDescription>()
                .address,
            RVA(0x8000000000000001));
        QVERIFY(!model.data({}).isValid());
    }

    void ambiguousDeletionPreservesMetadata()
    {
        Core()->cmd("avg char first @ 0x40;avg int second @ 0x40");
        const auto metadata = Core()->cmdj("Cj");
        QVERIFY(!Core()->deleteGlobalVariable(0x40, "first"));
        QVERIFY(!Core()->deleteGlobalVariable(0x40, "second"));
        QCOMPARE(Core()->getAllGlobalVariables().size(), 2);
        QCOMPARE(Core()->cmdj("Cj"), metadata);
    }

    void widgetWorkflow()
    {
        MainWindow main;
        main.resize(1100, 700);
        // Keep unrelated panels from exercising platform font fallbacks in this UI test.
        for (auto panel : main.findChildren<QDockWidget *>()) {
            panel->hide();
        }
        main.show();
        auto dock = main.findChild<GlobalVariablesWidget *>("GlobalVariablesWidget");
        QVERIFY(dock);
        dock->show();
        dock->raise();
        QTRY_VERIFY(dock->isVisibleToUser());
        auto view = dock->findChild<QTreeView *>("treeView");
        QVERIFY(view);
        auto add = dock->findChild<QAction *>("actionAddGlobalVariable");
        auto edit = dock->findChild<QAction *>("actionEditGlobalVariable");
        auto remove = dock->findChild<QAction *>("actionDeleteGlobalVariable");
        QVERIFY(add && edit && remove);
        QVERIFY(add->isEnabled());
        QVERIFY(!edit->isEnabled());
        QVERIFY(!remove->isEnabled());
        auto runEditor = [&](QAction *action, const QString &newName, const QString &newType) {
            bool processed = false;
            QString errorText;
            QTimer::singleShot(0, this, [&]() {
                auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dialog) {
                    return;
                }
                auto name = dialog->findChild<QLineEdit *>("globalVariableName");
                auto type = dialog->findChild<QComboBox *>("globalVariableType");
                if (!name || !type) {
                    errorText = "Unexpected modal dialog";
                    dialog->reject();
                    return;
                }
                name->setText(newName);
                type->setCurrentText(newType);
                auto buttons = dialog->findChild<QDialogButtonBox *>();
                emit buttons->accepted();
                processed = true;
                if (dialog->result() != QDialog::Accepted) {
                    errorText = dialog->findChild<QLabel *>("globalVariableError")->text();
                    dialog->reject();
                }
            });
            action->trigger();
            QVERIFY(processed);
            QVERIFY2(errorText.isEmpty(), qPrintable(errorText));
        };
        runEditor(add, "app.counter", "unsigned int");
        QTRY_COMPARE(view->model()->rowCount(), 1);
        const QString screenshot = qEnvironmentVariable("IAITO_GLOBAL_VARIABLES_SCREENSHOT");
        if (!screenshot.isEmpty()) {
            dock->setFloating(true);
            dock->resize(620, 260);
            QTest::qWait(20);
            QVERIFY(dock->grab().save(screenshot));
        }
        const auto item = view->model()->index(0, 0);
        view->setCurrentIndex(item);
        QVERIFY(edit->isEnabled());
        QVERIFY(remove->isEnabled());
        Core()->seek(0x200);
        emit view->activated(item);
        QCOMPARE(Core()->getOffset(), RVA(0x100));
        Core()->cmd("axd 0x100 0x80");
        auto addressMenu = dock->findChild<AddressableItemContextMenu *>();
        QVERIFY(addressMenu);
        QAction *xrefs = nullptr;
        for (auto action : addressMenu->actions()) {
            if (action->text().contains("X-Refs")) {
                xrefs = action;
            }
        }
        QVERIFY(xrefs);
        QVERIFY(xrefs->isEnabled());
        bool refsShown = false;
        QTimer::singleShot(0, this, [&]() {
            auto dialog = qobject_cast<XrefsDialog *>(QApplication::activeModalWidget());
            if (dialog) {
                for (auto tree : dialog->findChildren<QTreeView *>()) {
                    refsShown |= tree->model() && tree->model()->rowCount() > 0;
                }
                dialog->reject();
            }
        });
        xrefs->trigger();
        QVERIFY(refsShown);
        runEditor(edit, "app.renamed", "char *");
        QTRY_COMPARE(view->model()->rowCount(), 1);
        QCOMPARE(Core()->getAllGlobalVariables()[0].name, QString("app.renamed"));
        view->setCurrentIndex(view->model()->index(0, 0));
        remove->trigger();
        QTRY_COMPARE(view->model()->rowCount(), 0);
        QVERIFY(!edit->isEnabled());
        QVERIFY(!remove->isEnabled());
        QVERIFY(add->isEnabled());
    }
};

QTEST_MAIN(GlobalVariablesTests)
#include "test_globalvariables.moc"
