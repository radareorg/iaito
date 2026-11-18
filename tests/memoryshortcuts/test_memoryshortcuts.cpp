#include "common/Configuration.h"
#include "common/IaitoSeekable.h"
#include "core/MainWindow.h"
#include "widgets/MemoryDockWidget.h"

#include <QApplication>
#include <QPointer>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

class MemoryShortcutTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("memoryshortcuts");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        Core()->initialize(false);
        qRegisterMetaType<RVA>("RVA");
        Config()->loadInitial();
        Config()->setOutputRedirectionEnabled(false);
        Config()->setDecompilerAutoRefreshEnabled(false);
        Core()->cmd("o malloc://16384");
        Core()->seek(0x100);
    }

    void switchViews_data()
    {
        QTest::addColumn<int>("sourceType");
        QTest::addColumn<int>("targetType");
        QTest::addColumn<int>("key");
        QTest::addColumn<QString>("targetState");
        struct Switch
        {
            const char *name;
            MemoryWidgetType source;
            MemoryWidgetType target;
            Qt::Key key;
        };
        const Switch switches[] = {
            {"disasm-graph", MemoryWidgetType::Disassembly, MemoryWidgetType::Graph, Qt::Key_Space},
            {"graph-disasm", MemoryWidgetType::Graph, MemoryWidgetType::Disassembly, Qt::Key_Space},
            {"disasm-decompiler",
             MemoryWidgetType::Disassembly,
             MemoryWidgetType::Decompiler,
             Qt::Key_Tab},
            {"decompiler-disasm",
             MemoryWidgetType::Decompiler,
             MemoryWidgetType::Disassembly,
             Qt::Key_Tab},
        };
        for (const auto &viewSwitch : switches) {
            for (const QString &state : {"open", "hidden", "closed", "absent"}) {
                const QByteArray name = QByteArray(viewSwitch.name) + '-' + state.toUtf8();
                QTest::newRow(name.constData()) << int(viewSwitch.source) << int(viewSwitch.target)
                                                << int(viewSwitch.key) << state;
            }
        }
    }

    void switchViews()
    {
        QFETCH(int, sourceType);
        QFETCH(int, targetType);
        QFETCH(int, key);
        QFETCH(QString, targetState);
        MainWindow main;
        main.resize(1100, 700);
        main.show();
        QVERIFY(QTest::qWaitForWindowExposed(&main));

        auto *source = main.addNewMemoryWidget(MemoryWidgetType(sourceType), Core()->getOffset());
        QPointer<MemoryDockWidget> target;
        if (targetState != "absent") {
            target = main.addNewMemoryWidget(MemoryWidgetType(targetType), Core()->getOffset());
            if (targetState == "hidden") {
                target->hide();
            } else if (targetState == "closed") {
                target->close();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                QVERIFY(!target);
            }
        }
        source->raiseMemoryWidget();
        QTRY_VERIFY(source->hasFocus() || source->isAncestorOf(QApplication::focusWidget()));

        // Explicit switching must work even when the current address has no function graph.
        Core()->setGraphEmpty(true);
        QTest::keyClick(QApplication::focusWidget(), Qt::Key(key));
        QTRY_VERIFY(main.getLastMemoryWidget() != source);
        auto *raised = main.getLastMemoryWidget();
        QVERIFY(raised);
        QCOMPARE(int(raised->getType()), targetType);
        QVERIFY(raised->isVisible());
        QVERIFY(raised->hasFocus() || raised->isAncestorOf(QApplication::focusWidget()));
        QCOMPARE(raised->getSeekable()->getOffset(), Core()->getOffset());
        if (target) {
            QCOMPARE(raised, target.data());
        }
        int targetCount = 0;
        for (auto *widget : main.findChildren<MemoryDockWidget *>()) {
            targetCount += int(widget->getType()) == targetType;
        }
        QCOMPARE(targetCount, 1);
    }
};

QTEST_MAIN(MemoryShortcutTests)
#include "test_memoryshortcuts.moc"
