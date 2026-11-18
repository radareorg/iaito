#include "common/NameListModel.h"
#include "widgets/ComboQuickFilterView.h"
#include "widgets/NameListView.h"
#include "widgets/QuickFilterView.h"

#include <QAbstractItemModelTester>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

class SourceModel : public AddressableItemModel<QStandardItemModel>
{
public:
    SourceModel() { setHorizontalHeaderLabels({"Address", "Name"}); }
    RVA address(const QModelIndex &index) const override
    {
        return index.sibling(index.row(), 0).data(Qt::UserRole).toULongLong();
    }
    QString name(const QModelIndex &index) const override
    {
        return index.sibling(index.row(), 1).data(Qt::EditRole).toString();
    }
    void append(const QString &name, RVA address)
    {
        auto offset = new QStandardItem(QString::number(address));
        offset->setData(QVariant::fromValue(address), Qt::UserRole);
        appendRow({offset, new QStandardItem(name)});
    }
};

class NameListTests : public QObject
{
    Q_OBJECT

private slots:
    void compactNames()
    {
        SourceModel source;
        source.append("std::vector<int>::begin()", 10);
        source.append("std::vector<int>::end()", 20);
        source.append("std::vector<int>::end()", 30);
        NameListModel model(&source, 1);
        QAbstractItemModelTester
            tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setCompact(true);
        QCOMPARE(model.index(0, 1).data().toString(), source.name(source.index(0, 0)));
        QCOMPARE(model.index(1, 1).data().toString(), QString("▶ end()"));
        QCOMPARE(model.index(2, 1).data().toString(), source.name(source.index(2, 0)));
        QCOMPARE(model.name(model.index(1, 1)), QString("std::vector<int>::end()"));
        QVERIFY(
            model.index(1, 1).data(Qt::ToolTipRole).toString().contains("vector&lt;int&gt;::end()"));
        QCOMPARE(model.address(model.index(1, 1)), RVA(20));
        QCOMPARE(model.mapToSource(model.index(1, 1)), source.index(1, 1));
        model.setCompact(false);
        QCOMPARE(model.index(1, 1).data(), source.index(1, 1).data());
    }

