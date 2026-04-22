#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include <core/Iaito.h>
#include <QFont>
#include <QSettings>

#define Config() (Configuration::instance())
#define ConfigColor(x) Config()->getColor(x)

#ifdef IAITO_ENABLE_KSYNTAXHIGHLIGHTING
namespace KSyntaxHighlighting {
class Repository;
class Theme;
} // namespace KSyntaxHighlighting
#endif

class QSyntaxHighlighter;
class QTextDocument;

enum ColorFlags {
    LightFlag = 1,
    DarkFlag = 2,
};

struct IaitoInterfaceTheme
{
    QString name;
    ColorFlags flag;
};

class IAITO_EXPORT Configuration : public QObject
{
    Q_OBJECT
public:
    enum class VisualNavbarLocation { Top = 0, Bottom, Left, Right, SuperTop, SuperBottom };
    Q_ENUM(VisualNavbarLocation)

private:
    QPalette nativePalette;
    QSettings s;
    static Configuration *mPtr;

#ifdef IAITO_ENABLE_KSYNTAXHIGHLIGHTING
    KSyntaxHighlighting::Repository *kSyntaxHighlightingRepository;
#endif
    bool outputRedirectEnabled = true;
    // True while the most recent colorsUpdated signal represents only an
    // interface palette refresh (no radare2 color theme change). Heavy slots
    // can use isInterfacePaletteOnlyUpdate() to skip expensive content reloads.
    bool paletteOnlyUpdate = false;
    // Reentrancy guard to ignore palette change notifications triggered by us.
    bool applyingTheme = false;

    Configuration();
    // Colors
    void loadBaseThemeNative();
    void loadBaseThemeDark();
    void loadNativeStylesheet();
    void loadLightStylesheet();
    void loadDarkStylesheet();
    void loadMidnightStylesheet();

    void onSystemColorSchemeChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

    // Asm Options
    void applySavedAsmOptions();

public:
    static const QList<IaitoInterfaceTheme> &iaitoInterfaceThemesList();
    static const QHash<QString, ColorFlags> relevantThemes;
    static const QHash<QString, QHash<ColorFlags, QColor>> iaitoOptionColors;

    // Functions
    static Configuration *instance();

    void loadInitial();

    void resetAll();

    // Languages
    QLocale getCurrLocale() const;
    void setLocale(const QLocale &l);
    bool setLocaleByName(const QString &language);
    QStringList getAvailableTranslations();

    // Fonts

    /**
     * @brief Gets the configured font set by the font selection box
     * @return the configured font
     */
    const QFont getBaseFont() const;

    /**
     * @brief Gets the configured font with the point size adjusted by the
     * configured zoom level (minimum of 10%)
     * @return the configured font size adjusted by zoom level
     */
    const QFont getFont() const;

    /**
     * @brief Gets a smaller variant of the configured font, suitable for
     * list/tree views. Uses 85% of the normal font size.
     * @return the configured font at reduced size, adjusted by zoom level
     */
    const QFont getSmallFont() const;
    void setFont(const QFont &font);
    qreal getZoomFactor() const;
    void setZoomFactor(qreal zoom);
    VisualNavbarLocation getVisualNavbarLocation() const;
    void setVisualNavbarLocation(VisualNavbarLocation location);
    int getVisualNavbarThickness() const;
    void setVisualNavbarThickness(int thickness);
    bool getVisualNavbarUseThemeColors() const;
    void setVisualNavbarUseThemeColors(bool enabled);

    // Colors
    bool windowColorIsDark();
    static bool nativeWindowIsDark();
    /**
     * @brief Whether the current colorsUpdated signal only represents an
     * interface palette change, in which case widgets can skip costly
     * content reloads and just repaint.
     */
    bool isInterfacePaletteOnlyUpdate() const { return paletteOnlyUpdate; }
    void setLastThemeOf(const IaitoInterfaceTheme &currInterfaceTheme, const QString &theme);
    QString getLastThemeOf(const IaitoInterfaceTheme &currInterfaceTheme) const;
    void setInterfaceTheme(int theme);
    int getInterfaceTheme() { return s.value("ColorPalette", 0).toInt(); }

