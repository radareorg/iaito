#include "common/Configuration.h"
#include "common/ResponsiveDialog.h"
#include "common/theme/IaitoStyle.h"
#include "core/Iaito.h"
#include "core/MainWindow.h"
#include "dialogs/AboutDialog.h"
#include "dialogs/AddressSpaceManagerDialog.h"
#include "dialogs/AssemblerDialog.h"
#include "dialogs/BreakpointsDialog.h"
#include "dialogs/CommentsDialog.h"
#include "dialogs/DumpDialog.h"
#include "dialogs/EditFunctionDialog.h"
#include "dialogs/EditInstructionDialog.h"
#include "dialogs/EditMethodDialog.h"
#include "dialogs/EditStringDialog.h"
#include "dialogs/FlagDialog.h"
#include "dialogs/HexdumpRangeDialog.h"
#include "dialogs/InitialOptionsDialog.h"
#include "dialogs/LinkTypeDialog.h"
#include "dialogs/MapFileDialog.h"
#include "dialogs/NewFileDialog.h"
#include "dialogs/R2PluginsDialog.h"
#include "dialogs/SaveProjectDialog.h"
#include "dialogs/ScriptManagerDialog.h"
#include "dialogs/SetToDataDialog.h"
#include "dialogs/TypesInteractionDialog.h"
#include "dialogs/VersionInfoDialog.h"
#include "dialogs/WelcomeDialog.h"
#include "dialogs/WriteCommandsDialogs.h"
#include "dialogs/XrefsDialog.h"
#include "dialogs/settings/InterfaceThemeEditDialog.h"
#include "dialogs/settings/SettingsDialog.h"

#include <IaitoConfig.h>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

class ResponsiveDialogTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;

