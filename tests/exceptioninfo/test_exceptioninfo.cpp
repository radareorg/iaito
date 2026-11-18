#include "common/ExceptionInfo.h"
#include "widgets/ExceptionBar.h"
#include <QJsonDocument>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtTest>

class ExceptionInfoTest : public QObject
{
    Q_OBJECT
private slots:
    void compactBar()
    {
        QWidget pane;
        auto *layout = new QVBoxLayout(&pane);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        auto *bar = new ExceptionBar(&pane);
        auto *listing = new QWidget(&pane);
        listing->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
        layout->addWidget(bar);
        layout->addWidget(listing, 1);
        ExceptionRegion region;
        region.from = 0x100;
        region.to = 0x120;
        region.handler = 0x200;
        bar->setRegions({region}, 0x100);
        pane.resize(800, 700);
        pane.show();
        QApplication::processEvents();
        QCOMPARE(bar->height(), bar->sizeHint().height());
        QCOMPARE(bar->height(), bar->findChild<QToolButton *>()->sizeHint().height());
        QVERIFY(listing->height() > 600);
        pane.resize(800, 1000);
        QApplication::processEvents();
        QCOMPARE(bar->height(), bar->sizeHint().height());
        QVERIFY(listing->height() > 900);
        bar->setRegions({}, 0x100);
        QApplication::processEvents();
        QVERIFY(bar->isHidden());
        QCOMPARE(listing->height(), pane.height());
    }

    void navigation()
    {
        ExceptionBar bar;
        ExceptionRegion region;
        region.from = 0x100;
        region.to = 0x120;
        region.handler = 0x200;
        bar.setRegions({region}, 0x100);
        QSignalSpy seeks(&bar, &ExceptionBar::seekRequested);
        QMenu menu;
        bar.populateMenu(&menu, 0x100);
        QCOMPARE(menu.actions().size(), 1);
        menu.actions().first()->menu()->actions().first()->trigger();
        QCOMPARE(seeks.size(), 1);
        QCOMPARE(seeks.first().first().toULongLong(), quint64(0x200));
        bar.populateMenu(&menu, 0x120);
        QVERIFY(menu.isEmpty());
        bar.populateMenu(&menu, 0x200);
        QCOMPARE(menu.actions().size(), 1);
    }

    void functionScope()
    {
        const auto regions = ExceptionRegion::parse(QJsonDocument::fromJson(R"([
            {"from":256,"to":272,"handler":512},
            {"from":288,"to":304,"handler":544}
        ])").array());
        const auto withException = QJsonDocument::fromJson(R"([
            {"addr":256,"size":8}, {"addr":264,"size":8}
        ])").array();
        const auto withoutException = QJsonDocument::fromJson(R"([
            {"addr":272,"size":8}, {"addr":320,"size":8}
        ])").array();
        // Only actual member blocks count, not the gap in the function's span
        // or exceptions belonging to neighboring functions in the listing.
        QCOMPARE(ExceptionRegion::forBlocks(regions, withException).size(), 1);
        QVERIFY(ExceptionRegion::forBlocks(regions, withoutException).isEmpty());
        QVERIFY(ExceptionRegion::forBlocks(regions, {}).isEmpty());
        QVERIFY(ExceptionRegion::forBlocks({}, withException).isEmpty());
        const auto handler = QJsonDocument::fromJson(R"([{"addr":512,"size":8}])").array();
        QCOMPARE(ExceptionRegion::forBlocks(regions, handler).size(), 1);
    }

    void ranges()
    {
        ExceptionRegion region;
        region.from = 0x100;
        region.to = 0x120;
        region.handler = 0x200;
        QVERIFY(region.contains(0x100));
        QVERIFY(region.contains(0x11f));
        QVERIFY(!region.contains(0x120));
        QVERIFY(!region.protects(0xf0, 0x10));
        QVERIFY(region.protects(0xf0, 0x11));
        QVERIFY(region.protects(0x110, 0x100));
        QVERIFY(!region.protects(0x120, 0x10));
        QVERIFY(!region.protects(0x100, 0));
        QVERIFY(region.touches(0x1f0, 0x11));
        QVERIFY(!region.touches(0x1f0, 0x10));
        QVERIFY(!region.touches(0x201, 1));
        QVERIFY(!region.protects(ExceptionRegion::Invalid - 1, 10));
        region.from = ExceptionRegion::Invalid - 10;
        region.to = ExceptionRegion::Invalid - 1;
        QVERIFY(region.protects(ExceptionRegion::Invalid - 5, 10));
    }

    void metadata()
    {
        const auto json = QJsonDocument::fromJson(R"([
            {"index":7,"source":64,"from":256,"to":288,"handler":512,
             "kind":"catch","type":"std::runtime_error","typeFilter":-2},
            {"from":256,"to":288,"handler":528,"kind":"catch","catchAll":true},
            {"from":260,"to":276,"handler":544,"kind":"cleanup"},
            {"from":256,"to":288,"handler":560,"kind":"filter","filter":600},
            {"from":256,"to":288,"handler":576}
        ])");
        const auto regions = ExceptionRegion::parse(json.array());
        QCOMPARE(regions.size(), 5);
        QCOMPARE(regions[0].index, quint64(7));
        QCOMPARE(regions[0].typeFilter, qint64(-2));
        QVERIFY(regions[0].label().contains("std::runtime_error"));
        QVERIFY(regions[1].label().contains("all exceptions"));
        QVERIFY(regions[2].label().contains("Cleanup"));
        QVERIFY(regions[3].label().contains("Filter"));
        QCOMPARE(regions[3].filter, quint64(600));
        QVERIFY(regions[4].label().contains("unknown type"));
        QCOMPARE(regions[0].color(), regions[1].color());
        QVERIFY(regions[2].contains(260));
        QVERIFY(!regions[2].contains(276));
    }

    void invalidAndUnavailable()
    {
        QVERIFY(ExceptionRegion::parse({}).isEmpty());
        const auto json = QJsonDocument::fromJson(R"([
            {}, null, {"from":0,"to":8},
            {"from":8,"to":8,"handler":16},
            {"from":9,"to":8,"handler":16},
            {"from":-1,"to":8,"handler":16},
            {"from":1.5,"to":8,"handler":16},
            {"from":true,"to":8,"handler":16},
            {"from":0,"to":8,"handler":null},
            {"from":"bogus","to":8,"handler":16}
        ])");
        QVERIFY(ExceptionRegion::parse(json.array()).isEmpty());
        // Older radare2 can return text flag commands instead of JSON.
        const auto legacy = QJsonDocument::fromJson("f try.0.from=0x100\n");
        QVERIFY(ExceptionRegion::parse(legacy.array()).isEmpty());
    }

    void addressPrecision()
    {
        const auto json = QJsonDocument::fromJson(R"([
            {"from":"0xffff800000000001","to":"0xffff800000000010",
             "handler":"0xffff800000000020"},
            {"from":0,"to":8,"handler":0}
        ])");
        const auto regions = ExceptionRegion::parse(json.array());
        QCOMPARE(regions.size(), 2);
        QCOMPARE(regions[0].from, Q_UINT64_C(0xffff800000000001));
        QVERIFY(regions[0].contains(Q_UINT64_C(0xffff80000000000f)));
        QVERIFY(!regions[0].contains(Q_UINT64_C(0xffff800000000010)));
        QVERIFY(regions[1].contains(0));
        QCOMPARE(regions[1].handler, quint64(0));
    }
};

QTEST_MAIN(ExceptionInfoTest)
#include "test_exceptioninfo.moc"
