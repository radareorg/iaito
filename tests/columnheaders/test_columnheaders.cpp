#include "common/ColumnHeaderMenu.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QHeaderView>
#include <QMenu>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTableView>
#include <QTableWidget>
#include <QTest>
#include <QTreeView>
#include <QTreeWidget>

#include <memory>

class RefreshableItemModel : public QStandardItemModel
{
public:
    using QStandardItemModel::QStandardItemModel;

    void refresh()
    {
        beginResetModel();
        endResetModel();
    }
};

class ColumnHeaderTests : public QObject
{
    Q_OBJECT

    static QMenu *openMenu(QHeaderView *header)
    {
        const QPoint position(10, 10);
        QContextMenuEvent
            event(QContextMenuEvent::Mouse, position, header->viewport()->mapToGlobal(position));
        QCoreApplication::sendEvent(header->viewport(), &event);
        return qobject_cast<QMenu *>(QApplication::activePopupWidget());
    }

    static QList<QAction *> columns(QMenu *menu)
    {
        QList<QAction *> result;
        for (auto *action : menu->actions()) {
            if (action->isCheckable()) {
                result.append(action);
            }
        }
        return result;
    }

private slots:
    void initTestCase() { new ColumnHeaderMenu(qApp); }

    void tableAndTreeControls_data()
    {
        QTest::addColumn<int>("type");
        QTest::newRow("tree-view") << 0;
        QTest::newRow("tree-widget") << 1;
        QTest::newRow("table-view") << 2;
        QTest::newRow("table-widget") << 3;
    }

