#include "Configuration.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSet>
#include <QStandardPaths>
#include <QStyle>
#include <QTextEdit>
#include <QTimer>
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QStyleHints>
#endif

#ifdef IAITO_ENABLE_KSYNTAXHIGHLIGHTING
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Theme>
#endif

#include "common/ColorThemeWorker.h"
#include "common/ResourcePaths.h"
#include "common/SyntaxHighlighter.h"
#include "common/theme/IaitoStyle.h"
#include "common/theme/Theme.h"

#include <QMetaType>
#include <QVariant>

namespace {
static inline int variantTypeId(const QVariant &v)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return v.metaType().id();
#else
    return v.userType();
#endif
}

constexpr int VisualNavbarThicknessDefault = 15;
constexpr int VisualNavbarThicknessMin = 8;
constexpr int VisualNavbarThicknessMax = 128;
constexpr int VisualNavbarResolutionMax = 32;
constexpr const char *AddressRangeSelectionSyncKey = "syncAddressRangeSelection";
constexpr const char *DisassemblyJumpLineAdaptiveSpacingKey = "disassemblyJumpLineAdaptiveSpacing";
constexpr const char *DisassemblyJumpLineSpacingKey = "disassemblyJumpLineSpacing";
constexpr int DisassemblyJumpLineSpacingDefault = 20;
constexpr const char *DisassemblyJumpLineFollowKey = "disassemblyJumpLineFollow";
constexpr const char *DisassemblyJumpLineCurvesKey = "disassemblyJumpLineCurves";
constexpr const char *DisassemblyJumpLineColorizeKey = "disassemblyJumpLineColorize";
constexpr const char *DisassemblyJumpLineWidthKey = "disassemblyJumpLineWidth";
constexpr const char *MobileDisassemblyJumpLineWidthKey = "mobileDisassemblyJumpLineWidth";

bool mobilePlatform()
{
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    return true;
#else
    return QGuiApplication::platformName() == QLatin1String("android")
           || QGuiApplication::platformName() == QLatin1String("ios")
           || qEnvironmentVariableIntValue("IAITO_MOBILE_UI") == 1;
#endif
}

int defaultInterfaceTheme()
{
    if (mobilePlatform()) {
        const auto &themes = Configuration::iaitoInterfaceThemesList();
        for (int i = 0; i < themes.size(); ++i) {
            if (themes.at(i).name == QLatin1String("Tablet")) {
                return i;
            }
        }
    }
    return 0;
}
} // namespace

/* Map with names of themes associated with its color palette
 * (Dark or Light), so for dark interface themes will be shown only Dark color
 * themes and for light - only light ones.
 */
const QHash<QString, ColorFlags> Configuration::relevantThemes
    = {{"ayu", DarkFlag},
       {"basic", DarkFlag},
       {"bluy", DarkFlag},
       {"bold", DarkFlag},
       {"bright", DarkFlag},
       {"cga", DarkFlag},
       {"consonance", DarkFlag},
       {"darkda", DarkFlag},
       {"focus", DarkFlag},
       {"gb", DarkFlag},
       {"lima", DarkFlag},
       {"monokai", DarkFlag},
       {"ogray", DarkFlag},
       {"onedark", DarkFlag},
       {"pink", DarkFlag},
       {"rasta", static_cast<ColorFlags>(DarkFlag | LightFlag)},
       {"sepia", DarkFlag},
       {"smyck", DarkFlag},
       {"solarized", DarkFlag},
       {"twilight", DarkFlag},
       {"zenburn", DarkFlag},
       // light themes
       {"iaito", LightFlag},
       {"dark", LightFlag},
       {"matrix", LightFlag},
       {"tango", LightFlag},
       {"white", LightFlag},
       {"white2", LightFlag}};
static const QString DEFAULT_LIGHT_COLOR_THEME = "iaito";
static const QString DEFAULT_DARK_COLOR_THEME = "ayu";

