#include "common/Configuration.h"
#include "common/IaitoSeekable.h"
#include "common/theme/IaitoStyle.h"
#include "common/theme/Theme.h"
#include "widgets/DisassemblerGraphView.h"

#include <QImage>
#include <QPainter>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

class GraphColorTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("graphcolors");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        qApp->setStyle(new IaitoStyle);
        Core()->initialize(false);
        Config()->loadInitial();
        Config()->setOutputRedirectionEnabled(false);
        Config()->setGraphBlockShadow(false);
        Config()->setGraphBlockEntryOffset(true);
        Config()->setFont(QFont("Inconsolata", 14));
        Core()->cmd("o malloc://64");
        Core()->setConfig("asm.arch", "x86");
        Core()->setConfig("asm.bits", 64);
        Core()->cmd("wx 554889e5b82a0000005dc3 @ 0");
        Core()->cmd("ompg +x");
        Core()->cmd("af @ 0");
    }

    void nativeNodeBackgrounds_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<bool>("custom");
        QTest::newRow("light") << false << false;
        QTest::newRow("light-custom") << false << true;
        QTest::newRow("dark") << true << false;
        QTest::newRow("dark-custom") << true << true;
    }

    void nativeNodeBackgrounds()
    {
        QFETCH(bool, dark);
        QFETCH(bool, custom);
        Config()->setInterfaceTheme(0); // Native
        QPalette native = Theme::builtin(dark ? "Dark" : "Light").toPalette(qApp->palette());
        // A platform's alternating-row color is unrelated to the assembly palette.
        const QColor alternate = dark ? QColor("#8f8f8f") : QColor("#707070");
        native.setColor(QPalette::AlternateBase, alternate);
        qApp->setPalette(native);
        QCoreApplication::processEvents();
        Config()->setColorTheme(dark ? "ayu" : "iaito");
        if (custom) {
            Config()->setColor("gui.alt_background", dark ? QColor("#18202a") : QColor("#f8f4ed"));
        }
        const QColor nodeBackground = QSettings().value("colors.gui.alt_background").value<QColor>();
        QVERIFY(nodeBackground != alternate);

        IaitoSeekable seekable;
        seekable.setSynchronization(false);
        seekable.seek(0);
        DisassemblerGraphView graph(nullptr, &seekable, nullptr, {});
        graph.refreshView();
        QCOMPARE(graph.disassembly_blocks.size(), size_t(1));
        auto blocks = graph.getBlocks();
        auto block = blocks.at(0);
        for (bool selected : {false, true}) {
            QImage image(block.width + 8, block.height + 8, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.translate(4 - block.x, 4 - block.y);
            graph.drawBlock(painter, block, selected);
            painter.end();
            const QString screenshots = qEnvironmentVariable("IAITO_GRAPH_SCREENSHOTS");
            if (!screenshots.isEmpty()) {
                QVERIFY(image.save(
                    QStringLiteral("%1/%2-%3.png")
                        .arg(screenshots, QTest::currentDataTag(), selected ? "selected" : "normal")));
            }
            const QColor expected = selected ? ConfigColor("gui.disass_selected") : nodeBackground;
            QCOMPARE(image.pixelColor(block.width, block.height), expected);
        }
        QCOMPARE(ConfigColor("gui.alt_background"), nodeBackground);
        // Fixing the code background must preserve the actual native widget palette.
        QCOMPARE(qApp->palette().color(QPalette::AlternateBase), alternate);
    }
};

QTEST_MAIN(GraphColorTests)
#include "test_graphcolors.moc"
