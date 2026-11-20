#include "common/Configuration.h"
#include "dialogs/settings/AsmOptionsWidget.h"
#include "widgets/DisassemblyWidget.h"

#include <QCheckBox>
#include <QContextMenuEvent>
#include <QImage>
#include <QMenu>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QSignalSpy>
#include <QSplitter>
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
    QSplitter *gutterSplitter() { return qobject_cast<QSplitter *>(leftPanel->parentWidget()); }
    QList<DisassemblyLine> readLines(RVA address)
    {
        refreshDisasm(address);
        return lines;
    }

    void showInstructions(bool addresses = false)
    {
        cachedLines.clear();
        for (int i = 0; i < 128; ++i) {
            const RVA address = 0x100 + i * 4;
            const QString text = addresses ? QStringLiteral(
                                                 "<span style='color:#123456'>0x%1</span> "
                                                 "<span style='color:#654321'>jmp 0x100</span>")
                                                 .arg(address, 8, 16, QLatin1Char('0'))
                                           : QStringLiteral("nop");
            cachedLines.append({address, text, RVA_INVALID});
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
        const QColor background = image.pixelColor(0, qRound(y * scale));
        const QColor halfColor(
            (color.red() + background.red()) / 2,
            (color.green() + background.green()) / 2,
            (color.blue() + background.blue()) / 2);
        for (int x = 0; x < leftPanel->width(); ++x) {
            const QColor pixel = image.pixelColor(qRound(x * scale), qRound(y * scale));
            // Antialiased one-pixel strokes straddle two physical pixels.
            const bool halfCovered = qAbs(pixel.red() - halfColor.red()) <= 1
                                     && qAbs(pixel.green() - halfColor.green()) <= 1
                                     && qAbs(pixel.blue() - halfColor.blue()) <= 1;
            if (pixel == color || halfCovered) {
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

    QMenu *openMenu(QWidget *widget)
    {
        const QPoint pos = widget->rect().center();
        QContextMenuEvent event(QContextMenuEvent::Mouse, pos, widget->mapToGlobal(pos));
        QCoreApplication::sendEvent(widget, &event);
        return qobject_cast<QMenu *>(QApplication::activePopupWidget());
    }

    void clearBlockColor(RVA address)
    {
        auto core = Core()->core();
        if (auto *block = r_anal_get_block_at(core->anal, address)) {
            block->color = {};
        }
        emit Config() -> colorsUpdated();
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("disassemblyjumps");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        const QString savedSettings = qEnvironmentVariable("IAITO_JUMP_SETTINGS_TEST_DIR");
        QSettings::setPath(
            QSettings::IniFormat,
            QSettings::UserScope,
            savedSettings.isEmpty() ? settings.path() : savedSettings);
        Core()->initialize(false);
        qRegisterMetaType<RVA>("RVA");
        Config()->loadInitial();
        Config()->setOutputRedirectionEnabled(false);
        Core()->cmd("o malloc://32768");
        Core()->cmd("ompg +x");
        Core()->setConfig("asm.arch", "x86");
        Core()->setConfig("asm.bits", 64);
    }

    void init()
    {
        if (qEnvironmentVariableIsSet("IAITO_JUMP_SETTINGS_TEST_DIR")) {
            return;
        }
        Config()->setConfig("asm.lines", true);
        Config()->setDisassemblyJumpLineAdaptiveSpacingEnabled(true);
        Config()->setDisassemblyJumpLineSpacing(16);
        Config()->setDisassemblyJumpLineWidth(120);
        Config()->setDisassemblyJumpLineFollowEnabled(false);
        Config()->setDisassemblyJumpLineCurvesEnabled(false);
        Config()->setDisassemblyJumpLineColorizeEnabled(false);
    }

    void cleanup()
    {
        Core()->cmd("af- 0x100");
        Core()->cmd("af- 0x5000");
        Core()->cmd("f- jump_color_flag");
    }

    void clickJump_data()
    {
        QTest::addColumn<int>("panelWidth");
        QTest::addColumn<QString>("part");
        QTest::addColumn<bool>("curved");
        for (bool curved : {false, true}) {
            for (int width : {32, 120}) {
                for (const char *part : {"vertical", "horizontal", "arrow", "up", "offscreen"}) {
                    const QByteArray name = QByteArray::number(width) + "-" + part
                                            + (curved ? "-curved" : "-square");
                    QTest::newRow(name.constData()) << width << QString::fromLatin1(part) << curved;
                }
            }
        }
    }

    void clickJump()
    {
        QFETCH(int, panelWidth);
        QFETCH(QString, part);
        QFETCH(bool, curved);
        Config()->setDisassemblyJumpLineCurvesEnabled(curved);
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
            widget.text()->viewport(),
            Qt::LeftButton,
            Qt::NoModifier,
            QPoint(100, widget.rowY(source)));
        QCOMPARE(widget.highlightedRows(), QSet<int>({source}));
        widget.getSeekable()->seek(0x100);
        QCOMPARE(widget.highlightedRows(), QSet<int>({0}));
    }

    void hideAndReopenGutter()
    {
        AsmOptionsWidget options(nullptr);
        auto *show = options.findChild<QCheckBox *>("jumpLinesCheckBox");
        QVERIFY(show && show->isChecked());
        TestDisassemblyWidget widget;
        widget.resize(900, 800);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        widget.showInstructions();
        auto *splitter = widget.gutterSplitter();
        splitter->setSizes({120, 776});
        const int oldWidth = widget.panel()->width();
        auto *menu = openMenu(widget.panel());
        QVERIFY(menu);
        QVERIFY(!menu->findChild<QAction *>("showJumpLines"));
        menu->close();
        show->setChecked(false);
        QVERIFY(!Config()->getConfigBool("asm.lines"));
        QCOMPARE(splitter->sizes().first(), 0);
        QVERIFY(widget.panel()->isHidden());
        QTRY_VERIFY(!splitter->handle(1)->isVisible());
        QCOMPARE(Config()->getDisassemblyJumpLineWidth(), oldWidth);

        // New listings must honor the saved setting and update alongside existing ones.
        TestDisassemblyWidget second;
        second.resize(900, 800);
        second.show();
        QVERIFY(QTest::qWaitForWindowExposed(&second));
        QVERIFY(second.panel()->isHidden());
        show->setChecked(true);
        QVERIFY(Config()->getConfigBool("asm.lines"));
        QVERIFY(widget.panel()->isVisible());
        QVERIFY(second.panel()->isVisible());
        QTRY_VERIFY(splitter->handle(1)->isVisible());
        QCOMPARE(splitter->sizes().first(), oldWidth);
        widget.showInstructions();
        QVERIFY(widget.verticalLine(17, ConfigColor("flow")).x() >= 0);

        show->setChecked(false);
        QVERIFY(QMetaObject::invokeMethod(&options, "resetToDefault"));
        QVERIFY(show->isChecked());
        QVERIFY(widget.panel()->isVisible());
        QVERIFY(second.panel()->isVisible());
    }

    void gutterCannotCollapse_data()
    {
        QTest::addColumn<int>("savedWidth");
        QTest::newRow("previously-collapsed") << 1;
        QTest::newRow("normal-width") << 120;
    }

    void gutterCannotCollapse()
    {
        QFETCH(int, savedWidth);
        Config()->setDisassemblyJumpLineWidth(savedWidth);
        TestDisassemblyWidget widget;
        widget.resize(900, 800);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        auto *splitter = widget.gutterSplitter();
        const int minimum = widget.panel()->minimumWidth();
        QVERIFY(minimum >= 32);
        QVERIFY(widget.panel()->width() >= minimum);
        QVERIFY(!splitter->isCollapsible(0));
        splitter->setSizes({0, splitter->width()});
        QCOMPARE(splitter->sizes().first(), minimum);

        auto *handle = splitter->handle(1);
        auto drag = [handle](int distance) {
            QTest::mousePress(handle, Qt::LeftButton);
            const QPoint pos = handle->rect().center() + QPoint(distance, 0);
            QMouseEvent move(
                QEvent::MouseMove,
                pos,
                handle->mapToGlobal(pos),
                Qt::NoButton,
                Qt::LeftButton,
                Qt::NoModifier);
            QCoreApplication::sendEvent(handle, &move);
            QTest::mouseRelease(handle, Qt::LeftButton);
        };
        drag(-500);
        QCOMPARE(splitter->sizes().first(), minimum);
        QVERIFY(Config()->getConfigBool("asm.lines"));
        QVERIFY(handle->isVisible());
        drag(80);
        QVERIFY(splitter->sizes().first() > minimum);
        QCOMPARE(Config()->getDisassemblyJumpLineWidth(), splitter->sizes().first());
    }

    void hiddenLinesDoNotRenderOrReceiveClicks()
    {
        TestDisassemblyWidget widget;
        widget.resize(900, 800);
        widget.panel()->setFixedWidth(120);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        widget.showInstructions();
        const QPoint point = widget.verticalLine(17, ConfigColor("flow"));
        QVERIFY(point.x() >= 0);
        // Leave space allocated to exercise the disabled renderer and stale hit paths.
        Config()->setConfig("asm.lines", false);
        QSignalSpy clicks(widget.panel(), &DisassemblyLeftPanel::jumpClicked);
        QTest::mouseClick(widget.panel(), Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(clicks.count(), 0);
        QCOMPARE(widget.verticalLine(17, ConfigColor("flow")), QPoint(-1, -1));
    }

    void hiddenLinesSkipReferenceAnalysis()
    {
        Core()->cmd("wx eb029090c3 @ 0x400");
        TestDisassemblyWidget widget;
        widget.resize(900, 800);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        const auto visible = widget.readLines(0x400);
        QVERIFY(!visible.isEmpty());
        QCOMPARE(visible.first().arrow, RVA(0x404));
        const int originalLimit = Core()->getConfigi("asm.lines.limit");
        Config()->setConfig("asm.lines", false);
        const auto hidden = widget.readLines(0x400);
        QVERIFY(!hidden.isEmpty());
        for (const auto &line : hidden) {
            QCOMPARE(line.arrow, RVA_INVALID);
        }
        QCOMPARE(Core()->getConfigi("asm.lines.limit"), originalLimit);
        Config()->setConfig("asm.lines", true);
        QCOMPARE(widget.readLines(0x400).first().arrow, RVA(0x404));
    }

    void followJumpOnClick()
    {
        TestDisassemblyWidget widget;
        widget.resize(900, 800);
        widget.panel()->setFixedWidth(120);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        auto *menu = openMenu(widget.panel());
        QVERIFY(menu);
        auto *follow = menu->findChild<QAction *>("followJumpLines");
        QVERIFY(follow && !follow->isChecked());
        follow->trigger();
        menu->close();
        widget.showInstructions();
        const QPoint point = widget.verticalLine(17, ConfigColor("flow"));
        QVERIFY(point.x() >= 0);
        QTest::mouseClick(widget.panel(), Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(widget.getSeekable()->getOffset(), RVA(0x5000));
        QVERIFY(widget.isJumpSelected(0x13c, 0x5000));
    }

    void laneSpacing()
    {
        Config()->setDisassemblyJumpLineAdaptiveSpacingEnabled(false);
        TestDisassemblyWidget widget;
        widget.resize(900, 800);
        widget.panel()->setFixedWidth(200);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        widget.showInstructions();
        const QImage image = widget.panel()->grab().toImage();
        const qreal scale = image.devicePixelRatio();
        int nearest = -1;
        for (int x = 0; x < widget.panel()->width(); ++x) {
            const QColor color = image.pixelColor(qRound(x * scale), qRound(widget.rowY(6) * scale));
            if (color == ConfigColor("flow") || color == ConfigColor("cflow")) {
                nearest = x;
            }
        }
        QCOMPARE(widget.panel()->width() - nearest, Config()->getDisassemblyJumpLineSpacing());
    }

    void functionColors()
    {
        Core()->cmd("wx 31c0c3 @ 0x100");
        Core()->cmd("wx 31c0c3 @ 0x5000");
        Core()->cmd("af+ 0x100 jump_test_source");
        Core()->cmd("afb+ 0x100 0x100 0x80");
        Core()->cmd("af+ 0x5000 jump_test_target");
        Core()->cmd("afb+ 0x5000 0x5000 0x10");
        QCOMPARE(Core()->getFunctionStart(0x13c), RVA(0x100));
        QCOMPARE(Core()->getFunctionStart(0x5000), RVA(0x5000));
        Config()->setDisassemblyJumpLineColorizeEnabled(true);
        {
            TestDisassemblyWidget widget;
            widget.resize(900, 800);
            widget.panel()->setFixedWidth(120);
            widget.show();
            QVERIFY(QTest::qWaitForWindowExposed(&widget));
            widget.showInstructions();
            QVERIFY(widget.verticalLine(6, ConfigColor("flow")).x() >= 0);
            QVERIFY(widget.verticalLine(17, ConfigColor("call")).x() >= 0);
            QCOMPARE(widget.verticalLine(17, ConfigColor("flow")), QPoint(-1, -1));
        }
        Core()->cmd("af- 0x100");
        Core()->cmd("af- 0x5000");
    }

    void customColors_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<bool>("colorByFunction");
        for (const char *kind : {"block", "flag", "function"}) {
            for (bool colorByFunction : {false, true}) {
                const QByteArray name = QByteArray(kind) + (colorByFunction ? "-grouped" : "");
                QTest::newRow(name.constData()) << QString::fromLatin1(kind) << colorByFunction;
            }
        }
    }

    void customColors()
    {
        QFETCH(QString, kind);
        QFETCH(bool, colorByFunction);
        Core()->cmd("wx 31c0c3 @ 0x100");
        Core()->cmd("wx 31c0c3 @ 0x120");
        Core()->cmd("af+ 0x100 jump_color_source");
        Core()->cmd("afb+ 0x100 0x100 0x20");
        Core()->cmd("afb+ 0x100 0x120 0x60");
        const QColor color("#e040a0");
        if (kind == "flag") {
            Core()->addFlag(0x120, "jump_color_flag", 0x30, color.name());
        } else {
            Core()->cmd(QStringLiteral("abc rgb:%1 @ %2")
                            .arg(color.name().mid(1))
                            .arg(kind == "block" ? 0x120 : 0x100));
        }
        Config()->setConfig("asm.addr", true);
        Config()->setDisassemblyJumpLineColorizeEnabled(colorByFunction);
        TestDisassemblyWidget widget;
        widget.resize(900, 800);
        widget.panel()->setFixedWidth(120);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        widget.showInstructions(true);
        QCOMPARE(widget.getAddressColor(0x13c), color);
        QVERIFY(widget.verticalLine(17, color).x() >= 0);
        const QImage image = widget.panel()->grab().toImage();
        const QColor tinted
            = image.pixelColor(0, qRound(widget.rowY(17) * image.devicePixelRatio()));
        QVERIFY(tinted != ConfigColor("gui.background").darker(115));

        const QTextBlock block = widget.text()->document()->findBlockByNumber(15);
        QTextCursor cursor(block);
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        QCOMPARE(cursor.charFormat().foreground().color(), color);
        cursor.setPosition(block.position() + block.text().lastIndexOf("0x100"));
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        QCOMPARE(cursor.charFormat().foreground().color(), QColor("#654321"));

        // Resetting a color must invalidate both the address and gutter caches.
        if (kind == "flag") {
            Core()->delFlag("jump_color_flag");
        } else {
            clearBlockColor(kind == "block" ? 0x120 : 0x100);
        }
        widget.showInstructions(true);
        QVERIFY(!widget.getAddressColor(0x13c).isValid());
        QCOMPARE(widget.verticalLine(17, color), QPoint(-1, -1));
        QTextCursor resetCursor(widget.text()->document()->findBlockByNumber(15));
        resetCursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        QCOMPARE(resetCursor.charFormat().foreground().color(), QColor("#123456"));
    }

    void customColorPriority()
    {
        Core()->cmd("wx 31c0c3 @ 0x100");
        Core()->cmd("wx 31c0c3 @ 0x120");
        Core()->cmd("af+ 0x100 jump_color_source");
        Core()->cmd("afb+ 0x100 0x100 0x20");
        Core()->cmd("afb+ 0x100 0x120 0x60");
        Core()->cmd("abc rgb:a02020 @ 0x100");
        Core()->cmd("abc rgb:20a020 @ 0x120");
        Core()->addFlag(0x110, "jump_color_flag", 0x20, "#2020a0");
        TestDisassemblyWidget widget;
        QCOMPARE(widget.getAddressColor(0x104), QColor("#a02020"));
        QCOMPARE(widget.getAddressColor(0x120), QColor("#20a020"));
        QCOMPARE(widget.getAddressColor(0x150), QColor("#20a020"));
        clearBlockColor(0x120);
        QCOMPARE(widget.getAddressColor(0x120), QColor("#2020a0"));
        QCOMPARE(widget.getAddressColor(0x150), QColor("#a02020"));
        Core()->delFlag("jump_color_flag");
        QCOMPARE(widget.getAddressColor(0x120), QColor("#a02020"));
    }

    void coloredOffscreenDestination()
    {
        Core()->addFlag(0x5000, "jump_color_flag", 1, "#e040a0");
        TestDisassemblyWidget widget;
        widget.resize(900, 800);
        widget.panel()->setFixedWidth(120);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        widget.showInstructions();
        QVERIFY(widget.verticalLine(17, QColor("#e040a0")).x() >= 0);
        QVERIFY(!widget.getAddressColor(0x13c).isValid());
    }

    void instructionAddressColors_data()
    {
        QTest::addColumn<QString>("base");
        for (const char *base : {"8", "10", "16", "36"}) {
            QTest::newRow(base) << QString::fromLatin1(base);
        }
    }

    void instructionAddressColors()
    {
        QFETCH(QString, base);
        Config()->setConfig("asm.addr.base", base);
        Core()->addFlag(0x400, "jump_color_flag", 8, "#e040a0");
        Core()->cmd("wx eb029090c3 @ 0x400");
        TestDisassemblyWidget widget;
        widget.resize(900, 800);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        widget.readLines(0x400);
        bool found = false;
        for (QTextBlock block = widget.text()->document()->firstBlock(); block.isValid();
             block = block.next()) {
            if (!block.text().contains("jmp")) {
                continue;
            }
            QTextCursor cursor(block);
            cursor.setPosition(block.position() + block.text().indexOf(QRegularExpression("\\S")));
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            QCOMPARE(cursor.charFormat().foreground().color(), QColor("#e040a0"));
            found = true;
            break;
        }
        QVERIFY(found);
        Config()->setConfig("asm.addr.base", "16");
    }

    void savedOptions()
    {
        TestDisassemblyWidget widget;
        widget.resize(900, 800);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        auto *menu = openMenu(widget.panel());
        QVERIFY(menu);
        for (const char *name :
             {"followJumpLines", "curveJumpLines", "colorizeJumpLines", "fitJumpLines"}) {
            auto *action = menu->findChild<QAction *>(name);
            QVERIFY(action);
            action->trigger();
        }
        menu->close();
        AsmOptionsWidget options(nullptr);
        auto *show = options.findChild<QCheckBox *>("jumpLinesCheckBox");
        QVERIFY(show && show->isChecked());
        show->setChecked(false);
        QSettings().sync();
        QProcess child;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("IAITO_JUMP_SETTINGS_TEST_DIR", settings.path());
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(), {"restoredOptions"});
        QVERIFY(child.waitForFinished(30000));
        const QByteArray output = child.readAllStandardOutput() + child.readAllStandardError();
        QVERIFY2(
            child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0, output.constData());
    }

    void restoredOptions()
    {
        if (!qEnvironmentVariableIsSet("IAITO_JUMP_SETTINGS_TEST_DIR")) {
            QSKIP("Run by savedOptions in a fresh process");
        }
        QVERIFY(!Config()->getConfigBool("asm.lines"));
        QVERIFY(Config()->getDisassemblyJumpLineFollowEnabled());
        QVERIFY(Config()->getDisassemblyJumpLineCurvesEnabled());
        QVERIFY(Config()->getDisassemblyJumpLineColorizeEnabled());
        QVERIFY(!Config()->getDisassemblyJumpLineAdaptiveSpacingEnabled());
        TestDisassemblyWidget widget;
        widget.resize(900, 800);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        QCOMPARE(widget.gutterSplitter()->sizes().first(), 0);
        QVERIFY(widget.panel()->isHidden());
        QVERIFY(!widget.gutterSplitter()->handle(1)->isVisible());
    }
};

QTEST_MAIN(DisassemblyJumpTests)
#include "test_disassemblyjumps.moc"
