#include "common/Configuration.h"
#include "common/TempConfig.h"
#include "core/Iaito.h"
#include "widgets/DisassemblyWidget.h"

#include <QFontDatabase>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

class TestDisassemblyWidget : public DisassemblyWidget
{
public:
    TestDisassemblyWidget()
        : DisassemblyWidget(nullptr)
    {}

    void showListing(const QList<DisassemblyLine> &listing, int first)
    {
        cachedLines = listing;
        useCachedLines = true;
        refreshDisasm(listing.at(first).offset);
    }

    void rebuildListing(const QList<DisassemblyLine> &listing, int first)
    {
        lines.clear();
        showListing(listing, first);
    }

    void setFunctionRange(RVA start, RVA end)
    {
        functionRanges.clear();
        functionRanges.insert(start, end);
    }

    void showTokenLine(const QString &text, RVA offset = 0x100, int lineIndex = 0)
    {
        QList<DisassemblyLine> listing;
        for (int i = 0; i < 300; ++i) {
            DisassemblyLine line;
            line.offset = offset + i * 4;
            line.text = i == lineIndex ? text : QStringLiteral("ret");
            listing.append(line);
        }
        showListing(listing, 0);
    }
};

class DisassemblyTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;

    static QImage screenImage(QWidget *viewport)
    {
        // QWidget::grab() repaints the widget, hiding stale backing-store pixels.
        QWidget *window = viewport->window();
        const QPoint position = viewport->mapTo(window, QPoint());
        return window->screen()
            ->grabWindow(
                window->winId(), position.x(), position.y(), viewport->width(), viewport->height())
            .toImage();
    }