const QHash<QString, QHash<ColorFlags, QColor>> Configuration::iaitoOptionColors
    = {{"gui.cflow", {{DarkFlag, QColor(0xff, 0xff, 0xff)}, {LightFlag, QColor(0x00, 0x00, 0x00)}}},
       {"gui.dataoffset",
        {{DarkFlag, QColor(0xff, 0xff, 0xff)}, {LightFlag, QColor(0x00, 0x00, 0x00)}}},
       {"gui.imports",
        {{DarkFlag, QColor(0x32, 0x8c, 0xff)}, {LightFlag, QColor(0x32, 0x8c, 0xff)}}},
       {"gui.item_invalid",
        {{DarkFlag, QColor(0x9b, 0x9b, 0x9b)}, {LightFlag, QColor(0x9b, 0x9b, 0x9b)}}},
       {"gui.main", {{DarkFlag, QColor(0x00, 0x80, 0x00)}, {LightFlag, QColor(0x00, 0x80, 0x00)}}},
       {"gui.item_unsafe",
        {{DarkFlag, QColor(0xff, 0x81, 0x7b)}, {LightFlag, QColor(0xff, 0x81, 0x7b)}}},
       {"gui.item_thread_unsafe",
        {{DarkFlag, QColor(0x7b, 0x81, 0xff)}, {LightFlag, QColor(0x7b, 0x81, 0xff)}}},
       {"gui.navbar.seek",
        {{DarkFlag, QColor(0xe9, 0x56, 0x56)}, {LightFlag, QColor(0xff, 0x00, 0x00)}}},
       {"gui.navbar.pc",
        {{DarkFlag, QColor(0x42, 0xee, 0xf4)}, {LightFlag, QColor(0x42, 0xee, 0xf4)}}},
       {"gui.navbar.code",
        {{DarkFlag, QColor(0x82, 0xa8, 0x6f)}, {LightFlag, QColor(0x68, 0xc5, 0x45)}}},
       {"gui.navbar.data",
        {{DarkFlag, QColor(0xa0, 0x94, 0x68)}, {LightFlag, QColor(0xb0, 0x87, 0x2f)}}},
       {"gui.navbar.str",
        {{DarkFlag, QColor(0x6f, 0x86, 0xd8)}, {LightFlag, QColor(0x45, 0x68, 0xe5)}}},
       {"gui.navbar.sym",
        {{DarkFlag, QColor(0xdd, 0xa3, 0x68)}, {LightFlag, QColor(0xe5, 0x96, 0x45)}}},
       {"gui.navbar.instruction",
        {{DarkFlag, QColor(0xa0, 0x74, 0x58)}, {LightFlag, QColor(0xb9, 0x7a, 0x57)}}},
       {"gui.navbar.empty",
        {{DarkFlag, QColor(0x64, 0x64, 0x64)}, {LightFlag, QColor(0xdc, 0xec, 0xf5)}}},
       {"gui.breakpoint_background",
        {{DarkFlag, QColor(0x8c, 0x4c, 0x4c)}, {LightFlag, QColor(0xe9, 0x8f, 0x8f)}}},
       {"gui.overview.node",
        {{DarkFlag, QColor(0x64, 0x64, 0x64)}, {LightFlag, QColor(0xf5, 0xfa, 0xff)}}},
       {"gui.tooltip.background",
        {{DarkFlag, QColor(0x2a, 0x2c, 0x2e)}, {LightFlag, QColor(0xfa, 0xfc, 0xfe)}}},
       {"gui.tooltip.foreground",
        {{DarkFlag, QColor(0xfa, 0xfc, 0xfe)}, {LightFlag, QColor(0x2a, 0x2c, 0x2e)}}},
       {"gui.border", {{DarkFlag, QColor(0x64, 0x64, 0x64)}, {LightFlag, QColor(0x91, 0xc8, 0xfa)}}},
       {"gui.background",
        {{DarkFlag, QColor(0x25, 0x28, 0x2b)}, {LightFlag, QColor(0xff, 0xff, 0xff)}}},
       {"gui.alt_background",
        {{DarkFlag, QColor(0x1c, 0x1f, 0x24)}, {LightFlag, QColor(0xf5, 0xfa, 0xff)}}},
       {"gui.disass_selected",
        {{DarkFlag, QColor(0x1f, 0x22, 0x28)}, {LightFlag, QColor(0xff, 0xff, 0xff)}}},
       {"lineHighlight",
        {{DarkFlag, QColor(0x4d, 0x58, 0x6b, 0xb4)}, {LightFlag, QColor(0xd2, 0xd2, 0xff, 0x96)}}},
       {"wordHighlight",
        {{DarkFlag, QColor(0x34, 0x3a, 0x47, 0xff)}, {LightFlag, QColor(0xb3, 0x77, 0xd6, 0x3c)}}},
       {"highlightPC",
        {{DarkFlag, QColor(0x14, 0x30, 0x3d)}, {LightFlag, QColor(0xd2, 0xec, 0xf5)}}},
       {"gui.overview.fill",
        {{DarkFlag, QColor(0xff, 0xff, 0xff, 0x28)}, {LightFlag, QColor(0xaf, 0xd9, 0xea, 0x41)}}},
       {"gui.overview.border",
        {{DarkFlag, QColor(0x63, 0xda, 0xe8, 0x32)}, {LightFlag, QColor(0x63, 0xda, 0xe8, 0x32)}}},
       {"gui.navbar.err",
        {{DarkFlag, QColor(0x03, 0xaa, 0xf5)}, {LightFlag, QColor(0x03, 0xaa, 0xf5)}}}};

Configuration *Configuration::mPtr = nullptr;

/**
 * @brief Disassembly options saved as settings. Values are the default values.
 */
static const QHash<QString, QVariant> asmOptions
    = {{"asm.esil", false},
       {"asm.pseudo", false},
       {"asm.xrefs", true},
       {"asm.indent", false},
       {"asm.describe", false},
       {"asm.slow", true},
       {"asm.lines", true},
       {"asm.lines.fcn", true},
       {"asm.flags.offset", false},
       {"asm.emu", false},
       {"emu.str", true},
       {"asm.cmt.right", true},
       {"asm.cmt.col", 50},
       {"asm.var.summary", 4},
       {"asm.bytes", false},
       {"asm.size", false},
       {"asm.bytes.space", false},
       {"asm.lbytes", true},
       {"asm.nbytes", 10},
       {"asm.syntax", "intel"},
       {"scr.color.ops", true},
       {"scr.color.mnemonics", false},
       {"asm.ucase", false},
       {"asm.lines.bb", false},
       {"asm.capitalize", false},
       {"asm.sub.var", true},
       {"asm.sub.varonly", true},
       {"asm.tabs", 8},
       {"asm.tabs.off", 5},
       {"asm.marks", false},
       {"asm.refptr", false},
       {"asm.flags.real", true},
       {"esil.breakoninvalid", false},
       {"graph.addr", false},
       {"asm.addr", true},
       {"asm.addr.relto", ""},
       {"asm.addr.base", "16"},
       {"asm.flags.offset", false}};

Configuration::Configuration()
    : QObject()
    , nativePalette(qApp->palette())
    , defaultAppFont(qApp->font())
{
    mPtr = this;
    if (!s.isWritable()) {
        QMessageBox::critical(
            nullptr,
            tr("Critical!"),
            tr("!!! Settings are not writable! Make sure you "
               "have a write access to \"%1\"")
                .arg(s.fileName()));
    }
#ifdef IAITO_ENABLE_KSYNTAXHIGHLIGHTING
    kSyntaxHighlightingRepository = nullptr;
#endif

    // Live system theme detection: when the OS color scheme flips, re-apply
    // the Native interface theme without requiring a restart.
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (auto hints = QGuiApplication::styleHints()) {
        connect(
            hints,
            &QStyleHints::colorSchemeChanged,
            this,
            [this](Qt::ColorScheme) { onSystemColorSchemeChanged(); },
            Qt::QueuedConnection);
    }
#endif
    // Fallback for older Qt and for platforms that deliver the change via
    // a palette event rather than QStyleHints.
    if (qApp) {
        qApp->installEventFilter(this);
    }
}

bool Configuration::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange) {
        onSystemColorSchemeChanged();
    }
    return QObject::eventFilter(watched, event);
}

