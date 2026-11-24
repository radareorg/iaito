#include "common/Configuration.h"
#include "core/MainWindow.h"
#include "widgets/BinaryComparisonWidget.h"

#include <QFileDialog>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>
#include <QWheelEvent>

class BinaryComparisonTests : public QObject
{
    Q_OBJECT

    QTemporaryDir temp;

    static QByteArray sampleBinary()
    {
        // ELF64 with a five-byte named function and a separate one-byte entry point.
        return QByteArray::fromBase64(
            "f0VMRgIBAQAAAAAAAAAAAAIAPgABAAAAfQBAAAAAAABAAAAAAAAAAOgAAAAAAAAAAAAAAEAAOAA"
            "BAEAABQAEAAEAAAAFAAAAAAAAAAAAAAAAAEAAAAAAAAAAQAAAAAAAfgAAAAAAAAB+AAAAAAAAAA"
            "AQAAAAAAAAuAIAAADDAHJlY292ZXJlZF9uYW1lAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
            "QAAABIAAQB4AEAAAAAAAAYAAAAAAAAAAC50ZXh0AC5zdHJ0YWIALnN5bXRhYgAuc2hzdHJ0YWIA"
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
            "AAAAAAAAAAAAAAAAAAAABAAAAAQAAAAYAAAAAAAAAeABAAAAAAAB4AAAAAAAAAAYAAAAAAAAAAA"
            "AAAAAAAAABAAAAAAAAAAAAAAAAAAAABwAAAAMAAAAAAAAAAAAAAAAAAAAAAAAAfgAAAAAAAAAQA"
            "AAAAAAAAAAAAAAAAAAAAQAAAAAAAAAAAAAAAAAAAA8AAAACAAAAAAAAAAAAAAAAAAAAAAAAAJAA"
            "AAAAAAAAMAAAAAAAAAACAAAAAQAAAAgAAAAAAAAAGAAAAAAAAAAXAAAAAwAAAAAAAAAAAAAAAAA"
            "AAAAAAADAAAAAAAAAACEAAAAAAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAAAAA");
    }