private slots:
    void initTestCase()
    {
        // Avoid animated overlay scrollbars in the pixel comparisons.
        QApplication::setStyle("Fusion");
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("disassembly");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        Core()->initialize(false);
        qRegisterMetaType<RVA>("RVA");
        Config()->loadInitial();
        Core()->cmd("o malloc://16384");
        Core()->setConfig("asm.arch", "arm");
        Core()->setConfig("asm.bits", 64);
        Core()->seek(0);
        Core()->cmd("f click.Flag 1 @ 0x320");
        Core()->cmd("f sym.click_target 1 @ 0x340");
        Core()->cmd("af+ 0x360 click_function");
        Core()->cmd("wx 1f2003d5c0035fd6 @ 0x1000");
        Core()->cmd("af+ 0x1000 header_function");
        Core()->cmd("afb+ 0x1000 0x1000 8");
        Core()->cmd("afvr x0 arg0 int @ 0x1000");
        Core()->cmd("f header_flag 1 @ 0x1000");
        Core()->cmd("f interior_flag 1 @ 0x1004");
        Core()->cmd("f outside_flag 1 @ 0x1020");
    }

    void functionHeaderBackground_data()
    {
        QTest::addColumn<bool>("offsets");
        QTest::addColumn<bool>("flagOffsets");
        QTest::addColumn<bool>("calls");
        QTest::addColumn<int>("summary");
        QTest::newRow("default") << true << false << true << 4;
        QTest::newRow("hidden-addresses") << false << false << true << 4;
        QTest::newRow("flag-addresses") << true << true << true << 4;
        QTest::newRow("function-label") << true << true << false << 4;
        QTest::newRow("expanded-arguments") << false << true << true << 0;
        QTest::newRow("register-summary") << true << false << true << 1;
        QTest::newRow("argument-counts") << true << false << true << 2;
        QTest::newRow("compact-summary") << true << false << true << 3;
    }

    void functionHeaderBackground()
    {
        QFETCH(bool, offsets);
        QFETCH(bool, flagOffsets);
        QFETCH(bool, calls);
        QFETCH(int, summary);
        TempConfig config;
        config.set("asm.addr", offsets)
            .set("asm.flags.offset", flagOffsets)
            .set("asm.cmt.calls", calls)
            .set("asm.var.summary", summary)
            .set("asm.functions", true)
            .set("asm.flags", true)
            .set("asm.flags.inline", false)
            .set("asm.var", true)
            .set("asm.lines", false)
            .set("scr.color", COLOR_MODE_16M);

        TestDisassemblyWidget widget;
        widget.getSeekable()->setSynchronization(false);
        widget.resize(1100, 700);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        auto *edit = qobject_cast<DisassemblyTextEdit *>(widget.getTextWidget());
        QVERIFY(edit);
        const auto listing = Core()->disassembleLines(0xffc, 300);
        widget.getSeekable()->seek(0x1004);
        widget.showListing(listing, 0);

        QColor headerColor = ConfigColor("lineHighlight");
        headerColor.setAlpha(headerColor.alpha() / 2);
        const auto verifyBackgrounds = [&]() {
            bool flag = false;
            bool signature = false;
            bool arguments = false;
            bool instruction = false;
            bool interiorFlag = false;
            bool outsideFlag = false;
            for (QTextBlock block = edit->document()->firstBlock(); block.isValid();
                 block = block.next()) {
                const QString text = block.text();
                const QBrush background = block.blockFormat().background();
                const bool argumentLine = text.contains("args(") || text.contains("; arg ")
                                          || text.contains("args:") || text.contains("vars:")
                                          || text.contains("rg:") || text.contains("bp:")
                                          || text.contains("sp:");
                if (text.contains("header_flag") || text.contains("header_function")
                    || argumentLine) {
                    QCOMPARE(background.color(), headerColor);
                    flag |= text.contains("header_flag");
                    signature |= text.contains("header_function");
                    arguments |= argumentLine;
                } else {
                    QCOMPARE(background.style(), Qt::NoBrush);
                    instruction |= text.contains("nop");
                    interiorFlag |= text.contains("interior_flag");
                    outsideFlag |= text.contains("outside_flag");
                }
            }
            QVERIFY(flag);
            QVERIFY(signature);
            QVERIFY2(arguments, qPrintable(edit->toPlainText()));
            QVERIFY(instruction);
            QVERIFY(interiorFlag);
            QVERIFY(outsideFlag);
        };
        verifyBackgrounds();

        // Selecting the entry and leaving it must preserve the header's base tint.
        widget.getSeekable()->seek(0x1000);
        widget.getSeekable()->seek(0x1004);
        widget.highlightCurrentLine();
        verifyBackgrounds();
        bool selectedInstruction = false;
        for (const auto &selection : edit->extraSelections()) {
            if (selection.format.boolProperty(QTextFormat::FullWidthSelection)
                && selection.cursor.block().text().contains("ret")) {
                QCOMPARE(selection.format.background().color(), ConfigColor("lineHighlight"));
                selectedInstruction = true;
            }
        }
        QVERIFY(selectedInstruction);

        const QString screenshotDir = qEnvironmentVariable("IAITO_DISASSEMBLY_SCREENSHOTS");
        if (!screenshotDir.isEmpty()) {
            QVERIFY(widget.grab().save(
                screenshotDir + "/function-header-" + QTest::currentDataTag() + ".png"));
        }

        // Exercise both incremental scroll directions across the header group.
        widget.showListing(listing, 1);
        verifyBackgrounds();
        widget.showListing(listing, 0);
        verifyBackgrounds();
    }

    void scrollRepaint_data()
    {
        QTest::addColumn<qreal>("pointSize");
        QTest::newRow("normal") << qreal(14);
        QTest::newRow("fractional") << qreal(15.5);
        QTest::newRow("large") << qreal(20);
    }

    void followToken_data()
    {
        QTest::addColumn<QString>("token");
        QTest::addColumn<qulonglong>("target");
        QTest::newRow("address") << QString("0x00000300") << qulonglong(0x300);
        QTest::newRow("zero-address") << QString("0x0") << qulonglong(0);
        QTest::newRow("uppercase-address") << QString("0X0380") << qulonglong(0x380);
        QTest::newRow("flag") << QString("click.Flag") << qulonglong(0x320);
        QTest::newRow("symbol") << QString("sym.click_target") << qulonglong(0x340);
        QTest::newRow("function") << QString("click_function") << qulonglong(0x360);
    }

    void followToken()
    {
        QFETCH(QString, token);
        QFETCH(qulonglong, target);
        TestDisassemblyWidget widget;
        widget.resize(1100, 700);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        auto *edit = qobject_cast<DisassemblyTextEdit *>(widget.getTextWidget());
        QVERIFY(edit);
        widget.showTokenLine("mov x0, " + token);
        QVERIFY(edit->toPlainText().contains(token));
        QTextCursor cursor(edit->document());
        cursor.setPosition(edit->toPlainText().indexOf(token) + token.size() / 2);
        const QPoint point = edit->cursorRect(cursor).center();
        QTest::mouseMove(edit->viewport(), point);
        QCOMPARE(edit->viewport()->cursor().shape(), Qt::PointingHandCursor);
        QTest::mouseClick(edit->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(widget.getSeekable()->getOffset(), RVA(target));

        // The second press of a double-click must not follow a different token
        // that appeared beneath the pointer after the first click navigated.
        widget.showTokenLine("mov x0, 0x000003c0", target);
        QTest::mouseDClick(edit->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseRelease(edit->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(widget.getSeekable()->getOffset(), RVA(target));

        for (Qt::Key key : {Qt::Key_Return, Qt::Key_Enter}) {
            widget.showTokenLine("mov x0, " + token);
            cursor = QTextCursor(edit->document());
            cursor.setPosition(edit->toPlainText().indexOf(token) + token.size() / 2);
            edit->setTextCursor(cursor);
            widget.activateWindow();
            widget.setFocus();
            QTRY_VERIFY(widget.hasFocus());
            const int position = edit->textCursor().position();
            QTest::keyClick(&widget, Qt::Key_Left);
            QCOMPARE(edit->textCursor().position(), position - 1);
            QTest::keyClick(&widget, Qt::Key_L);
            QCOMPARE(edit->textCursor().position(), position);
            QTest::keyClick(&widget, Qt::Key_H);
            QTest::keyClick(&widget, Qt::Key_Right);
            QCOMPARE(edit->textCursor().position(), position);
            QTest::keyClick(&widget, key);
            QCOMPARE(widget.getSeekable()->getOffset(), RVA(target));
        }
    }

    void followInstructionFromAddressColumn()
    {
        Core()->cmd("axc 0x2400 0x2000");
        TestDisassemblyWidget widget;
        widget.resize(1100, 700);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        widget.showTokenLine("0x00002000 jmp 0x2400", 0x2000);
        auto *edit = qobject_cast<DisassemblyTextEdit *>(widget.getTextWidget());
        QVERIFY(edit);
        edit->moveCursor(QTextCursor::Start);
        widget.activateWindow();
        widget.setFocus();
        QTRY_VERIFY(widget.hasFocus());
        QTest::keyClick(&widget, Qt::Key_Return);
        QCOMPARE(widget.getSeekable()->getOffset(), RVA(0x2400));
    }

    void selectTokenWithoutFollowing()
    {
        TestDisassemblyWidget widget;
        widget.getSeekable()->setSynchronization(false);
        widget.resize(1100, 700);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        auto *edit = qobject_cast<DisassemblyTextEdit *>(widget.getTextWidget());
        QVERIFY(edit);
        widget.showTokenLine("mov x0, 0x300 ; 0x380");
        QVERIFY(edit->toPlainText().startsWith("mov x0, 0x300 ; 0x380"));
        QTextCursor cursor(edit->document());
        cursor.setPosition(9);
        const QPoint start = edit->cursorRect(cursor).center();
        cursor.setPosition(18);
        const QPoint end = edit->cursorRect(cursor).center();
        QTest::mousePress(edit->viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QMouseEvent move(
            QEvent::MouseMove,
            end,
            edit->viewport()->mapToGlobal(end),
            Qt::NoButton,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(edit->viewport(), &move);
        QCOMPARE(edit->viewport()->cursor().shape(), Qt::IBeamCursor);
        QTest::mouseRelease(edit->viewport(), Qt::LeftButton, Qt::NoModifier, end);
        QVERIFY(edit->textCursor().hasSelection());
        QCOMPARE(widget.getSeekable()->getOffset(), RVA(0x100));
    }

    void tokenHitBounds_data()
    {
        QTest::addColumn<QString>("token");
        QTest::addColumn<int>("lineIndex");
        QTest::addColumn<qreal>("fontSize");
        QTest::addColumn<int>("scroll");
        QTest::newRow("first-line") << QString("0x300") << 0 << qreal(14) << 0;
        QTest::newRow("second-line") << QString("click.Flag") << 1 << qreal(14) << 0;
        QTest::newRow("scrolled") << QString("sym.click_target") << 1 << qreal(14) << 80;
        QTest::newRow("fractional-font") << QString("0x300") << 1 << qreal(15.5) << 80;
        QTest::newRow("deep-link") << QString("iaito://seek?addr=0x300") << 1 << qreal(14) << 0;
    }

    void tokenHitBounds()
    {
        QFETCH(QString, token);
        QFETCH(int, lineIndex);
        QFETCH(qreal, fontSize);
        QFETCH(int, scroll);
        QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPointSizeF(fontSize);
        Config()->setFont(font);
        TestDisassemblyWidget widget;
        widget.getSeekable()->setSynchronization(false);
        auto *edit = qobject_cast<DisassemblyTextEdit *>(widget.getTextWidget());
        QVERIFY(edit);
        edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        widget.resize(1100, 700);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        QCoreApplication::processEvents();
        widget.showTokenLine(
            QString(24, ' ') + "mov x0, " + token + ")" + QString(500, ' ') + "end",
            0x100,
            lineIndex);
        edit->horizontalScrollBar()->setValue(scroll);
        QCOMPARE(edit->horizontalScrollBar()->value(), scroll);
        const int start = edit->toPlainText().indexOf(token);
        QVERIFY(start >= 0);
        QTextCursor cursor(edit->document());
        cursor.setPosition(start);
        const QRect first = edit->cursorRect(cursor);
        cursor.setPosition(start + token.size());
        const QRect end = edit->cursorRect(cursor);
        const int y = first.center().y();

        QTest::mouseMove(edit->viewport(), QPoint(end.left() + 2, y));
        QCOMPARE(edit->viewport()->cursor().shape(), Qt::ArrowCursor);
        QTest::mouseMove(edit->viewport(), QPoint(first.left() - 2, y));
        QCOMPARE(edit->viewport()->cursor().shape(), Qt::ArrowCursor);
        QTest::mouseMove(edit->viewport(), QPoint(first.left() + 2, y));
        QCOMPARE(edit->viewport()->cursor().shape(), Qt::PointingHandCursor);
        QTest::mouseMove(edit->viewport(), QPoint(end.left() - 2, y));
        QCOMPARE(edit->viewport()->cursor().shape(), Qt::PointingHandCursor);

        QTest::mouseClick(edit->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(end.left() + 2, y));
        QCOMPARE(widget.getSeekable()->getOffset(), RVA(0x100 + lineIndex * 4));
    }

    void horizontalScrollRepaint_data() { scrollRepaint_data(); }

    void verticalScrollPreservesHorizontalPosition()
    {
        TestDisassemblyWidget widget;
        widget.getSeekable()->setSynchronization(false);
        widget.resize(500, 500);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        auto *edit = qobject_cast<DisassemblyTextEdit *>(widget.getTextWidget());
        QList<DisassemblyLine> listing;
        for (int i = 0; i < 300; ++i) {
            DisassemblyLine line;
            line.offset = 0x100 + i * 4;
            line.text = QStringLiteral("mov x0, x1 ; ")
                        + QStringLiteral("long comment ").repeated(30);
            listing.append(line);
        }
        widget.showListing(listing, 40);
        widget.getSeekable()->seek(listing.at(44).offset);
        QTextCursor cursor = edit->textCursor();
        cursor.movePosition(QTextCursor::EndOfBlock);
        edit->setTextCursor(cursor);
        const RVA selected = widget.getSeekable()->getOffset();
        for (int horizontal : {0, 120}) {
            edit->horizontalScrollBar()->setValue(horizontal);
            for (int first : {41, 43, 42, 40, 39, 40}) {
                widget.showListing(listing, first);
                QCOMPARE(edit->horizontalScrollBar()->value(), horizontal);
                QCOMPARE(widget.getSeekable()->getOffset(), selected);
            }
        }
    }

    void horizontalScrollRepaint()
    {
        QFETCH(qreal, pointSize);
        TempConfig config;
        config.set("asm.lines.fcn", true);
        QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPointSizeF(pointSize);
        Config()->setFont(font);
        TestDisassemblyWidget widget;
        widget.resize(1100, 700);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        auto *edit = qobject_cast<DisassemblyTextEdit *>(widget.getTextWidget());
        QVERIFY(edit);
        edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        // Let the viewport resize refresh finish before installing the synthetic listing.
        QTest::qWait(200);
        widget.showTokenLine(QString("mov x0, x1 ; ") + QString("long comment ").repeated(100));
        widget.setFunctionRange(0x110, 0x150);
        edit->viewport()->update();
        QTest::qWait(200);
        QVERIFY(Config()->getConfigBool("asm.lines.fcn"));
        QVERIFY(edit->toPlainText().contains("long comment"));
        QVERIFY(edit->horizontalScrollBar()->maximum() >= 240);

        // Scroll in both directions, especially left: pixel scrolling shifts the fixed
        // function line right into the listing, where exposed-strip repaints miss it.
        const QList<int> positions = {240, 220, 200, 180, 160, 140, 120, 80, 40, 0, 120, 100};
        for (int position : positions) {
            edit->horizontalScrollBar()->setValue(position);
            QCOMPARE(edit->horizontalScrollBar()->value(), position);
            QTest::qWait(100);
            const QImage scrolled = screenImage(edit->viewport());
            QVERIFY(!scrolled.isNull());
            edit->viewport()->update();
            QTest::qWait(100);
            const QImage repainted = screenImage(edit->viewport());
            QVERIFY2(
                scrolled == repainted,
                qPrintable(QString("Horizontal repaint differs at position %1").arg(position)));
        }
    }

    void scrollRepaint()
    {
        QFETCH(qreal, pointSize);
        QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPointSizeF(pointSize);
        Config()->setFont(font);
        TestDisassemblyWidget widget;
        widget.resize(1100, 700);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        auto *edit = qobject_cast<DisassemblyTextEdit *>(widget.getTextWidget());
        QVERIFY(edit);
        edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        QTest::qWait(200);
        QList<DisassemblyLine> listing;
        for (int i = 0; i < 300; ++i) {
            DisassemblyLine line;
            line.offset = 0x100 + i * 4;
            line.arrow = i % 8 == 0 ? line.offset + 16 : RVA_INVALID;
            line.text = QString(
                            "<span style='color:#44dddd'>mov x%1, </span>"
                            "<span style='color:#ddcc44'>0x%2</span> ; row %3")
                            .arg(i % 31)
                            .arg(line.offset, 0, 16)
                            .arg(i);
            line.text += QString("<span style='color:#44dddd;font-weight:bold'>%1</span>")
                             .arg(QString(" ; more text").repeated(i % 8 + 15));
            if (i % 7 == 0) {
                line.text += "<br>second line";
            }
            listing.append(line);
        }
        widget.showListing(listing, 40);
        edit->horizontalScrollBar()->setValue(120);
        QTest::qWait(200);
        QVERIFY(!screenImage(edit->viewport()).isNull());
        QVERIFY(edit->toPlainText().startsWith("mov x9,"));
        const QList<int> positions = {41, 44, 47, 46, 43, 40, 39, 38, 37, 42, 45};
        for (int first : positions) {
            // Leave partial damage pending when the next scroll arrives.
            for (int y = 0; y < edit->viewport()->height(); y += 30) {
                edit->viewport()->update(0, y, edit->viewport()->width(), 7);
            }
            widget.showListing(listing, first);
            QTest::qWait(100);
            QVERIFY(edit->toPlainText().startsWith(QString("mov x%1,").arg(first % 31)));
            const QImage scrolled = screenImage(edit->viewport());
            widget.rebuildListing(listing, first);
            QTest::qWait(100);
            const QImage repainted = screenImage(edit->viewport());
            QVERIFY2(
                scrolled == repainted,
                qPrintable(QString("Partial repaint differs at row %1").arg(first)));
        }
    }
};

QTEST_MAIN(DisassemblyTests)
#include "test_disassembly.moc"