    void hierarchy_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QStringList>("groups");
        QTest::addColumn<QString>("leaf");
        QTest::newRow("cpp") << QString("sym.std::vector<std::pair<int, int>>::begin()")
                             << QStringList({"sym.", "std::", "vector<std::pair<int, int>>::"})
                             << QString("begin()");
        QTest::newRow("cpp-operator")
            << QString("std::Thing::operator<<(std::ostream &)")
            << QStringList({"std::", "Thing::"}) << QString("operator<<(std::ostream &)");
        QTest::newRow("rust") << QString("core::ptr::drop_in_place<alloc::vec::Vec<u8>>")
                              << QStringList({"core::", "ptr::"})
                              << QString("drop_in_place<alloc::vec::Vec<u8>>");
        QTest::newRow("rust-trait")
            << QString("<alloc::vec::Vec<u8> as core::fmt::Debug>::fmt")
            << QStringList({"<alloc::vec::Vec<u8> as core::fmt::Debug>::"}) << QString("fmt");
        QTest::newRow("rust-operator-namespace")
            << QString("crate::operator_helpers::run")
            << QStringList({"crate::", "operator_helpers::"}) << QString("run");
        QTest::newRow("java") << QString("org.example.Outer$Inner.run(java.lang.String)")
                              << QStringList({"org.", "example.", "Outer$", "Inner."})
                              << QString("run(java.lang.String)");
        QTest::newRow("dalvik") << QString("Lorg/example/Thing;->run(Ljava/lang/String;)V")
                                << QStringList({"Lorg/", "example/", "Thing;->"})
                                << QString("run(Ljava/lang/String;)V");
        QTest::newRow("constructor")
            << QString("Lorg/example/Thing;-><init>()V")
            << QStringList({"Lorg/", "example/", "Thing;->"}) << QString("<init>()V");
        QTest::newRow("no-separators") << QString("_Z3foov") << QStringList() << QString("_Z3foov");
    }

    void hierarchy()
    {
        QFETCH(QString, name);
        QFETCH(QStringList, groups);
        QFETCH(QString, leaf);
        SourceModel source;
        source.append(name, 42);
        NameListModel model(&source, 1);
        QAbstractItemModelTester
            tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setHierarchy(true);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.index(0, 1).data().toString(), name);
        QCOMPARE(model.address(model.index(0, 1)), RVA(42));
        QVERIFY(!model.mapFromSource(source.index(0, 1)).parent().isValid());
        source.append(name + "_second", 43);
        QCOMPARE(model.headerData(0, Qt::Horizontal).toString(), QString("Address"));
        QCOMPARE(model.headerData(1, Qt::Horizontal).toString(), QString("Name"));
        QModelIndex parent;
        QString path;
        for (const auto &group : groups) {
            QCOMPARE(model.rowCount(parent), 1);
            auto item = model.index(0, 1, parent);
            QCOMPARE(item.data().toString(), group);
            QCOMPARE(model.parent(item), parent);
            QCOMPARE(model.address(item), RVA_INVALID);
            QVERIFY(!model.mapToSource(item).isValid());
            path += group;
            parent = model.index(0, 0, parent);
            QCOMPARE(model.groupIndex(path), parent);
        }
        auto item = model.index(0, 1, parent);
        QCOMPARE(item.data().toString(), leaf);
        QCOMPARE(model.name(item), name);
        QCOMPARE(model.address(item), RVA(42));
        QCOMPARE(model.mapFromSource(source.index(0, 1)), item);
        QCOMPARE(model.rowCount(item), 0);
        model.setHierarchy(false);
        QCOMPARE(model.index(0, 1).data().toString(), name);
    }

    void sortingFilteringAndUpdates()
    {
        SourceModel source;
        source.append("crate::module::zebra", 20);
        source.append("crate::module::alpha", 10);
        AddressableFilterProxyModel filter(&source, nullptr);
        filter.setFilterKeyColumn(1);
        NameListModel model(&filter, 1);
        QAbstractItemModelTester
            tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setCompact(true);
        model.sort(1, Qt::AscendingOrder);
        QCOMPARE(model.address(model.index(0, 0)), RVA(10));
        QCOMPARE(model.index(1, 1).data().toString(), QString("▶ zebra"));
        filter.setFilterWildcard("*zebra*");
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.index(0, 1).data().toString(), QString("crate::module::zebra"));
        model.setHierarchy(true);
        filter.setFilterWildcard("");
        auto group = model.groupIndex("crate::module::");
        QCOMPARE(model.rowCount(group), 2);
        QCOMPARE(model.address(model.index(0, 1, group)), RVA(10));
        source.setData(source.index(1, 1), "other::renamed");
        QVERIFY(!model.groupIndex("crate::module::").isValid());
        QVERIFY(!model.groupIndex("other::").isValid());
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.index(1, 1).data().toString(), QString("other::renamed"));
        QCOMPARE(model.address(model.index(1, 1)), RVA(10));
        source.removeRow(0);
        QVERIFY(!model.groupIndex("crate::").isValid());
        source.append("other::renamed", 99);
        QCOMPARE(model.rowCount(model.groupIndex("other::")), 2);
        source.clear();
        QCOMPARE(model.rowCount(), 0);
    }

    void detailsAndDataRoles()
    {
        SourceModel source;
        source.append("foo::bar", 42);
        source.item(0, 0)->appendRow({new QStandardItem("Size: 16"), new QStandardItem()});
        NameListModel model(&source, 1);
        QAbstractItemModelTester
            tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setHierarchy(true);
        auto leaf = model.mapFromSource(source.index(0, 0));
        QCOMPARE(model.rowCount(leaf), 1);
        QCOMPARE(model.index(0, 0, leaf).data().toString(), QString("Size: 16"));
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        source.setData(source.index(0, 0), QColor(Qt::red), Qt::ForegroundRole);
        QCOMPARE(resets.count(), 0);
        QCOMPARE(leaf.data(Qt::ForegroundRole).value<QColor>(), QColor(Qt::red));
    }

    void sourceLifetimeAndReplacement()
    {
        auto source = new SourceModel;
        source->append("foo::bar", 1);
        NameListModel model(source, 1);
        QAbstractItemModelTester
            tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setHierarchy(true);
        delete source;
        QCOMPARE(model.rowCount(), 0);
        SourceModel replacement;
        replacement.append("", 2);
        model.setSourceModel(&replacement);
        replacement.setData(replacement.index(0, 1), "new::name");
        QVERIFY(!model.groupIndex("new::").isValid());
        QCOMPARE(model.index(0, 1).data().toString(), QString("new::name"));
        QCOMPARE(model.address(model.mapFromSource(replacement.index(0, 0))), RVA(2));
    }

    void largeListing()
    {
        SourceModel source;
        for (int i = 0; i < 10000; ++i) {
            source.append(
                QString("crate::module%1::Type<other::Generic>::method%2()").arg(i % 100).arg(i), i);
        }
        NameListModel model(&source, 1);
        model.setHierarchy(true);
        model.setCompact(true);
        for (int i = 0; i < 10000; ++i) {
            auto index = model.mapFromSource(source.index(i, 1));
            QVERIFY(index.isValid());
            QCOMPARE(model.address(index), RVA(i));
            QCOMPARE(model.name(index), source.name(source.index(i, 1)));
        }
    }

    void sharedToggleAndSelection()
    {
        QTemporaryDir settingsDir;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
        SourceModel source;
        source.append("org.example.First.run()", 100);
        source.append("org.example.Second.run()", 200);
        AddressableFilterProxyModel filter(&source, nullptr);
        filter.setFilterKeyColumn(1);
        QTreeView view;
        QMenu menu;
        QuickFilterView quickFilter;
        NameListView presentation(&view, &menu, &filter, 1, "TestNames");
        quickFilter.addActionButton(presentation.toggleAction());
        auto button = quickFilter.findChild<QToolButton *>();
        QVERIFY(button);
        auto model = dynamic_cast<NameListModel *>(view.model());
        QVERIFY(model);
        QAbstractItemModelTester
            tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        view.setCurrentIndex(model->index(1, 1));
        button->click();
        QVERIFY(model->isHierarchy());
        QVERIFY(presentation.toggleAction()->isChecked());
        QCOMPARE(model->address(view.currentIndex()), RVA(200));
        QCOMPARE(view.header()->visualIndex(1), 0);
        filter.setFilterWildcard("*First*");
        QVERIFY(!model->groupIndex("org.").isValid());
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(model->index(0, 1).data().toString(), QString("org.example.First.run()"));
        presentation.toggleAction()->trigger();
        QVERIFY(!model->isHierarchy());
        QVERIFY(!button->isChecked());
        QCOMPARE(view.header()->visualIndex(1), 1);
        quickFilter.closeFilter();
        QVERIFY(!quickFilter.isHidden());
        QVERIFY(!button->isHidden());
    }

    void expandByClickingRow()
    {
        SourceModel source;
        source.append("std::Widget", 100);
        source.append("std::Other", 300);
        source.item(0, 0)->appendRow(
            {new QStandardItem("200"), new QStandardItem("std::Widget::run()")});
        QTreeView view;
        QMenu menu;
        NameListView names(&view, &menu, &source, 1, "ClickNames");
        view.setEditTriggers(QAbstractItemView::NoEditTriggers);
        names.toggleAction()->setChecked(true);
        auto model = names.presentationModel();
        auto group = model->groupIndex("std::");
        view.resize(600, 300);
        view.show();
        QCoreApplication::processEvents();

        auto label = group.sibling(group.row(), 1);
        auto position = view.visualRect(label).center();
        QVERIFY(!view.isExpanded(group));
        QTest::mouseClick(view.viewport(), Qt::RightButton, Qt::NoModifier, position);
        QVERIFY(!view.isExpanded(group));
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, position);
        QVERIFY(view.isExpanded(group));
        QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, position);
        QVERIFY(view.isExpanded(group));

        // The disclosure arrow still collapses; clicking another column also expands.
        const auto rect = view.visualRect(label);
        const QPoint arrow(rect.left() - view.indentation() / 2, rect.center().y());
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, arrow);
        QVERIFY(!view.isExpanded(group));
        QTest::mouseClick(
            view.viewport(), Qt::LeftButton, Qt::NoModifier, view.visualRect(group).center());
        QVERIFY(view.isExpanded(group));

        const auto owner = model->index(0, 0, group);
        position = view.visualRect(owner.sibling(owner.row(), 1)).center();
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, position);
        QVERIFY(view.isExpanded(owner));
        const auto member = model->index(0, 1, owner);
        position = view.visualRect(member).center();
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, position);
        QCOMPARE(view.currentIndex(), member);
        QSignalSpy activated(&view, &QTreeView::activated);
        QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, position);
        QCOMPARE(activated.count(), 1);
        QCOMPARE(activated.first().first().value<QModelIndex>(), member);
    }

    void singleResultBranchesAndFiltering()
    {
        SourceModel source;
        source.append("a::b::one", 10);
        source.append("a::b::two", 20);
        source.append("a::c::three", 30);
        source.append("z::solo", 40);
        AddressableFilterProxyModel filter(&source, nullptr);
        filter.setFilterKeyColumn(1);
        QTreeView view;
        QMenu menu;
        NameListView names(&view, &menu, &filter, 1, "SingleResults");
        names.toggleAction()->setChecked(true);
        auto model = names.presentationModel();
        QAbstractItemModelTester
            tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(model->rowCount(), 2);
        QVERIFY(model->groupIndex("a::b::").isValid());
        QVERIFY(!model->groupIndex("a::c::").isValid());
        QVERIFY(!model->groupIndex("z::").isValid());
        auto third = model->mapFromSource(filter.index(2, 1));
        QCOMPARE(third.parent(), model->groupIndex("a::"));
        QCOMPARE(third.data().toString(), QString("c::three"));
        QCOMPARE(model->name(third), QString("a::c::three"));
        QCOMPARE(model->address(third), RVA(30));
        QCOMPARE(model->index(1, 1).data().toString(), QString("z::solo"));

        view.setCurrentIndex(model->mapFromSource(filter.index(0, 1)));
        filter.setFilterWildcard("*one*");
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(model->index(0, 1).data().toString(), QString("a::b::one"));
        QVERIFY(!model->groupIndex("a::").isValid());
        QCOMPARE(model->address(view.currentIndex()), RVA(10));
        filter.setFilterWildcard("");
        QVERIFY(model->groupIndex("a::b::").isValid());
        QCOMPARE(model->address(view.currentIndex()), RVA(10));
        QCOMPARE(view.currentIndex().parent(), model->groupIndex("a::b::"));
        QVERIFY(view.isExpanded(view.currentIndex().parent()));
    }

    void typeFilterChoicesAndText()
    {
        SourceModel source;
        source.setColumnCount(3);
        source.append("alpha", 1);
        source.append("beta", 2);
        source.append("beta_global", 3);
        source.append("untyped", 4);
        source.setData(source.index(0, 2), "LOCAL FUNC");
        source.setData(source.index(1, 2), "LOCAL FUNC");
        source.setData(source.index(2, 2), "GLOBAL FUNC");
        AddressableFilterProxyModel filter(&source, nullptr);
        filter.setFilterKeyColumn(1);
        QAbstractItemModelTester
            tester(&filter, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QuickFilterView quick;
        auto combo = quick.findChild<QComboBox *>("typeFilterComboBox");
        QVERIFY(combo->isHidden());
        quick.setTypeFilter(&filter, 2);
        connect(
            &quick,
            &QuickFilterView::filterTextChanged,
            &filter,
            &AddressableFilterProxyModel::setFilterWildcard);
        auto text = quick.findChild<QLineEdit *>();
        QVERIFY(!combo->isHidden());
        QCOMPARE(combo->count(), 4);
        QCOMPARE(combo->currentText(), QString("All types"));
        QVERIFY(!combo->currentData().isValid());
        combo->setCurrentIndex(combo->findData("LOCAL FUNC"));
        QCOMPARE(filter.rowCount(), 2);
        text->setText("*beta*");
        QTRY_COMPARE(filter.rowCount(), 1);
        QCOMPARE(filter.address(filter.index(0, 0)), RVA(2));
        // Text and type selections never shrink the available type choices.
        QCOMPARE(combo->count(), 4);
        QVERIFY(combo->findData("GLOBAL FUNC") > 0);
        combo->setCurrentIndex(0);
        QCOMPARE(filter.rowCount(), 2);
        quick.clearFilter();
        QCOMPARE(filter.rowCount(), 4);
        combo->setCurrentIndex(combo->findData(QString("")));
        QCOMPARE(filter.rowCount(), 1);
        QCOMPARE(filter.address(filter.index(0, 0)), RVA(4));
        quick.closeFilter();
        QCOMPARE(filter.rowCount(), 4);
        QVERIFY(!filter.typeFilter().isValid());
        QVERIFY(combo->isHidden());
        quick.showFilter();
        QVERIFY(!combo->isHidden());
        quick.setTypeFilter(nullptr, -1);
        QVERIFY(combo->isHidden());
    }

    void typeFilterRefreshAndSourceChanges()
    {
        SourceModel source;
        source.setColumnCount(3);
        source.append("one", 1);
        source.append("two", 2);
        source.setData(source.index(0, 2), "LOCAL FUNC");
        source.setData(source.index(1, 2), "LOCAL FUNC");
        AddressableFilterProxyModel filter(&source, nullptr);
        QuickFilterView quick;
        quick.setTypeFilter(&filter, 2);
        auto combo = quick.findChild<QComboBox *>("typeFilterComboBox");
        combo->setCurrentIndex(combo->findData("LOCAL FUNC"));
        source.setData(source.index(0, 2), "LOCAL OBJ");
        QCOMPARE(combo->currentData().toString(), QString("LOCAL FUNC"));
        QCOMPARE(filter.rowCount(), 1);
        QVERIFY(combo->findData("LOCAL OBJ") > 0);
        source.removeRow(1);
        QCOMPARE(combo->currentIndex(), 0);
        QCOMPARE(filter.rowCount(), 1);
        QVERIFY(!filter.typeFilter().isValid());
        QVERIFY(combo->findData("LOCAL FUNC") < 0);
        combo->setCurrentIndex(combo->findData("LOCAL OBJ"));
        auto replacement = new SourceModel;
        replacement->setColumnCount(3);
        replacement->append("new", 3);
        replacement->setData(replacement->index(0, 2), "GLOBAL FUNC");
        filter.setSourceModel(static_cast<AddressableItemModelI *>(replacement));
        QCOMPARE(combo->currentIndex(), 0);
        QCOMPARE(combo->count(), 2);
        QCOMPARE(filter.rowCount(), 1);
        combo->setCurrentIndex(1);
        delete replacement;
        QCOMPARE(combo->count(), 1);
        QCOMPARE(combo->currentIndex(), 0);
        QCOMPARE(filter.rowCount(), 0);
    }

    void typeFilterWithExistingCombo()
    {
        SourceModel source;
        source.setColumnCount(3);
        source.append("text", 1);
        source.setData(source.index(0, 2), "ASCII");
        AddressableFilterProxyModel filter(&source, nullptr);
        ComboQuickFilterView quick;
        quick.setLabelText("Section:");
        quick.comboBox()->addItems({"All sections", ".rodata"});
        quick.setTypeFilter(&filter, 2);
        quick.comboBox()->setCurrentIndex(1);
        auto type = quick.findChild<QComboBox *>("typeFilterComboBox");
        type->setCurrentIndex(1);
        quick.clearFilter();
        QCOMPARE(type->currentIndex(), 0);
        QCOMPARE(quick.comboBox()->currentIndex(), 0);
        QVERIFY(!filter.typeFilter().isValid());
    }

    void largeFilteredListing()
    {
        SourceModel source;
        for (int i = 0; i < 3000; ++i) {
            source.append(QString("crate::module::%1%2").arg(i % 2 ? "drop" : "keep").arg(i), i);
        }
        AddressableFilterProxyModel filter(&source, nullptr);
        filter.setFilterKeyColumn(1);
        NameListModel model(&filter, 1);
        model.setHierarchy(true);
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        QElapsedTimer timer;
        timer.start();
        filter.setFilterWildcard("*keep*");
        qInfo() << "Filtering 3000 interleaved names:" << timer.elapsed() << "ms," << resets.count()
                << "tree rebuilds";
        QCOMPARE(filter.rowCount(), 1500);
        QCOMPARE(resets.count(), 1);
        filter.setFilterWildcard("");
        QCOMPARE(filter.rowCount(), 3000);
        QCOMPARE(resets.count(), 2);
    }

    void coalesceTypingAndClearImmediately()
    {
        QuickFilterView quick;
        auto text = quick.findChild<QLineEdit *>();
        QSignalSpy changes(&quick, &QuickFilterView::filterTextChanged);
        quick.show();
        QTest::keyClicks(text, "long_symbol_name");
        QCOMPARE(changes.count(), 0);
        QTRY_COMPARE(changes.count(), 1);
        QCOMPARE(changes.last().first().toString(), QString("long_symbol_name"));
        text->setText("pending");
        quick.clearFilter();
        QCOMPARE(changes.count(), 2);
        QVERIFY(changes.last().first().toString().isEmpty());
        QTest::qWait(200);
        QCOMPARE(changes.count(), 2);
    }

    void renderSharedWidgets()
    {
        SourceModel source;
        source.append("std::vector<std::pair<int, std::string>>::begin()", 0x1000);
        source.append("std::vector<std::pair<int, std::string>>::end()", 0x1100);
        source.append("std::vector<std::pair<int, std::string>>::size() const", 0x1200);
        source.append("core::ptr::drop_in_place<alloc::vec::Vec<u8>>", 0x1300);
        source.append("org.example.Outer$Inner.run(java.lang.String)", 0x1400);
        source.append("Lorg/example/Thing;->run(Ljava/lang/String;)V", 0x1500);
        QWidget window;
        QVBoxLayout layout(&window);
        QTreeView view;
        view.setAlternatingRowColors(true);
        view.setUniformRowHeights(true);
        QMenu menu;
        QuickFilterView filter;
        layout.addWidget(&view);
        layout.addWidget(&filter);
        NameListView names(&view, &menu, &source, 1, "RenderNames");
        filter.addActionButton(names.toggleAction());
        auto compact = menu.findChild<QAction *>("actionCompactNames");
        QVERIFY(compact);
        compact->setChecked(true);
        window.resize(760, 520);
        window.show();
        QCoreApplication::processEvents();
        const QString directory = qEnvironmentVariable("IAITO_NAMELIST_SCREENSHOTS");
        if (!directory.isEmpty()) {
            QVERIFY(window.grab().save(directory + "/iaito-names-list.png"));
        }
        names.toggleAction()->setChecked(true);
        view.expandAll();
        QCoreApplication::processEvents();
        if (!directory.isEmpty()) {
            QVERIFY(window.grab().save(directory + "/iaito-names-tree.png"));
        }
    }
};

QTEST_MAIN(NameListTests)
#include "test_namelist.moc"
