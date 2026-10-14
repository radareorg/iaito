#include "common/Configuration.h"
#include "common/DecompilerHighlighter.h"
#include "common/IaitoSeekable.h"
#include "common/R2pdcCmdDecompiler.h"
#include "core/MainWindow.h"
#include "menus/DecompilerContextMenu.h"
#include "widgets/DecompilerWidget.h"

#include <algorithm>
#include <QClipboard>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextLayout>

class TestDecompilerWidget : public DecompilerWidget
{
public:
    explicit TestDecompilerWidget(MainWindow *main)
        : DecompilerWidget(main)
    {}
    DecompilerContextMenu *contextMenu() const { return mCtxMenu; }

    bool showCode(RCodeMeta *code)
    {
        return QMetaObject::invokeMethod(
            this, "decompilationFinished", Qt::DirectConnection, Q_ARG(RCodeMeta *, code));
    }
};

class DecompilerColumnTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;

    static void columnCombinations()
    {
        QTest::addColumn<bool>("addresses");
        QTest::addColumn<bool>("disassembly");
        QTest::newRow("plain") << false << false;
        QTest::newRow("addresses") << true << false;
        QTest::newRow("disassembly") << false << true;
        QTest::newRow("both") << true << true;
    }

    static QString lineContaining(QPlainTextEdit *edit, const QString &needle)
    {
        for (const QString &line : edit->toPlainText().split(QLatin1Char('\n'))) {
            if (line.contains(needle)) {
                return line;
            }
        }
        return {};
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("decompilercolumns");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        Core()->initialize(false);
        qRegisterMetaType<RVA>("RVA");
        Config()->loadInitial();
        Config()->setOutputRedirectionEnabled(false);
        Config()->setDecompilerAutoRefreshEnabled(false);
        QVERIFY(Core()->registerDecompiler(new R2pdcCmdDecompiler));
        Config()->setSelectedDecompiler("pdc");
        Core()->cmd("o malloc://64");
        Core()->setConfig("asm.arch", "x86");
        Core()->setConfig("asm.bits", 64);
        Core()->cmd("wx 554889e5b82a0000005dc3 @ 0");
        Core()->cmd("ompg +x");
        Core()->cmd("af @ 0");
    }

    void init()
    {
        Config()->setDecompilerShowAddresses(false);
        Config()->setDecompilerShowDisassembly(false);
        Core()->seek(0);
    }

    void pdcColumnsAndNavigation_data() { columnCombinations(); }

    void pdcColumnsAndNavigation()
    {
        QFETCH(bool, addresses);
        QFETCH(bool, disassembly);
        MainWindow main;
        TestDecompilerWidget widget(&main);
        widget.getSeekable()->setSynchronization(false);
        auto *edit = widget.findChild<QPlainTextEdit *>("textEdit");
        QVERIFY(edit);
        QVERIFY(widget.showCode(Core()->getDecompilerById("pdc")->decompileSync(0)));
        const QString plain = edit->toPlainText();
        QVERIFY(plain.contains("eax = 0x2a"));

        DecompilerContextMenu *menu = widget.contextMenu();
        QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShowSlot", Qt::DirectConnection));
        auto *addressAction = menu->findChild<QAction *>("actionShowAddresses");
        auto *assemblyAction = menu->findChild<QAction *>("actionShowDisassembly");
        QVERIFY(addressAction && addressAction->isCheckable());
        QVERIFY(assemblyAction && assemblyAction->isCheckable());
        QVERIFY(!addressAction->isChecked());
        QVERIFY(!assemblyAction->isChecked());
        if (addresses) {
            addressAction->trigger();
        }
        if (disassembly) {
            assemblyAction->trigger();
        }
        QCOMPARE(widget.isAddressColumnVisible(), addresses);
        QCOMPARE(widget.isDisassemblyColumnVisible(), disassembly);
        QCOMPARE(addressAction->isChecked(), addresses);
        QCOMPARE(assemblyAction->isChecked(), disassembly);

        const QString row = lineContaining(edit, "eax = 0x2a");
        QCOMPARE(row.contains(RAddressString(4)), addresses);
        QCOMPARE(row.contains("mov eax, 0x2a"), disassembly);
        const int token = disassembly ? edit->toPlainText().indexOf("mov eax, 0x2a")
                                      : edit->toPlainText().indexOf("eax = 0x2a");
        QVERIFY(token >= 0);
        QTextCursor cursor(edit->document());
        cursor.setPosition(token + 1);
        edit->setTextCursor(cursor);
        QCOMPARE(widget.getSeekable()->getOffset(), RVA(4));
        QCOMPARE(Core()->getOffset(), RVA(0));

        widget.setAddressColumnVisible(false);
        widget.setDisassemblyColumnVisible(false);
        QCOMPARE(edit->toPlainText(), plain);
        QCOMPARE(widget.getSeekable()->getOffset(), RVA(4));
    }

    void referenceAndHighlightRanges_data() { columnCombinations(); }

    void referenceAndHighlightRanges()
    {
        QFETCH(bool, addresses);
        QFETCH(bool, disassembly);
        RCodeMeta *code = r_codemeta_new("callee();\nreturn;\n");
        RCodeMetaItem *offset = r_codemeta_item_new();
        offset->type = R_CODEMETA_TYPE_OFFSET;
        offset->start = 0;
        offset->end = 18;
        offset->offset.offset = 0;
        r_codemeta_add_item(code, offset);
        RCodeMetaItem *reference = r_codemeta_item_new();
        reference->type = R_CODEMETA_TYPE_FUNCTION_NAME;
        reference->start = 0;
        reference->end = 6;
        reference->reference.offset = 0x20;
        reference->reference.name = strdup("callee");
        r_codemeta_add_item(code, reference);
        RCodeMetaItem *highlight = r_codemeta_item_new();
        highlight->type = R_CODEMETA_TYPE_SYNTAX_HIGHLIGHT;
        highlight->start = 0;
        highlight->end = 6;
        highlight->syntax_highlight.type = R_SYNTAX_HIGHLIGHT_TYPE_FUNCTION_NAME;
        r_codemeta_add_item(code, highlight);

        MainWindow main;
        TestDecompilerWidget widget(&main);
        QVERIFY(widget.showCode(code));
        widget.setAddressColumnVisible(addresses);
        widget.setDisassemblyColumnVisible(disassembly);
        auto *edit = widget.findChild<QPlainTextEdit *>("textEdit");
        QVERIFY(edit);
        // An offset annotation spanning several lines applies to each line.
        QCOMPARE(lineContaining(edit, "return;").contains(RAddressString(0)), addresses);
        QCOMPARE(lineContaining(edit, "return;").contains("push rbp"), disassembly);

        // Exercise repeated remapping while a context reference is selected.
        for (int i = 0; i < 3; ++i) {
            const int token = edit->toPlainText().indexOf("callee");
            QTextCursor cursor(edit->document());
            cursor.setPosition(token + 2);
            edit->setTextCursor(cursor);
            auto *menu = widget.contextMenu();
            QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShowSlot", Qt::DirectConnection));
            QAction *copyReference = nullptr;
            for (QAction *action : menu->actions()) {
                if (action->text().startsWith("Copy address of")) {
                    copyReference = action;
                }
            }
            QVERIFY(copyReference && copyReference->isVisible());
            copyReference->trigger();
            QCOMPARE(QApplication::clipboard()->text(), RAddressString(0x20));

            const auto highlighters = widget.findChildren<DecompilerHighlighter *>();
            QVERIFY(!highlighters.isEmpty());
            highlighters.last()->rehighlight();
            const auto ranges = edit->document()->firstBlock().layout()->formats();
            QVERIFY(std::any_of(ranges.begin(), ranges.end(), [token](const auto &range) {
                return range.start == token && range.length == 6;
            }));
            widget.setAddressColumnVisible(!widget.isAddressColumnVisible());
            widget.setDisassemblyColumnVisible(!widget.isDisassemblyColumnVisible());
        }
    }

    void rememberColumnsAndLeaveWarningsReadable()
    {
        MainWindow main;
        {
            TestDecompilerWidget widget(&main);
            widget.setAddressColumnVisible(true);
            widget.setDisassemblyColumnVisible(true);
        }
        TestDecompilerWidget widget(&main);
        QVERIFY(widget.isAddressColumnVisible());
        QVERIFY(widget.isDisassemblyColumnVisible());
        auto *edit = widget.findChild<QPlainTextEdit *>("textEdit");
        QVERIFY(edit);
        QVERIFY(widget.showCode(Decompiler::makeWarning("No function found")));
        QCOMPARE(edit->toPlainText(), QString("No function found"));
    }
};

QTEST_MAIN(DecompilerColumnTests)
#include "test_decompilercolumns.moc"
