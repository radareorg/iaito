#include "common/Configuration.h"
#include "common/ResponsiveDialog.h"
#include "common/theme/IaitoStyle.h"
#include "core/DockManager.h"
#include "core/Iaito.h"
#include "core/MainWindow.h"
#include "dialogs/NewFileDialog.h"
#include "menus/AddressableItemContextMenu.h"
#include "widgets/ConsoleWidget.h"
#include "widgets/DisassemblyWidget.h"
#include "widgets/FunctionsWidget.h"
#include "widgets/SymbolsWidget.h"
#include "widgets/VisualNavbar.h"
#include "widgets/ZoomWidget.h"

#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>
#include <QTimer>
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
        ResponsiveDialog::installApplicationHandler();
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

    void startupPageAndInput()
    {
        QScrollBar vertical(Qt::Vertical);
        QScrollBar horizontal(Qt::Horizontal);
        QVERIFY(vertical.sizeHint().width() >= 28);
        QVERIFY(horizontal.sizeHint().height() >= 28);
        QVERIFY(!vertical.style()->styleHint(QStyle::SH_ScrollBar_Transient));
        MainWindow window;
        NewFileDialog dialog(&window);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QVERIFY(dialog.isMaximized());
        QTRY_COMPARE(dialog.frameGeometry().size(), dialog.screen()->availableGeometry().size());
        QVERIFY(!dialog.findChild<QWidget *>("iaitoDialogCloseButton"));
        auto *browse = dialog.findChild<QPushButton *>("selectFileButton");
        auto *path = dialog.findChild<QLineEdit *>("newFileEdit");
        auto *plugin = dialog.findChild<QComboBox *>("ioPlugin");
        QVERIFY(browse && path && plugin);
        QVERIFY(!path->hasFocus());
        QVERIFY(path->hasFrame());
        QVERIFY(browse->mapTo(&dialog, QPoint()).y() < path->mapTo(&dialog, QPoint()).y());
        const QRect page = dialog.geometry();
        QTest::mouseClick(path, Qt::LeftButton);
        QTest::keyClicks(path, "malloc://1024");
        QCOMPARE(path->text(), QString("malloc://1024"));
        QCOMPARE(plugin->currentText(), QString("malloc://"));
        QTest::keyClick(path, Qt::Key_Return);
        QVERIFY(!path->hasFocus());
        QVERIFY(dialog.isVisible());
        QCOMPARE(path->text(), QString("malloc://1024"));
        plugin->showPopup();
        QTest::qWait(200);
        QCOMPARE(dialog.geometry(), page);
        plugin->hidePopup();
        QTest::keyClick(&dialog, Qt::Key_Escape);
        QVERIFY(dialog.isVisible());
        QCOMPARE(dialog.geometry(), page);
        if (qEnvironmentVariableIsSet("IAITO_MOBILE_SCREENSHOTS")) {
            dialog.grab().save(qEnvironmentVariable("IAITO_MOBILE_SCREENSHOTS") + "/startup.png");
        }
    }

    void filePickerCancellationDismissesPathFocus()
    {
        // Offscreen uses Qt's fallback, so this exercises the picker lifecycle
        // without requiring interaction with a host system dialog.
        MainWindow window;
        NewFileDialog dialog(&window);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto *path = dialog.findChild<QLineEdit *>("newFileEdit");
        auto *browse = dialog.findChild<QPushButton *>("selectFileButton");
        path->setText("malloc://1024");
        path->setFocus();
        QTimer cancel;
        connect(&cancel, &QTimer::timeout, this, []() {
            for (auto *widget : QApplication::topLevelWidgets()) {
                if (auto *picker = qobject_cast<QFileDialog *>(widget)) {
                    picker->reject();
                }
            }
        });
        cancel.start(20);
        QVERIFY(QMetaObject::invokeMethod(&dialog, "on_selectFileButton_clicked"));
        QVERIFY(!path->hasFocus());
        QCOMPARE(dialog.focusWidget(), browse);
        QCOMPARE(path->text(), QString("malloc://1024"));
        QVERIFY(dialog.isVisible());
    }

    void filePickerSelection()
    {
        QTemporaryDir documents;
        const QString name = documents.filePath(QString::fromUtf8("binary é %25.bin"));
        QFile file(name);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        MainWindow window;
        NewFileDialog dialog(&window);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto *path = dialog.findChild<QLineEdit *>("newFileEdit");
        auto *load = dialog.findChild<QPushButton *>("loadFileButton");
        QTimer select;
        connect(&select, &QTimer::timeout, this, [&]() {
            for (auto *widget : QApplication::topLevelWidgets()) {
                if (auto *picker = qobject_cast<QFileDialog *>(widget)) {
                    // selectFile() deliberately leaves a focused filename editor
                    // unchanged. Move focus as a user selecting a file would.
                    picker->setFocus();
                    picker->selectFile(name);
                    QMetaObject::invokeMethod(picker, "accept");
                }
            }
        });
        select.start(20);
        QTimer::singleShot(5000, &dialog, []() {
            for (auto *widget : QApplication::topLevelWidgets()) {
                if (auto *picker = qobject_cast<QFileDialog *>(widget)) {
                    picker->reject();
                }
            }
        });
        QVERIFY(QMetaObject::invokeMethod(&dialog, "on_selectFileButton_clicked"));
        QCOMPARE(path->text(), name);
        QVERIFY(load->isEnabled());
        QCOMPARE(dialog.focusWidget(), load);
        QCOMPARE(Config()->getRecentFolder(), documents.path());
        QVERIFY(dialog.isVisible());
    }

    void popupKeepsItsLayout()
    {
        QDialog dialog;
        auto *layout = new QVBoxLayout(&dialog);
        layout->addWidget(new QLabel("Dialog body"));
        // A popup is a QObject child of its owner, but a separate window whose
        // layout must not be folded to match the owner's narrower width.
        QWidget popup(&dialog, Qt::Popup);
        auto *row = new QHBoxLayout(&popup);
        for (int i = 0; i < 2; ++i) {
            auto *button = new QPushButton(QString::number(i));
            button->setMinimumWidth(240);
            row->addWidget(button);
        }
        dialog.resize(360, 480);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QTRY_COMPARE(row->direction(), QBoxLayout::LeftToRight);
        popup.show();
        QVERIFY(QTest::qWaitForWindowExposed(&popup));
        const QSize popupSize = popup.size();
        dialog.layout()->invalidate();
        QTest::qWait(200);
        QCOMPARE(row->direction(), QBoxLayout::LeftToRight);
        QCOMPARE(popup.size(), popupSize);
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
        auto *addressDialog = window.findChild<QDialog *>("mobileAddressDialog");
        auto *go = window.findChild<QAction *>("actionMobileGoTo");
        auto *functions = window.findChild<FunctionsWidget *>();
        auto *address = window.findChild<QLineEdit *>("mobileAddressField");
        auto *menuButton = window.findChild<QToolButton *>("mobileMenuButton");
        auto *panelsButton = window.findChild<QToolButton *>("mobilePanelsButton");
        auto *disassembly = window.findChild<DisassemblyWidget *>();
        QVERIFY(
            toolbar && addressDialog && go && address && menuButton && panelsButton && disassembly);
        QVERIFY(functions);
        auto *functionMenu = functions->findChild<AddressableItemContextMenu *>();
        QVERIFY(functionMenu);
        auto *hierarchy = functionMenu->findChild<QAction *>("actionNameHierarchy");
        QVERIFY(hierarchy && hierarchy->isCheckable());
        const QRect available = window.screen()->availableGeometry();
        functionMenu->popup(available.bottomRight());
        QTRY_VERIFY(functionMenu->isVisible());
        QVERIFY(available.contains(functionMenu->frameGeometry()));
        int menuItems = 0;
        for (auto *action : functionMenu->actions()) {
            menuItems += action->isVisible() && !action->isSeparator();
        }
        QVERIFY(menuItems <= 7);
        if (qEnvironmentVariableIsSet("IAITO_MOBILE_SCREENSHOTS")) {
            functionMenu->grab().save(
                qEnvironmentVariable("IAITO_MOBILE_SCREENSHOTS") + "/functions-menu.png");
        }
        functionMenu->close();
        QVERIFY(window.menuBar()->isHidden());
        QVERIFY(window.statusBar()->isHidden());
        QVERIFY(window.findChild<VisualNavbar *>()->isHidden());
        QCOMPARE(menuButton->menu()->actions(), window.menuBar()->actions());
        QVERIFY(!window.menuBar()->isNativeMenuBar());
        QVERIFY(menuButton->menu()->actions().contains(
            window.getMenuByType(MainWindow::MenuType::File)->menuAction()));

        for (QSize size :
             {QSize(320, 480),
              QSize(360, 640),
              QSize(640, 360),
              QSize(393, 852),
              QSize(852, 393),
              QSize(956, 440),
              QSize(768, 1024),
              QSize(1024, 768)}) {
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
                 {static_cast<QWidget *>(toolbar),
                  static_cast<QWidget *>(menuButton),
                  static_cast<QWidget *>(panelsButton)}) {
                QVERIFY2(control->isVisible(), qPrintable(control->objectName()));
                const QRect rect(control->mapTo(&window, QPoint()), control->size());
                QVERIFY2(window.rect().contains(rect), qPrintable(control->objectName()));
            }
            QTRY_VERIFY(!disassembly->visibleRegion().isEmpty());
            const bool sidebar = size.width() >= 600 && size.width() > size.height()
                                 && size.height() >= 280;
            QTRY_COMPARE(window.tabifiedDockWidgets(disassembly).contains(functions), !sidebar);
            if (sidebar) {
                QTRY_VERIFY(!functions->visibleRegion().isEmpty());
                QTRY_VERIFY(functions->width() >= 200);
                QVERIFY(functions->geometry().right() < disassembly->geometry().left());
                QVERIFY(disassembly->width() >= size.width() / 2);
            } else {
                QVERIFY(disassembly->width() >= size.width() - 16);
            }
            QVERIFY(addressDialog->isHidden());
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

        go->trigger();
        QTRY_VERIFY(addressDialog->isVisible());
        QVERIFY(address->isVisible());
        if (qEnvironmentVariableIsSet("IAITO_MOBILE_SCREENSHOTS")) {
            addressDialog->grab().save(
                qEnvironmentVariable("IAITO_MOBILE_SCREENSHOTS") + "/address.png");
        }
        address->setText("0x10");
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_COMPARE(Core()->getOffset(), RVA(0x10));
        QTRY_VERIFY(addressDialog->isHidden());

        auto *console = window.findChild<ConsoleWidget *>();
        QVERIFY(console);
        QTest::mouseClick(panelsButton, Qt::LeftButton);
        auto *panels = window.findChild<QDialog *>("mobilePanelDialog");
        QVERIFY(panels);
        QTRY_VERIFY(panels->isVisible());
        auto *list = panels->findChild<QListWidget *>("mobilePanelList");
        auto *filter = panels->findChild<QLineEdit *>("mobilePanelFilter");
        QVERIFY(list && filter);
        QVERIFY(list->gridSize().height() >= 44);
        for (QSize size : {QSize(300, 420), QSize(760, 540)}) {
            panels->resize(size);
            QTRY_VERIFY(panels->screen()->availableGeometry().contains(panels->frameGeometry()));
            QTRY_COMPARE(list->horizontalScrollBar()->maximum(), 0);
            QVERIFY(list->gridSize().width() <= list->viewport()->width());
            auto *close = panels->findChild<QToolButton *>("iaitoDialogCloseButton");
            QVERIFY(close);
            QTRY_VERIFY(
                panels->rect().contains(QRect(close->mapTo(panels, QPoint()), close->size())));
        }
        if (qEnvironmentVariableIsSet("IAITO_MOBILE_SCREENSHOTS")) {
            panels->grab().save(qEnvironmentVariable("IAITO_MOBILE_SCREENSHOTS") + "/panels.png");
        }
        filter->setText("Console");
        QListWidgetItem *consoleItem = nullptr;
        for (int i = 0; i < list->count(); ++i) {
            auto *item = list->item(i);
            if (item->data(Qt::UserRole).toString() == console->objectName()) {
                consoleItem = item;
            } else {
                QVERIFY(item->isHidden());
            }
        }
        QVERIFY(consoleItem && !consoleItem->isHidden());
        QCoreApplication::processEvents();
        QTest::mouseClick(
            list->viewport(),
            Qt::LeftButton,
            Qt::NoModifier,
            list->visualItemRect(consoleItem).center());
        QTRY_VERIFY(!console->visibleRegion().isEmpty());
        QVERIFY(!console->isFloating());
        window.resize(768, 1024);
        QTRY_VERIFY(console->width() >= window.width() - 16);
        QTRY_VERIFY(!console->visibleRegion().isEmpty());
        window.resize(1024, 768);
        QTRY_VERIFY(!console->visibleRegion().isEmpty());
        auto *sidebar = window.findChild<QAction *>("actionMobileSidebar");
        QVERIFY(sidebar);
        sidebar->setChecked(false);
        QTRY_VERIFY(console->width() >= window.width() - 16);
        sidebar->setChecked(true);
        QTRY_VERIFY(!functions->visibleRegion().isEmpty());
        auto *selector = window.findChild<QComboBox *>("mobileSidebarSelector");
        auto *symbols = window.findChild<SymbolsWidget *>();
        QVERIFY(selector && symbols);
        const int symbolsIndex = selector->findData(symbols->objectName());
        QVERIFY(symbolsIndex >= 0);
        selector->setCurrentIndex(symbolsIndex);
        QVERIFY(QMetaObject::invokeMethod(selector, "activated", Q_ARG(int, symbolsIndex)));
        QTRY_VERIFY(!symbols->visibleRegion().isEmpty());
        QTRY_VERIFY(symbols->width() >= 200 && symbols->width() <= 340);
        QTRY_VERIFY(!console->visibleRegion().isEmpty());
        QVERIFY(!window.tabifiedDockWidgets(console).contains(symbols));
        window.resize(768, 1024);
        QTRY_VERIFY(window.tabifiedDockWidgets(console).contains(symbols));
        auto *browse = window.findChild<QAction *>("actionMobileBrowse");
        QVERIFY(browse);
        browse->trigger();
        QTRY_VERIFY(!symbols->visibleRegion().isEmpty());
        browse->trigger();
        QTRY_VERIFY(!console->visibleRegion().isEmpty());

        auto *device = QTest::createTouchDevice();
        const QPoint start(4, window.height() / 2);
        QTest::touchEvent(&window, device).press(0, start, &window);
        QTest::touchEvent(&window, device).move(0, start + QPoint(100, 0), &window);
        QTest::touchEvent(&window, device).release(0, start + QPoint(100, 0), &window);
        QTRY_VERIFY(!symbols->visibleRegion().isEmpty());

        // Recovery must bring controls back even after restoring a state that hid them.
        toolbar->hide();

        auto *reset = window.findChild<QAction *>("actionDefault");
        QVERIFY(reset);
        reset->trigger();
        QTRY_VERIFY(toolbar->isVisible());
        QVERIFY(addressDialog->isHidden());
        QVERIFY(window.menuBar()->isHidden());
        QVERIFY(window.statusBar()->isHidden());
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
        window.saveSettings();
        MainWindow restored;
        restored.finalizeOpen();
        QTRY_VERIFY(restored.isUiReady());
        restored.showNormal();
        restored.resize(1024, 768);
        auto *restoredSymbols = restored.findChild<SymbolsWidget *>();
        auto *restoredSelector = restored.findChild<QComboBox *>("mobileSidebarSelector");
        QVERIFY(restoredSymbols && restoredSelector);
        QCOMPARE(restoredSelector->currentData().toString(), symbols->objectName());
        QTRY_VERIFY(!restoredSymbols->visibleRegion().isEmpty());
        QTRY_VERIFY(restoredSymbols->width() >= 200 && restoredSymbols->width() <= 340);
        QVERIFY(restored.menuBar()->isHidden());
        QVERIFY(restored.statusBar()->isHidden());
    }

    void zoomOptionsOnPhones()
    {
        MainWindow window;
        window.finalizeOpen();
        QTRY_VERIFY(window.isUiReady());
        window.showNormal();
        auto *zoom = window.findChild<ZoomWidget *>();
        QVERIFY(zoom);
        zoom->raiseMemoryWidget();
        auto *button = zoom->findChild<QToolButton *>("zoomOptionsButton");
        auto *wrap = zoom->findChild<QCheckBox *>("zoomWrapCheck");
        auto *columns = zoom->findChild<QSpinBox *>("zoomColumnsSpinBox");
        auto *options = zoom->findChild<QDialog *>("zoomOptions");
        auto *scroll = zoom->findChild<QScrollArea *>("zoomOptionsScroll");
        QVERIFY(button && wrap && columns && options && scroll);
        for (const QSize size : {QSize(360, 640), QSize(640, 300)}) {
            window.resize(size);
            QTRY_COMPARE(window.size(), size);
            QTRY_VERIFY(button->isVisible());
            QVERIFY(window.rect().contains(QRect(button->mapTo(&window, QPoint()), button->size())));
            options->resize(
                qMin(360, size.width() - 16),
                qMin(options->sizeHint().height(), size.height() - 16));
            QTest::mouseClick(button, Qt::LeftButton);
            QTRY_VERIFY(options->isVisible());
            QTRY_COMPARE(scroll->horizontalScrollBar()->maximum(), 0);
            QVERIFY(options->height() <= window.height());
            if (qEnvironmentVariableIsSet("IAITO_MOBILE_SCREENSHOTS")) {
                options->grab().save(
                    qEnvironmentVariable("IAITO_MOBILE_SCREENSHOTS")
                    + QString("/zoom-options-%1.png").arg(size.width()));
            }
            scroll->ensureWidgetVisible(wrap);
            QTRY_VERIFY(scroll->viewport()->rect().contains(
                QRect(wrap->mapTo(scroll->viewport(), QPoint()), wrap->size())));
            QTest::mouseClick(wrap, Qt::LeftButton, Qt::NoModifier, QPoint(12, wrap->height() / 2));
            QVERIFY(wrap->isChecked());
            QVERIFY(!columns->isEnabled());
            QVERIFY(zoom->serializeViewProprties().value("autoWrap").toBool());
            QTest::mouseClick(wrap, Qt::LeftButton, Qt::NoModifier, QPoint(12, wrap->height() / 2));
            QVERIFY(!wrap->isChecked());
            options->close();
        }
    }
};

QTEST_MAIN(MobileMainWindowTests)
#include "test_mobilemainwindow.moc"
