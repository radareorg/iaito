#include "common/Configuration.h"
#include "common/DecompilerHighlighter.h"
#include "common/IaitoSeekable.h"
#include "common/R2pdcCmdDecompiler.h"
#include "common/ShortcutManager.h"
#include "common/TextEditDialog.h"
#include "core/MainWindow.h"
#include "dialogs/CommentsDialog.h"
#include "menus/DecompilerContextMenu.h"
#include "menus/DisassemblyContextMenu.h"
#include "widgets/DecompilerWidget.h"

#include <algorithm>
#include <QClipboard>
#include <QFileInfo>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextLayout>
#include <QTimer>
#include <QUrl>

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

class HeaderCommentDecompiler : public Decompiler
{
public:
    int calls = 0;

    HeaderCommentDecompiler()
        : Decompiler("test-header-comment", "Header comment test")
    {}

    RCodeMeta *decompileSync(RVA address) override
    {
        calls++;
        const QString text = Core()->getCommentAt(address) + "\nfirst();\n";
        RCodeMeta *code = r_codemeta_new(text.toUtf8().constData());
        RCodeMetaItem *offset = r_codemeta_item_new();
        offset->type = R_CODEMETA_TYPE_OFFSET;
        offset->start = text.indexOf("first");
        offset->end = text.size();
        offset->offset.offset = address + 4;
        r_codemeta_add_item(code, offset);
        return code;
    }

    void decompileAt(RVA address) override { emit finished(decompileSync(address)); }
};

class DecompilerColumnTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;
    HeaderCommentDecompiler *headerDecompiler = nullptr;

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

    static RCodeMeta *hoverCode(const QString &token, int type = -1)
    {
        const QString text = "// references\n    " + token + " ();\n";
        RCodeMeta *code = r_codemeta_new(text.toUtf8().constData());
        RCodeMetaItem *offset = r_codemeta_item_new();
        offset->type = R_CODEMETA_TYPE_OFFSET;
        offset->end = text.size();
        offset->offset.offset = 0;
        r_codemeta_add_item(code, offset);
        if (type >= 0) {
            RCodeMetaItem *reference = r_codemeta_item_new();
            reference->type = static_cast<RCodeMetaItemType>(type);
            reference->start = text.indexOf(token);
            reference->end = reference->start + token.size();
            if (type == R_CODEMETA_TYPE_LOCAL_VARIABLE) {
                reference->variable.name = strdup(token.toUtf8().constData());
            } else {
                reference->reference.offset = 0x20;
                if (type == R_CODEMETA_TYPE_FUNCTION_NAME) {
                    reference->reference.name = strdup(token.toUtf8().constData());
                }
            }
            r_codemeta_add_item(code, reference);
        }
        return code;
    }

    static QString hoveredText(QPlainTextEdit *edit)
    {
        for (const auto &selection : edit->extraSelections()) {
            if (selection.format.boolProperty(QTextFormat::UserProperty)) {
                return selection.cursor.selectedText();
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
        headerDecompiler = new HeaderCommentDecompiler;
        QVERIFY(Core()->registerDecompiler(headerDecompiler));
        Config()->setSelectedDecompiler("pdc");
        Core()->cmd("o malloc://64");
        Core()->setConfig("asm.arch", "x86");
        Core()->setConfig("asm.bits", 64);
        Core()->setConfig("dir.cache", settings.path() + "/annotations");
        Core()->cmd("wx 554889e5b82a0000005dc3 @ 0");
        Core()->cmd("ompg +x");
        Core()->cmd("af @ 0");
        Core()->cmd("wx c3 @ 0x20");
        Core()->cmd("af @ 0x20");
    }

    void init()
    {
        Config()->setDecompilerShowAddresses(false);
        Config()->setDecompilerShowDisassembly(false);
        Config()->setSelectedDecompiler("pdc");
        Core()->seek(0);
        Core()->cmdRawAt("ano-", 0);
        Core()->cmdRawAt("ano-", 0x20);
        Core()->cmdRawAt("CC-", 0);
        Core()->cmdRawAt("CC-", 4);
        Core()->cmdRawAt("CC-", 0x20);
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

    void hoverAndFollowReference_data()
    {
        QTest::addColumn<bool>("addresses");
        QTest::addColumn<bool>("disassembly");
        QTest::addColumn<int>("type");
        QTest::addColumn<QString>("token");
        for (bool addresses : {false, true}) {
            for (bool disassembly : {false, true}) {
                for (int type :
                     {-1,
                      int(R_CODEMETA_TYPE_FUNCTION_NAME),
                      int(R_CODEMETA_TYPE_GLOBAL_VARIABLE),
                      int(R_CODEMETA_TYPE_CONSTANT_VARIABLE),
                      int(R_CODEMETA_TYPE_LOCAL_VARIABLE)}) {
                    const QByteArray name
                        = QString("%1-%2-%3").arg(addresses).arg(disassembly).arg(type).toLatin1();
                    QTest::newRow(name.constData()) << addresses << disassembly << type
                                                    << QString(type < 0 ? "0X20" : "callee");
                }
            }
        }
        QTest::newRow("flag") << false << false << -1 << QString("hover.flag");
        QTest::newRow("function") << false << false << -1 << QString("hover_function");
        QTest::newRow("import") << false << false << -1 << QString("hover_import");
        QTest::newRow("sanitized-import")
            << false << false << -1 << QString("sym_imp_hover_import");
        QTest::newRow("sanitized-symbol") << false << false << -1 << QString("hover_symbol");
        QTest::newRow("cpp-method") << false << false << -1 << QString("HoverClass::method");
        QTest::newRow("sanitized-method") << false << false << -1 << QString("HoverClass__method");
    }

    void hoverAndFollowReference()
    {
        QFETCH(bool, addresses);
        QFETCH(bool, disassembly);
        QFETCH(int, type);
        QFETCH(QString, token);
        Core()->cmd("f hover.flag 1 @ 0x20");
        Core()->cmd("f callee 1 @ 0x20");
        Core()->cmd("f sym.imp.hover_import 1 @ 0x20");
        Core()->cmd("f sym.hover.symbol 1 @ 0x20");
        Core()->cmd("f sym.HoverClass::method 1 @ 0x20");
        Core()->cmd("afn hover_function @ 0x20");
        MainWindow main;
        TestDecompilerWidget widget(&main);
        widget.getSeekable()->setSynchronization(false);
        widget.setParent(nullptr);
        widget.resize(1000, 500);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        QVERIFY(widget.showCode(hoverCode(token, type)));
        widget.setAddressColumnVisible(addresses);
        widget.setDisassemblyColumnVisible(disassembly);
        auto *edit = widget.findChild<QPlainTextEdit *>("textEdit");
        QVERIFY(edit);
        const int start = edit->toPlainText().indexOf(token);
        QTextCursor cursor(edit->document());
        cursor.setPosition(start);
        const QRect first = edit->cursorRect(cursor);
        cursor.setPosition(start + token.size());
        const QRect end = edit->cursorRect(cursor);
        const int y = first.center().y();
        QTest::mouseMove(edit->viewport(), QPoint(first.left() - 2, y));
        QCOMPARE(edit->viewport()->cursor().shape(), Qt::ArrowCursor);
        QTest::mouseMove(edit->viewport(), QPoint(end.left() + 2, y));
        QCOMPARE(edit->viewport()->cursor().shape(), Qt::ArrowCursor);
        const bool actionable = type != R_CODEMETA_TYPE_LOCAL_VARIABLE;
        const QPoint point(first.left() + 2, y);
        QTest::mouseMove(edit->viewport(), point);
        QCOMPARE(
            edit->viewport()->cursor().shape(),
            actionable ? Qt::PointingHandCursor : Qt::ArrowCursor);
        QCOMPARE(hoveredText(edit), actionable ? token : QString());
        QTest::mouseMove(edit->viewport(), QPoint(end.left() - 2, y));
        QCOMPARE(hoveredText(edit), actionable ? token : QString());
        // Cursor highlights must preserve the independent hover selection.
        edit->moveCursor(QTextCursor::End);
        QCOMPARE(hoveredText(edit), actionable ? token : QString());
        QTest::mouseClick(edit->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(widget.getSeekable()->getOffset(), RVA(actionable ? 0x20 : 0));
        QCOMPARE(Core()->getOffset(), RVA(0));
        if (actionable) {
            // A double-click must not follow new text that replaces the clicked reference.
            QVERIFY(widget.showCode(hoverCode("0x30")));
            QTest::mouseDClick(edit->viewport(), Qt::LeftButton, Qt::NoModifier, point);
            QTest::mouseRelease(edit->viewport(), Qt::LeftButton, Qt::NoModifier, point);
            QCOMPARE(widget.getSeekable()->getOffset(), RVA(0x20));
        }
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(edit->viewport(), &leave);
        QCOMPARE(edit->viewport()->cursor().shape(), Qt::ArrowCursor);
        QVERIFY(hoveredText(edit).isEmpty());
    }

    void keyboardFollowsReference_data()
    {
        QTest::addColumn<QString>("token");
        QTest::addColumn<int>("type");
        QTest::addColumn<int>("key");
        QUrl link = QUrl::fromLocalFile(QFileInfo(Core()->getFilePath()).absoluteFilePath());
        link.setScheme("iaito");
        link.setQuery("addr=0x20");
        for (Qt::Key key : {Qt::Key_Return, Qt::Key_Enter}) {
            for (const QString &token :
                 {QString("0x20"),
                  QString("keyboard.flag"),
                  QString("callee"),
                  link.toString(QUrl::FullyEncoded)}) {
                const QByteArray name = token.toUtf8() + '-' + QByteArray::number(key);
                QTest::newRow(name.constData())
                    << token << (token == "callee" ? int(R_CODEMETA_TYPE_FUNCTION_NAME) : -1)
                    << int(key);
            }
        }
    }

    void keyboardFollowsReference()
    {
        QFETCH(QString, token);
        QFETCH(int, type);
        QFETCH(int, key);
        Core()->cmd("f keyboard.flag 1 @ 0x20");
        MainWindow main;
        TestDecompilerWidget widget(&main);
        widget.getSeekable()->setSynchronization(false);
        widget.setParent(nullptr);
        widget.resize(1000, 500);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        QVERIFY(widget.showCode(hoverCode(token, type)));
        auto *edit = widget.findChild<QPlainTextEdit *>("textEdit");
        QVERIFY(edit);
        QTextCursor cursor(edit->document());
        cursor.setPosition(edit->toPlainText().indexOf(token) + 2);
        edit->setTextCursor(cursor);
        widget.activateWindow();
        edit->setFocus();
        QTRY_VERIFY(edit->hasFocus());
        QTest::keyClick(edit, Qt::Key(key));
        if (token.startsWith("iaito://")) {
            QCOMPARE(Core()->getOffset(), RVA(0x20));
        } else {
            QCOMPARE(widget.getSeekable()->getOffset(), RVA(0x20));
            QCOMPARE(Core()->getOffset(), RVA(0));
        }
    }

    void hoverLinksAndDragSelection()
    {
        MainWindow main;
        TestDecompilerWidget widget(&main);
        widget.getSeekable()->setSynchronization(false);
        widget.setParent(nullptr);
        widget.resize(1000, 500);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        const QString link = "iaito://seek?addr=0x20";
        QVERIFY(widget.showCode(hoverCode(link)));
        auto *edit = widget.findChild<QPlainTextEdit *>("textEdit");
        QVERIFY(edit);
        QTextCursor cursor(edit->document());
        cursor.setPosition(edit->toPlainText().indexOf(link) + 2);
        QTest::mouseMove(edit->viewport(), edit->cursorRect(cursor).center());
        QCOMPARE(edit->viewport()->cursor().shape(), Qt::PointingHandCursor);
        QCOMPARE(hoveredText(edit), link);
        QVERIFY(widget.showCode(Decompiler::makeWarning("No code")));
        QVERIFY(hoveredText(edit).isEmpty());
        QVERIFY(widget.showCode(hoverCode("0x20")));
        const int start = edit->toPlainText().indexOf("0x20");
        cursor = QTextCursor(edit->document());
        cursor.setPosition(start);
        const QPoint from = edit->cursorRect(cursor).center();
        cursor.setPosition(start + 4);
        const QPoint to = edit->cursorRect(cursor).center();
        QTest::mousePress(edit->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QMouseEvent move(
            QEvent::MouseMove,
            to,
            edit->viewport()->mapToGlobal(to),
            Qt::NoButton,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(edit->viewport(), &move);
        QCOMPARE(edit->viewport()->cursor().shape(), Qt::IBeamCursor);
        QVERIFY(hoveredText(edit).isEmpty());
        QTest::mouseRelease(edit->viewport(), Qt::LeftButton, Qt::NoModifier, to);
        QVERIFY(edit->textCursor().hasSelection());
        QCOMPARE(widget.getSeekable()->getOffset(), RVA(0));
    }

    void editFunctionAnnotation_data()
    {
        QTest::addColumn<bool>("decompiler");
        QTest::addColumn<QString>("replacement");
        QTest::addColumn<bool>("save");
        const QString text = QString::fromUtf8("caf\xc3\xa9\nsecond line");
        QTest::newRow("decompiler-delete") << true << QString() << true;
        QTest::newRow("decompiler-edit") << true << text << true;
        QTest::newRow("decompiler-cancel") << true << QString() << false;
        QTest::newRow("disassembly-delete") << false << QString() << true;
        QTest::newRow("disassembly-edit") << false << text << true;
        QTest::newRow("disassembly-cancel") << false << QString() << false;
    }

    void editFunctionAnnotation()
    {
        QFETCH(bool, decompiler);
        QFETCH(QString, replacement);
        QFETCH(bool, save);
        Core()->cmdRawAt("ano=original annotation", 0);
        Core()->cmdRawAt("ano=other annotation", 0x20);
        MainWindow main;
        TestDecompilerWidget widget(&main);
        widget.getSeekable()->setSynchronization(false);
        QMenu *menu;
        const char *slot;
        if (decompiler) {
            widget.contextMenu()->setDecompiledFunctionAddress(0);
            menu = widget.contextMenu();
            slot = "actionEditAnnotationTriggered";
        } else {
            auto *disassemblyMenu = new DisassemblyContextMenu(&widget, &main);
            disassemblyMenu->setOffset(4);
            menu = disassemblyMenu;
            slot = "on_actionEditAnnotation_triggered";
        }

        // Editing an unsynchronized view must not change the globally sought function.
        Core()->seek(0x20);
        QSignalSpy refresh(Core(), &IaitoCore::refreshAll);
        bool visitedDialog = false;
        QString initialText;
        QTimer::singleShot(0, [&]() {
            auto *dialog = qobject_cast<TextEditDialog *>(QApplication::activeModalWidget());
            if (!dialog) {
                if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                    modal->reject();
                }
                return;
            }
            auto *editor = dialog->findChild<QTextEdit *>();
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            if (!editor || !buttons) {
                dialog->reject();
                return;
            }
            visitedDialog = true;
            initialText = editor->toPlainText();
            editor->setPlainText(replacement);
            buttons->button(save ? QDialogButtonBox::Save : QDialogButtonBox::Cancel)->click();
        });
        QVERIFY(QMetaObject::invokeMethod(menu, slot, Qt::DirectConnection));
        QVERIFY(visitedDialog);
        QCOMPARE(initialText, QString("original annotation\n"));
        const QString expected = !save                   ? QString("original annotation\n")
                                 : replacement.isEmpty() ? QString()
                                                         : replacement + QLatin1Char('\n');
        QCOMPARE(Core()->cmdRawAt("anos", 0), expected);
        QCOMPARE(Core()->cmdRawAt("ano*", 0).isEmpty(), save && replacement.isEmpty());
        QCOMPARE(Core()->cmdRawAt("anos", 0x20), QString("other annotation\n"));
        QCOMPARE(Core()->getOffset(), RVA(0x20));
        QCOMPARE(refresh.count(), save ? 1 : 0);
    }

    void lineNavigationAndCommentEntry_data() { columnCombinations(); }

    void lineNavigationAndCommentEntry()
    {
        QFETCH(bool, addresses);
        QFETCH(bool, disassembly);
        const QString text = "first();\nsecond();\nthird();\n";
        RCodeMeta *code = r_codemeta_new(text.toUtf8().constData());
        int start = 0;
        const QList<RVA> offsets = {0, 4, 9};
        for (int line = 0; line < offsets.size(); ++line) {
            RCodeMetaItem *offset = r_codemeta_item_new();
            offset->type = R_CODEMETA_TYPE_OFFSET;
            // Include the preceding newline, as pdc does.
            offset->start = qMax(0, start - 1);
            offset->end = text.indexOf(QLatin1Char('\n'), start);
            offset->offset.offset = offsets.at(line);
            r_codemeta_add_item(code, offset);
            start = offset->end + 1;
        }
        MainWindow main;
        TestDecompilerWidget widget(&main);
        widget.getSeekable()->setSynchronization(false);
        QVERIFY(widget.showCode(code));
        widget.setAddressColumnVisible(addresses);
        widget.setDisassemblyColumnVisible(disassembly);
        auto *edit = widget.findChild<QPlainTextEdit *>("textEdit");
        QVERIFY(edit);
        QTextCursor cursor(edit->document());
        cursor.setPosition(edit->toPlainText().indexOf("first") + 2);
        edit->setTextCursor(cursor);
        Core()->seek(0x20);

        QTest::keyClick(edit, Qt::Key_J);
        QCOMPARE(edit->textCursor().blockNumber(), 1);
        QCOMPARE(widget.getSeekable()->getOffset(), RVA(4));
        auto *menu = widget.contextMenu();
        QCOMPARE(menu->getFirstOffsetInLine(), RVA(4));
        QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShowSlot", Qt::DirectConnection));
        auto *comment = menu->findChild<QAction *>("actionAddComment");
        QVERIFY(comment && comment->isVisible() && comment->isEnabled());
        QCOMPARE(comment->text(), QString("Add Comment"));
        QTest::keyClick(edit, Qt::Key_Down);
        QCOMPARE(edit->textCursor().blockNumber(), 2);
        QCOMPARE(widget.getSeekable()->getOffset(), RVA(9));
        QTest::keyClick(edit, Qt::Key_Up);
        QCOMPARE(edit->textCursor().blockNumber(), 1);
        QTest::keyClick(edit, Qt::Key_K);
        QCOMPARE(edit->textCursor().blockNumber(), 0);
        QTest::keyClick(edit, Qt::Key_J);
        const int position = edit->textCursor().position();
        QTest::keyClick(edit, Qt::Key_H);
        QCOMPARE(edit->textCursor().position(), position - 1);
        QTest::keyClick(edit, Qt::Key_L);
        QCOMPARE(edit->textCursor().position(), position);
        QTest::keyClick(edit, Qt::Key_Left);
        QCOMPARE(edit->textCursor().position(), position - 1);
        QTest::keyClick(edit, Qt::Key_Right);
        QCOMPARE(edit->textCursor().position(), position);
        QTest::keyClick(edit, Qt::Key_Right, Qt::ShiftModifier);
        QVERIFY(edit->textCursor().hasSelection());
        QCOMPARE(widget.getSeekable()->getOffset(), RVA(4));
        QCOMPARE(Core()->getOffset(), RVA(0x20));

        QVERIFY(widget.showCode(Decompiler::makeWarning("No function found")));
        // Opening the menu on unmapped text still offers the entry, disabled.
        QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShowSlot", Qt::DirectConnection));
        QVERIFY(comment->isVisible());
        QVERIFY(!comment->isEnabled());
    }

    void commentChangesRefreshOutput_data()
    {
        QTest::addColumn<bool>("columns");
        QTest::addColumn<bool>("keyboard");
        QTest::newRow("context-plain") << false << false;
        QTest::newRow("keyboard-plain") << false << true;
        QTest::newRow("context-columns") << true << false;
        QTest::newRow("keyboard-columns") << true << true;
    }

    void commentChangesRefreshOutput()
    {
        QFETCH(bool, columns);
        QFETCH(bool, keyboard);
        MainWindow main;
        TestDecompilerWidget widget(&main);
        widget.getSeekable()->setSynchronization(false);
        widget.getSeekable()->seek(0);
        // Exercise refreshes without running MainWindow's file-opening workflow.
        widget.setParent(nullptr);
        widget.resize(900, 500);
        widget.show();
        QTRY_VERIFY(widget.isVisibleToUser());
        QVERIFY(QMetaObject::invokeMethod(&widget, "refreshDecompiler", Qt::DirectConnection));
        auto *edit = widget.findChild<QPlainTextEdit *>("textEdit");
        QVERIFY(edit);
        QTRY_VERIFY(edit->toPlainText().contains("eax = 0x2a"));
        widget.setAddressColumnVisible(columns);
        widget.setDisassemblyColumnVisible(columns);
        QTextCursor cursor(edit->document());
        cursor.setPosition(edit->toPlainText().indexOf("eax = 0x2a") + 2);
        edit->setTextCursor(cursor);
        edit->setFocus();
        const int block = edit->textCursor().blockNumber();
        const int column = edit->textCursor().positionInBlock();
        auto *menu = widget.contextMenu();
        QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShowSlot", Qt::DirectConnection));
        auto *comment = menu->findChild<QAction *>("actionAddComment");
        auto *remove = menu->findChild<QAction *>("actionDeleteComment");
        QVERIFY(comment && comment->isVisible() && comment->isEnabled());
        QVERIFY(remove);
        QCOMPARE(comment->text(), QString("Add Comment"));

        const auto editComment = [&](const QString &text) {
            if (keyboard) {
                // The offscreen platform drops window activation after modal dialogs.
                widget.activateWindow();
                if (!QTest::qWaitForWindowActive(&widget)) {
                    return false;
                }
                edit->setFocus();
            }
            bool visited = false;
            QTimer timer;
            timer.setSingleShot(true);
            connect(&timer, &QTimer::timeout, [&]() {
                auto *dialog = qobject_cast<CommentsDialog *>(QApplication::activeModalWidget());
                if (!dialog) {
                    if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                        modal->reject();
                    }
                    return;
                }
                visited = true;
                dialog->setComment(text);
                auto *editor = dialog->findChild<QPlainTextEdit *>("textEdit");
                QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
            });
            timer.start(0);
            if (keyboard) {
                QTest::keyClick(edit, Qt::Key_Semicolon);
            } else {
                comment->trigger();
            }
            return visited;
        };
        QVERIFY(editComment("new-comment"));
        QCOMPARE(Core()->getCommentAt(4), QString("new-comment"));
        QTRY_VERIFY(edit->toPlainText().contains("new-comment"));
        QCOMPARE(edit->textCursor().blockNumber(), block);
        QCOMPARE(edit->textCursor().positionInBlock(), column);
        QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShowSlot", Qt::DirectConnection));
        QCOMPARE(comment->text(), QString("Edit Comment"));
        QVERIFY(editComment("edited-comment"));
        QTRY_VERIFY(edit->toPlainText().contains("edited-comment"));
        QVERIFY(!edit->toPlainText().contains("new-comment"));
        QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShowSlot", Qt::DirectConnection));
        QVERIFY(remove->isVisible());
        remove->trigger();
        QVERIFY(Core()->getCommentAt(4).isEmpty());
        QTRY_VERIFY(!edit->toPlainText().contains("edited-comment"));
        QCOMPARE(edit->textCursor().blockNumber(), block);
        QCOMPARE(edit->textCursor().positionInBlock(), column);
    }

    void refreshCommentOutsideAnnotationRange()
    {
        Config()->setSelectedDecompiler(headerDecompiler->getId());
        MainWindow main;
        TestDecompilerWidget widget(&main);
        widget.setParent(nullptr);
        widget.resize(900, 500);
        widget.show();
        QTRY_VERIFY(widget.isVisibleToUser());
        QVERIFY(QMetaObject::invokeMethod(&widget, "refreshDecompiler", Qt::DirectConnection));
        auto *edit = widget.findChild<QPlainTextEdit *>("textEdit");
        QVERIFY(edit);
        QTRY_VERIFY(edit->toPlainText().contains("first();"));
        const int calls = headerDecompiler->calls;
        // Only address 4 is annotated, but the function starts at address 0.
        Core()->setComment(0, "header-comment");
        QTRY_VERIFY(edit->toPlainText().contains("header-comment"));
        QCOMPARE(headerDecompiler->calls, calls + 1);
        Core()->setComment(0x20, "other-function");
        QCOMPARE(headerDecompiler->calls, calls + 1);
    }
};

QTEST_MAIN(DecompilerColumnTests)
#include "test_decompilercolumns.moc"
