#include "common/CachedFontMetrics.h"
#include "common/ColorThemeWorker.h"
#include "common/Configuration.h"
#include "common/Decompiler.h"
#include "common/Helpers.h"
#include "common/IaitoSeekable.h"
#include "common/RichTextPainter.h"
#include "common/SyntaxHighlighter.h"
#include "common/TempConfig.h"
#include "common/theme/IaitoStyle.h"
#include "core/DockManager.h"
#include "core/MainWindow.h"
#include "dialogs/settings/AppearanceOptionsWidget.h"
#include "dialogs/settings/AsmOptionsWidget.h"
#include "dialogs/settings/SettingsDialog.h"
#include "widgets/AddressScrollBar.h"
#include "widgets/DecompilerWidget.h"
#include "widgets/DisassemblerGraphView.h"
#include "widgets/DisassemblyWidget.h"
#include "widgets/ZoomWidget.h"

#include <cmath>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFrame>
#include <QGestureEvent>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPinchGesture>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextLayout>
#include <QToolButton>
#include <QToolTip>
#include <QWheelEvent>

class ZoomEventProbe : public ZoomView
{
public:
    using ZoomView::event;
};

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
    QPalette systemPalette;
    QString systemStyle;

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

    static QColor textColor(const QTextDocument &document, const QString &token)
    {
        const QTextCursor cursor = document.find(token);
        if (cursor.isNull()) {
            return {};
        }
        const QTextCharFormat format = cursor.charFormat();
        return format.hasProperty(QTextFormat::ForegroundBrush)
                   ? format.foreground().color()
                   : qApp->palette().color(QPalette::Text);
    }

    static double contrast(const QColor &a, const QColor &b)
    {
        auto luminance = [](const QColor &color) {
            auto channel = [](qreal value) {
                return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
            };
            return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF())
                   + 0.0722 * channel(color.blueF());
        };
        const double x = luminance(a), y = luminance(b);
        return (qMax(x, y) + 0.05) / (qMin(x, y) + 0.05);
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
        QVERIFY(QFontDatabase::addApplicationFont(":/fonts/IaitoW95FA.ttf") >= 0);
        QVERIFY(QFontDatabase::addApplicationFont(":/fonts/InconsolataRegular.ttf") >= 0);
        systemPalette = qApp->palette();
        systemStyle = qApp->style()->objectName();
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
        QSettings saved;
        for (const QString &key : saved.allKeys()) {
            if (key.startsWith("interfaceFontSize.")) {
                saved.remove(key);
            }
        }
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
                QCOMPARE(interfaceFont.pixelSize(), 10);
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
        const int w95fa = family->findData("Iaito W95FA");
        QVERIFY(w95fa >= 0);
        family->setCurrentIndex(w95fa);
        QCOMPARE(QFontInfo(Config()->getBaseFont()).family(), QString("Iaito W95FA"));
        QVERIFY(Config()->getBaseFont().styleStrategy() & QFont::NoAntialias);
        QCOMPARE(Config()->getBaseFont().pixelSize(), 18);
        family->setCurrentIndex(family->findData("PT Mono"));
        QCOMPARE(Config()->getBaseFont().family(), QString("PT Mono"));
        QVERIFY(!(Config()->getBaseFont().styleStrategy() & QFont::NoAntialias));
        QCOMPARE(Config()->getBaseFont().pixelSize(), 18);
        QFont legacy("W95FA");
        legacy.setPixelSize(18);
        Config()->setFont(legacy);
        QCOMPARE(Config()->getBaseFont().family(), QString("Iaito W95FA"));
        QCOMPARE(family->currentData().toString(), QString("Iaito W95FA"));
        Config()->setFont(QFont("Inconsolata", 14));
        QCOMPARE(size->suffix(), QString(" pt"));
        QCOMPARE(size->value(), 14);
        Config()->setZoomFactor(1.5);
        auto *reset = appearance.findChild<QPushButton *>("fontResetButton");
        QVERIFY(reset);
        reset->click();
        QCOMPARE(Config()->getZoomFactor(), 1.0);
        QCOMPARE(Config()->getBaseFont().pointSizeF(), 9.0);
        QCOMPARE(qApp->font().pixelSize(), 10);
        QCOMPARE(Config()->getSmallFont(), Config()->getFont());
    }

    void interfaceFontControls()
    {
        selectTheme("Classic");
        Config()->setZoomFactor(1.5);
        QPlainTextEdit code;
        qhelpers::bindFont(&code, false);
        const QFont codeFont = code.font();
        const QFont baseFont = Config()->getBaseFont();
        QSignalSpy codeUpdates(Config(), &Configuration::fontsUpdated);
        AppearanceOptionsWidget appearance(nullptr);
        auto *size = appearance.findChild<QSpinBox *>("interfaceFontSizeSpinBox");
        auto *family = appearance.findChild<QLabel *>("interfaceFontFamilyLabel");
        auto *reset = appearance.findChild<QPushButton *>("interfaceFontResetButton");
        QVERIFY(size);
        QVERIFY(family);
        QVERIFY(reset);
        QCOMPARE(family->text(), QString("Iaito W95FA"));
        QCOMPARE(size->suffix(), QString(" px"));
        QCOMPARE(size->value(), 10);
        QVERIFY(!reset->isEnabled());
        QVERIFY(!Config()->hasCustomInterfaceFontSize());

        size->setValue(12);
        QCOMPARE(qApp->font().pixelSize(), 12);
        QTRY_COMPARE(appearance.font().pixelSize(), 12);
        QCOMPARE(qApp->font().family(), QString("Iaito W95FA"));
        QVERIFY(qApp->font().styleStrategy() & QFont::NoAntialias);
        QCOMPARE(qApp->font().hintingPreference(), QFont::PreferNoHinting);
        QVERIFY(reset->isEnabled());
        QCOMPARE(Config()->getBaseFont(), baseFont);
        QCOMPARE(Config()->getZoomFactor(), 1.5);
        QCOMPARE(code.font(), codeFont);
        QCOMPARE(codeUpdates.count(), 0);

        Config()->setInterfaceFontSize(14);
        QCOMPARE(size->value(), 14);
        reset->click();
        QCOMPARE(size->value(), 10);
        QCOMPARE(qApp->font().pixelSize(), 10);
        QVERIFY(!reset->isEnabled());
        QVERIFY(!Config()->hasCustomInterfaceFontSize());
        QCOMPARE(code.font(), codeFont);
        QCOMPARE(codeUpdates.count(), 0);
    }

    void interfaceFontSizePersistsPerTheme()
    {
        AppearanceOptionsWidget appearance(nullptr);
        auto *size = appearance.findChild<QSpinBox *>("interfaceFontSizeSpinBox");
        QVERIFY(size);
        const QFont darkDefault = qApp->font();
        QVERIFY(darkDefault.pointSize() > 0);
        QCOMPARE(size->suffix(), QString(" pt"));
        size->setValue(darkDefault.pointSize() + 2);
        const QFont darkCustom = qApp->font();

        selectTheme("Classic");
        QCOMPARE(size->value(), 10);
        QCOMPARE(size->suffix(), QString(" px"));
        size->setValue(14);
        QSettings saved;
        QCOMPARE(saved.value("interfaceFontSize.Classic").toInt(), 14);
        selectTheme("Dark");
        QCOMPARE(qApp->font(), darkCustom);
        QCOMPARE(size->suffix(), QString(" pt"));
        Config()->resetInterfaceFontSize();
        QCOMPARE(qApp->font(), darkDefault);
        selectTheme("Classic");
        QCOMPARE(size->value(), 14);
        Config()->loadInitial();
        QCOMPARE(qApp->font().pixelSize(), 14);
        Config()->resetInterfaceFontSize();
        QCOMPARE(qApp->font().pixelSize(), 10);
        QVERIFY(!saved.contains("interfaceFontSize.Classic"));
        selectTheme("Dark");
        QCOMPARE(qApp->font(), darkDefault);
        QVERIFY(!Config()->hasCustomInterfaceFontSize());
    }

    void interfaceFontSizePreservesDefaultCodeFont()
    {
        Config()->resetFont();
        QPlainTextEdit code;
        qhelpers::bindFont(&code, false);
        for (const QString &name :
             {QString("Classic"), QString("Phosphor"), QString("Dark"), QString("Native")}) {
            selectTheme(name);
            const QFont interfaceDefault = qApp->font();
            const QFont codeDefault = Config()->getBaseFont();
            const QFont codeRendered = code.font();
            const int size = interfaceDefault.pixelSize() > 0 ? interfaceDefault.pixelSize()
                                                              : interfaceDefault.pointSize();
            Config()->setInterfaceFontSize(size + 2);
            QCOMPARE(Config()->getBaseFont(), codeDefault);
            QCOMPARE(code.font(), codeRendered);
            selectTheme(name);
            QCOMPARE(qApp->font(), Config()->getInterfaceFont());
            QCOMPARE(Config()->getBaseFont(), codeDefault);
            QCOMPARE(code.font(), codeRendered);
            Config()->resetInterfaceFontSize();
            QCOMPARE(qApp->font(), interfaceDefault);
        }
    }

    void interfaceFontResetUsesCurrentThemeDefault()
    {
        Theme custom = Theme::builtin("Classic");
        custom.name = "Interface font test";
        custom.fonts.base.setPixelSize(14);
        Config()->loadEngineTheme(custom);
        Config()->setInterfaceFontSize(12);
        custom.fonts.base.setPixelSize(16);
        Config()->loadEngineTheme(custom);
        QCOMPARE(qApp->font().pixelSize(), 12);
        Config()->resetInterfaceFontSize();
        QCOMPARE(qApp->font().pixelSize(), 16);
        QVERIFY(!Config()->hasCustomInterfaceFontSize());
    }

    void classicThemeRoundTrip()
    {
        const Theme classic = Theme::builtin("Classic");
        const QString path = settings.filePath("classic.theme");
        classic.save(path);
        const Theme loaded = Theme::load(path, false);
        QCOMPARE(loaded.skin, Skin::Windows);
        QCOMPARE(loaded.fonts.base.family(), QString("Iaito W95FA"));
        QCOMPARE(loaded.fonts.base.pixelSize(), 10);
        QVERIFY(loaded.fonts.base.styleStrategy() & QFont::NoAntialias);
        Config()->loadEngineTheme(loaded);
        QCOMPARE(QFontInfo(qApp->font()).family(), QString("Iaito W95FA"));
        QVERIFY(qApp->font().styleStrategy() & QFont::NoAntialias);
        const QPalette p = qApp->palette();
        QCOMPARE(p.color(QPalette::Light), QColor(Qt::white));
        QCOMPARE(p.color(QPalette::Shadow), QColor(Qt::black));
        QCOMPARE(p.color(QPalette::Dark), QColor("#808080"));
        QCOMPARE(p.color(QPalette::Disabled, QPalette::Base), p.color(QPalette::Button));
        QSettings legacy(path, QSettings::IniFormat);
        legacy.setValue("font/family", "W95FA");
        legacy.sync();
        QCOMPARE(Theme::load(path, false).fonts.base.family(), QString("Iaito W95FA"));
    }

    void classicControlsScaleTogether()
    {
        selectTheme("Classic");
        auto *style = IaitoStyle::instance();
        QCOMPARE(style->pixelMetric(QStyle::PM_ScrollBarExtent), 17);
        QCOMPARE(style->pixelMetric(QStyle::PM_SmallIconSize), 16);
        QCOMPARE(style->pixelMetric(QStyle::PM_IndicatorWidth), 13);

        QLineEdit address;
        QComboBox combo;
        combo.addItem("Automatic");
        QPushButton button("Open");
        QCheckBox check("Analyze");
        QTabBar tabs;
        tabs.addTab("Disassembly");
        for (QWidget *widget : QList<QWidget *>{&address, &combo, &button, &check, &tabs}) {
            widget->ensurePolished();
            QVERIFY2(widget->sizeHint().height() >= 23, widget->metaObject()->className());
        }

        QScrollBar scrollbar(Qt::Vertical);
        scrollbar.setRange(0, 100);
        scrollbar.setValue(50);
        scrollbar.resize(17, 240);
        scrollbar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&scrollbar));
        QStyleOptionSlider option;
        option.initFrom(&scrollbar);
        option.orientation = Qt::Vertical;
        option.minimum = 0;
        option.maximum = 100;
        option.sliderPosition = 50;
        option.sliderValue = 50;
        option.pageStep = scrollbar.pageStep();
        const QRect arrow = style->subControlRect(
            QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarAddLine, &scrollbar);
        QCOMPARE(arrow.size(), QSize(17, 17));
        QTest::mouseClick(&scrollbar, Qt::LeftButton, Qt::NoModifier, arrow.center());
        QCOMPARE(scrollbar.value(), 51);
    }

    void classicCheckMarksMatchWindows()
    {
        selectTheme("Classic");
        std::unique_ptr<QStyle> windows(QStyleFactory::create("windows"));
        QVERIFY(windows);
        for (int dpr : {1, 2}) {
            for (auto state : {QStyle::State_Off, QStyle::State_On, QStyle::State_NoChange}) {
                QStyleOptionButton option;
                option.rect = QRect(0, 0, 13, 13);
                option.palette = qApp->palette();
                option.state = QStyle::State_Enabled | state;
                auto render = [&](QStyle *style) {
                    QImage image(13 * dpr, 13 * dpr, QImage::Format_RGB32);
                    image.setDevicePixelRatio(dpr);
                    image.fill(Qt::white);
                    QPainter painter(&image);
                    style->drawPrimitive(QStyle::PE_IndicatorCheckBox, &option, &painter);
                    return image;
                };
                QCOMPARE(render(IaitoStyle::instance()), render(windows.get()));
            }
        }
    }

    void classicFontHasSharpPixels()
    {
        selectTheme("Classic");
        for (int dpr : {1, 2}) {
            QImage image(240 * dpr, 40 * dpr, QImage::Format_RGB32);
            image.setDevicePixelRatio(dpr);
            image.fill(Qt::white);
            {
                QPainter painter(&image);
                painter.setFont(qApp->font());
                painter.setPen(Qt::black);
                painter.drawText(
                    QRect(0, 0, 240, 40), Qt::AlignCenter, QString::fromUtf8("W95FA Windows × …"));
            }
            int ink = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    const QColor color = image.pixelColor(x, y);
                    QVERIFY(color == Qt::black || color == Qt::white);
                    ink += color == Qt::black;
                }
            }
            QVERIFY(ink > 0);
        }
    }

    void classicBevelsScaleWithDisplay()
    {
        selectTheme("Classic");
        for (int dpr : {1, 2}) {
            for (bool raised : {false, true}) {
                QImage image(80 * dpr, 36 * dpr, QImage::Format_RGB32);
                image.setDevicePixelRatio(dpr);
                image.fill(Qt::white);
                QStyleOptionFrame option;
                option.rect = QRect(0, 0, 80, 36);
                option.palette = qApp->palette();
                option.lineWidth = 2;
                option.frameShape = QFrame::StyledPanel;
                option.state = QStyle::State_Enabled
                               | (raised ? QStyle::State_Raised : QStyle::State_Sunken);
                {
                    QPainter painter(&image);
                    IaitoStyle::instance()->drawPrimitive(QStyle::PE_Frame, &option, &painter);
                }
                const QColor outer = raised ? QColor(Qt::white) : QColor("#808080");
                const QColor inner = raised ? QColor("#dfdfdf") : QColor(Qt::black);
                for (int y = 0; y < dpr; ++y) {
                    QCOMPARE(image.pixelColor(10 * dpr, y), outer);
                    QCOMPARE(image.pixelColor(10 * dpr, y + dpr), inner);
                }
                QCOMPARE(image.pixelColor(10 * dpr, 2 * dpr), QColor(Qt::white));
            }
        }
    }

    void classicScrollPatternScalesWithDisplay_data()
    {
        QTest::addColumn<qreal>("dpr");
        QTest::addColumn<int>("state");
        for (qreal dpr : {1.0, 1.5, 2.0}) {
            for (int state = 0; state < 3; ++state) {
                QTest::newRow(qPrintable(QString("%1x-%2").arg(dpr).arg(state))) << dpr << state;
            }
        }
    }

    void classicScrollPatternScalesWithDisplay()
    {
        QFETCH(qreal, dpr);
        QFETCH(int, state);
        selectTheme("Classic");
        QStyleOptionSlider option;
        option.rect = QRect(0, 0, 16, 16);
        option.palette = qApp->palette();
        option.state = state == 2 ? QStyle::State_None : QStyle::State_Enabled;
        if (state == 1) {
            option.state |= QStyle::State_Sunken;
        }
        QImage image(qRound(16 * dpr), qRound(16 * dpr), QImage::Format_RGB32);
        image.setDevicePixelRatio(dpr);
        image.fill(Qt::magenta);
        {
            QPainter painter(&image);
            IaitoStyle::instance()->drawControl(
                state == 2 ? QStyle::CE_ScrollBarSlider : QStyle::CE_ScrollBarAddPage,
                &option,
                &painter);
        }
        const QColor foreground = option.palette.color(
            state == 1 ? QPalette::Shadow : QPalette::Light);
        const QColor background = option.palette.color(
            state == 1 ? QPalette::Dark : QPalette::Window);
        // One Windows pixel is 2x2 physical pixels on Retina, not 1x1 or 4x4.
        const int period = qRound(2 * dpr);
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor color = image.pixelColor(x, y);
                QVERIFY(color == foreground || color == background);
                QCOMPARE(color, image.pixelColor(x % period, y % period));
                if (dpr != 1.5) {
                    const int cell = qRound(dpr);
                    QCOMPARE(color, (x / cell + y / cell) % 2 ? background : foreground);
                }
            }
        }
    }

    void consoleThemeBackgroundsRemainReadable()
    {
        for (const QString &interfaceTheme :
             {QString("Light"), QString("Dark"), QString("Tablet")}) {
            selectTheme(interfaceTheme);
            for (const QString &console : {QString("default"), QString("rasta")}) {
                const auto theme = ThemeWorker().getTheme(console).object();
                const auto rgb = theme.value("gui.background").toArray();
                QCOMPARE(rgb.size(), 4);
                const QColor
                    background(rgb[0].toInt(), rgb[1].toInt(), rgb[2].toInt(), rgb[3].toInt());
                QCOMPARE(background.alpha(), 255);
                QVERIFY2(
                    contrast(background, qApp->palette().color(QPalette::Text)) >= 4.5,
                    qPrintable(interfaceTheme + "/" + console));
            }
        }
    }

    void syntaxColorsFollowThemes()
    {
        using Language = FallbackSyntaxHighlighter::Language;
        for (auto language :
             {Language::C, Language::JavaScript, Language::Python, Language::Radare2}) {
            QTextDocument document;
            FallbackSyntaxHighlighter highlighter(&document, language);
            document.setPlainText(
                "int test() { return 0x10; }\nconst x = \"text\"; // comment\n"
                "# note\npd 16\n/* comment\ncontinued */");
            for (const auto &theme : Configuration::iaitoInterfaceThemesList()) {
                selectTheme(theme.name);
                highlighter.rehighlight();
                int formats = 0;
                for (auto block = document.begin(); block.isValid(); block = block.next()) {
                    for (const auto &range : block.layout()->formats()) {
                        QVERIFY2(
                            contrast(
                                range.format.foreground().color(),
                                qApp->palette().color(QPalette::Base))
                                >= 4.5,
                            qPrintable(theme.name));
                        ++formats;
                    }
                }
                QVERIFY(formats > 0);
            }
        }
    }

    void nativeThemeSwitching_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }

    void nativeThemeSwitching()
    {
        QFETCH(bool, dark);
        selectTheme("Native");
        QPalette native = Theme::builtin(dark ? "Dark" : "Light").toPalette(systemPalette);
        native.setColor(QPalette::Inactive, QPalette::Highlight, QColor("#777777"));
        native.setColor(QPalette::Disabled, QPalette::Text, QColor("#888888"));
        native.setColor(QPalette::AlternateBase, dark ? QColor("#242424") : QColor("#eeeeee"));
        QPlainTextEdit existing;
        existing.show();
        // Simulate the complete palette delivered by the platform theme.
        qApp->setPalette(native);
        QTRY_COMPARE(
            IaitoStyle::instance()->theme().background.normal, native.color(QPalette::Window));
        QCOMPARE(Configuration::nativeWindowIsDark(), dark);

        for (const QString &previous : {QString("Light"), QString("Classic"), QString("Dark")}) {
            selectTheme(previous);
            selectTheme("Native");
            QCoreApplication::processEvents();
            QCOMPARE(IaitoStyle::instance()->theme().skin, Skin::Native);
            QCOMPARE(IaitoStyle::instance()->baseStyle()->objectName(), systemStyle);
            QCOMPARE(qApp->palette(), native);
            QPlainTextEdit created;
            for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
                for (auto role :
                     {QPalette::Base,
                      QPalette::Text,
                      QPalette::Highlight,
                      QPalette::AlternateBase,
                      QPalette::ToolTipText}) {
                    QCOMPARE(existing.palette().color(group, role), native.color(group, role));
                    QCOMPARE(created.palette().color(group, role), native.color(group, role));
                }
            }
            QCOMPARE(ConfigColor("gui.background"), native.color(QPalette::Base));
            QCOMPARE(
                ConfigColor("gui.tooltip.foreground"),
                native.color(QPalette::Inactive, QPalette::ToolTipText));
        }
        qApp->setPalette(systemPalette);
        QCoreApplication::processEvents();
    }

    void nativeControlsUsePlatformStyle()
    {
        selectTheme("Native");
        std::unique_ptr<QStyle> platform(QStyleFactory::create(systemStyle));
        QVERIFY(platform);
        auto *style = IaitoStyle::instance();
        QStyleOptionFrame frame;
        frame.rect = QRect(0, 0, 100, 30);
        frame.state = QStyle::State_Enabled | QStyle::State_HasFocus;
        frame.palette = qApp->palette();
        frame.lineWidth = platform->pixelMetric(QStyle::PM_DefaultFrameWidth, &frame);
        QCOMPARE(style->pixelMetric(QStyle::PM_DefaultFrameWidth, &frame), frame.lineWidth);
        QStyleOptionTab tab;
        tab.rect = frame.rect;
        tab.palette = frame.palette;
        tab.state = QStyle::State_Enabled | QStyle::State_Selected;
        tab.shape = QTabBar::RoundedNorth;
        tab.position = QStyleOptionTab::Beginning;
        tab.text = "Native";
        auto render = [&](QStyle *s) {
            QImage image(100, 60, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            s->drawPrimitive(QStyle::PE_PanelLineEdit, &frame, &painter);
            painter.translate(0, 30);
            s->drawControl(QStyle::CE_TabBarTab, &tab, &painter);
            return image;
        };
        QCOMPARE(render(style), render(platform.get()));
    }

    void disassemblyScrollbarsReserveCorner()
    {
        selectTheme("Classic");
        DisassemblyWidget disassembly(nullptr);
        auto *edit = disassembly.findChild<QPlainTextEdit *>();
        auto *vertical = disassembly.findChild<AddressScrollBar *>();
        auto *corner = disassembly.findChild<QWidget *>("disassemblyScrollCorner");
        auto *splitter = disassembly.findChild<QSplitter *>();
        QVERIFY(edit);
        QVERIFY(vertical);
        QVERIFY(corner);
        QVERIFY(splitter);
        auto *horizontal = edit->horizontalScrollBar();
        edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        disassembly.resize(600, 300);
        disassembly.show();
        QVERIFY(QTest::qWaitForWindowExposed(&disassembly));
        for (const QString &theme : {QString("Classic"), QString("Dark"), QString("Classic")}) {
            selectTheme(theme);
            QTRY_VERIFY(corner->isVisible());
            QTRY_COMPARE(corner->height(), horizontal->height());
            QTRY_COMPARE(corner->width(), vertical->width());
            QTRY_COMPARE(splitter->width() + vertical->width(), disassembly.widget()->width());
            QTRY_COMPARE(
                vertical->mapTo(&disassembly, QPoint(0, vertical->height())).y(),
                horizontal->mapTo(&disassembly, QPoint()).y());
        }
        vertical->hide();
        QTRY_VERIFY(corner->isHidden());
        vertical->show();
        QTRY_VERIFY(corner->isVisible());
        edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        QTRY_VERIFY(corner->isHidden());
    }

    void classicStatusBarHasBezel()
    {
        selectTheme("Classic");
        QStatusBar status;
        status.showMessage("Ready");
        status.resize(320, 23);
        status.show();
        QVERIFY(QTest::qWaitForWindowExposed(&status));
        const QImage image = status.grab().toImage();
        const int dpr = qRound(status.devicePixelRatioF());
        QCOMPARE(image.pixelColor(100 * dpr, 0), QColor("#808080"));
        QCOMPARE(image.pixelColor(100 * dpr, dpr), QColor(Qt::black));
        QCOMPARE(image.pixelColor(100 * dpr, image.height() - 1), QColor(Qt::white));
    }

    void dockHeaderControlsAlign()
    {
        selectTheme("Classic");
        MainWindow main;
        for (auto *dock : main.findChildren<IaitoDockWidget *>()) {
            dock->hide();
        }
        auto *dock = new IaitoDockWidget(&main);
        dock->setObjectName("header-alignment-test");
        dock->setWindowTitle("Graph Overview");
        dock->setWindowIcon(QIcon(":/img/icons/graph.svg"));
        dock->setWidget(new QWidget(dock));
        main.addWidget(dock);
        main.addDockWidget(Qt::TopDockWidgetArea, dock);
        dock->show();
        main.resize(700, 300);
        main.showNormal();
        QVERIFY(QTest::qWaitForWindowExposed(&main));
        auto *manager = main.findChild<DockManager *>();
        QVERIFY(manager);
        manager->applyDockPanelChrome();
        QCoreApplication::processEvents();
        auto *header = dock->titleBarWidget();
        QVERIFY(header);
        auto *icon = header->findChild<QLabel *>("dockDragHandleIcon");
        auto *title = header->findChild<QLabel *>("dockDragHandleTitle");
        auto *close = header->findChild<QToolButton *>("dockHandleCloseButton");
        QVERIFY(icon);
        QVERIFY(title);
        QVERIFY(close);
        QCOMPARE(title->geometry().center().y(), icon->geometry().center().y());
        QCOMPARE(close->geometry().center().y(), icon->geometry().center().y());
        QVERIFY(header->rect().contains(icon->geometry()));
        QVERIFY(header->rect().contains(title->geometry()));
        QCOMPARE(close->text(), QString(QChar(0x00d7)));
        QVERIFY(!close->autoRaise());
    }

    void dockTabsUseAvailableWidth()
    {
        MainWindow main;
        main.setAnimated(false);
        for (auto *dock : main.findChildren<IaitoDockWidget *>()) {
            dock->hide();
        }
        IaitoDockWidget *first = nullptr;
        for (const QString &name :
             {QString("Functions"), QString("Disassembly"), QString("Switch Tables")}) {
            auto *dock = new IaitoDockWidget(&main);
            dock->setObjectName("width-test-" + name);
            dock->setWindowTitle(name);
            dock->setWidget(new QWidget(dock));
            main.addWidget(dock);
            main.addDockWidget(Qt::TopDockWidgetArea, dock);
            if (first) {
                main.tabifyDockWidget(first, dock);
            } else {
                first = dock;
            }
            dock->show();
        }
        main.resize(1100, 400);
        main.showNormal();
        QVERIFY(QTest::qWaitForWindowExposed(&main));
        auto *manager = main.findChild<DockManager *>();
        QVERIFY(manager);
        for (const QString &theme : {QString("Classic"), QString("Dark"), QString("Classic")}) {
            selectTheme(theme);
            manager->updateDockTabBars();
            QCoreApplication::processEvents();
            QTabBar *tabs = nullptr;
            for (auto *bar : main.findChildren<QTabBar *>()) {
                if (bar->isVisible() && bar->count() == 3 && bar->tabText(0) == "Functions") {
                    tabs = bar;
                    break;
                }
            }
            QVERIFY(tabs);
            QVERIFY(tabs->expanding());
            QRect occupied;
            for (int i = 0; i < tabs->count(); ++i) {
                const QRect rect = tabs->tabRect(i);
                occupied = occupied.united(rect);
                QStyleOptionTab option;
                option.initFrom(tabs);
                option.rect = rect;
                option.leftButtonSize = tabs->tabButton(i, QTabBar::LeftSide)->size();
                const QRect text
                    = tabs->style()->subElementRect(QStyle::SE_TabBarTabText, &option, tabs);
                QVERIFY2(
                    text.width() >= tabs->fontMetrics().horizontalAdvance(tabs->tabText(i)),
                    qPrintable(tabs->tabText(i)));
            }
            QVERIFY(occupied.width() >= tabs->width() - 2);
        }
    }

    void dockTabIcons_data()
    {
        QTest::addColumn<QString>("theme");
        QTest::addColumn<bool>("bottom");
        for (const QString &theme :
             {QString("Light"),
              QString("Dark"),
              QString("Classic"),
              QString("Native"),
              QString("Phosphor")}) {
            QTest::newRow(qPrintable(theme + "-top")) << theme << false;
            QTest::newRow(qPrintable(theme + "-bottom")) << theme << true;
        }
    }

    void dockTabIcons()
    {
        QFETCH(QString, theme);
        QFETCH(bool, bottom);
        selectTheme(theme);
        MainWindow main;
        main.setAnimated(false);
        main.resize(700, 350);
        for (auto *dock : main.findChildren<IaitoDockWidget *>()) {
            dock->hide();
        }
        auto addPanel = [&](const QString &name) {
            auto *dock = new IaitoDockWidget(&main);
            dock->setObjectName(name);
            dock->setWindowTitle(name);
            dock->setWidget(new QWidget(dock));
            main.addWidget(dock);
            main.addDockWidget(Qt::TopDockWidgetArea, dock);
            return dock;
        };
        auto *first = addPanel("Short");
        auto *second = addPanel("A very long panel title that needs to be elided in a narrow tab");
        main.tabifyDockWidget(first, second);
        main.setTabPosition(Qt::AllDockWidgetAreas, bottom ? QTabWidget::South : QTabWidget::North);
        main.show();
        QVERIFY(QTest::qWaitForWindowExposed(&main));
        auto *manager = main.findChild<DockManager *>();
        manager->updateDockTabBars();
        QTabBar *tabs = nullptr;
        for (auto *bar : main.findChildren<QTabBar *>()) {
            if (bar->isVisible() && bar->count() == 2 && bar->tabText(0) == first->windowTitle()) {
                tabs = bar;
                break;
            }
        }
        QVERIFY(tabs);
        QTest::mouseMove(&main, QPoint(5, main.height() - 5));
        auto *close = tabs->findChild<QToolButton *>("dockTabCloseButton");
        auto *icon = qobject_cast<QLabel *>(tabs->tabButton(0, QTabBar::LeftSide));
        QVERIFY(close);
        QVERIFY(icon);
        QTRY_VERIFY(!close->isVisible());
        const QPixmap contentIcon = icon->property("pixmap").value<QPixmap>();
        QVERIFY(!contentIcon.isNull());
        QCOMPARE(contentIcon.devicePixelRatio(), tabs->devicePixelRatioF());
        const QRect tabRect = tabs->tabRect(0);
        QStyleOptionTab option;
        option.initFrom(tabs);
        option.rect = tabRect;
        option.leftButtonSize = icon->size();
        const QRect titleRect
            = tabs->style()->subElementRect(QStyle::SE_TabBarTabText, &option, tabs);
        QCOMPARE(titleRect.center().x(), tabRect.center().x());
        QVERIFY(!titleRect.intersects(icon->geometry()));
        QVERIFY(titleRect.left() - icon->geometry().right() <= 4);

        QTest::mouseMove(tabs, tabRect.center());
        QTRY_VERIFY(close->isVisible());
        QCOMPARE(close->geometry(), icon->geometry());
        QVERIFY(icon->property("pixmap").value<QPixmap>().isNull());
        QCOMPARE(tabs->tabRect(0), tabRect);
        QTest::mouseMove(tabs, tabs->tabRect(1).center());
        QCOMPARE(icon->property("pixmap").value<QPixmap>().toImage(), contentIcon.toImage());
        QCOMPARE(close->geometry(), tabs->tabButton(1, QTabBar::LeftSide)->geometry());
        QTest::mouseMove(&main, QPoint(5, main.height() - 5));
        QTRY_VERIFY(!close->isVisible());

        // Reordering must carry the icon and close the panel now occupying that tab.
        tabs->moveTab(0, 1);
        QTest::mouseMove(tabs, tabs->tabRect(1).center());
        QTRY_COMPARE(close->geometry(), tabs->tabButton(1, QTabBar::LeftSide)->geometry());
        QTest::mouseClick(close, Qt::LeftButton);
        QTRY_VERIFY(first->isHidden());
        QVERIFY(!second->isHidden());
    }

    void panelMenuIconsFollowTabs()
    {
        MainWindow main;
        QVERIFY(QMetaObject::invokeMethod(&main, "on_actionDefault_triggered"));
        for (const QString &theme : {QString("Light"), QString("Dark"), QString("Native")}) {
            selectTheme(theme);
            const auto memoryDocks = main.findChildren<MemoryDockWidget *>();
            QCOMPARE(memoryDocks.size(), 4);
            for (auto *dock : memoryDocks) {
                QStringList names;
                switch (dock->getType()) {
                case MemoryWidgetType::Disassembly:
                    names = {"actionToolbarDisassembly", "actionExtraDisassembly"};
                    break;
                case MemoryWidgetType::Graph:
                    names = {"actionToolbarGraph", "actionExtraGraph"};
                    break;
                case MemoryWidgetType::Hexdump:
                    names = {"actionToolbarHexdump", "actionExtraHexdump"};
                    break;
                case MemoryWidgetType::Decompiler:
                    names = {"actionViewDecompiler", "actionExtraDecompiler"};
                    break;
                }
                for (const QString &name : names) {
                    auto *action = main.findChild<QAction *>(name);
                    QVERIFY(action);
                    QVERIFY(action->isIconVisibleInMenu());
                    QCOMPARE(
                        action->icon().pixmap(16, 16).toImage(),
                        dock->windowIcon().pixmap(16, 16).toImage());
                }
            }
            auto *view = main.findChild<QMenu *>("menuView");
            QVERIFY(
                view->actions().contains(main.findChild<QAction *>("actionToolbarDisassembly")));
            for (auto *dock : main.findChildren<IaitoDockWidget *>()) {
                QVERIFY(!dock->windowIcon().isNull());
                QCOMPARE(dock->toggleViewAction()->icon().cacheKey(), dock->windowIcon().cacheKey());
                QVERIFY(dock->toggleViewAction()->isIconVisibleInMenu());
            }
        }
    }

    void tooltipPaletteFollowsTheme()
    {
        ZoomView view;
        view.setData({{0, 128}, {16, 64}});
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        for (const QString &theme : {QString("Light"), QString("Dark"), QString("Native")}) {
            selectTheme(theme);
            if (theme == "Native") {
                const QPalette dark = Theme::builtin("Dark").toPalette(systemPalette);
                qApp->setPalette(dark);
                QTRY_COMPARE(
                    IaitoStyle::instance()->theme().background.normal, dark.color(QPalette::Window));
            }
            const QPointF pos(2, 2);
            QMouseEvent move(
                QEvent::MouseMove,
                pos,
                view.mapToGlobal(pos.toPoint()),
                Qt::NoButton,
                Qt::NoButton,
                Qt::NoModifier);
            QApplication::sendEvent(&view, &move);
            QVERIFY(QToolTip::isVisible());
            QWidget *tip = nullptr;
            for (QWidget *w : qApp->topLevelWidgets()) {
                if (w->inherits("QTipLabel")) {
                    tip = w;
                    break;
                }
            }
            QVERIFY(tip);
            const QPalette p = qApp->palette();
            QCOMPARE(
                tip->palette().color(QPalette::Inactive, tip->foregroundRole()),
                p.color(QPalette::Inactive, QPalette::ToolTipText));
            QCOMPARE(
                tip->palette().color(QPalette::Inactive, tip->backgroundRole()),
                p.color(QPalette::Inactive, QPalette::ToolTipBase));
        }
        QToolTip::hideText();
        qApp->setPalette(systemPalette);
        QCoreApplication::processEvents();
    }

    void zoomGesturesAdjustBlocks()
    {
        ZoomWidget widget(nullptr);
        auto *view = widget.findChild<ZoomView *>();
        auto *blocks = widget.findChild<QSpinBox *>("zoomBlocksSpinBox");
        QVERIFY(view);
        QVERIFY(blocks);
        const int cellSize = view->cellSize();
        auto wheel = [&](int delta, Qt::KeyboardModifiers modifiers) {
            QWheelEvent event(
                QPointF(2, 2),
                QPointF(2, 2),
                QPoint(),
                QPoint(0, delta),
                Qt::NoButton,
                modifiers,
                Qt::NoScrollPhase,
                false);
            QApplication::sendEvent(view, &event);
        };
        const int initial = blocks->value();
        wheel(120, Qt::ControlModifier);
        QCOMPARE(blocks->value(), initial + blocks->singleStep());
        wheel(-120, Qt::ControlModifier);
        QCOMPARE(blocks->value(), initial);
        wheel(60, Qt::ControlModifier);
        QCOMPARE(blocks->value(), initial);
        wheel(60, Qt::ControlModifier);
        QCOMPARE(blocks->value(), initial + blocks->singleStep());
        wheel(120, Qt::NoModifier);
        QCOMPARE(blocks->value(), initial + blocks->singleStep());

        QPinchGesture pinch;
        pinch.setTotalScaleFactor(1.3);
        pinch.setChangeFlags(QPinchGesture::ScaleFactorChanged);
        QGestureEvent gesture({&pinch});
        QApplication::sendEvent(view, &gesture);
        QCOMPARE(blocks->value(), initial + 2 * blocks->singleStep());

        ZoomEventProbe nativeView;
        QSignalSpy nativeSteps(&nativeView, &ZoomView::blocksChangeRequested);
        auto nativeGesture = [&nativeView](Qt::NativeGestureType type, qreal value) {
            const QPointF local(2, 2);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            QNativeGestureEvent event(
                type,
                QPointingDevice::primaryPointingDevice(),
                2,
                local,
                local,
                local,
                value,
                QPointF());
#else
            QNativeGestureEvent event(type, nullptr, local, local, local, value, 0, 0);
#endif
            // Qt's gesture manager can consume synthetic native events before delivery.
            nativeView.event(&event);
        };
        nativeGesture(Qt::BeginNativeGesture, 0);
        nativeGesture(Qt::ZoomNativeGesture, 0.3);
        nativeGesture(Qt::EndNativeGesture, 0);
        QCOMPARE(nativeSteps.size(), 1);
        QCOMPARE(nativeSteps.first().first().toInt(), 1);
        QCOMPARE(view->cellSize(), cellSize);
        QCOMPARE(widget.serializeViewProprties().value("blocks").toInt(), blocks->value());

        blocks->setValue(blocks->maximum());
        wheel(120, Qt::ControlModifier);
        QCOMPARE(blocks->value(), blocks->maximum());
        blocks->setValue(blocks->minimum());
        wheel(-120, Qt::ControlModifier);
        QCOMPARE(blocks->value(), blocks->minimum());
    }

    void disassemblyMnemonicColors_data()
    {
        QTest::addColumn<bool>("colorize");
        QTest::addColumn<bool>("showBytes");
        QTest::newRow("plain") << false << false;
        QTest::newRow("plain-with-bytes") << false << true;
        QTest::newRow("colored") << true << false;
        QTest::newRow("colored-with-bytes") << true << true;
    }

    void disassemblyMnemonicColors()
    {
        QFETCH(bool, colorize);
        QFETCH(bool, showBytes);
        TempConfig config;
        config.set("asm.arch", "x86")
            .set("asm.bits", 64)
            .set("asm.bytes", showBytes)
            .set("graph.addr", false)
            .set("scr.color.ops", true)
            .set("scr.color.mnemonics", true);
        AsmOptionsWidget options(nullptr);
        auto *checkbox = options.findChild<QCheckBox *>("colorizeMnemonicsCheckBox");
        QVERIFY(checkbox);
        checkbox->setChecked(colorize);
        QCOMPARE(Core()->getConfigb("scr.color.mnemonics"), colorize);
        QVERIFY(Core()->getConfigb("scr.color.ops"));
        Core()->cmd("wx 554889e5b82a000000488d04257856341290c3 @ 0");
        Core()->cmd("ompg +x");
        Core()->cmd("af @ 0");
        DisassemblyWidget widget(nullptr);
        widget.getSeekable()->setSynchronization(false);
        widget.getSeekable()->seek(0);
        widget.resize(900, 500);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        auto *text = qobject_cast<QPlainTextEdit *>(widget.getTextWidget());
        QVERIFY(text);
        IaitoSeekable seekable;
        seekable.setSynchronization(false);
        seekable.seek(0);
        DisassemblerGraphView graph(nullptr, &seekable, nullptr, {});

        for (const QString &theme : {QString("Dark"), QString("Classic"), QString("Dark")}) {
            selectTheme(theme);
            QCoreApplication::processEvents();
            graph.refreshView();
            QString graphHtml;
            for (const auto &block : graph.disassembly_blocks) {
                for (const auto &instruction : block.second.instrs) {
                    for (const auto &line : instruction.text.lines) {
                        QString html, plain;
                        RichTextPainter::htmlRichText(line, html, plain);
                        graphHtml += html + "<br/>";
                    }
                }
            }
            QTextDocument graphText;
            graphText.setHtml(graphHtml);
            for (const QString &mnemonic :
                 {QString("push"), QString("mov"), QString("nop"), QString("ret")}) {
                const QColor expected = colorize ? ConfigColor(mnemonic)
                                                 : qApp->palette().color(QPalette::Text);
                QCOMPARE(textColor(*text->document(), mnemonic), expected);
                QCOMPARE(textColor(graphText, mnemonic), expected);
            }
            for (const QString &reg : {QString("rbp"), QString("rsp"), QString("eax")}) {
                QCOMPARE(textColor(*text->document(), reg), ConfigColor("reg"));
                QCOMPARE(textColor(graphText, reg), ConfigColor("reg"));
            }
            for (const QString &number : {QString("0x2a"), QString("0x12345678")}) {
                QCOMPARE(textColor(*text->document(), number), ConfigColor("num"));
                QCOMPARE(textColor(graphText, number), ConfigColor("num"));
            }
        }
    }

    void mnemonicColorPreferenceMigration_data()
    {
        QTest::addColumn<bool>("legacyValue");
        QTest::addColumn<bool>("hasNewValue");
        QTest::newRow("disabled") << false << false;
        QTest::newRow("enabled") << true << false;
        QTest::newRow("new-disabled") << true << true;
        QTest::newRow("new-enabled") << false << true;
    }

    void mnemonicColorPreferenceMigration()
    {
        QFETCH(bool, legacyValue);
        QFETCH(bool, hasNewValue);
        QSettings saved;
        saved.remove("scr.color.mnemonics");
        saved.setValue("scr.color.ops", legacyValue);
        if (hasNewValue) {
            saved.setValue("scr.color.mnemonics", !legacyValue);
        }
        Config()->loadInitial();
        const bool expected = hasNewValue ? !legacyValue : legacyValue;
        QCOMPARE(Core()->getConfigb("scr.color.mnemonics"), expected);
        QCOMPARE(saved.value("scr.color.mnemonics").toBool(), expected);
        QCOMPARE(Core()->getConfigb("scr.color.ops"), hasNewValue ? legacyValue : true);
        AsmOptionsWidget options(nullptr);
        auto *checkbox = options.findChild<QCheckBox *>("colorizeMnemonicsCheckBox");
        QVERIFY(checkbox);
        QCOMPARE(checkbox->isChecked(), expected);
        Config()->setConfig("scr.color.ops", true);
        Config()->setConfig("scr.color.mnemonics", true);
    }

    void plainGraphTextColors()
    {
        for (const QString &theme : {QString("Dark"), QString("Classic")}) {
            selectTheme(theme);
            for (const QString &highlight : {QString(), QString("background-color:#123456")}) {
                QTextDocument document;
                document.setHtml(
                    "<span style='color:red'>address</span><span style='" + highlight
                    + "'>nop</span>");
                const auto text = RichTextPainter::fromTextDocument(document);
                QImage image(300, 50, QImage::Format_ARGB32_Premultiplied);
                image.fill(Qt::transparent);
                QPainter painter(&image);
                painter.setFont(Config()->getFont());
                CachedFontMetrics<qreal> metrics(painter.font());
                RichTextPainter::paintRichText<qreal>(
                    &painter, 0, 0, image.width(), image.height(), 0, text, &metrics);
                QCOMPARE(painter.pen().color(), qApp->palette().color(QPalette::Text));
            }
        }
    }

    void windowShrinksAfterThemeChange()
    {
        // Measure the default workspace without analysis left by the disassembly fixtures.
        for (const auto &function : Core()->getAllFunctions()) {
            Core()->delFunction(function.offset);
        }
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