void Configuration::onSystemColorSchemeChanged()
{
    if (applyingTheme) {
        return;
    }
    const auto *style = IaitoStyle::instance();
    if (!style || style->theme().skin != Skin::Native) {
        return;
    }
    // Never mix a custom palette with the platform's standard palette. Keep
    // all native roles and color groups, including their Qt resolve mask.
    nativePalette = qApp->palette();
    if (nativeThemeRefreshPending) {
        return;
    }
    nativeThemeRefreshPending = true;
    // Let Qt finish propagating the system palette before re-polishing widgets.
    QTimer::singleShot(0, this, [this]() {
        nativeThemeRefreshPending = false;
        const IaitoInterfaceTheme *current = getCurrentTheme();
        if (!applyingTheme && current && current->name == QStringLiteral("Native")) {
            setInterfaceTheme(getInterfaceTheme());
        }
    });
}

Configuration *Configuration::instance()
{
    if (!mPtr)
        mPtr = new Configuration();
    return mPtr;
}

void Configuration::loadInitial()
{
    int theme = getInterfaceTheme();
    // Older mobile installs inherited the desktop Native default, which falls
    // back to desktop-looking controls on iOS. Migrate that default once while
    // preserving other themes and any subsequent choice of Native.
    if (mobilePlatform() && !s.value("mobileThemeInitialized", false).toBool()) {
        if (theme == 0) {
            theme = defaultInterfaceTheme();
        }
        s.setValue("mobileThemeInitialized", true);
    }
    setInterfaceTheme(theme);
    setColorTheme(getColorTheme());
    applySavedAsmOptions();

#ifdef IAITO_ENABLE_KSYNTAXHIGHLIGHTING
    kSyntaxHighlightingRepository = new KSyntaxHighlighting::Repository();
#endif
}

QString Configuration::getDirProjects()
{
    auto projectsDir = s.value("dir.projects").toString();
    if (projectsDir.isEmpty()) {
        projectsDir = Core()->getConfig("dir.projects");
        setDirProjects(projectsDir);
    }

    return QDir::toNativeSeparators(projectsDir);
}

void Configuration::setDirProjects(const QString &dir)
{
    s.setValue("dir.projects", QDir::toNativeSeparators(dir));
}

QString Configuration::getRecentFolder()
{
    QString recentFolder = s.value("dir.recentFolder", QDir::homePath()).toString();

    return QDir::toNativeSeparators(recentFolder);
}

void Configuration::setRecentFolder(const QString &dir)
{
    s.setValue("dir.recentFolder", QDir::toNativeSeparators(dir));
}

QString Configuration::getSha256LookupBaseUrl() const
{
    return s.value("sha256.lookupBaseUrl", "https://www.virustotal.com/gui/file/").toString();
}

void Configuration::setSha256LookupBaseUrl(const QString &url)
{
    s.setValue("sha256.lookupBaseUrl", url);
}

/**
 * @brief Configuration::setFilesTabLastClicked
 * Set the new file dialog last clicked tab
 * @param lastClicked
 */
void Configuration::setNewFileLastClicked(int lastClicked)
{
    s.setValue("newFileLastClicked", lastClicked);
}

int Configuration::getNewFileLastClicked()
{
    return s.value("newFileLastClicked").toInt();
}

void Configuration::resetAll()
{
    // Don't reset all r2 vars, that currently breaks a bunch of stuff.
    // settingsFile.remove()+loadInitials() should reset all settings
    // configurable using Iaito GUI.
    // Core()->cmdRaw("e-");

    Core()->setSettings();
    // Delete the file so no extra configuration is in it.
    QFile settingsFile(s.fileName());
    settingsFile.remove();
    s.clear();

    loadInitial();
    emit fontsUpdated();
}

/**
 * @brief get the current Locale set in Iaito's user configuration
 * @return a QLocale object describes user's current locale
 */
QLocale Configuration::getCurrLocale() const
{
    return s.value("locale", QLocale().system()).toLocale();
}

/**
 * @brief sets Iaito's locale
 * @param l - a QLocale object describes the locate to configure
 */
void Configuration::setLocale(const QLocale &l)
{
    s.setValue("locale", l);
}

/**
 * @brief set Iaito's interface language by a given locale name
 * @param language - a string represents the name of a locale language
 * @return true on success
 */
bool Configuration::setLocaleByName(const QString &language)
{
    const auto &allLocales
        = QLocale::matchingLocales(QLocale::AnyLanguage, QLocale::AnyScript, QLocale::AnyCountry);

    for (auto &it : allLocales) {
        if (QString::compare(it.nativeLanguageName(), language, Qt::CaseInsensitive) == 0) {
            setLocale(it);
            return true;
        }
    }
    return false;
}

bool Configuration::windowColorIsDark()
{
    ColorFlags currentThemeColorFlags = getCurrentTheme()->flag;
    if (currentThemeColorFlags == ColorFlags::LightFlag) {
        return false;
    } else if (currentThemeColorFlags == ColorFlags::DarkFlag) {
        return true;
    }
    return nativeWindowIsDark();
}

bool Configuration::nativeWindowIsDark()
{
    const QPalette palette = mPtr ? mPtr->nativePalette : qApp->palette();
    return palette.color(QPalette::Window).lightnessF() < 0.5;
}

static void applyInterfaceFont(const QFont &font)
{
    qApp->setFont(font);
    for (const char *cls : {"QWidget",           "QMenu",          "QMenuBar",
                            "QPushButton",       "QToolButton",    "QHeaderView",
                            "QAbstractItemView", "QListView",      "QTreeView",
                            "QTableView",        "QComboBox",      "QLineEdit",
                            "QTextEdit",         "QPlainTextEdit", "QLabel",
                            "QGroupBox",         "QTabBar",        "QTabWidget",
                            "QStatusBar",        "QToolTip",       "QCheckBox",
                            "QRadioButton",      "QSpinBox",       "QDoubleSpinBox",
                            "QAbstractSpinBox",  "QDockWidget",    "QToolBar",
                            "QDialog",           "QMainWindow"}) {
        qApp->setFont(font, cls);
    }
}

