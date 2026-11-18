#include "common/NameListModel.h"
#include "core/Iaito.h"
#include "widgets/ClassesWidget.h"
#include "widgets/NameListView.h"
#include "widgets/QuickFilterView.h"
#include "widgets/SymbolsWidget.h"

#include <QAbstractItemModelTester>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLineEdit>
#include <QMenu>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>

class BinListingTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("binlisting");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        Core()->initialize(false);
    }

    void symbolNames()
    {
        QList<SymbolDescription> symbols;
        SymbolDescription first;
        first.vaddr = 0x1000;
        first.name = "_ZN3foo3barEv";
        first.demangledName = "foo::bar()";
        symbols << first;
        SymbolDescription plain;
        plain.vaddr = 0x2000;
        plain.name = "plain_function";
        symbols << plain;
        SymbolsModel source(&symbols);
        SymbolsProxyModel filter(&source);
        NameListModel model(&filter, SymbolsModel::NameColumn);
        QAbstractItemModelTester
            tester(&source, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QAbstractItemModelTester
            treeTester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(source.index(0, SymbolsModel::NameColumn).data().toString(), first.demangledName);
        QCOMPARE(source.index(1, SymbolsModel::NameColumn).data().toString(), plain.name);
        model.setHierarchy(true);
        QVERIFY(!model.groupIndex("foo::").isValid());
        auto leaf = model.index(0, SymbolsModel::NameColumn);
        QCOMPARE(leaf.data().toString(), QString("foo::bar()"));
        const auto tooltip = leaf.data(Qt::ToolTipRole).toString();
        QVERIFY(tooltip.contains(first.name));
        QVERIFY(tooltip.contains(first.demangledName));
        QCOMPARE(model.address(leaf), first.vaddr);
        source.setDemangled(false);
        QVERIFY(!model.groupIndex("foo::").isValid());
        QCOMPARE(model.index(0, SymbolsModel::NameColumn).data().toString(), first.name);
        filter.setFilterWildcard("*foo*");
        QCOMPARE(filter.rowCount(), 1);
        source.setDemangled(true);
        filter.setFilterWildcard("*_ZN3*");
        QCOMPARE(filter.rowCount(), 1);
        QVERIFY(!model.groupIndex("foo::").isValid());
        QCOMPARE(model.index(0, SymbolsModel::NameColumn).data().toString(), first.demangledName);
        filter.setFilterWildcard("");
        model.sort(SymbolsModel::NameColumn, Qt::AscendingOrder);
        source.setDemangled(false);
        QCOMPARE(model.index(0, SymbolsModel::NameColumn).data().toString(), first.name);

        Core()->cmd("CCu foo::bar() @ 0x1000");
        QCOMPARE(source.index(0, SymbolsModel::CommentColumn).data().toString(), QString());
        QCOMPARE(Core()->getCommentAt(0x1000), first.demangledName);
        Core()->cmd("CC- @ 0x1000");
        Core()->cmd("CCu user comment @ 0x1000");
        QCOMPARE(
            source.index(0, SymbolsModel::CommentColumn).data().toString(), QString("user comment"));
    }

    void symbolsByType()
    {
        QList<SymbolDescription> symbols;
        SymbolDescription local;
        local.vaddr = 0x100;
        local.name = "pkg::local";
        local.bind = "LOCAL";
        local.type = "FUNC";
        symbols << local;
        auto global = local;
        global.vaddr = 0x200;
        global.name = "pkg::global";
        global.bind = "GLOBAL";
        symbols << global;
        auto object = local;
        object.vaddr = 0x300;
        object.name = "pkg::object";
        object.type = "OBJ";
        symbols << object;
        SymbolsModel source(&symbols);
        SymbolsProxyModel filter(&source);
        QuickFilterView quick;
        quick.setTypeFilter(&filter, SymbolsModel::TypeColumn);
        connect(
            &quick,
            &QuickFilterView::filterTextChanged,
            &filter,
            &AddressableFilterProxyModel::setFilterWildcard);
        NameListModel model(&filter, SymbolsModel::NameColumn);
        model.setHierarchy(true);
        QAbstractItemModelTester
            tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        auto combo = quick.findChild<QComboBox *>("typeFilterComboBox");
        QCOMPARE(combo->count(), 4);
        combo->setCurrentIndex(combo->findData("LOCAL FUNC"));
        QCOMPARE(resets.count(), 1);
        QCOMPARE(filter.rowCount(), 1);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.index(0, SymbolsModel::NameColumn).data().toString(), local.name);
        QCOMPARE(model.address(model.index(0, 0)), local.vaddr);
        filter.setFilterWildcard("*global*");
        QCOMPARE(filter.rowCount(), 0);
        combo->setCurrentIndex(0);
        QCOMPARE(filter.rowCount(), 1);
        QCOMPARE(model.address(model.index(0, 0)), global.vaddr);
        filter.setFilterWildcard("");
        QCOMPARE(filter.rowCount(), 3);
        QVERIFY(model.groupIndex("pkg::").isValid());
    }

    void classesAndMembers()
    {
        BinClassDescription cls;
        cls.name = "org.example.Outer$Inner";
        BinClassMethodDescription method;
        method.name = "run(java.lang.String)";
        method.mangledName = "run(Ljava/lang/String;)V";
        method.addr = 0x100;
        cls.methods << method;
        method.name = "stop()";
        method.mangledName.clear();
        method.addr = 0x200;
        cls.methods << method;
        BinClassFieldDescription field;
        field.name = "count";
        cls.fields << field;
        BinClassesModel source;
        source.setClasses({cls});
        QAbstractItemModel *base = &source;
        QAbstractItemModelTester
            sourceTester(base, QAbstractItemModelTester::FailureReportingMode::QtTest);
        ClassesSortFilterProxyModel filter;
        filter.setClassesModel(&source);
        NameListModel model(&filter, ClassesModel::NAME);
        QAbstractItemModelTester
            treeTester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setHierarchy(true);
        auto owner = model.mapFromSource(filter.index(0, 0));
        QCOMPARE(owner.data().toString(), cls.name);
        QVERIFY(!owner.parent().isValid());
        QCOMPARE(model.rowCount(owner), 3);
        auto leaf = model.index(0, 0, owner);
        QCOMPARE(leaf.data().toString(), QString("run(java.lang.String)"));
        QCOMPARE(model.name(leaf), QString("org.example.Outer$Inner.run(java.lang.String)"));
        QVERIFY(leaf.data(Qt::ToolTipRole).toString().contains("run(Ljava/lang/String;)V"));
        QCOMPARE(model.address(leaf), RVA(0x100));
        QCOMPARE(model.address(model.index(2, 0, owner)), RVA_INVALID);
        QCOMPARE(model.mapToSource(leaf).parent().data(ClassesModel::NameRole).toString(), cls.name);
        filter.setFilterWildcard("*run*");
        QCOMPARE(filter.rowCount(), 1);
        QCOMPARE(filter.rowCount(filter.index(0, 0)), 1);
        filter.setFilterWildcard("*Inner*");
        QCOMPARE(filter.rowCount(filter.index(0, 0)), 3);
        filter.setFilterWildcard("*Ljava*");
        QCOMPARE(filter.rowCount(filter.index(0, 0)), 1);
        filter.setFilterWildcard("missing");
        QCOMPARE(model.rowCount(), 0);
        filter.setFilterWildcard("");
        model.setHierarchy(false);
        QCOMPARE(model.index(0, 0).data().toString(), cls.name);
        QCOMPARE(model.rowCount(model.index(0, 0)), 3);
        filter.setClassesModel(nullptr);
        QCOMPARE(model.rowCount(), 0);
        filter.setClassesModel(&source);
        QCOMPARE(model.rowCount(), 1);

        QuickFilterView quick;
        quick.setTypeFilter(&filter, ClassesModel::TYPE);
        auto combo = quick.findChild<QComboBox *>("typeFilterComboBox");
        QVERIFY(combo->findData("class") > 0);
        QVERIFY(combo->findData("method") > 0);
        QVERIFY(combo->findData("field") > 0);
        combo->setCurrentIndex(combo->findData("method"));
        QCOMPARE(filter.rowCount(), 1);
        QCOMPARE(filter.rowCount(filter.index(0, 0)), 2);
        filter.setFilterWildcard("*count*");
        QCOMPARE(filter.rowCount(), 0); // A matching field must not keep an empty class visible.
        combo->setCurrentIndex(0);
        QCOMPARE(filter.rowCount(), 1);
        QCOMPARE(filter.rowCount(filter.index(0, 0)), 1);
        filter.setFilterWildcard("");
        combo->setCurrentIndex(combo->findData("class"));
        QCOMPARE(filter.rowCount(), 1);
        QCOMPARE(filter.rowCount(filter.index(0, 0)), 0);
        combo->setCurrentIndex(combo->findData("method"));
        cls.methods.clear();
        source.setClasses({cls});
        QCOMPARE(combo->currentIndex(), 0);
        QCOMPARE(filter.rowCount(filter.index(0, 0)), 1);
    }

    void classExpansionSurvivesToggle()
    {
        BinClassDescription cls;
        cls.name = "std::Widget";
        BinClassMethodDescription method;
        method.name = "start()";
        cls.methods << method;
        BinClassesModel source;
        BinClassDescription second;
        second.name = "std::Other";
        source.setClasses({cls, second});
        ClassesSortFilterProxyModel filter;
        filter.setClassesModel(&source);
        QTreeView view;
        view.setModel(&filter);
        QMenu menu;
        NameListView names(&view, &menu, &filter, 0, "testClasses");
        auto model = names.presentationModel();
        view.setExpanded(model->index(0, 0), true);
        names.toggleAction()->setChecked(true);
        auto owner = model->mapFromSource(filter.index(0, 0));
        QVERIFY(view.isExpanded(owner));
        QVERIFY(view.isExpanded(owner.parent()));
        names.toggleAction()->setChecked(false);
        QVERIFY(view.isExpanded(model->index(0, 0)));
    }

    void analysisClassUpdates()
    {
        IaitoDockWidget dock(nullptr);
        dock.show();
        QApplication::processEvents();
        Core()->createNewClass("crate::Widget");
        AnalMethodDescription method;
        method.name = "start";
        method.addr = 0x500;
        method.vtableOffset = -1;
        Core()->setAnalMethod("crate::Widget", method);
        AnalClassesModel source(&dock);
        ClassesSortFilterProxyModel filter;
        filter.setClassesModel(&source);
        NameListModel model(&filter, ClassesModel::NAME);
        model.setHierarchy(true);
        QAbstractItemModelTester
            tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto owner = model.index(0, 0);
        QCOMPARE(owner.data().toString(), QString("crate::Widget"));
        auto member = model.index(0, 0, owner);
        QCOMPARE(model.name(member), QString("crate::Widget::start"));
        QCOMPARE(model.address(member), RVA(0x500));
        QCOMPARE(
            model.mapToSource(member).parent().data(ClassesModel::NameRole).toString(),
            QString("crate::Widget"));
        Core()->renameClass("crate::Widget", "other::Renamed");
        QVERIFY(!model.groupIndex("crate::").isValid());
        QVERIFY(!model.groupIndex("other::").isValid());
        owner = model.index(0, 0);
        QCOMPARE(owner.data().toString(), QString("other::Renamed"));
        method.name = "finish";
        method.addr = 0x600;
        Core()->setAnalMethod("other::Renamed", method);
        owner = model.index(0, 0);
        QCOMPARE(model.rowCount(owner), 2);
        Core()->deleteClass("other::Renamed");
        QCOMPARE(model.rowCount(), 0);
    }

    void realBinary()
    {
        const QByteArray path = qgetenv("IAITO_TEST_BINARY");
        if (path.isEmpty()) {
            QSKIP("Set IAITO_TEST_BINARY to exercise a C++ binary");
        }
        Core()->setConfig("bin.demangle", true);
        QVERIFY(r_core_file_open(Core()->core_, path.constData(), R_PERM_R, 0));
        QVERIFY(r_core_bin_load(Core()->core_, path.constData(), UT64_MAX));
        auto symbols = Core()->getAllSymbols();
        QVERIFY(!symbols.isEmpty());
        SymbolsModel source(&symbols);
        int demangled = 0;
        for (int row = 0; row < symbols.size(); ++row) {
            const auto &symbol = symbols[row];
            if (!symbol.demangledName.isEmpty()) {
                ++demangled;
                QCOMPARE(
                    source.index(row, SymbolsModel::NameColumn).data().toString(),
                    symbol.demangledName);
            } else {
                QCOMPARE(source.index(row, SymbolsModel::NameColumn).data().toString(), symbol.name);
            }
        }
        QVERIFY(demangled > 0);
        qInfo() << symbols.size() << "symbols," << demangled << "demangled";
        SymbolsProxyModel symbolFilter(&source);
        QuickFilterView symbolQuickFilter;
        symbolQuickFilter.setTypeFilter(&symbolFilter, SymbolsModel::TypeColumn);
        NameListModel symbolNames(&symbolFilter, SymbolsModel::NameColumn);
        symbolNames.setHierarchy(true);
        auto typeCombo = symbolQuickFilter.findChild<QComboBox *>("typeFilterComboBox");
        QVERIFY(typeCombo->findData("LOCAL FUNC") > 0);
        typeCombo->setCurrentIndex(typeCombo->findData("LOCAL FUNC"));
        QVERIFY(symbolFilter.rowCount() > 0);
        for (int row = 0; row < symbolFilter.rowCount(); ++row) {
            QCOMPARE(
                symbolFilter.index(row, SymbolsModel::TypeColumn).data().toString(),
                QString("LOCAL FUNC"));
        }
        typeCombo->setCurrentIndex(0);
        QSignalSpy resets(&symbolNames, &QAbstractItemModel::modelReset);
        QElapsedTimer timer;
        timer.start();
        symbolFilter.setFilterWildcard("*moneypunct*");
        qInfo() << "Filtering real symbol list:" << timer.elapsed() << "ms," << resets.count()
                << "tree rebuilds";
        QCOMPARE(resets.count(), 1);
        QVERIFY(symbolFilter.rowCount() > 0);
        const auto classes = Core()->getAllClassesFromBin();
        QVERIFY(!classes.isEmpty());
        ClassesWidget widget(nullptr);
        widget.resize(800, 600);
        widget.show();
        auto view = widget.findChild<QTreeView *>("classesTreeView");
        auto filter = widget.findChild<QLineEdit *>();
        auto combo = widget.findChild<QComboBox *>("classSourceCombo");
        QVERIFY(view && filter && combo);
        QCOMPARE(combo->currentIndex(), 0);
        QCOMPARE(view->model()->rowCount(), classes.size());
        const auto classRow = view->model()->index(0, 0);
        const auto position = view->visualRect(classRow).center();
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, position);
        QVERIFY(view->isExpanded(classRow));
        QTest::mouseDClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, position);
        QVERIFY(view->isExpanded(classRow));
        widget.findChild<QAction *>("actionNameHierarchy")->setChecked(true);
        QApplication::processEvents();
        view->expandToDepth(1);
        auto quick = widget.findChild<QuickFilterView *>();
        QSignalSpy textFilters(quick, &QuickFilterView::filterTextChanged);
        filter->setText("*pos_format*");
        QTRY_COMPARE(textFilters.count(), 1);
        QVERIFY(view->model()->rowCount() > 0);
        combo->setCurrentIndex(1);
        QApplication::processEvents();
        combo->setCurrentIndex(0);
        QApplication::processEvents();
        QVERIFY(view->model()->rowCount() > 0);
    }
};

QTEST_MAIN(BinListingTests)
#include "test_binlisting.moc"
