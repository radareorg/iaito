#include "common/Configuration.h"
#include "common/theme/IaitoStyle.h"
#include "core/DockManager.h"
#include "core/Iaito.h"
#include "core/MainWindow.h"
#include "dialogs/NewFileDialog.h"
#include "widgets/ConsoleWidget.h"
#include "widgets/DisassemblyWidget.h"

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>
#include <QToolBar>
#include <QToolButton>

class MobileMainWindowTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;

private slots:
    void initTestCase()
    {
        qputenv("IAITO_MOBILE_UI", "1");
        qputenv("XDG_DATA_HOME", settings.path().toUtf8());
        qputenv("XDG_CACHE_HOME", settings.path().toUtf8());
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("mobilemainwindow");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        qApp->setStyle(new IaitoStyle);
        Core()->initialize(false);
        Config()->loadInitial();
        Config()->setOutputRedirectionEnabled(false);
        Config()->setDecompilerAutoRefreshEnabled(false);
        Core()->cmd("o malloc://256");
        Core()->cmd("e asm.arch=x86");
        Core()->cmd("e asm.bits=64");
        Core()->cmd("wx 9090c3 @0");
        Core()->cmd("e anal.in=io.maps");
        Core()->cmd("af @0");
        Core()->seek(0);
    }

    void fileUriSelection()
    {
        const QString uri = "content://provider/document/primary%3ADownload%2Fa%20b.bin";
        QTemporaryFile local;
        QVERIFY(local.open());
        QSettings().setValue("recentFileList", QStringList{uri, local.fileName()});
        MainWindow window;
        NewFileDialog dialog(&window);
        auto *plugin = dialog.findChild<QComboBox *>("ioPlugin");
        auto *path = dialog.findChild<QLineEdit *>("newFileEdit");
        auto *load = dialog.findChild<QPushButton *>("loadFileButton");
        auto *recents = dialog.findChild<QListWidget *>("recentsListWidget");
        QVERIFY(plugin && path && load && recents);
        QCOMPARE(plugin->currentText(), QString("file://"));
        // Older desktop radare2 builds may not have Android's content plugin.
        if (plugin->findText("content://") < 0) {
            plugin->addItem("content://");
        }
        QCOMPARE(recents->count(), 2);
        QCOMPARE(recents->item(0)->data(Qt::UserRole).toString(), uri);
        QVERIFY(QSettings().value("recentFileList").toStringList().contains(uri));
        path->setText(uri);
        QCOMPARE(plugin->currentText(), QString("content://"));
        QCOMPARE(path->text(), uri);
        QVERIFY(load->isEnabled());
        QVERIFY(
            QMetaObject::invokeMethod(
                &dialog,
                "on_recentsListWidget_itemClicked",
                Q_ARG(QListWidgetItem *, recents->item(1))));
        QCOMPARE(plugin->currentText(), QString("file://"));
        QVERIFY(load->isEnabled());
        path->setText(local.fileName() + ".missing");
        QVERIFY(!load->isEnabled());
        path->setText("malloc://1024");
        QCOMPARE(plugin->currentText(), QString("malloc://"));
        QVERIFY(load->isEnabled());
        path->setText("content://");
        QVERIFY(!load->isEnabled());
    }

    void openFromMobileWindow()
    {
        MainWindow window;
        auto findDialog = []() -> NewFileDialog * {
            for (auto *widget : QApplication::topLevelWidgets()) {
                if (auto *dialog = qobject_cast<NewFileDialog *>(widget)) {
                    return dialog;
                }
            }
            return nullptr;
        };
        QVERIFY(QMetaObject::invokeMethod(&window, "on_actionNew_triggered"));
        QPointer<NewFileDialog> dialog = findDialog();
        QVERIFY(dialog && dialog->isVisible());
        QVERIFY(QMetaObject::invokeMethod(&window, "on_actionNew_triggered"));
        QCOMPARE(findDialog(), dialog.data());
        window.closeNewFileDialog();
        QTRY_VERIFY(dialog.isNull());
        QVERIFY(QMetaObject::invokeMethod(&window, "on_actionNew_triggered"));
        QVERIFY(findDialog());
        window.closeNewFileDialog();
        QTRY_VERIFY(!findDialog());
    }

    void controlsAndPanels()
    {
        // Upgrades must not restore a desktop layout with a hidden toolbar.
        QMainWindow desktop;
        auto *oldToolbar = desktop.addToolBar("Main toolbar");
        oldToolbar->setObjectName("mainToolBar");
        oldToolbar->hide();
        QSettings saved;
        saved.beginWriteArray("layouts", 1);
        saved.setArrayIndex(0);
        saved.setValue("name", Iaito::LAYOUT_DEFAULT);
        saved.setValue("geometry", desktop.saveGeometry());
        saved.setValue("state", desktop.saveState());
        saved.endArray();

        MainWindow window;
        QVERIFY(window.isMobileUI());
        window.finalizeOpen();
        QTRY_VERIFY(window.isUiReady());
        window.showNormal();

        auto *toolbar = window.findChild<QToolBar *>("mainToolBar");
        auto *addressBar = window.findChild<QToolBar *>("mobileNavigationBar");
        auto *address = window.findChild<QLineEdit *>("mobileAddressField");
        auto *menuButton = window.findChild<QToolButton *>("mobileMenuButton");
        auto *panelsButton = window.findChild<QToolButton *>("mobilePanelsButton");
        auto *disassembly = window.findChild<DisassemblyWidget *>();
        QVERIFY(toolbar && addressBar && address && menuButton && panelsButton && disassembly);
        QVERIFY(!window.menuBar()->isNativeMenuBar());
        QVERIFY(menuButton->menu()->actions().contains(
            window.getMenuByType(MainWindow::MenuType::File)->menuAction()));

        for (QSize size :
             {QSize(320, 480), QSize(360, 640), QSize(640, 360), QSize(768, 1024), QSize(1024, 768)}) {
            window.resize(size);
            QTRY_COMPARE(window.size(), size);
            QCoreApplication::processEvents();
            for (auto *tabs : window.findChildren<QTabBar *>()) {
                if (tabs->isVisible() && tabs->parentWidget() == &window) {
                    QTRY_VERIFY(tabs->height() >= 40);
                }
            }
            if (qEnvironmentVariableIsSet("IAITO_MOBILE_SCREENSHOTS")) {
                window.grab().save(
                    qEnvironmentVariable("IAITO_MOBILE_SCREENSHOTS")
                    + QString("/mainwindow-%1x%2.png").arg(size.width()).arg(size.height()));
            }
            for (QWidget *control :
                 {static_cast<QWidget *>(window.menuBar()),
                  static_cast<QWidget *>(toolbar),
                  static_cast<QWidget *>(addressBar),
                  static_cast<QWidget *>(address),
                  static_cast<QWidget *>(menuButton),
                  static_cast<QWidget *>(panelsButton)}) {
                QVERIFY2(control->isVisible(), qPrintable(control->objectName()));
                const QRect rect(control->mapTo(&window, QPoint()), control->size());
                QVERIFY2(window.rect().contains(rect), qPrintable(control->objectName()));
            }
            QTRY_VERIFY(!disassembly->visibleRegion().isEmpty());
            QVERIFY(disassembly->width() >= size.width() - 16);
            QVERIFY2(
                disassembly->height() >= size.height() / 3,
                qPrintable(QString("Panel height %1 in %2x%3 window")
                               .arg(disassembly->height())
                               .arg(size.width())
                               .arg(size.height())));
            auto *text = qobject_cast<QPlainTextEdit *>(disassembly->getTextWidget());
            QVERIFY(text);
            QTRY_VERIFY(!text->toPlainText().isEmpty());
            if (qEnvironmentVariableIsSet("IAITO_MOBILE_SCREENSHOTS")) {
                window.grab().save(
                    qEnvironmentVariable("IAITO_MOBILE_SCREENSHOTS")
                    + QString("/mainwindow-%1x%2.png").arg(size.width()).arg(size.height()));
            }
        }

        address->setText("0x10");
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_COMPARE(Core()->getOffset(), RVA(0x10));

        auto *console = window.findChild<ConsoleWidget *>();
        QVERIFY(console);
        auto *panels = panelsButton->menu();
        panels->popup(panelsButton->mapToGlobal(QPoint(0, panelsButton->height())));
        QTRY_VERIFY(panels->isVisible());
        QAction *consoleAction = nullptr;
        for (auto *action : panels->actions()) {
            if (action->text() == console->windowTitle()) {
                consoleAction = action;
            }
        }
        QVERIFY(consoleAction);
        panels->hide();
        consoleAction->trigger();
        QTRY_VERIFY(!console->visibleRegion().isEmpty());
        QVERIFY(!console->isFloating());
        QVERIFY(console->width() >= window.width() - 16);

        // Recovery must bring controls back even after restoring a state that hid them.
        toolbar->hide();
        addressBar->hide();
        auto *reset = window.findChild<QAction *>("actionDefault");
        QVERIFY(reset);
        reset->trigger();
        QTRY_VERIFY(toolbar->isVisible() && addressBar->isVisible());
        disassembly = nullptr;
        for (auto *dock : window.findChildren<DisassemblyWidget *>()) {
            if (!dock->isHidden()) {
                disassembly = dock;
                break;
            }
        }
        QVERIFY(disassembly);
        QTRY_VERIFY(!disassembly->visibleRegion().isEmpty());
        for (auto *dock : window.findChild<DockManager *>()->docks()) {
            QVERIFY(!dock->isFloating());
            QVERIFY2(
                !(dock->features() & QDockWidget::DockWidgetFloatable),
                qPrintable(dock->objectName()));
        }
    }
};

QTEST_MAIN(MobileMainWindowTests)
#include "test_mobilemainwindow.moc"
