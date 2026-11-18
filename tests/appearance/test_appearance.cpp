#include "common/Configuration.h"
#include "common/Decompiler.h"
#include "common/Helpers.h"
#include "common/theme/IaitoStyle.h"
#include "core/MainWindow.h"
#include "dialogs/settings/AppearanceOptionsWidget.h"
#include "dialogs/settings/SettingsDialog.h"
#include "widgets/DecompilerWidget.h"
#include "widgets/DisassemblyWidget.h"

#include <QApplication>
#include <QComboBox>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>

class LongOptionsDecompiler : public Decompiler
{
public:
    LongOptionsDecompiler()
        : Decompiler("long-options", "Test decompiler")
    {}
    QString getConfigPrefix() const override { return "test"; }
    QStringList listOptions() override { return {"prompt=" + QString(512, 'x')}; }
    void decompileAt(RVA) override {}
    RCodeMeta *decompileSync(RVA) override { return nullptr; }
};

class AppearanceTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;

    static void selectTheme(const QString &name)
    {
        const auto &themes = Configuration::iaitoInterfaceThemesList();
        for (int i = 0; i < themes.size(); ++i) {
            if (themes[i].name == name) {
                Config()->setInterfaceTheme(i);
                return;
            }
        }
        QFAIL("Theme not found");
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("iaito-tests");
        QCoreApplication::setApplicationName("appearance");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        QVERIFY(QFontDatabase::addApplicationFont(":/fonts/LiberationSans-Regular.ttf") >= 0);
        QVERIFY(QFontDatabase::addApplicationFont(":/fonts/LiberationSans-Bold.ttf") >= 0);
        QVERIFY(QFontDatabase::addApplicationFont(":/fonts/InconsolataRegular.ttf") >= 0);
        qApp->setStyle(new IaitoStyle);
        Core()->initialize(false);
        qRegisterMetaType<RVA>("RVA");
        Config()->loadInitial();
        Config()->setOutputRedirectionEnabled(false);
        Config()->setDecompilerAutoRefreshEnabled(false);
        Core()->cmd("o malloc://64");
        Core()->registerDecompiler(new LongOptionsDecompiler);
        Config()->setSelectedDecompiler("long-options");
    }

    void init()
    {
        Config()->setFont(QFont("Inconsolata", 13));
        Config()->setZoomFactor(1.0);
        selectTheme("Dark");
    }

    void themePreservesCodeFont()
    {
        QPlainTextEdit code;
        qhelpers::bindFont(&code, false);
        QFont selected("Inconsolata", 16);
        Config()->setFont(selected);
        Config()->setZoomFactor(1.1);

        for (const QString &name : {QString("Classic"), QString("Phosphor"), QString("Dark")}) {
            selectTheme(name);
            QCOMPARE(Config()->getBaseFont(), selected);
            QCOMPARE(code.font().family(), selected.family());
            QCOMPARE(code.font().pointSizeF(), 17.6);
            QCOMPARE(Config()->getSmallFont(false).pointSizeF(), 16.0);
            QVERIFY(!(code.font().styleStrategy() & QFont::NoAntialias));
        }
    }

    void defaultFontFollowsTheme()
    {
        Config()->resetFont();
        QPlainTextEdit code;
        qhelpers::bindFont(&code, false);
        QPlainTextEdit listing;
        qhelpers::bindFont(&listing, true);
        for (const QString &name : {QString("Classic"), QString("Phosphor"), QString("Dark")}) {
            selectTheme(name);
            const QFont interfaceFont = qApp->font();
            QFont expected = interfaceFont;
            if (name == "Classic") {
                QCOMPARE(interfaceFont.pointSizeF(), 10.0);
                expected.setPointSize(9);
            }
            QCOMPARE(Config()->getBaseFont().family(), QString("Inconsolata"));
            QCOMPARE(code.font().pointSizeF(), expected.pointSizeF());
            QCOMPARE(code.font().pixelSize(), expected.pixelSize());
            QCOMPARE(listing.font().pointSizeF(), expected.pointSizeF());
            QCOMPARE(listing.font().pixelSize(), expected.pixelSize());
        }
    }

    void fontControlsRespectPixelSizes()
    {
        selectTheme("Classic");
        QFont selected("Inconsolata");
        selected.setPixelSize(16);
        Config()->setFont(selected);
        AppearanceOptionsWidget appearance(nullptr);
        auto *size = appearance.findChild<QSpinBox *>("fontSizeSpinBox");
        auto *family = appearance.findChild<QComboBox *>("fontFamilyComboBox");
        QVERIFY(size);
        QVERIFY(family);
        QCOMPARE(size->suffix(), QString(" px"));
        QCOMPARE(size->value(), 16);
        size->setValue(18);
        QCOMPARE(Config()->getBaseFont().pixelSize(), 18);
        family->setCurrentIndex(family->findData("PT Mono"));
        QCOMPARE(Config()->getBaseFont().family(), QString("PT Mono"));
        QCOMPARE(Config()->getBaseFont().pixelSize(), 18);
        Config()->setFont(QFont("Inconsolata", 14));
        QCOMPARE(size->suffix(), QString(" pt"));
        QCOMPARE(size->value(), 14);
        Config()->setZoomFactor(1.5);
        auto *reset = appearance.findChild<QPushButton *>("fontResetButton");
        QVERIFY(reset);
        reset->click();
        QCOMPARE(Config()->getZoomFactor(), 1.0);
        QCOMPARE(Config()->getBaseFont().pointSizeF(), 9.0);
        QCOMPARE(qApp->font().pointSizeF(), 10.0);
        QCOMPARE(Config()->getSmallFont(), Config()->getFont());
    }

    void classicThemeRoundTrip()
    {
        const Theme classic = Theme::builtin("Classic");
        const QString path = settings.filePath("classic.theme");
        classic.save(path);
        const Theme loaded = Theme::load(path, false);
        QCOMPARE(loaded.skin, Skin::Windows);
        QCOMPARE(loaded.fonts.base.family(), QString("Liberation Sans"));
        QVERIFY(!(loaded.fonts.base.styleStrategy() & QFont::NoAntialias));
        Config()->loadEngineTheme(loaded);
        QVERIFY(!(qApp->font().styleStrategy() & QFont::NoAntialias));
        const QPalette p = qApp->palette();
        QCOMPARE(p.color(QPalette::Light), QColor(Qt::white));
        QCOMPARE(p.color(QPalette::Shadow), QColor(Qt::black));
        QCOMPARE(p.color(QPalette::Dark), QColor("#808080"));
        QCOMPARE(p.color(QPalette::Disabled, QPalette::Base), p.color(QPalette::Button));
    }

    void windowShrinksAfterThemeChange()
    {
        MainWindow main;
        main.finalizeOpen();
        main.showNormal();
        QVERIFY(QTest::qWaitForWindowExposed(&main));
        auto *decompiler = main.findChild<DecompilerWidget *>();
        QVERIFY(decompiler);
        decompiler->raise();
        auto *options = decompiler->findChild<QComboBox *>("optionsComboBox");
        QVERIFY(options);
        QVERIFY(options->itemText(0).size() > 512);
        QCOMPARE(options->itemData(0, Qt::UserRole).toString(), QString("prompt"));
        QCOMPARE(options->itemData(0, Qt::ToolTipRole).toString(), options->itemText(0));

        for (const QString &name : {QString("Classic"), QString("Dark"), QString("Classic")}) {
            selectTheme(name);
            QCoreApplication::processEvents();
            QVERIFY2(main.minimumSizeHint().width() < 1000, "Long option forces window offscreen");
            main.resize(1000, 700);
            QTRY_COMPARE(main.width(), 1000);
            main.resize(800, 600);
            QTRY_COMPARE(main.width(), 800);
            QCOMPARE(options->itemData(0, Qt::UserRole).toString(), QString("prompt"));
        }

        if (qEnvironmentVariableIsSet("IAITO_APPEARANCE_PREVIEW")) {
            Config()->resetFont();
            main.resize(1000, 700);
            main.findChild<DisassemblyWidget *>()->raise();
            QCoreApplication::processEvents();
            main.grab().save(qEnvironmentVariable("IAITO_APPEARANCE_PREVIEW"));
            SettingsDialog dialog(&main);
            dialog.showSection(SettingsDialog::Section::Appearance);
            dialog.show();
            QCoreApplication::processEvents();
            dialog.grab().save(qEnvironmentVariable("IAITO_APPEARANCE_PREVIEW") + ".settings.png");
        }
    }
};

QTEST_MAIN(AppearanceTests)
#include "test_appearance.moc"
