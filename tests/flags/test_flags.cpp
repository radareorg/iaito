#include "common/Configuration.h"
#include "common/IaitoSeekable.h"
#include "common/NameListModel.h"
#include "common/ShortcutManager.h"
#include "dialogs/FlagDialog.h"
#include "menus/AddressableItemContextMenu.h"
#include "menus/DisassemblyContextMenu.h"
#include "widgets/AddressableItemList.h"
#include "widgets/DisassemblyWidget.h"
#include "widgets/ExportsWidget.h"
#include "widgets/FlagsWidget.h"
#include "widgets/FunctionsWidget.h"
#include "widgets/HexWidget.h"
#include "widgets/ImportsWidget.h"
#include "widgets/NameListView.h"
#include "widgets/SymbolsWidget.h"

#include <QColorDialog>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QFontMetricsF>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

class TestDisassemblyWidget : public DisassemblyWidget
{
public:
    TestDisassemblyWidget()
        : DisassemblyWidget(nullptr)
    {
        getSeekable()->setSynchronization(false);
    }

    void showInstructions()
    {
        cachedLines.clear();
        for (int i = 0; i < 64; ++i) {
            DisassemblyLine line;
            line.offset = 0x100 + i * 4;
            line.text = QStringLiteral("nop");
            cachedLines.append(line);
        }
        useCachedLines = true;
        refreshDisasm(0x100);
    }
};

class FlagTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;

    void pressFlagKey(
        QWidget *widget,
        const std::function<void(FlagDialog *)> &edit,
        Qt::Key key = Qt::Key_F,
        Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        widget->window()->activateWindow();
        widget->setFocus();
        QTRY_VERIFY(widget->hasFocus());
        bool opened = false;
        QTimer::singleShot(0, this, [&]() {
            auto *dialog = qobject_cast<FlagDialog *>(QApplication::activeModalWidget());
            if (!dialog) {
                return;
            }
            opened = true;
            edit(dialog);
            if (dialog->isVisible()) {
                dialog->reject();
            }
        });
        QTest::keyClick(widget, key, modifiers);
        QCoreApplication::processEvents();
        QVERIFY(opened);
    }

    static QLineEdit *field(FlagDialog *dialog, const char *name)
    {
        return dialog->findChild<QLineEdit *>(QString::fromLatin1(name));
    }

    static void save(FlagDialog *dialog)
    {
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("flags");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        Core()->initialize(false);
        qRegisterMetaType<RVA>("RVA");
        Config()->loadInitial();
        Core()->cmd("o malloc://16384");
        Core()->setConfig("asm.arch", "arm");
        Core()->setConfig("asm.bits", 64);
        Core()->cmd("wx 1f2003d51f2003d51f2003d5 @ 0x100");
    }

    void cleanup()
    {
        Core()->cmd("f-*");
        ShortcutMgr()->resetToDefault("hex.flag");
        ShortcutMgr()->resetToDefault("disasm.flag");
    }

    void addressableMenusKeepHorizontalPosition_data()
    {
        QTest::addColumn<bool>("functions");
        QTest::newRow("symbols") << false;
        QTest::newRow("functions") << true;
    }

    void addressableMenusKeepHorizontalPosition()
    {
        QFETCH(bool, functions);
        QList<SymbolDescription> symbols;
        QList<FunctionDescription> functionList;
        QSet<RVA> imports;
        ut64 mainAddress = RVA_INVALID;
        for (int i = 0; i < 2; ++i) {
            SymbolDescription symbol;
            symbol.vaddr = 0x100 + i * 0x100;
            symbol.name = QStringLiteral("sym.long_namespace::function%1()").arg(i);
            symbols.append(symbol);
            FunctionDescription function;
            function.offset = symbol.vaddr;
            function.name = symbol.name;
            functionList.append(function);
        }
        SymbolsModel symbolModel(&symbols);
        FunctionModel functionModel(&functionList, &imports, &mainAddress, false, QFont(), QFont());
        AddressableItemModelI *source = functions
                                            ? static_cast<AddressableItemModelI *>(&functionModel)
                                            : static_cast<AddressableItemModelI *>(&symbolModel);
        const int nameColumn = functions ? int(FunctionModel::NameColumn)
                                         : int(SymbolsModel::NameColumn);
        AddressableItemView view;
        AddressableItemContextMenu menu(&view, nullptr);
        view.setItemContextMenu(&menu);
        view.setAddressModel(source);
        NameListView names(
            &view, &menu, source, nameColumn, functions ? "FunctionMenuScroll" : "SymbolMenuScroll");
        view.setSelectionBehavior(QAbstractItemView::SelectRows);
        view.setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        view.header()->setStretchLastSection(false);
        for (int column = 0; column < view.model()->columnCount(); ++column) {
            view.setColumnWidth(column, column == nameColumn ? 900 : 180);
        }
        view.resize(380, 250);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.setCurrentIndex(view.model()->index(0, nameColumn));
        view.horizontalScrollBar()->setValue(140);
        QCOMPARE(view.horizontalScrollBar()->value(), 140);

        const QPoint point(20, view.visualRect(view.model()->index(1, 0)).center().y());
        QTest::mouseClick(view.viewport(), Qt::RightButton, Qt::NoModifier, point);
        QTimer::singleShot(0, &menu, &QMenu::close);
        QContextMenuEvent event(QContextMenuEvent::Mouse, point, view.viewport()->mapToGlobal(point));
        QApplication::sendEvent(view.viewport(), &event);
        QApplication::processEvents();
        QCOMPARE(view.currentIndex().row(), 1);
        QCOMPARE(view.horizontalScrollBar()->value(), 140);

        // Model rebuilds caused by view actions must keep the same horizontal position.
        names.toggleAction()->setChecked(!names.toggleAction()->isChecked());
        QApplication::processEvents();
        QCOMPARE(view.horizontalScrollBar()->value(), 140);
        QCOMPARE(names.presentationModel()->address(view.currentIndex()), RVA(0x200));
    }

    void flagColorsUseExactNames()
    {
        const QString symbolFlag = QStringLiteral("sym._ZN3foo3barEv");
        Core()->addFlag(0x100, symbolFlag, 16, "", "keep comment");
        Core()->addFlag(0x100, "unrelated.alias", 32);
        Core()->addFlag(0x200, "sym.imp.puts", 4);
        Core()->addFlag(0x300, "reloc.malloc", 8);
        SymbolDescription symbol;
        symbol.vaddr = 0x100;
        symbol.name = "_ZN3foo3barEv";
        symbol.demangledName = "foo::bar()";
        QList<SymbolDescription> symbols{symbol};
        SymbolsModel symbolModel(&symbols);
        SymbolsProxyModel filter(&symbolModel);
        NameListModel names(&filter, SymbolsModel::NameColumn);
        names.setHierarchy(true);
        const auto leaf = names.mapFromSource(filter.index(0, SymbolsModel::NameColumn));
        QCOMPARE(leaf.data(AddressableItemModelI::FlagNameRole).toString(), symbolFlag);

        QWidget parent;
        AddressableItemContextMenu menu(&parent, nullptr);
        menu.setTarget(
            names.address(leaf),
            names.name(leaf),
            leaf.data(AddressableItemModelI::FlagNameRole).toString());
        auto color = menu.findChild<QAction *>("actionSetFlagColor");
        QVERIFY(color && color->menu());
        QVERIFY(color->isVisible());
        auto swatch = color->menu()->actions().first();
        QCOMPARE(swatch->text(), QString("red"));
        QSignalSpy changed(Core(), &IaitoCore::flagsChanged);
        swatch->trigger();
        QCOMPARE(changed.count(), 1);
        QCOMPARE(Core()->flagColor(symbolFlag), QString("red"));
        QVERIFY(Core()->flagColor("unrelated.alias").isEmpty());
        QCOMPARE(leaf.data(Qt::ForegroundRole).value<QColor>(), QColor(Qt::red));
        auto flag = r_flag_get(Core()->core()->flags, symbolFlag.toUtf8().constData());
        QVERIFY(flag);
        QCOMPARE(flag->size, ut64(16));
        QCOMPARE(
            QString::fromUtf8(r_flag_get_meta(Core()->core()->flags, flag->id)->comment),
            QString("keep comment"));
        color->menu()->actions().last()->trigger();
        QVERIFY(Core()->flagColor(symbolFlag).isEmpty());
        QVERIFY(!leaf.data(Qt::ForegroundRole).isValid());
        menu.clearTarget();
        QVERIFY(!color->isVisible());
        QVERIFY(!menu.findChild<QAction *>("actionAnalyzeFunction")->isEnabled());

        ImportDescription imp;
        imp.plt = 0x200;
        imp.name = "puts";
        auto reloc = imp;
        reloc.plt = 0x300;
        reloc.name = "malloc";
        QList<ImportDescription> imports{imp, reloc};
        ImportsModel importModel(&imports);
        QCOMPARE(
            importModel.index(0, 0).data(AddressableItemModelI::FlagNameRole).toString(),
            QString("sym.imp.puts"));
        QCOMPARE(
            importModel.index(1, 0).data(AddressableItemModelI::FlagNameRole).toString(),
            QString("reloc.malloc"));
        ExportDescription exp;
        exp.vaddr = symbol.vaddr;
        exp.name = symbol.demangledName;
        exp.flag_name = symbolFlag;
        QList<ExportDescription> exports{exp};
        ExportsModel exportModel(&exports);
        QCOMPARE(
            exportModel.index(0, 0).data(AddressableItemModelI::FlagNameRole).toString(),
            symbolFlag);
        FlagDescription desc;
        desc.offset = symbol.vaddr;
        desc.name = symbolFlag;
        QList<FlagDescription> flags{desc};
        FlagsModel flagModel(&flags);
        Core()->setFlagColor(symbolFlag, "#33aaff");
        Core()->setFlagColor("sym.imp.puts", "#33aaff");
        QCOMPARE(
            importModel.index(0, ImportsModel::NameColumn).data(Qt::ForegroundRole).value<QColor>(),
            QColor("#33aaff"));
        QCOMPARE(
            exportModel.index(0, ExportsModel::NameColumn).data(Qt::ForegroundRole).value<QColor>(),
            QColor("#33aaff"));
        QCOMPARE(
            flagModel.index(0, FlagsModel::NAME).data(Qt::ForegroundRole).value<QColor>(),
            QColor("#33aaff"));
    }

    void analyzeFromContextMenus()
    {
        Core()->cmd("af-*");
        Core()->cmd("ompg +x");
        Core()->cmd("wx c0035fd6 @ 0x400");
        QWidget parent;
        AddressableItemContextMenu menu(&parent, nullptr);
        auto analyze = menu.findChild<QAction *>("actionAnalyzeFunction");
        QVERIFY(analyze);
        menu.setTarget(0x400, "symbol_to_analyze");
        QVERIFY(analyze->isEnabled());
        analyze->trigger();
        QVERIFY(Core()->functionIn(0x400));
        DisassemblyContextMenu disasmMenu(&parent, nullptr);
        QCOMPARE(disasmMenu.actions().first()->text(), QString("Analyze function"));
        QCOMPARE(
            disasmMenu.actions().first()->shortcut(),
            ShortcutMgr()->sequence("disasm.defineFunction"));
    }

    void hexFlags_data()
    {
        QTest::addColumn<int>("selectedBytes");
        QTest::newRow("cursor") << 0;
        QTest::newRow("one-byte") << 1;
        QTest::newRow("range") << 17;
    }

    void hexFlags()
    {
        QFETCH(int, selectedBytes);
        HexWidget widget;
        widget.resize(1000, 500);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        widget.seek(0x100);
        if (selectedBytes) {
            widget.selectRange(0x100, 0x100 + selectedBytes - 1);
        }
        widget.setFocus();
        pressFlagKey(&widget, [&](FlagDialog *dialog) {
            QCOMPARE(field(dialog, "offsetEdit")->text(), RAddressString(0x100));
            QCOMPARE(field(dialog, "sizeEdit")->text(), QString::number(qMax(1, selectedBytes)));
            field(dialog, "nameEdit")->setText("test.flag");
            field(dialog, "commentEdit")->setText("selected bytes");
            bool pickedColor = false;
            QTimer::singleShot(0, this, [&]() {
                auto *picker = qobject_cast<QColorDialog *>(QApplication::activeModalWidget());
                if (picker) {
                    pickedColor = true;
                    picker->setCurrentColor(QColor("#33aaff"));
                    picker->accept();
                }
            });
            dialog->findChild<QPushButton *>("colorButton")->click();
            QVERIFY(pickedColor);
            QCOMPARE(field(dialog, "colorEdit")->text(), QString("#33aaff"));
            save(dialog);
        });
        RFlagItem *flag = r_flag_get(Core()->core()->flags, "test.flag");
        QVERIFY(flag);
        QCOMPARE(flag->addr, ut64(0x100));
        QCOMPARE(flag->size, ut64(qMax(1, selectedBytes)));
        auto *meta = r_flag_get_meta(Core()->core()->flags, flag->id);
        QVERIFY(meta);
        QCOMPARE(QString::fromUtf8(meta->color), QString("#33aaff"));

        pressFlagKey(&widget, [&](FlagDialog *dialog) {
            QCOMPARE(field(dialog, "nameEdit")->text(), QString("test.flag"));
            QCOMPARE(field(dialog, "colorEdit")->text(), QString("#33aaff"));
            QCOMPARE(field(dialog, "commentEdit")->text(), QString("selected bytes"));
            field(dialog, "nameEdit")->setText("test.edited");
            field(dialog, "sizeEdit")->setText("5");
            save(dialog);
        });
        QVERIFY(!r_flag_get(Core()->core()->flags, "test.flag"));
        flag = r_flag_get(Core()->core()->flags, "test.edited");
        QVERIFY(flag);
        QCOMPARE(flag->size, ut64(5));
    }

    void disassemblyFlags_data()
    {
        QTest::addColumn<int>("direction");
        QTest::newRow("cursor") << 0;
        QTest::newRow("forward-range") << 1;
        QTest::newRow("backward-range") << -1;
    }

    void disassemblyFlags()
    {
        QFETCH(int, direction);
        TestDisassemblyWidget widget;
        widget.resize(1000, 500);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        widget.showInstructions();
        auto *edit = qobject_cast<QPlainTextEdit *>(widget.getTextWidget());
        QVERIFY(edit);
        QTextCursor cursor(edit->document());
        if (direction) {
            const int end = 11; // Three "nop" lines, including the final instruction.
            cursor.setPosition(direction > 0 ? 0 : end);
            cursor.setPosition(direction > 0 ? end : 0, QTextCursor::KeepAnchor);
        }
        edit->setTextCursor(cursor);
        edit->setFocus();
        pressFlagKey(edit, [&](FlagDialog *dialog) {
            QCOMPARE(field(dialog, "offsetEdit")->text(), RAddressString(0x100));
            QCOMPARE(field(dialog, "sizeEdit")->text(), QString(direction ? "12" : "1"));
            field(dialog, "nameEdit")->setText("test.disasm");
            field(dialog, "colorEdit")->setText("#ff8844");
            save(dialog);
        });
        RFlagItem *flag = r_flag_get(Core()->core()->flags, "test.disasm");
        QVERIFY(flag);
        QCOMPARE(flag->addr, ut64(0x100));
        QCOMPARE(flag->size, ut64(direction ? 12 : 1));
    }

    void flagMenuDeletion()
    {
        QWidget parent;
        Core()->addFlag(0x100, "test.delete", 8);
        Core()->addFlag(0x108, "test.keep", 1);
        HexWidget hex;
        QFont font = Config()->getFont();
        font.setPointSize(10);
        hex.setMonospaceFont(font);
        hex.setFixedLineSize(16);
        hex.resize(1000, 500);
        hex.show();
        QVERIFY(QTest::qWaitForWindowExposed(&hex));
        hex.seek(0);
        hex.selectRange(0x100, 0x107);
        bool menuOpened = false;
        connect(&hex, &HexWidget::contextMenuAboutToShow, this, [&](QMenu *menu, RVA start, RVA end) {
            menuOpened = true;
            // Always close the popup, including when a check fails.
            QTimer::singleShot(0, menu, &QMenu::close);
            QCOMPARE(start, RVA(0x100));
            QCOMPARE(end, RVA(0x107));
            QAction *remove = nullptr;
            QAction *edit = nullptr;
            for (auto *action : menu->actions()) {
                if (action->text() == "Delete flag") {
                    remove = action;
                } else if (action->text() == "Edit flag...") {
                    edit = action;
                }
            }
            QVERIFY(edit);
            QVERIFY(remove);
            QVERIFY(edit->isEnabled());
            QVERIFY(remove->isEnabled());
            QCOMPARE(edit->shortcut(), QKeySequence(Qt::Key_F));
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
            QVERIFY(edit->isShortcutVisibleInContextMenu());
#endif
            remove->trigger();
        });
        QFontMetricsF metrics(font);
        const QPoint point(
            qRound(22 * metrics.size(Qt::TextSingleLine, "F").width()),
            qRound(17.5 * qCeil(metrics.height())));
        QContextMenuEvent event(QContextMenuEvent::Mouse, point, hex.viewport()->mapToGlobal(point));
        QApplication::sendEvent(hex.viewport(), &event);
        QVERIFY(menuOpened);
        QVERIFY(!r_flag_get(Core()->core()->flags, "test.delete"));
        QVERIFY(r_flag_get(Core()->core()->flags, "test.keep"));

        Core()->addFlag(0x100, "test.delete", 8);
        DisassemblyContextMenu disasmMenu(&parent, nullptr);
        disasmMenu.setOffset(0x108);
        disasmMenu.setSelectionRange(0x100, 8);
        disasmMenu.popup(QPoint());
        QAction *remove = nullptr;
        for (auto *action : disasmMenu.findChildren<QAction *>()) {
            if (action->text().startsWith("Delete flag at")) {
                remove = action;
            }
        }
        QVERIFY(remove);
        QVERIFY(remove->isEnabled());
        remove->trigger();
        disasmMenu.close();
        QVERIFY(!r_flag_get(Core()->core()->flags, "test.delete"));
        QVERIFY(r_flag_get(Core()->core()->flags, "test.keep"));
    }

    void largeFlagSize()
    {
        const ut64 size = ut64(1) << 32;
        FlagDialog dialog(0x100, size);
        QCOMPARE(field(&dialog, "sizeEdit")->text(), QString::number(size));
        field(&dialog, "nameEdit")->setText("test.large");
        QVERIFY(field(&dialog, "sizeEdit")->hasAcceptableInput());
        save(&dialog);
        auto *flag = r_flag_get(Core()->core()->flags, "test.large");
        QVERIFY(flag);
        QCOMPARE(flag->size, size);
    }

    void configurableShortcuts()
    {
        const QKeySequence custom(Qt::CTRL | Qt::SHIFT | Qt::Key_F7);
        HexWidget hex;
        hex.resize(1000, 500);
        hex.show();
        QVERIFY(QTest::qWaitForWindowExposed(&hex));
        ShortcutMgr()->setSequence("hex.flag", custom);
        hex.setFocus();
        pressFlagKey(&hex, [](FlagDialog *) {}, Qt::Key_F7, Qt::ControlModifier | Qt::ShiftModifier);

        TestDisassemblyWidget disasm;
        disasm.resize(1000, 500);
        disasm.show();
        QVERIFY(QTest::qWaitForWindowExposed(&disasm));
        disasm.showInstructions();
        ShortcutMgr()->setSequence("disasm.flag", custom);
        auto *edit = disasm.getTextWidget();
        edit->setFocus();
        pressFlagKey(edit, [](FlagDialog *) {}, Qt::Key_F7, Qt::ControlModifier | Qt::ShiftModifier);

        auto *menu = disasm.findChild<DisassemblyContextMenu *>();
        QVERIFY(menu);
        bool shownInMenu = false;
        for (auto *action : menu->findChildren<QAction *>()) {
            shownInMenu |= action->shortcut() == custom && action->text().contains("flag");
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
            if (action->shortcut() == custom) {
                QVERIFY(action->isShortcutVisibleInContextMenu());
            }
#endif
        }
        QVERIFY(shownInMenu);
        QCOMPARE(ShortcutMgr()->sequence("graph.takeFalse"), QKeySequence(Qt::Key_F));
    }
};

QTEST_MAIN(FlagTests)
#include "test_flags.moc"
