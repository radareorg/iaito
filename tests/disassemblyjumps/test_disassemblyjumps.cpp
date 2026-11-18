#include "common/Configuration.h"
#include "widgets/DisassemblyWidget.h"

#include <QImage>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

class TestDisassemblyWidget : public DisassemblyWidget
{
public:
    TestDisassemblyWidget()
        : DisassemblyWidget(nullptr)
    {
        getSeekable()->setSynchronization(false);
        getSeekable()->seek(0x100);
    }

    DisassemblyLeftPanel *panel() { return leftPanel; }
    DisassemblyTextEdit *text() { return mDisasTextEdit; }

    void showInstructions()
    {
        cachedLines.clear();
        for (int i = 0; i < 128; ++i) {
            cachedLines.append({RVA(0x100 + i * 4), QStringLiteral("nop"), RVA_INVALID});
        }
        cachedLines[1].arrow = 0x128;
        cachedLines[3].arrow = 0x120;
        cachedLines[10].arrow = 0x104;
        cachedLines[15].arrow = 0x5000;
        useCachedLines = true;
        refreshDisasm(0x100);
        QCoreApplication::processEvents();
    }

    int rowY(int row) { return qRound(mDisasTextEdit->blockRects().at(row).center().y()); }

    QSet<int> highlightedRows()
    {
        QSet<int> rows;
        for (const auto &selection : mDisasTextEdit->extraSelections()) {
            if (selection.format.boolProperty(QTextFormat::FullWidthSelection)
                && selection.format.background().color() == ConfigColor("lineHighlight")) {
                rows.insert(selection.cursor.blockNumber());
            }
        }
        return rows;
    }

    // Locate an actual rendered stroke instead of duplicating the lane layout calculation.
    QPoint verticalLine(int row, const QColor &color)
    {
        const QImage image = leftPanel->grab().toImage();
        const qreal scale = image.devicePixelRatio();
        const int y = rowY(row);
        for (int x = 0; x < leftPanel->width(); ++x) {
            if (image.pixelColor(qRound(x * scale), qRound(y * scale)) == color) {
                return {x, y};
            }
        }
        return {-1, -1};
    }
};

class DisassemblyJumpTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("disassemblyjumps");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        Core()->initialize(false);
        qRegisterMetaType<RVA>("RVA");
        Config()->loadInitial();
        Config()->setOutputRedirectionEnabled(false);
        Config()->setDisassemblyJumpLineAdaptiveSpacingEnabled(true);
        Config()->setDisassemblyJumpLineSpacing(16);
        Core()->cmd("o malloc://16384");
        Core()->setConfig("asm.arch", "x86");
        Core()->setConfig("asm.bits", 64);
    }

    void clickJump_data()
    {
        QTest::addColumn<int>("panelWidth");
        QTest::addColumn<QString>("part");
        for (int width : {32, 120}) {
            for (const char *part : {"vertical", "horizontal", "arrow", "up", "offscreen"}) {
                const QByteArray name = QByteArray::number(width) + "-" + part;
                QTest::newRow(name.constData()) << width << QString::fromLatin1(part);
            }
        }
    }

    void clickJump()
    {
        QFETCH(int, panelWidth);
        QFETCH(QString, part);
        TestDisassemblyWidget widget;
        widget.resize(900, 800);
        widget.panel()->setFixedWidth(panelWidth);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        widget.showInstructions();

        QPoint point;
        int source = 1;
        int target = 10;
        if (part == "vertical") {
            point = widget.verticalLine(6, ConfigColor("flow"));
        } else if (part == "horizontal") {
            source = 3;
            target = 8;
            point = {panelWidth - 3, widget.rowY(source)};
        } else if (part == "arrow") {
            point = {panelWidth - 3, widget.rowY(target) + 2};
        } else if (part == "up") {
            source = 10;
            target = 1;
            point = widget.verticalLine(6, ConfigColor("cflow"));
        } else {
            source = 15;
            target = -1;
            point = widget.verticalLine(17, ConfigColor("flow"));
        }
        QVERIFY(point.x() >= 0);
        QTest::mouseMove(widget.panel(), point);
        QCOMPARE(widget.panel()->cursor().shape(), Qt::PointingHandCursor);
        QTest::mouseClick(widget.panel(), Qt::RightButton, Qt::NoModifier, point);
        QCOMPARE(widget.getSeekable()->getOffset(), RVA(0x100));
        QTest::mouseClick(widget.panel(), Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(widget.getSeekable()->getOffset(), RVA(0x100 + source * 4));
        const RVA destination = target < 0 ? 0x5000 : 0x100 + target * 4;
        QVERIFY(widget.isJumpSelected(0x100 + source * 4, destination));
        QSet<int> expected = {source};
        if (target >= 0) {
            expected.insert(target);
        }
        QCOMPARE(widget.highlightedRows(), expected);

        // Clicking the current instruction must also clear the second endpoint.
        QTest::mouseClick(
            widget.text()->viewport(), Qt::LeftButton, Qt::NoModifier,
            QPoint(100, widget.rowY(source)));
        QCOMPARE(widget.highlightedRows(), QSet<int>({source}));
        widget.getSeekable()->seek(0x100);
        QCOMPARE(widget.highlightedRows(), QSet<int>({0}));
    }
};

QTEST_MAIN(DisassemblyJumpTests)
#include "test_disassemblyjumps.moc"