    void writeFile(const QString &path, const QByteArray &bytes)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
    }

    void browse(QWidget *dock, const char *button, const QString &path)
    {
        QTimer::singleShot(0, this, [dock, path] {
            auto *dialog = dock->findChild<QFileDialog *>();
            QVERIFY(dialog);
            dialog->selectFile(path);
            QMetaObject::invokeMethod(dialog, "accept");
        });
        dock->findChild<QPushButton *>(button)->click();
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("binarycomparison");
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
        Core()->initialize(false);
        Config()->loadInitial();
        Config()->setOutputRedirectionEnabled(false);
    }

    void diskDiffKeepsOneSidedData()
    {
        const QString current = temp.filePath("current image.img");
        const QString reference = temp.filePath("reference image.img");
        writeFile(current, QByteArray(8192, 'A'));
        writeFile(reference, QByteArray(4096, 'A') + QByteArray(4096, 'B') + "tail");
        MainWindow window;
        window.findChild<QAction *>("compareDiskImagesAction")->trigger();
        auto *dock = window.findChild<BinaryComparisonWidget *>();
        QVERIFY(dock);
        browse(dock, "leftBrowse", current);
        browse(dock, "rightBrowse", reference);
        dock->findChild<QPushButton *>("compareButton")->click();
        auto *results = dock->findChild<QTreeWidget *>("results");
        QTRY_COMPARE_WITH_TIMEOUT(results->topLevelItemCount(), 3, 10000);
        QVERIFY(results->topLevelItem(0)->text(2).contains("collapsed"));
        results->itemActivated(results->topLevelItem(2), 0);
        auto *hex = dock->findChild<QTableView *>("hexTable")->model();
        QTRY_COMPARE_WITH_TIMEOUT(hex->rowCount(), 1, 10000);
        QCOMPARE(hex->index(0, 0).data().toString(), QString());
        QVERIFY(hex->index(0, 1).data().toString().contains("74 61 69 6c"));
    }

    void graphsFillTheTabAndCanBeResized()
    {
        MainWindow window;
        window.resize(1200, 760);
        window.show();
        window.findChild<QAction *>("compareBinaryAction")->trigger();
        auto *dock = window.findChild<BinaryComparisonWidget *>();
        dock->setFloating(true);
        dock->resize(1000, 700);
        auto *tabs = dock->findChild<QTabWidget *>("details");
        auto *graphs = dock->findChild<QSplitter *>("graphs");
        auto *legend = dock->findChild<QLabel *>("graphLegend");
        tabs->setCurrentWidget(dock->findChild<QWidget *>("graphTab"));
        QTRY_VERIFY(graphs->isVisible());
        QTRY_VERIFY(graphs->height() > tabs->currentWidget()->height() / 2);
        QVERIFY(legend->height() <= legend->sizeHint().height());

        auto *handle = graphs->handle(1);
        QVERIFY(handle->isVisible());
        QVERIFY(handle->width() >= 8);
        const int before = graphs->sizes().first();
        const QPoint start = handle->rect().center();
        QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(handle, start + QPoint(60, 0));
        QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, handle->rect().center());
        QVERIFY(graphs->sizes().first() > before);
    }

    void functionDiffAndNameImport()
    {
        const QByteArray binary = sampleBinary();
        QCOMPARE(binary.size(), 552);
        const QString current = temp.filePath("current binary");
        const QString reference = temp.filePath("reference binary");
        QByteArray before = binary;
        before[121] = 1;
        before.replace("recovered_name", "generic_helper");
        writeFile(current, before);
        writeFile(reference, binary);
        QVERIFY(Core()->loadFile(current, RVA_INVALID, RVA_INVALID, R_PERM_R, 1, false, true));
        Core()->cmd("aa; afn fcn.00400078 0x400078");
        MainWindow window;
        window.setFilename(current);
        window.findChild<QAction *>("compareBinaryAction")->trigger();
        auto *dock = window.findChild<BinaryComparisonWidget *>();
        browse(dock, "rightBrowse", reference);
        dock->findChild<QPushButton *>("compareButton")->click();
        auto *results = dock->findChild<QTreeWidget *>("results");
        QTRY_COMPARE_WITH_TIMEOUT(results->topLevelItemCount(), 2, 10000);
        const auto items = results->findItems("sym.recovered_name", Qt::MatchContains, 1);
        QCOMPARE(items.size(), 1);
        auto *match = items.first();
        match->setSelected(true);
        results->itemActivated(match, 0);
        auto *codeTable = dock->findChild<QTableView *>("codeTable");
        auto *hexTable = dock->findChild<QTableView *>("hexTable");
        auto *code = codeTable->model();
        QTRY_COMPARE_WITH_TIMEOUT(code->rowCount(), 1, 10000);
        QVERIFY(code->index(0, 0).data().toString().contains("mov eax, 1"));
        QVERIFY(code->index(0, 1).data().toString().contains("mov eax, 2"));

        const int codeHeight = codeTable->rowHeight(0);
        const int hexHeight = hexTable->rowHeight(0);
        Config()->setZoomFactor(2.0);
        for (auto *table : {codeTable, hexTable}) {
            QCOMPARE(
                table->model()->index(0, 0).data(Qt::FontRole).value<QFont>(), Config()->getFont());
        }
        QVERIFY(codeTable->rowHeight(0) > codeHeight);
        QVERIFY(hexTable->rowHeight(0) > hexHeight);

        QWheelEvent wheel(
            QPointF(),
            QPointF(),
            QPoint(),
            QPoint(0, 120),
            Qt::NoButton,
            Qt::ControlModifier,
            Qt::NoScrollPhase,
            false);
        QApplication::sendEvent(codeTable->viewport(), &wheel);
        QCOMPARE(Config()->getZoomFactor(), 2.1);
        window.findChild<QAction *>("actionZoomReset")->trigger();
        QCOMPARE(Config()->getZoomFactor(), 1.0);
        QCOMPARE(codeTable->rowHeight(0), codeHeight);
        QCOMPARE(hexTable->rowHeight(0), hexHeight);

        dock->findChild<QPushButton *>("importButton")->click();
        QCOMPARE(Core()->cmdRawAt("afn", 0x400078).trimmed(), QString("sym.recovered_name"));
    }

    void typeAndZignatureRoundTrip()
    {
        const QString types = temp.filePath("custom types.sdb");
        QVERIFY2(
            Core()->addTypes("struct ComparisonType { int value; };").isEmpty(),
            "C declaration import failed");
        QVERIFY(Core()->saveTypeDatabase(types));
        Core()->cmdRaw("t- ComparisonType");
        QVERIFY(Core()->loadTypeDatabase(types));
        QVERIFY(Core()->getTypeAsC("ComparisonType", "Struct").contains("int value"));

        const QString zigns = temp.filePath("custom zignatures.sdb");
        Core()->cmdRaw("za ComparisonZignature b b801000000");
        QVERIFY(Core()->saveZignatureDatabase(zigns));
        Core()->cmdRaw("z-*");
        QVERIFY(Core()->loadZignatureDatabase(zigns));
        const auto signatures = Core()->getAllZignatures();
        QCOMPARE(signatures.size(), 1);
        QCOMPARE(signatures.constFirst().name, QString("ComparisonZignature"));
    }

    void flirtInspectionAndScan()
    {
        const QString path = temp.filePath("sample flirt.sig");
        writeFile(
            path,
            QByteArray::fromBase64(
                "SURBU0dOBwD//////////wAAAAAAAAAAAAAAAAAAAAAAAAAAAAEAAAABIMeAAABBVkFVuEFU"
                "VU2JxFNIic1Nic1IgeyQAAAASIXAABbSooKYAF9fbGliY19zdGFydF9tYWluAA=="));
        const auto report = Core()->inspectFlirtSignature(path, false);
        QVERIFY(report.contains("__libc_start_main"));
        QSignalSpy refresh(Core(), &IaitoCore::refreshAll);
        QCOMPARE(Core()->inspectFlirtSignature(path, true), report);
        QCOMPARE(refresh.count(), 1);
    }

    void warpPluginRoundTrip()
    {
        const QString plugin = qEnvironmentVariable("IAITO_TEST_WARP_PLUGIN");
        if (plugin.isEmpty()) {
            QSKIP("Set IAITO_TEST_WARP_PLUGIN to test with a compatible r2warp library.");
        }
        {
            MainWindow window;
            QVERIFY(window.findChild<QToolButton *>("warpButton")->isHidden());
        }
        Core()->cmdRaw(QStringLiteral("L ") + plugin);
        const QString binary = temp.filePath("warp binary");
        writeFile(binary, sampleBinary());
        QVERIFY(Core()->loadFile(binary, RVA_INVALID, RVA_INVALID, R_PERM_R, 1, false, true));
        Core()->cmdRaw("aa");
        MainWindow window;
        QVERIFY(!window.findChild<QToolButton *>("warpButton")->isHidden());
        const QString created = Core()->cmdRaw("zw create -a");
        QVERIFY2(created.contains("Created signatures"), qPrintable(created));
        const QString signatures = temp.filePath("warp signatures.warp");
        QVERIFY(Core()->saveWarpDatabase(signatures));
        Core()->cmdRaw("zw clear");
        const QString loaded = Core()->loadWarpDatabase(signatures);
        QVERIFY2(loaded.contains("Successfully loaded"), qPrintable(loaded));
        const QString matched = Core()->cmdRaw("zw match -a");
        QVERIFY2(matched.contains("Matched: 2 / 2"), qPrintable(matched));
    }
};

QTEST_MAIN(BinaryComparisonTests)
#include "test_binarycomparison.moc"
