#include "common/Configuration.h"
#include "dialogs/FunctionDetailsDialog.h"
#include "menus/AddressableItemContextMenu.h"
#include "menus/DecompilerContextMenu.h"
#include "menus/DisassemblyContextMenu.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

class FunctionDetailsTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;

    static QJsonObject info(RVA address)
    {
        return Core()->cmdjAt("afij", address).array().first().toObject();
    }

    static void apply(FunctionDetailsDialog &dialog)
    {
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
    }

    static QString status(FunctionDetailsDialog &dialog)
    {
        return dialog.findChild<QLabel *>("functionDetailsStatus")->text();
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("functiondetails");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        Core()->initialize(false);
        qRegisterMetaType<RVA>("RVA");
        Config()->loadInitial();
        Config()->setOutputRedirectionEnabled(false);
        Config()->setDecompilerAutoRefreshEnabled(false);
        Core()->cmd("o malloc://512");
        Core()->setConfig("asm.arch", "x86");
        Core()->setConfig("asm.bits", 64);
        Core()->cmd("wx 554889e54883ec10897dfc8b45fcc9c3 @ 0");
        Core()->cmd("wx 554889e54883ec10897dfc8b45fcc9c3 @ 64");
        Core()->cmd("ompg +x");
    }

    void init()
    {
        Core()->cmd("af-*; f--; ax-*");
        Core()->cmdRawAt("af", 0);
        Core()->cmdRawAt("afn first", 0);
        Core()->cmdRawAt("'afs int first(int arg1);", 0);
        Core()->cmdRawAt("afia=", 0);
        Core()->cmdRawAt("af", 64);
        Core()->cmdRawAt("afn second", 64);
        Core()->cmdRawAt("'afs int second(int arg1);", 64);
        Core()->cmdRawAt("afia=", 64);
        Core()->seek(0x100);
    }

    void allInformationAndAttributes()
    {
        QSignalSpy seeks(Core(), &IaitoCore::seekChanged);
        FunctionDetailsDialog dialog(4);
        QCOMPARE(dialog.findChild<QLineEdit *>("functionName")->text(), QString("first"));
        QVERIFY(!dialog.isModal());
        auto tree = dialog.findChild<QTreeWidget *>("functionInformation");
        QCOMPARE(tree->topLevelItemCount(), info(0).size());
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QVERIFY(!(tree->topLevelItem(i)->flags() & Qt::ItemIsEditable));
        }
        const auto attrs = info(0).value("attrs").toString();
        for (size_t i = 0; const auto attr = r_anal_function_attr_info(i); ++i) {
            auto checkbox = dialog.findChild<QCheckBox *>(QString("attribute%1").arg(attr->letter));
            QVERIFY(checkbox);
            QCOMPARE(checkbox->isChecked(), attrs.contains(QLatin1Char(attr->letter)));
            QCOMPARE(checkbox->isEnabled(), !(attr->bit & R_ANAL_FCN_ATTR_COMPUTED));
        }
        QCOMPARE(Core()->getOffset(), RVA(0x100));
        QCOMPARE(seeks.count(), 0);
        dialog.show();
        QTest::qWait(30);
        QVERIFY(dialog.grab().save("/tmp/iaito-function-details.png"));
    }

    void multipleWindowsAndPinnedEdits()
    {
        QWidget owner;
        QPointer<FunctionDetailsDialog> first = FunctionDetailsDialog::showFunction(4, &owner);
        QPointer<FunctionDetailsDialog> second = FunctionDetailsDialog::showFunction(68, &owner);
        QCOMPARE(owner.findChildren<FunctionDetailsDialog *>().size(), 2);
        QVERIFY(first->isVisible() && second->isVisible());
        QVERIFY(!QApplication::activeModalWidget());
        Core()->seek(64);
        QSignalSpy seeks(Core(), &IaitoCore::seekChanged);
        first->findChild<QLineEdit *>("functionName")->setText("renamed");
        first->findChild<QLineEdit *>("stackSize")->setText("64");
        auto convention = first->findChild<QComboBox *>("callingConvention");
        QVERIFY(convention->findText("ms") >= 0);
        convention->setCurrentText("ms");
        first->findChild<QCheckBox *>("attributeI")->setChecked(true);
        apply(*first);
        QVERIFY2(status(*first).isEmpty(), qPrintable(status(*first)));
        const auto changed = info(0);
        QCOMPARE(changed.value("name").toString(), QString("renamed"));
        QCOMPARE(changed.value("stackframe").toInt(), 64);
        QCOMPARE(changed.value("calltype").toString(), QString("ms"));
        QVERIFY(changed.value("attrs").toString().contains('I'));
        QCOMPARE(info(64).value("name").toString(), QString("second"));
        QVERIFY(!info(64).value("attrs").toString().contains('I'));
        QCOMPARE(Core()->getOffset(), RVA(64));
        QCOMPARE(seeks.count(), 0);
        first->close();
        QTRY_VERIFY(first.isNull());
        QVERIFY(second && second->isVisible());
        second->close();
        QTRY_VERIFY(second.isNull());
    }

    void signatureAndLocalVariables()
    {
        FunctionDetailsDialog dialog(0);
        dialog.findChild<QPlainTextEdit *>("functionSignature")
            ->setPlainText("char *first(const char *text, int count, ...);");
        auto table = dialog.findChild<QTableWidget *>("functionVariables");
        int localRow = -1;
        for (int row = 0; row < table->rowCount(); ++row) {
            if (table->item(row, 0)->flags() & Qt::ItemIsEditable) {
                localRow = row;
            } else {
                QVERIFY(!(table->item(row, 1)->flags() & Qt::ItemIsEditable));
            }
        }
        QVERIFY(localRow >= 0);
        table->item(localRow, 0)->setText("counter");
        table->item(localRow, 1)->setText("unsigned int");
        apply(dialog);
        QVERIFY2(status(dialog).isEmpty(), qPrintable(status(dialog)));
        const auto signature = Core()->cmdRawAt("afs", 0);
        QVERIFY2(signature.contains("char *"), qPrintable(signature));
        QVERIFY2(signature.contains("text") && signature.contains("count"), qPrintable(signature));
        QVERIFY(signature.contains("..."));
        QCOMPARE(
            dialog.findChild<QCheckBox *>("attributeV")->isChecked(),
            info(0).value("attrs").toString().contains('V'));
        bool found = false;
        for (const auto &variable : Core()->getVariables(0)) {
            if (variable.name == "counter") {
                found = true;
                QCOMPARE(variable.type, QString("unsigned int"));
            }
        }
        QVERIFY(found);
        QCOMPARE(Core()->getOffset(), RVA(0x100));
    }

    void attributesRoundTrip()
    {
        FunctionDetailsDialog dialog(0);
        const QString editable = "NITLSCDKQ";
        for (QChar letter : editable) {
            dialog.findChild<QCheckBox *>(QString("attribute%1").arg(letter))->setChecked(true);
        }
        apply(dialog);
        for (QChar letter : editable) {
            QVERIFY(info(0).value("attrs").toString().contains(letter));
            dialog.findChild<QCheckBox *>(QString("attribute%1").arg(letter))->setChecked(false);
        }
        apply(dialog);
        for (QChar letter : editable) {
            QVERIFY(!info(0).value("attrs").toString().contains(letter));
        }
    }

    void renamedSignatureAndMultilineInput()
    {
        FunctionDetailsDialog dialog(0);
        dialog.findChild<QLineEdit *>("functionName")->setText("sym.myfirst");
        dialog.findChild<QPlainTextEdit *>("functionSignature")
            ->setPlainText("char *sym.myfirst(\n  int value\n);");
        apply(dialog);
        QVERIFY2(status(dialog).isEmpty(), qPrintable(status(dialog)));
        QCOMPARE(info(0).value("name").toString(), QString("sym.myfirst"));
        QVERIFY(Core()->cmdRawAt("afs", 0).contains("char *"));
        QVERIFY(Core()->cmdRawAt("afs", 0).contains("value"));
    }

    void localTypes_data()
    {
        QTest::addColumn<QString>("type");
        QTest::newRow("pointer") << QString("char *");
        QTest::newRow("array") << QString("char[16]");
        QTest::newRow("structure") << QString("struct example *");
        QTest::newRow("function-pointer") << QString("int (*)(int)");
    }

    void localTypes()
    {
        QFETCH(QString, type);
        FunctionDetailsDialog dialog(0);
        auto table = dialog.findChild<QTableWidget *>("functionVariables");
        QString name;
        for (int row = 0; row < table->rowCount(); ++row) {
            if (table->item(row, 1)->flags() & Qt::ItemIsEditable) {
                name = table->item(row, 0)->text();
                table->item(row, 1)->setText(type);
                break;
            }
        }
        QVERIFY(!name.isEmpty());
        apply(dialog);
        QVERIFY2(status(dialog).isEmpty(), qPrintable(status(dialog)));
        bool found = false;
        for (const auto &variable : Core()->getVariables(0)) {
            if (variable.name == name) {
                found = true;
                QCOMPARE(variable.type, type);
            }
        }
        QVERIFY(found);
    }

    void concurrentWindowsPreservePendingEdits()
    {
        FunctionDetailsDialog first(0);
        FunctionDetailsDialog second(0);
        first.findChild<QLineEdit *>("stackSize")->setText("80");
        second.findChild<QCheckBox *>("attributeI")->setChecked(true);
        apply(second);
        QCOMPARE(first.findChild<QLineEdit *>("stackSize")->text(), QString("80"));
        apply(first);
        QCOMPARE(info(0).value("stackframe").toInt(), 80);
        QVERIFY(info(0).value("attrs").toString().contains('I'));
        QCOMPARE(second.findChild<QLineEdit *>("stackSize")->text(), QString("80"));
        first.findChild<QLineEdit *>("functionName")->setText("stale");
        second.findChild<QLineEdit *>("functionName")->setText("winner");
        apply(second);
        apply(first);
        QVERIFY(!status(first).isEmpty());
        QCOMPARE(info(0).value("name").toString(), QString("winner"));
        first.findChild<QPushButton *>("refreshFunctionDetails")->click();
        QCOMPARE(first.findChild<QLineEdit *>("functionName")->text(), QString("winner"));
    }

    void registerLocalsAreEditable()
    {
        {
            auto core = Core()->core();
            auto reg = r_reg_get(core->anal->reg, "r12", R_REG_TYPE_GPR);
            QVERIFY(reg);
            QVERIFY(r_anal_function_set_var(
                r_anal_get_function_at(core->anal, 0),
                reg->index,
                R_ANAL_VAR_KIND_REG,
                "int",
                4,
                false,
                "register_local"));
        }
        FunctionDetailsDialog dialog(0);
        auto table = dialog.findChild<QTableWidget *>("functionVariables");
        bool found = false;
        for (int row = 0; row < table->rowCount(); ++row) {
            if (table->item(row, 0)->text() == "register_local") {
                found = true;
                QVERIFY(table->item(row, 0)->flags() & Qt::ItemIsEditable);
                QVERIFY(table->item(row, 1)->flags() & Qt::ItemIsEditable);
                table->item(row, 0)->setText("accumulator");
                table->item(row, 1)->setText("unsigned int");
            }
        }
        QVERIFY(found);
        apply(dialog);
        QVERIFY2(status(dialog).isEmpty(), qPrintable(status(dialog)));
    }

    void invalidInputDoesNotPartiallyApply()
    {
        FunctionDetailsDialog dialog(0);
        dialog.findChild<QLineEdit *>("functionName")->setText("invalid");
        dialog.findChild<QPlainTextEdit *>("functionSignature")->setPlainText("not a signature");
        apply(dialog);
        QVERIFY(!status(dialog).isEmpty());
        QCOMPARE(info(0).value("name").toString(), QString("first"));
        dialog.findChild<QPushButton *>("refreshFunctionDetails")->click();
        auto table = dialog.findChild<QTableWidget *>("functionVariables");
        for (int row = 0; row < table->rowCount(); ++row) {
            if (table->item(row, 1)->flags() & Qt::ItemIsEditable) {
                table->item(row, 1)->setText("int;f injected @ 32");
            }
        }
        dialog.findChild<QLineEdit *>("functionName")->setText("must_not_rename");
        apply(dialog);
        QVERIFY(!status(dialog).isEmpty());
        QVERIFY(!r_flag_get(Core()->core()->flags, "injected"));
        QCOMPARE(info(0).value("name").toString(), QString("first"));
        dialog.findChild<QPushButton *>("refreshFunctionDetails")->click();
        dialog.findChild<QLineEdit *>("functionName")->setText("bad;f injected @ 32");
        apply(dialog);
        QVERIFY(!status(dialog).isEmpty());
        QVERIFY(!r_flag_get(Core()->core()->flags, "injected"));
        QCOMPARE(info(0).value("name").toString(), QString("first"));
    }

    void deletionAndMenuIntegration()
    {
        QWidget owner;
        AddressableItemContextMenu menu(&owner, nullptr);
        auto action = menu.findChild<QAction *>("actionFunctionDetails");
        QVERIFY(action);
        menu.setOffset(4);
        QVERIFY(action->isEnabled());
        action->trigger();
        auto dialog = owner.findChild<FunctionDetailsDialog *>();
        QVERIFY(dialog && dialog->isVisible());
        dialog->findChild<QLineEdit *>("functionName")->setText("deleted");
        Core()->delFunction(0);
        apply(*dialog);
        QVERIFY(!dialog->findChild<QLineEdit *>("functionName")->isEnabled());
        QVERIFY(!status(*dialog).isEmpty());
        QVERIFY(!Core()->functionAt(0));
        menu.clearTarget();
        QVERIFY(!action->isEnabled());
        menu.setOffset(0x100);
        QVERIFY(!action->isEnabled());

        DisassemblyContextMenu disassembly(&owner, nullptr);
        disassembly.setOffset(64);
        QVERIFY(QMetaObject::invokeMethod(&disassembly, "on_actionEditFunction_triggered"));
        QCOMPARE(owner.findChildren<FunctionDetailsDialog *>().size(), 2);
        DecompilerContextMenu decompiler(&owner, nullptr);
        decompiler.setDecompiledFunctionAddress(64);
        decompiler.findChild<QAction *>("actionFunctionDetails")->trigger();
        QCOMPARE(owner.findChildren<FunctionDetailsDialog *>().size(), 3);
    }
};

QTEST_MAIN(FunctionDetailsTests)
#include "test_functiondetails.moc"