    const IaitoInterfaceTheme *getCurrentTheme();

#ifdef IAITO_ENABLE_KSYNTAXHIGHLIGHTING
    KSyntaxHighlighting::Repository *getKSyntaxHighlightingRepository();
    KSyntaxHighlighting::Theme getKSyntaxHighlightingTheme();
#endif
    QSyntaxHighlighter *createSyntaxHighlighter(QTextDocument *document);

    QString getDirProjects();
    void setDirProjects(const QString &dir);

    QString getRecentFolder();
    void setRecentFolder(const QString &dir);

    void setNewFileLastClicked(int lastClicked);
    int getNewFileLastClicked();

    // Images
    QString getLogoFile();

    // Asm Options
    void resetToDefaultAsmOptions();

    QString getColorTheme() const { return s.value("theme", "iaito").toString(); }
    void setColorTheme(const QString &theme);
    /**
     * @brief Change current color theme if it doesn't much native theme's
     * darkness.
     */
    void adjustColorThemeDarkness();
    int colorThemeDarkness(const QString &colorTheme) const;

    void setColor(const QString &name, const QColor &color);
    const QColor getColor(const QString &name) const;

    /**
     * @brief Get the value of a config var either from r2 or settings,
     * depending on the key.
     */
    QVariant getConfigVar(const QString &key);
    bool getConfigBool(const QString &key);
    int getConfigInt(const QString &key);
    QString getConfigString(const QString &key);

    /**
     * @brief Set the value of a config var either to r2 or settings, depending
     * on the key.
     */
    void setConfig(const QString &key, const QVariant &value);
    bool isFirstExecution();

    /**
     * @return id of the last selected decompiler (see
     * IaitoCore::getDecompilerById)
     */
    QString getSelectedDecompiler();
    void setSelectedDecompiler(const QString &id);

    bool getDecompilerAutoRefreshEnabled();
    void setDecompilerAutoRefreshEnabled(bool enabled);

    // Decompiler execution mode
    bool getDecompilerRunInBackground();
    void setDecompilerRunInBackground(bool enabled);

    void enableDecompilerAnnotationHighlighter(bool useDecompilerHighlighter);
    bool isDecompilerAnnotationHighlighterEnabled();

    // Graph
    int getGraphBlockMaxChars() const { return s.value("graph.maxcols", 100).toInt(); }
    void setGraphBlockMaxChars(int ch) { s.setValue("graph.maxcols", ch); }

    /**
     * @brief Getters and setters for the transaparent option state and scale
     * factor for bitmap graph exports.
     */
    bool getBitmapTransparentState();
    double getBitmapExportScaleFactor();
    void setBitmapTransparentState(bool inputValueGraph);
    void setBitmapExportScaleFactor(double inputValueGraph);
    void setGraphSpacing(QPoint blockSpacing, QPoint edgeSpacing);
    QPoint getGraphBlockSpacing();
    QPoint getGraphEdgeSpacing();

    /**
     * @brief Gets whether the entry offset of each block has to be displayed or
     * not
     * @return true if the entry offset has to be displayed, false otherwise
     */
    bool getGraphBlockEntryOffset();

    /**
     * @brief Enable or disable the displaying of the entry offset in each graph
     * block
     * @param enabled set this to true for displaying the entry offset in each
     * graph block, false otherwise
     */
    void setGraphBlockEntryOffset(bool enabled);

    bool getGraphBlockShadow();
    void setGraphBlockShadow(bool enabled);

    /**
     * @brief Enable or disable Iaito output redirection.
     * Output redirection state can only be changed early during Iaito
     * initialization. Changing it later will have no effect
     * @param enabled set this to false for disabling output redirection
     */
    void setOutputRedirectionEnabled(bool enabled);
    bool getOutputRedirectionEnabled() const;

public slots:
    void refreshFont();
signals:
    void fontsUpdated();
    void colorsUpdated();
    /**
     * @brief Emitted in addition to colorsUpdated when only the interface
     * palette changed (no radare2 color theme switch). Listeners can use
     * this as a lightweight hint to repaint without re-fetching content.
     */
    void interfacePaletteUpdated();
    void interfaceThemeChanged();
    void visualNavbarLocationChanged(VisualNavbarLocation location);
    void visualNavbarThicknessChanged(int thickness);
#ifdef IAITO_ENABLE_KSYNTAXHIGHLIGHTING
    void kSyntaxHighlightingThemeChanged();
#endif
};

#endif // CONFIGURATION_H
