#include "common/Configuration.h"
#include "common/IaitoSeekable.h"
#include "dialogs/settings/GraphOptionsWidget.h"
#include "widgets/DisassemblerGraphView.h"

#include <QAction>
#include <QCheckBox>
#include <QMenu>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

class GraphAddressTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;

    static QAction *addressAction(
        QObject &parent, const QString &text = QStringLiteral("Show instruction addresses"))
    {
        for (auto *action : parent.findChildren<QAction *>()) {
            if (action->text() == text) {
                return action;
            }
        }
        return nullptr;
    }

    static QString instructionText(const DisassemblerGraphView &graph)
    {
        for (const auto &block : graph.disassembly_blocks) {
            for (const auto &instr : block.second.instrs) {
                if (instr.addr == 4 && instr.size == 5) {
                    return instr.plainText.simplified();
                }
            }
        }
        return {};
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("graphaddresses");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        Core()->initialize(false);
        qRegisterMetaType<RVA>("RVA");
        Config()->loadInitial();
        Config()->setOutputRedirectionEnabled(false);
        Core()->cmd("o malloc://64");
        Core()->setConfig("asm.arch", "x86");
        Core()->setConfig("asm.bits", 64);
        Core()->cmd("wx 554889e5b82a0000005dc3 @ 0");
        Core()->cmd("ompg +x");
        Core()->cmd("af @ 0");
    }

    void visibilityAndNavigation_data()
    {
        QTest::addColumn<bool>("graphAddresses");
        QTest::addColumn<bool>("disassemblyAddresses");
        QTest::newRow("both-hidden") << false << false;
        QTest::newRow("graph-hidden") << false << true;
        QTest::newRow("disassembly-hidden") << true << false;
        QTest::newRow("both-visible") << true << true;
    }

    void visibilityAndNavigation()
    {
        QFETCH(bool, graphAddresses);
        QFETCH(bool, disassemblyAddresses);
        Config()->setConfig("graph.addr", graphAddresses);
        Config()->setConfig("asm.addr", disassemblyAddresses);
        Config()->setGraphBlockEntryOffset(true);
        IaitoSeekable seekable;
        seekable.setSynchronization(false);
        seekable.seek(0);
        DisassemblerGraphView graph(nullptr, &seekable, nullptr, {});
        graph.refreshView();
        QVERIFY(instructionText(graph).contains("mov eax, 0x2a"));
        QCOMPARE(instructionText(graph).contains(RAddressString(4)), graphAddresses);
        QCOMPARE(Core()->getConfigb("asm.addr"), disassemblyAddresses);
        QCOMPARE(graph.disassembly_blocks.size(), size_t(1));
        QVERIFY(graph.disassembly_blocks.at(0).header_text.ToQString().contains(RAddressString(0)));

        graph.nextInstr();
        QCOMPARE(seekable.getOffset(), RVA(1));
        graph.nextInstr();
        QCOMPARE(seekable.getOffset(), RVA(4));
        graph.nextInstr();
        QCOMPARE(seekable.getOffset(), RVA(9));
        graph.prevInstr();
        QCOMPARE(seekable.getOffset(), RVA(4));
    }

    void menusAndSettingsStaySynchronized()
    {
        Config()->setConfig("graph.addr", false);
        IaitoSeekable seekable;
        seekable.setSynchronization(false);
        seekable.seek(0);
        DisassemblerGraphView graph(nullptr, &seekable, nullptr, {});
        DisassemblerGraphView secondGraph(nullptr, &seekable, nullptr, {});
        GraphOptionsWidget options(nullptr);
        auto *checkbox = options.findChild<QCheckBox *>("graphOffsetCheckBox");
        auto *action = addressAction(graph);
        auto *secondAction = addressAction(secondGraph);
        QVERIFY(checkbox && action && secondAction);
        QVERIFY(action->isCheckable());
        QVERIFY(!checkbox->isChecked());
        QVERIFY(!action->isChecked());

        int menuCount = 0;
        for (auto *menu : graph.findChildren<QMenu *>()) {
            if (menu->actions().contains(action)) {
                ++menuCount;
            }
        }
        QCOMPARE(menuCount, 2); // Both the background and basic-block menus.

        action->trigger();
        QVERIFY(checkbox->isChecked());
        QVERIFY(secondAction->isChecked());
        QVERIFY(instructionText(graph).contains(RAddressString(4)));
        QVERIFY(instructionText(secondGraph).contains(RAddressString(4)));
        QVERIFY(QSettings().value("graph.addr").toBool());

        checkbox->setChecked(false);
        QVERIFY(!action->isChecked());
        QVERIFY(!secondAction->isChecked());
        QVERIFY(instructionText(graph).contains("mov eax, 0x2a"));
        QVERIFY(!instructionText(graph).contains(RAddressString(4)));
        QVERIFY(!QSettings().value("graph.addr").toBool());
        Core()->setConfig("graph.addr", true);
        Config()->loadInitial();
        QVERIFY(!Core()->getConfigb("graph.addr"));
    }

    void viewMenuControlsGraphAddresses_data() { visibilityAndNavigation_data(); }

    void viewMenuControlsGraphAddresses()
    {
        QFETCH(bool, graphAddresses);
        QFETCH(bool, disassemblyAddresses);
        Config()->setConfig("graph.addr", graphAddresses);
        Config()->setConfig("asm.addr", disassemblyAddresses);
        IaitoSeekable seekable;
        seekable.setSynchronization(false);
        seekable.seek(0);
        DisassemblerGraphView graph(nullptr, &seekable, nullptr, {});
        DisassemblerGraphView secondGraph(nullptr, &seekable, nullptr, {});
        GraphOptionsWidget options(nullptr);
        auto *checkbox = options.findChild<QCheckBox *>("graphOffsetCheckBox");
        auto *menu = graph.findChild<DisassemblyContextMenu *>();
        auto *secondMenu = secondGraph.findChild<DisassemblyContextMenu *>();
        QVERIFY(checkbox && menu && secondMenu);
        auto *action = addressAction(*menu, QStringLiteral("Show addresses"));
        auto *secondAction = addressAction(*secondMenu, QStringLiteral("Show addresses"));
        auto *backgroundAction = addressAction(graph);
        QVERIFY(action && secondAction && backgroundAction);
        QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection));
        QCOMPARE(action->isChecked(), graphAddresses);

        action->trigger();
        QCOMPARE(instructionText(graph).contains(RAddressString(4)), !graphAddresses);
        QCOMPARE(instructionText(secondGraph).contains(RAddressString(4)), !graphAddresses);
        QCOMPARE(backgroundAction->isChecked(), !graphAddresses);
        QCOMPARE(secondAction->isChecked(), !graphAddresses);
        QCOMPARE(checkbox->isChecked(), !graphAddresses);
        QCOMPARE(QSettings().value("graph.addr").toBool(), !graphAddresses);
        QCOMPARE(Core()->getConfigb("asm.addr"), disassemblyAddresses);

        backgroundAction->trigger();
        QCOMPARE(action->isChecked(), graphAddresses);
        QCOMPARE(secondAction->isChecked(), graphAddresses);
        QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection));
        QCOMPARE(action->isChecked(), graphAddresses);

        QWidget disassemblyParent;
        DisassemblyContextMenu disassemblyMenu(&disassemblyParent, nullptr);
        auto *disassemblyAction = addressAction(disassemblyMenu, QStringLiteral("Show addresses"));
        QVERIFY(disassemblyAction);
        QCOMPARE(disassemblyAction->isChecked(), disassemblyAddresses);
        disassemblyAction->trigger();
        QCOMPARE(Core()->getConfigb("asm.addr"), !disassemblyAddresses);
        QCOMPARE(Core()->getConfigb("graph.addr"), graphAddresses);
        QCOMPARE(instructionText(graph).contains(RAddressString(4)), graphAddresses);
    }
};

QTEST_MAIN(GraphAddressTests)
#include "test_graphaddresses.moc"