private slots:
    void initTestCase()
    {
        qputenv("XDG_DATA_HOME", settings.path().toUtf8());
        qputenv("XDG_CACHE_HOME", settings.path().toUtf8());
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("responsivedialogs");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        qApp->setStyle(new IaitoStyle);
        ResponsiveDialog::installApplicationHandler();
        Core()->initialize(false);
        Config()->loadInitial();
        Config()->setOutputRedirectionEnabled(false);
        Config()->setDecompilerAutoRefreshEnabled(false);
        Core()->cmd("o malloc://64");
    }

    void fit_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::addColumn<int>("fontSize");
        QTest::newRow("small-phone") << QSize(320, 480) << 10;
        QTest::newRow("phone") << QSize(360, 640) << 10;
        QTest::newRow("large-text") << QSize(360, 640) << 16;
        QTest::newRow("landscape") << QSize(640, 320) << 10;
        QTest::newRow("desktop") << QSize(760, 700) << 10;
    }

    void fit()
    {
        QFETCH(QSize, size);
        QFETCH(int, fontSize);
        WelcomeDialog welcome;
        AboutDialog about;
        for (QDialog *dialog : {static_cast<QDialog *>(&welcome), static_cast<QDialog *>(&about)}) {
            QFont font = dialog->font();
            font.setPointSize(fontSize);
            dialog->setFont(font);
            dialog->resize(size);
            dialog->show();
            QVERIFY(QTest::qWaitForWindowExposed(dialog));
            QTRY_COMPARE(dialog->size(), size);
            auto *scroll = dialog->findChild<QScrollArea *>("scrollArea");
            QVERIFY(scroll);
            QTRY_COMPARE(scroll->horizontalScrollBar()->maximum(), 0);
            QVERIFY(scroll->widget()->width() <= scroll->viewport()->width());
            for (auto *label : scroll->findChildren<QLabel *>()) {
                if (label->wordWrap()) {
                    QVERIFY2(
                        label->height() >= label->heightForWidth(label->width()),
                        qPrintable(label->objectName()));
                }
            }
            for (auto *button : scroll->findChildren<QPushButton *>()) {
                QVERIFY2(
                    button->width() >= button->sizeHint().width(), qPrintable(button->objectName()));
            }
            auto *close = dialog == &welcome ? dialog->findChild<QPushButton *>("continueButton")
                                             : dialog->findChild<QDialogButtonBox *>("buttonBox")
                                                   ->button(QDialogButtonBox::Close);
            QVERIFY(close);
            const QRect buttonRect(close->mapTo(dialog, QPoint()), close->size());
            QVERIFY(dialog->rect().contains(buttonRect));
            QVERIFY(buttonRect.top() >= scroll->geometry().bottom());
            scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
            QCoreApplication::processEvents();
            auto *last = dialog->findChild<QLabel *>(
                dialog == &welcome ? "contributingTextLabel" : "label");
            const int bottom = last->mapTo(scroll->viewport(), last->rect().bottomLeft()).y();
            QVERIFY(bottom < scroll->viewport()->height());
            scroll->verticalScrollBar()->setValue(0);
            if (qEnvironmentVariableIsSet("IAITO_DIALOG_SCREENSHOTS")) {
                dialog->grab().save(
                    qEnvironmentVariable("IAITO_DIALOG_SCREENSHOTS") + "/" + dialog->objectName()
                    + "-" + QTest::currentDataTag() + ".png");
            }
            QTest::mouseClick(close, Qt::LeftButton);
            QVERIFY(!dialog->isVisible());
        }
        QVERIFY(welcome.findChild<QLabel *>("versionLabel")->text().contains(IAITO_VERSION_FULL));
        QVERIFY(about.findChild<QLabel *>("label")->text().contains(IAITO_VERSION_FULL));
    }

    void resizeReflows()
    {
        WelcomeDialog welcome;
        AboutDialog about;
        for (QDialog *dialog : {static_cast<QDialog *>(&welcome), static_cast<QDialog *>(&about)}) {
            dialog->show();
            for (QSize size : {QSize(360, 640), QSize(760, 480), QSize(320, 480)}) {
                dialog->resize(size);
                QTRY_COMPARE(dialog->size(), size);
                auto *scroll = dialog->findChild<QScrollArea *>("scrollArea");
                QTRY_COMPARE(scroll->horizontalScrollBar()->maximum(), 0);
                if (dialog == &welcome) {
                    auto *links = dialog->findChild<QLabel *>("communityTitleLabel");
                    auto *contributing = dialog->findChild<QLabel *>("contributingLabel");
                    QTRY_COMPARE(contributing->y() > links->y(), size.width() < 600);
                } else {
                    auto *plugins = dialog->findChild<QPushButton *>("showPluginsButton");
                    auto *version = dialog->findChild<QPushButton *>("showVersionButton");
                    QTRY_COMPARE(version->y() > plugins->y(), size.width() < 600);
                }
            }
        }
    }

    void catalog_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<int>("fontSize");
        for (const auto &name : {"analysis",      "new-file",       "settings",
                                 "assembler",     "breakpoint",     "comments",
                                 "dump",          "edit-function",  "edit-instruction",
                                 "edit-method",   "edit-string",    "flag",
                                 "hexdump-range", "link-type",      "map-file",
                                 "plugins",       "save-project",   "scripts",
                                 "set-data",      "types",          "version-info",
                                 "base64",        "increment",      "duplicate",
                                 "xrefs",         "address-spaces", "interface-theme"}) {
            QTest::newRow(name) << QString(name) << 10;
            QTest::newRow(qPrintable(QString(name) + "-large-text")) << QString(name) << 16;
        }
    }

    void catalog()
    {
        QFETCH(QString, name);
        QFETCH(int, fontSize);
        std::unique_ptr<MainWindow> main;
        if (name == "analysis" || name == "new-file" || name == "xrefs"
            || name == "address-spaces") {
            main.reset(new MainWindow);
        }
        std::unique_ptr<QDialog> dialog;
        if (name == "analysis")
            dialog.reset(new InitialOptionsDialog(main.get()));
        if (name == "new-file")
            dialog.reset(new NewFileDialog(main.get()));
        if (name == "settings")
            dialog.reset(new SettingsDialog);
        if (name == "assembler")
            dialog.reset(new AssemblerDialog);
        if (name == "breakpoint")
            dialog.reset(new BreakpointsDialog);
        if (name == "comments")
            dialog.reset(new CommentsDialog);
        if (name == "dump")
            dialog.reset(new DumpDialog);
        if (name == "edit-function")
            dialog.reset(new EditFunctionDialog);
        if (name == "edit-instruction")
            dialog.reset(new EditInstructionDialog(EDIT_TEXT));
        if (name == "edit-method")
            dialog.reset(new EditMethodDialog(false));
        if (name == "edit-string")
            dialog.reset(new EditStringDialog);
        if (name == "flag")
            dialog.reset(new FlagDialog(0));
        if (name == "hexdump-range")
            dialog.reset(new HexdumpRangeDialog);
        if (name == "link-type")
            dialog.reset(new LinkTypeDialog);
        if (name == "map-file")
            dialog.reset(new MapFileDialog);
        if (name == "plugins")
            dialog.reset(new R2PluginsDialog);
        if (name == "save-project")
            dialog.reset(new SaveProjectDialog(true));
        if (name == "scripts")
            dialog.reset(new ScriptManagerDialog);
        if (name == "set-data")
            dialog.reset(new SetToDataDialog(0));
        if (name == "types")
            dialog.reset(new TypesInteractionDialog);
        if (name == "version-info")
            dialog.reset(new VersionInfoDialog);
        if (name == "base64")
            dialog.reset(new Base64EnDecodedWriteDialog);
        if (name == "increment")
            dialog.reset(new IncrementDecrementDialog);
        if (name == "duplicate")
            dialog.reset(new DuplicateFromOffsetDialog);
        if (name == "xrefs")
            dialog.reset(new XrefsDialog(main.get(), nullptr));
        if (name == "address-spaces")
            dialog.reset(new AddressSpaceManagerDialog(main.get()));
        if (name == "interface-theme")
            dialog.reset(new InterfaceThemeEditDialog);
        QVERIFY(dialog);
        dialog->setAttribute(Qt::WA_DeleteOnClose, false);
        QFont font = dialog->font();
        font.setPointSize(fontSize);
        dialog->setFont(font);
        dialog->show();
        QVERIFY(QTest::qWaitForWindowExposed(dialog.get()));

        auto checkActions = [&]() {
            if (qEnvironmentVariableIsSet("IAITO_DIALOG_SCREENSHOTS")) {
                dialog->grab().save(
                    qEnvironmentVariable("IAITO_DIALOG_SCREENSHOTS") + "/catalog-"
                    + QTest::currentDataTag() + ".png");
            }
            const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
            QTRY_VERIFY2_WITH_TIMEOUT(
                available.contains(dialog->frameGeometry()),
                qPrintable(
                    name + " frame " + QString::number(dialog->width()) + "x"
                    + QString::number(dialog->height())),
                1500);
            for (auto *box : dialog->findChildren<QDialogButtonBox *>()) {
                if (!box->isVisible())
                    continue;
                for (auto *button : box->buttons()) {
                    if (!button->isVisible())
                        continue;
                    QTRY_VERIFY2_WITH_TIMEOUT(
                        dialog->rect().contains(
                            QRect(button->mapTo(dialog.get(), QPoint()), button->size())),
                        qPrintable(name + ": " + button->text()),
                        1500);
                }
            }
        };
        checkActions();
        if (name == "analysis") {
            dialog->findChild<QSlider *>("analSlider")->setValue(6);
            dialog->findChild<QToolButton *>("AdvOptButton")->click();
            dialog->findChild<QCheckBox *>("pdbCheckBox")->setChecked(true);
            dialog->findChild<QCheckBox *>("scriptCheckBox")->setChecked(true);
            QCoreApplication::processEvents();
            checkActions();
            for (const auto &buttonName : {"okButton", "cancelButton"}) {
                auto *button = dialog->findChild<QPushButton *>(buttonName);
                QVERIFY(dialog->rect().contains(
                    QRect(button->mapTo(dialog.get(), QPoint()), button->size())));
            }
            auto *scroll = dialog->findChild<QScrollArea *>("scrollArea");
            QTRY_COMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        }
        if (name == "settings") {
            auto *categories = dialog->findChild<QComboBox *>("categoryComboBox");
            for (int i = 0; i < categories->count(); ++i) {
                categories->setCurrentIndex(i);
                QCoreApplication::processEvents();
                checkActions();
            }
            categories->setCurrentIndex(0);
        }
        if (name == "new-file") {
            auto *tabs = dialog->findChild<QTabWidget *>("tabWidget");
            for (int i = 0; i < tabs->count(); ++i) {
                tabs->setCurrentIndex(i);
                QCoreApplication::processEvents();
                auto *page = tabs->currentWidget();
                auto *actions = page->findChild<QWidget *>(page->objectName() + "Actions");
                QVERIFY(actions);
                QTRY_VERIFY2_WITH_TIMEOUT(
                    dialog->rect().contains(
                        QRect(actions->mapTo(dialog.get(), QPoint()), actions->size())),
                    qPrintable(page->objectName()),
                    1500);
                checkActions();
            }
            tabs->setCurrentIndex(0);
        }
        if (qEnvironmentVariableIsSet("IAITO_DIALOG_SCREENSHOTS")) {
            QCoreApplication::processEvents();
            dialog->grab().save(
                qEnvironmentVariable("IAITO_DIALOG_SCREENSHOTS") + "/catalog-"
                + QTest::currentDataTag() + ".png");
        }
    }

    void initialSizeFitsScreen()
    {
        WelcomeDialog welcome;
        AboutDialog about;
        for (QDialog *dialog : {static_cast<QDialog *>(&welcome), static_cast<QDialog *>(&about)}) {
            dialog->show();
            QVERIFY(QTest::qWaitForWindowExposed(dialog));
            const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
            QVERIFY(available.contains(dialog->frameGeometry()));
            auto *scroll = dialog->findChild<QScrollArea *>("scrollArea");
            QTRY_COMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        }
    }
};

QTEST_MAIN(ResponsiveDialogTests)
#include "test_responsivedialogs.moc"
