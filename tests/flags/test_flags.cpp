#include "common/Configuration.h"
#include "common/IaitoSeekable.h"
#include "common/ShortcutManager.h"
#include "dialogs/FlagDialog.h"
#include "menus/DisassemblyContextMenu.h"
#include "widgets/DisassemblyWidget.h"
#include "widgets/HexWidget.h"

#include <QColorDialog>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QFontMetricsF>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
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
