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
#include "widgets/DecompilerWidget.h"
#include "widgets/DisassemblerGraphView.h"
#include "widgets/DisassemblyWidget.h"
#include "widgets/ZoomWidget.h"

#include <cmath>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QGestureEvent>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPinchGesture>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
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