Theme Configuration::nativeEngineTheme()
{
    return Theme::fromPalette(nativePalette, nativeWindowIsDark());
}

void Configuration::loadEngineTheme(const Theme &t)
{
    QScopedValueRollback<bool> guard(applyingTheme, true);
    const QFont previousFont = getFont();
    qApp->setStyleSheet(QString());
    const QPalette p = t.toPalette(nativePalette);
    // Let Qt propagate the interface font without replacing explicit code-view fonts.
    applyInterfaceFont(t.fonts.custom ? t.fonts.base : defaultAppFont);
    if (IaitoStyle::instance()) {
        IaitoStyle::instance()->setTheme(t, p);
        m_interfaceChrome = IaitoStyle::instance()->theme().toChrome();
    } else {
        qApp->setPalette(p);
        m_interfaceChrome = t.toChrome();
    }
    if (t.skin == Skin::Native) {
        nativePalette = qApp->palette();
    }
    if (getFont() != previousFont) {
        emit fontsUpdated();
    }
}

QString Configuration::userThemesDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QDir::separator() + QStringLiteral("themes");
    QDir().mkpath(dir);
    return dir;
}

bool Configuration::isCustomInterfaceTheme(const QString &name)
{
    return QFileInfo::exists(QDir(userThemesDir()).filePath(name + QStringLiteral(".theme")));
}

bool Configuration::isValidThemeName(const QString &name)
{
    return r_name_check(name.toUtf8().constData());
}

const QStringList &Configuration::interfaceThemeVariableKeys()
{
    static const QStringList keys
        = {"palette/background",
           "palette/panel",
           "palette/surface",
           "palette/border",
           "palette/text",
           "palette/mutedText",
           "palette/accent",
           "palette/accentText"};
    return keys;
}

QHash<QString, QColor> Configuration::defaultInterfaceThemeVariables()
{
    QWidget base;
    QLineEdit edit;
    QPushButton button;
    base.ensurePolished();
    edit.ensurePolished();
    button.ensurePolished();
    const QPalette &bp = base.palette();
    return {
        {"palette/background", bp.color(QPalette::Window)},
        {"palette/panel", edit.palette().color(QPalette::Base)},
        {"palette/surface", button.palette().color(QPalette::Button)},
        {"palette/border", bp.color(QPalette::Midlight)},
        {"palette/text", bp.color(QPalette::WindowText)},
        {"palette/mutedText", bp.color(QPalette::Disabled, QPalette::WindowText)},
        {"palette/accent", bp.color(QPalette::Highlight)},
        {"palette/accentText", bp.color(QPalette::HighlightedText)}};
}

QHash<QString, QColor> Configuration::loadInterfaceThemeVariables(const QString &name)
{
    QHash<QString, QColor> vars = defaultInterfaceThemeVariables();
    QSettings
        theme(QDir(userThemesDir()).filePath(name + QStringLiteral(".theme")), QSettings::IniFormat);
    for (const QString &key : interfaceThemeVariableKeys()) {
        const QColor color(theme.value(key).toString());
        if (color.isValid()) {
            vars[key] = color;
        }
    }
    return vars;
}

void Configuration::saveInterfaceThemeVariables(
    const QString &name, const QHash<QString, QColor> &vars, bool dark)
{
    QSettings
        theme(QDir(userThemesDir()).filePath(name + QStringLiteral(".theme")), QSettings::IniFormat);
    for (const QString &key : interfaceThemeVariableKeys()) {
        const QColor c = vars.value(key);
        theme.setValue(key, c.alpha() == 255 ? c.name(QColor::HexRgb) : c.name(QColor::HexArgb));
    }
    theme.setValue(QStringLiteral("meta/dark"), dark);
    theme.setValue(QStringLiteral("meta/name"), name);
}

void Configuration::applyInterfaceVariables(const QHash<QString, QColor> &vars, bool dark)
{
    loadEngineTheme(Theme::fromChrome(vars, dark));
}

bool Configuration::interfaceThemeIsDark(const QString &name)
{
    if (isCustomInterfaceTheme(name)) {
        QSettings
            s(QDir(userThemesDir()).filePath(name + QStringLiteral(".theme")), QSettings::IniFormat);
        if (s.contains(QStringLiteral("meta/dark"))) {
            return s.value(QStringLiteral("meta/dark")).toBool();
        }
    }
    const auto &list = iaitoInterfaceThemesList();
    for (const IaitoInterfaceTheme &t : list) {
        if (t.name == name) {
            return t.flag == DarkFlag;
        }
    }
    return true;
}

bool Configuration::loadUserStylesheet(const QString &name)
{
    if (!isCustomInterfaceTheme(name)) {
        return false;
    }
    const QString path = QDir(userThemesDir()).filePath(name + QStringLiteral(".theme"));
    Theme t = Theme::fromChrome(loadInterfaceThemeVariables(name), interfaceThemeIsDark(name));
    t.applyIni(path);
    if (t.name.isEmpty()) {
        t.name = name;
    }
    loadEngineTheme(t);
    return true;
}

const QFont Configuration::getBaseFont() const
{
    if (s.contains("font")) {
        QFont font = s.value("font").value<QFont>();
        if (font.family() == QLatin1String("W95FA")) {
            font.setFamily(QStringLiteral("Iaito W95FA"));
        }
        if (font.family() == QLatin1String("Iaito W95FA")) {
            font.setStyleStrategy(QFont::NoAntialias);
            font.setHintingPreference(QFont::PreferNoHinting);
        }
        return font;
    }
    // Match Classic's compact interface text and retain normal sizing for other themes.
    const QFont interfaceFont = qApp->font();
    QFont font("Inconsolata");
    const auto *style = IaitoStyle::instance();
    if (style && style->theme().skin == Skin::Windows) {
        font.setPointSize(9);
    } else if (interfaceFont.pixelSize() > 0) {
        font.setPixelSize(interfaceFont.pixelSize());
    } else {
        font.setPointSizeF(interfaceFont.pointSizeF());
    }
    return font;
}