    void tableAndTreeControls()
    {
        QFETCH(int, type);
        QStandardItemModel model(2, 3);
        const QStringList titles = {"Address", "Name&kind", ""};
        model.setHorizontalHeaderLabels(titles);
        std::unique_ptr<QAbstractItemView> view;
        QHeaderView *header = nullptr;
        switch (type) {
        case 0: {
            auto *tree = new QTreeView;
            tree->setModel(&model);
            header = tree->header();
            view.reset(tree);
            break;
        }
        case 1: {
            auto *tree = new QTreeWidget;
            tree->setColumnCount(3);
            tree->setHeaderLabels(titles);
            header = tree->header();
            view.reset(tree);
            break;
        }
        case 2: {
            auto *table = new QTableView;
            table->setModel(&model);
            header = table->horizontalHeader();
            view.reset(table);
            break;
        }
        case 3: {
            auto *table = new QTableWidget(2, 3);
            table->setHorizontalHeaderLabels(titles);
            header = table->horizontalHeader();
            view.reset(table);
            break;
        }
        }
        view->resize(600, 200);
        // A header menu must not propagate to a dock or item context menu.
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        QSignalSpy viewMenu(view.get(), &QWidget::customContextMenuRequested);
        view->show();
        QCoreApplication::processEvents();
        QVERIFY(header->sectionsMovable());
        QVERIFY(header->isFirstSectionMovable());

        const QPoint start(header->sectionViewportPosition(0) + header->sectionSize(0) / 2, 10);
        const QPoint end(header->sectionViewportPosition(2) + header->sectionSize(2) * 3 / 4, 10);
        QTest::mousePress(header->viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(header->viewport(), start + QPoint(QApplication::startDragDistance() + 1, 0));
        QTest::mouseMove(header->viewport(), end);
        QTest::mouseRelease(header->viewport(), Qt::LeftButton, Qt::NoModifier, end);
        QCOMPARE(header->visualIndex(0), 2);
        header->moveSection(2, 0);

        header->hideSection(1);
        header->moveSection(2, 0);
        auto *menu = openMenu(header);
        QVERIFY(menu);
        const auto actions = columns(menu);
        QCOMPARE(actions.size(), 3);
        QCOMPARE(actions[0]->data().toInt(), 2);
        QCOMPARE(actions[1]->data().toInt(), 0);
        QCOMPARE(actions[2]->data().toInt(), 1);
        QCOMPARE(actions[0]->text(), QString("Column 3"));
        QCOMPARE(actions[2]->text(), QString("Name&&kind"));
        QVERIFY(actions[0]->isChecked());
        QVERIFY(!actions[2]->isChecked());
        actions[0]->trigger();
        QVERIFY(header->isSectionHidden(2));
        menu->close();

        menu = openMenu(header);
        QVERIFY(menu);
        // The last visible column remains reachable; hidden ones can be restored.
        const auto lastVisible = columns(menu);
        QVERIFY(!lastVisible[1]->isEnabled());
        QVERIFY(lastVisible[0]->isEnabled());
        QVERIFY(lastVisible[2]->isEnabled());
        lastVisible[2]->trigger();
        QVERIFY(!header->isSectionHidden(1));
        menu->close();

        menu = openMenu(header);
        QVERIFY(menu);
        auto *showAll = menu->actions().at(menu->actions().size() - 2);
        auto *resetOrder = menu->actions().last();
        QVERIFY(showAll->isEnabled());
        QVERIFY(resetOrder->isEnabled());
        showAll->trigger();
        QCOMPARE(header->hiddenSectionCount(), 0);
        resetOrder->trigger();
        for (int logical = 0; logical < 3; ++logical) {
            QCOMPARE(header->visualIndex(logical), logical);
        }
        menu->close();
        QCOMPARE(viewMenu.count(), 0);

        // Body context menus keep their original behavior.
        const QPoint position(10, 10);
        QContextMenuEvent
            bodyEvent(QContextMenuEvent::Mouse, position, view->viewport()->mapToGlobal(position));
        QCoreApplication::sendEvent(view->viewport(), &bodyEvent);
        QCOMPARE(viewMenu.count(), 1);
    }

    void modelChangesAndKeyboardMenu()
    {
        RefreshableItemModel model(1, 2);
        model.setHorizontalHeaderLabels({"Address", "Name"});
        QTreeView view;
        view.setModel(&model);
        view.show();
        QCoreApplication::processEvents();
        auto *header = view.header();
        header->hideSection(1);
        header->moveSection(1, 0);
        // Row refreshes must not undo the user's column choices.
        model.removeRows(0, model.rowCount());
        model.insertRows(0, 3);
        model.refresh();
        QVERIFY(header->isSectionHidden(1));
        QCOMPARE(header->visualIndex(1), 0);

        model.setHeaderData(1, Qt::Horizontal, "Renamed");
        model.setColumnCount(3);
        model.setHeaderData(2, Qt::Horizontal, "Size");
        QContextMenuEvent event(QContextMenuEvent::Keyboard, QPoint(), QPoint());
        QCoreApplication::sendEvent(header, &event);
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        QVERIFY(menu);
        const auto actions = columns(menu);
        QCOMPARE(actions.size(), 3);
        QCOMPARE(actions[0]->text(), QString("Renamed"));
        QCOMPARE(actions[2]->text(), QString("Size"));
        QVERIFY(!actions[0]->isChecked());
        menu->close();

        QStandardItemModel replacement(2, 2);
        replacement.setHorizontalHeaderLabels({"New A", "New B"});
        view.setModel(&replacement);
        menu = openMenu(header);
        QVERIFY(menu);
        QCOMPARE(columns(menu).size(), 2);
        menu->close();
        view.setModel(nullptr);
    }

    void verticalHeadersKeepTheirBehavior()
    {
        QTableWidget table(2, 3);
        table.show();
        QCoreApplication::processEvents();
        QVERIFY(!table.verticalHeader()->sectionsMovable());
        table.verticalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
        QSignalSpy requested(table.verticalHeader(), &QWidget::customContextMenuRequested);
        QVERIFY(!openMenu(table.verticalHeader()));
        QCOMPARE(requested.count(), 1);
    }
};

QTEST_MAIN(ColumnHeaderTests)
#include "test_columnheaders.moc"
