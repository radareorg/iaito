#ifndef IAITO_THEME_H
#define IAITO_THEME_H

#include <QColor>
#include <QFont>
#include <QHash>
#include <QPalette>
#include <QString>
#include <QStyle>

struct ColorGroup
{
    QColor normal;
    QColor hovered;
    QColor pressed;
    QColor disabled;
    QColor checked;
    QColor focused;

    QColor forState(QStyle::State state) const;
};

struct ThemeMetrics
{
    int borderRadius = 4;
    int checkBoxRadius = 4;
    int borderWidth = 1;
    int focusWidth = 2;
    int spacing = 6;
    int menuItemPadding = 6;
    int iconSize = 16;
    int controlHeight = 24;
    int checkBoxSize = 16;
    int scrollBarThickness = 12;
    int sliderGrooveHeight = 4;
    int tabBorderWidth = 0;
    int tabIndicatorWidth = 2;
};

struct ThemeFonts
{
    QFont base;
    bool custom = false;
};

enum class ChromeStyle {
    Flat,
    Accent,
};

enum class Skin {
    Modern,
    Bevel,
};

class Theme
{
public:
    ColorGroup background;
    ColorGroup panel;
    ColorGroup surface;
    ColorGroup border;
    ColorGroup text;
    ColorGroup mutedText;
    ColorGroup accent;
    ColorGroup accentText;

    QColor bevelHighlight;
    QColor bevelLight;
    QColor bevelDark;
    QColor bevelShadow;

    QColor tooltipBase;
    QColor tooltipText;

    ThemeMetrics metrics;
    ThemeFonts fonts;
    ChromeStyle chromeStyle = ChromeStyle::Flat;
    Skin skin = Skin::Modern;
    bool dark = true;
    QString name;

    static Theme fromChrome(const QHash<QString, QColor> &vars, bool dark);
    static Theme fromPalette(const QPalette &palette, bool dark);
    static Theme loadBuiltin(const QString &name);
    static Theme iaitoDefault();
    static Theme builtin(const QString &name);
    QHash<QString, QColor> toChrome() const;

    QPalette toPalette(const QPalette &base) const;

    static Theme load(const QString &path, bool dark);
    void save(const QString &path) const;
    void applyIni(const QString &path);
};

#endif