const QFont Configuration::getFont() const
{
    QFont font = getBaseFont();
    qreal zoom = getZoomFactor();
    if (font.pixelSize() > 0) {
        font.setPixelSize(qMax(int(font.pixelSize() * zoom), 1));
    } else {
        font.setPointSizeF(font.pointSizeF() * zoom);
    }
    return font;
}

const QFont Configuration::getSmallFont(bool zoomed) const
{
    return zoomed ? getFont() : getBaseFont();
}

void Configuration::setFont(const QFont &font)
{
    s.setValue("font", font);
    emit fontsUpdated();
}

void Configuration::resetFont()
{
    s.remove("font");
    emit fontsUpdated();
}

void Configuration::refreshFont()
{
    emit fontsUpdated();
}

qreal Configuration::getZoomFactor() const
{
    qreal fontZoom = s.value("zoomFactor", 1.0).value<qreal>();
    return qMax(fontZoom, 0.1);
}

void Configuration::setZoomFactor(qreal zoom)
{
    s.setValue("zoomFactor", qMax(zoom, 0.1));
    emit fontsUpdated();
}

Configuration::VisualNavbarLocation Configuration::getVisualNavbarLocation() const
{
    auto location = static_cast<VisualNavbarLocation>(
        s.value("visualNavbarLocation", static_cast<int>(VisualNavbarLocation::Left)).toInt());
    switch (location) {
    case VisualNavbarLocation::Top:
    case VisualNavbarLocation::Bottom:
    case VisualNavbarLocation::Left:
    case VisualNavbarLocation::Right:
    case VisualNavbarLocation::SuperTop:
    case VisualNavbarLocation::SuperBottom:
        return location;
    }
    return VisualNavbarLocation::Left;
}

void Configuration::setVisualNavbarLocation(VisualNavbarLocation location)
{
    if (getVisualNavbarLocation() == location) {
        return;
    }
    s.setValue("visualNavbarLocation", static_cast<int>(location));
    emit visualNavbarLocationChanged(location);
}

int Configuration::getVisualNavbarThickness() const
{
    int thickness = s.value("visualNavbarThickness", VisualNavbarThicknessDefault).toInt();
    return qBound(VisualNavbarThicknessMin, thickness, VisualNavbarThicknessMax);
}

void Configuration::setVisualNavbarThickness(int thickness)
{
    thickness = qBound(VisualNavbarThicknessMin, thickness, VisualNavbarThicknessMax);
    if (getVisualNavbarThickness() == thickness) {
        return;
    }

    s.setValue("visualNavbarThickness", thickness);
    emit visualNavbarThicknessChanged(thickness);
}

int Configuration::getVisualNavbarResolution() const
{
    return qBound(1, s.value("visualNavbarResolution", 1).toInt(), VisualNavbarResolutionMax);
}

void Configuration::setVisualNavbarResolution(int divisor)
{
    divisor = qBound(1, divisor, VisualNavbarResolutionMax);
    if (getVisualNavbarResolution() == divisor) {
        return;
    }
    s.setValue("visualNavbarResolution", divisor);
    emit visualNavbarResolutionChanged(divisor);
}

bool Configuration::getVisualNavbarLegendVisible() const
{
    return s.value("visualNavbarLegendVisible", true).toBool();
}

void Configuration::setVisualNavbarLegendVisible(bool visible)
{
    if (getVisualNavbarLegendVisible() == visible) {
        return;
    }
    s.setValue("visualNavbarLegendVisible", visible);
    emit visualNavbarLegendVisibleChanged(visible);
}

Configuration::MemoryScrollBarMode Configuration::getMemoryScrollBarMode() const
{
    const int mode
        = s.value("memoryScrollBarMode", static_cast<int>(MemoryScrollBarMode::Bounded)).toInt();
    switch (mode) {
    case static_cast<int>(MemoryScrollBarMode::Spring):
        return MemoryScrollBarMode::Spring;
    case static_cast<int>(MemoryScrollBarMode::Hidden):
        return MemoryScrollBarMode::Hidden;
    case static_cast<int>(MemoryScrollBarMode::Bounded):
    default:
        return MemoryScrollBarMode::Bounded;
    }
}

void Configuration::setMemoryScrollBarMode(MemoryScrollBarMode mode)
{
    if (getMemoryScrollBarMode() == mode) {
        return;
    }
    s.setValue("memoryScrollBarMode", static_cast<int>(mode));
    emit memoryScrollBarModeChanged(mode);
}

QString Configuration::getLastThemeOf(const IaitoInterfaceTheme &currInterfaceTheme) const
{
    return s.value("lastThemeOf." + currInterfaceTheme.name, Config()->getColorTheme()).toString();
}

int Configuration::getInterfaceTheme()
{
    if (s.contains("ColorPalette")) {
        return s.value("ColorPalette").toInt();
    }
    return defaultInterfaceTheme();
}

void Configuration::setInterfaceTheme(int theme)
{
    if (theme >= iaitoInterfaceThemesList().size() || theme < 0) {
        theme = 0;
    }
    s.setValue("ColorPalette", theme);

    IaitoInterfaceTheme interfaceTheme = iaitoInterfaceThemesList()[theme];

    applyingTheme = true;
    static const QSet<QString> builtinEngineThemes
        = {"Dark",
           "Midnight",
           "Light",
           "Classic",
           "Pro",
           "Pro Dark",
           "Nord",
           "High Contrast",
           "Cobalt",
           "Capsule",
           "Phosphor",
           "Tablet"};
    if (interfaceTheme.name == "Native") {
        loadEngineTheme(nativeEngineTheme());
    } else if (builtinEngineThemes.contains(interfaceTheme.name)) {
        loadEngineTheme(Theme::builtin(interfaceTheme.name));
    } else if (!loadUserStylesheet(interfaceTheme.name)) {
        loadEngineTheme(nativeEngineTheme());
    }
    if (interfaceTheme.name == QStringLiteral("Native")) {
        interfaceTheme.flag = nativeWindowIsDark() ? DarkFlag : LightFlag;
    }

    for (auto it = iaitoOptionColors.cbegin(); it != iaitoOptionColors.cend(); it++) {
        setColor(it.key(), it.value()[interfaceTheme.flag]);
    }

    // adjustColorThemeDarkness() may call setColorTheme() which emits a
    // heavy colorsUpdated() by itself; in that case we don't need to emit
    // another one. Otherwise this is a palette-only change and heavy
    // widgets can skip their content reloads.
    QString prevColorTheme = getColorTheme();
    adjustColorThemeDarkness();
    bool colorThemeChanged = (prevColorTheme != getColorTheme());
    applyingTheme = false;

    emit interfaceThemeChanged();
    if (!colorThemeChanged) {
        paletteOnlyUpdate = true;
        emit colorsUpdated();
        emit interfacePaletteUpdated();
        paletteOnlyUpdate = false;
    } else {
        // adjustColorThemeDarkness -> setColorTheme already emitted the
        // heavy colorsUpdated(). Still notify palette-only listeners.
        emit interfacePaletteUpdated();
    }
#ifdef IAITO_ENABLE_KSYNTAXHIGHLIGHTING
    emit kSyntaxHighlightingThemeChanged();
#endif
}

const IaitoInterfaceTheme *Configuration::getCurrentTheme()
{
    int i = getInterfaceTheme();
    if (i < 0 || i >= iaitoInterfaceThemesList().size()) {
        i = 0;
        setInterfaceTheme(i);
    }
    return &iaitoInterfaceThemesList()[i];
}

#ifdef IAITO_ENABLE_KSYNTAXHIGHLIGHTING
KSyntaxHighlighting::Repository *Configuration::getKSyntaxHighlightingRepository()
{
    return kSyntaxHighlightingRepository;
}

KSyntaxHighlighting::Theme Configuration::getKSyntaxHighlightingTheme()
{
    auto repo = getKSyntaxHighlightingRepository();
    if (!repo) {
        return KSyntaxHighlighting::Theme();
    }
    return repo->defaultTheme(
        getCurrentTheme()->flag & DarkFlag
            ? KSyntaxHighlighting::Repository::DefaultTheme::DarkTheme
            : KSyntaxHighlighting::Repository::DefaultTheme::LightTheme);
}
#endif

QSyntaxHighlighter *Configuration::createSyntaxHighlighter(QTextDocument *document)
{
#ifdef IAITO_ENABLE_KSYNTAXHIGHLIGHTING
    auto syntaxHighlighter = new SyntaxHighlighter(document);
    auto repo = getKSyntaxHighlightingRepository();
    if (repo) {
        syntaxHighlighter->setDefinition(repo->definitionForName("C"));
    }
    return syntaxHighlighter;
#else
    return new FallbackSyntaxHighlighter(document);
#endif
}

QString Configuration::getLogoFile()
{
    return windowColorIsDark() ? QStringLiteral(":/img/iaito-o-light.svg")
                               : QStringLiteral(":/img/iaito-o.svg");
}

/**
 * @brief Configuration::setColor sets the local Iaito configuration color
 * @param name Color Name
 * @param color The color you want to set
 */
void Configuration::setColor(const QString &name, const QColor &color)
{
    s.setValue("colors." + name, color);
}

void Configuration::setLastThemeOf(
    const IaitoInterfaceTheme &currInterfaceTheme, const QString &theme)
{
    s.setValue("lastThemeOf." + currInterfaceTheme.name, theme);
}

const QColor Configuration::getColor(const QString &name) const
{
    const auto *style = IaitoStyle::instance();
    if (style && style->theme().skin == Skin::Native) {
        const QPalette p = qApp->palette();
        if (name == QLatin1String("gui.background")) {
            return p.color(QPalette::Base);
        }
        if (name == QLatin1String("gui.alt_background")) {
            return p.color(QPalette::AlternateBase);
        }
        if (name == QLatin1String("gui.border")) {
            return p.color(QPalette::Mid);
        }
        if (name == QLatin1String("gui.tooltip.background")) {
            return p.color(QPalette::Inactive, QPalette::ToolTipBase);
        }
        if (name == QLatin1String("gui.tooltip.foreground")) {
            return p.color(QPalette::Inactive, QPalette::ToolTipText);
        }
    }
    if (name.contains(QLatin1Char('/'))) {
        return m_interfaceChrome.value(name);
    }
    if (s.contains("colors." + name)) {
        return s.value("colors." + name).value<QColor>();
    } else {
        return s.value("colors.other").value<QColor>();
    }
}

void Configuration::setColorTheme(const QString &theme)
{
    if (theme == "default") {
        Core()->cmdRaw("ecd");
        s.setValue("theme", "default");
    } else {
        Core()->cmdRaw(QStringLiteral("eco %1").arg(theme));
        s.setValue("theme", theme);
    }

    QJsonObject colorTheme = ThemeWorker().getTheme(theme).object();
    for (auto it = colorTheme.constBegin(); it != colorTheme.constEnd(); it++) {
        QJsonArray rgb = it.value().toArray();
        if (rgb.size() != 4) {
            continue;
        }
        setColor(it.key(), QColor(rgb[0].toInt(), rgb[1].toInt(), rgb[2].toInt(), rgb[3].toInt()));
    }

    // Derive highlightPC from graph.current with reduced alpha for use as background
    QColor graphCurrent = getColor("graph.current");
    if (graphCurrent.isValid()) {
        graphCurrent.setAlpha(0x30);
        setColor("highlightPC", graphCurrent);
    }

    emit colorsUpdated();
}

void Configuration::adjustColorThemeDarkness()
{
    bool windowIsDark = windowColorIsDark();
    int windowDarkness = windowIsDark ? DarkFlag : LightFlag;
    int currentColorThemeDarkness = colorThemeDarkness(getColorTheme());

    if ((currentColorThemeDarkness & windowDarkness) == 0) {
        setColorTheme(windowIsDark ? DEFAULT_DARK_COLOR_THEME : DEFAULT_LIGHT_COLOR_THEME);
    }
}

int Configuration::colorThemeDarkness(const QString &colorTheme) const
{
    auto flags = relevantThemes.find(colorTheme);
    if (flags != relevantThemes.end()) {
        return static_cast<int>(*flags);
    }
    return DarkFlag | LightFlag;
}

bool Configuration::getAddressRangeSelectionSyncEnabled() const
{
    return s.value(AddressRangeSelectionSyncKey, true).toBool();
}

void Configuration::setAddressRangeSelectionSyncEnabled(bool enabled)
{
    if (getAddressRangeSelectionSyncEnabled() == enabled) {
        return;
    }

    s.setValue(AddressRangeSelectionSyncKey, enabled);
    if (!enabled) {
        Core()->clearAddressRangeSelection();
    }
    emit addressRangeSelectionSyncEnabledChanged(enabled);
}

void Configuration::resetToDefaultAsmOptions()
{
    for (auto it = asmOptions.cbegin(); it != asmOptions.cend(); it++) {
        setConfig(it.key(), it.value());
    }
    setDisassemblyJumpLineAdaptiveSpacingEnabled(true);
    setDisassemblyJumpLineSpacing(DisassemblyJumpLineSpacingDefault);
    setDisassemblyJumpLineFollowEnabled(false);
    setDisassemblyJumpLineCurvesEnabled(false);
    setDisassemblyJumpLineColorizeEnabled(true);
}

bool Configuration::getDisassemblyJumpLineAdaptiveSpacingEnabled() const
{
    return s.value(DisassemblyJumpLineAdaptiveSpacingKey, true).toBool();
}

void Configuration::setDisassemblyJumpLineAdaptiveSpacingEnabled(bool enabled)
{
    s.setValue(DisassemblyJumpLineAdaptiveSpacingKey, enabled);
}

int Configuration::getDisassemblyJumpLineSpacing() const
{
    const int spacing
        = s.value(DisassemblyJumpLineSpacingKey, DisassemblyJumpLineSpacingDefault).toInt();
    return spacing > 0 ? spacing : DisassemblyJumpLineSpacingDefault;
}

void Configuration::setDisassemblyJumpLineSpacing(int spacing)
{
    s.setValue(DisassemblyJumpLineSpacingKey, qMax(1, spacing));
}

bool Configuration::getDisassemblyJumpLineFollowEnabled() const
{
    return s.value(DisassemblyJumpLineFollowKey, false).toBool();
}

void Configuration::setDisassemblyJumpLineFollowEnabled(bool enabled)
{
    s.setValue(DisassemblyJumpLineFollowKey, enabled);
}

bool Configuration::getDisassemblyJumpLineCurvesEnabled() const
{
    return s.value(DisassemblyJumpLineCurvesKey, false).toBool();
}

void Configuration::setDisassemblyJumpLineCurvesEnabled(bool enabled)
{
    s.setValue(DisassemblyJumpLineCurvesKey, enabled);
}

bool Configuration::getDisassemblyJumpLineColorizeEnabled() const
{
    return s.value(DisassemblyJumpLineColorizeKey, true).toBool();
}

void Configuration::setDisassemblyJumpLineColorizeEnabled(bool enabled)
{
    s.setValue(DisassemblyJumpLineColorizeKey, enabled);
}

int Configuration::getDisassemblyJumpLineWidth() const
{
    if (mobilePlatform()) {
        return qMax(1, s.value(MobileDisassemblyJumpLineWidthKey, 48).toInt());
    }
    return qMax(1, s.value(DisassemblyJumpLineWidthKey, 100).toInt());
}

void Configuration::setDisassemblyJumpLineWidth(int width)
{
    s.setValue(
        mobilePlatform() ? MobileDisassemblyJumpLineWidthKey : DisassemblyJumpLineWidthKey,
        qMax(1, width));
}

void Configuration::applySavedAsmOptions()
{
    if (!s.contains("scr.color.mnemonics") && s.contains("scr.color.ops")) {
        s.setValue("scr.color.mnemonics", s.value("scr.color.ops"));
        s.remove("scr.color.ops");
    }
    for (auto it = asmOptions.cbegin(); it != asmOptions.cend(); it++) {
        Core()->setConfig(it.key(), s.value(it.key(), it.value()));
    }
}

QList<QPair<QString, QString>> Configuration::getAddrBaseOptions()
{
    QStringList values = Core()->cmdList("e asm.addr.base=?");
    values.removeAll(QString());
    if (values.isEmpty()) {
        values = QStringList{"8", "10", "16", "36"};
    }
    QList<QPair<QString, QString>> options;
    for (const QString &v : values) {
        QString label = v;
        if (v == QLatin1String("8")) {
            label = tr("Octal (8)");
        } else if (v == QLatin1String("10")) {
            label = tr("Decimal (10)");
        } else if (v == QLatin1String("16")) {
            label = tr("Hexadecimal (16)");
        } else if (v == QLatin1String("36")) {
            label = tr("Base36 (36)");
        }
        options.append({v, label});
    }
    return options;
}

QString Configuration::getAddrBase()
{
    QString base = getConfigString("asm.addr.base");
    return base.isEmpty() ? QStringLiteral("16") : base;
}

void Configuration::setAddrBase(const QString &base)
{
    setConfig("asm.addr.base", base);
}

const QList<IaitoInterfaceTheme> &Configuration::iaitoInterfaceThemesList()
{
    static QList<IaitoInterfaceTheme> list;
    list
        = {{"Native", Configuration::nativeWindowIsDark() ? DarkFlag : LightFlag},
           {"Dark", DarkFlag},
           {"Midnight", DarkFlag},
           {"Light", LightFlag},
           {"Classic", LightFlag},
           {"Pro", LightFlag},
           {"Pro Dark", DarkFlag},
           {"Nord", DarkFlag},
           {"High Contrast", DarkFlag},
           {"Cobalt", DarkFlag},
           {"Capsule", LightFlag},
           {"Phosphor", DarkFlag},
           {"Tablet", LightFlag}};
    const auto userThemes
        = QDir(userThemesDir())
              .entryInfoList(QStringList{QStringLiteral("*.theme")}, QDir::Files, QDir::Name);
    for (const QFileInfo &fi : userThemes) {
        const bool dark = QSettings(fi.absoluteFilePath(), QSettings::IniFormat)
                              .value("meta/dark", true)
                              .toBool();
        list.append({fi.completeBaseName(), dark ? DarkFlag : LightFlag});
    }
    return list;
}

QVariant Configuration::getConfigVar(const QString &key)
{
    QHash<QString, QVariant>::const_iterator it = asmOptions.find(key);
    if (it != asmOptions.end()) {
        switch (variantTypeId(it.value())) {
        case QMetaType::Bool:
            return Core()->getConfigb(key);
        case QMetaType::Int:
            return Core()->getConfigi(key);
        default:
            return Core()->getConfig(key);
        }
    }
    return QVariant();
}

bool Configuration::getConfigBool(const QString &key)
{
    return getConfigVar(key).toBool();
}

int Configuration::getConfigInt(const QString &key)
{
    return getConfigVar(key).toInt();
}

QString Configuration::getConfigString(const QString &key)
{
    return getConfigVar(key).toString();
}

/**
 * @brief Configuration::setConfig
 * Set radare2 configuration value (e.g. "asm.lines")
 * @param key
 * @param value
 */
void Configuration::setConfig(const QString &key, const QVariant &value)
{
    if (asmOptions.contains(key)) {
        s.setValue(key, value);
    }

    Core()->setConfig(key, value);
}

/**
 * @brief this function will gather and return available translation for Iaito
 * @return a list of all available translations
 */
QStringList Configuration::getAvailableTranslations()
{
    const auto &trDirs = Iaito::getTranslationsDirectories();
    QSet<QString> fileNamesSet;
    for (const auto &trDir : trDirs) {
        QDir dir(trDir);
        if (!dir.exists()) {
            continue;
        }
        const QStringList &currTrFileNames
            = dir.entryList(QStringList("iaito_*.qm"), QDir::Files, QDir::Name);
        for (const auto &trFile : currTrFileNames) {
            fileNamesSet << trFile;
        }
    }

    QStringList fileNames = fileNamesSet.values();
    std::sort(fileNames.begin(), fileNames.end());
    QStringList languages;
    QString currLanguageName;
    auto allLocales
        = QLocale::matchingLocales(QLocale::AnyLanguage, QLocale::AnyScript, QLocale::AnyCountry);

    for (auto i : fileNames) {
        QString localeName = i.mid(sizeof("iaito_") - 1, 2);
        for (auto j : allLocales) {
            if (j.name().startsWith(localeName)) {
                currLanguageName = j.nativeLanguageName();
                if (currLanguageName == nullptr) {
                    break;
                }
                currLanguageName = currLanguageName.at(0).toUpper()
                                   + currLanguageName.right(currLanguageName.length() - 1);
                languages << currLanguageName;
                break;
            }
        }
    }
    return languages << QLatin1String("English");
}

/**
 * @brief check if this is the first time Iaito's is executed on this computer
 * @return true if this is first execution; otherwise returns false.
 */
bool Configuration::isFirstExecution()
{
    // check if a variable named firstExecution existed in the configuration
    if (s.contains("firstExecution")) {
        return false;
    } else {
        s.setValue("firstExecution", false);
        return true;
    }
}

QString Configuration::getSelectedDecompiler()
{
    return s.value("selectedDecompiler").toString();
}

void Configuration::setSelectedDecompiler(const QString &id)
{
    s.setValue("selectedDecompiler", id);
}

bool Configuration::getDecompilerAutoRefreshEnabled()
{
    return s.value("decompilerAutoRefresh", true).toBool();
}

void Configuration::setDecompilerAutoRefreshEnabled(bool enabled)
{
    s.setValue("decompilerAutoRefresh", enabled);
}

bool Configuration::getDecompilerRunInBackground()
{
    // Disabled by default: run synchronously (safer)
    return s.value("decompilerRunInBackground", false).toBool();
}

void Configuration::setDecompilerRunInBackground(bool enabled)
{
    s.setValue("decompilerRunInBackground", enabled);
}

bool Configuration::getDecompilerShowAddresses()
{
    return s.value("decompilerShowAddresses", false).toBool();
}

void Configuration::setDecompilerShowAddresses(bool enabled)
{
    s.setValue("decompilerShowAddresses", enabled);
}

bool Configuration::getDecompilerShowDisassembly()
{
    return s.value("decompilerShowDisassembly", false).toBool();
}

void Configuration::setDecompilerShowDisassembly(bool enabled)
{
    s.setValue("decompilerShowDisassembly", enabled);
}

bool Configuration::getBitmapTransparentState()
{
    return s.value("bitmapGraphExportTransparency", false).value<bool>();
}

double Configuration::getBitmapExportScaleFactor()
{
    return s.value("bitmapGraphExportScale", 1.0).value<double>();
}

void Configuration::setBitmapTransparentState(bool inputValueGraph)
{
    s.setValue("bitmapGraphExportTransparency", inputValueGraph);
}

void Configuration::setBitmapExportScaleFactor(double inputValueGraph)
{
    s.setValue("bitmapGraphExportScale", inputValueGraph);
}

void Configuration::setGraphSpacing(QPoint blockSpacing, QPoint edgeSpacing)
{
    s.setValue("graph.blockSpacing", blockSpacing);
    s.setValue("graph.edgeSpacing", edgeSpacing);
}

QPoint Configuration::getGraphBlockSpacing()
{
    return s.value("graph.blockSpacing", QPoint(20, 40)).value<QPoint>();
}

QPoint Configuration::getGraphEdgeSpacing()
{
    return s.value("graph.edgeSpacing", QPoint(10, 10)).value<QPoint>();
}

void Configuration::setOutputRedirectionEnabled(bool enabled)
{
    this->outputRedirectEnabled = enabled;
}

bool Configuration::getOutputRedirectionEnabled() const
{
    return outputRedirectEnabled;
}

bool Configuration::getGraphBlockEntryOffset()
{
    return s.value("graphBlockEntryOffset", true).value<bool>();
}

void Configuration::setGraphBlockEntryOffset(bool enabled)
{
    s.setValue("graphBlockEntryOffset", enabled);
}

bool Configuration::getGraphBlockShadow()
{
    return s.value("graphBlockShadow", true).value<bool>();
}

void Configuration::setGraphBlockShadow(bool enabled)
{
    s.setValue("graphBlockShadow", enabled);
}
