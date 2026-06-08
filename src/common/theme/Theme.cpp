#include "Theme.h"

#include <QApplication>
#include <QFile>
#include <QSettings>

QColor ColorGroup::forState(QStyle::State state) const
{
    if (!(state & QStyle::State_Enabled)) {
        return disabled;
    }
    if (state & QStyle::State_Sunken) {
        return pressed;
    }
    if (state & QStyle::State_On) {
        return checked;
    }
    if (state & QStyle::State_MouseOver) {
        return hovered;
    }
    if (state & QStyle::State_HasFocus) {
        return focused;
    }
    return normal;
}

static ColorGroup deriveGroup(const QColor &base, bool dark)
{
    ColorGroup g;
    g.normal = base;
    g.disabled = base;
    if (dark) {
        g.hovered = base.lighter(120);
        g.pressed = base.darker(115);
        g.checked = base.lighter(135);
        g.focused = base.lighter(110);
    } else {
        g.hovered = base.darker(108);
        g.pressed = base.darker(120);
        g.checked = base.darker(112);
        g.focused = base.darker(105);
    }
    return g;
}

Theme Theme::fromChrome(const QHash<QString, QColor> &v, bool dark)
{
    Theme t;
    t.dark = dark;
    t.background = deriveGroup(v.value("palette/background"), dark);
    t.panel = deriveGroup(v.value("palette/panel"), dark);
    t.surface = deriveGroup(v.value("palette/surface"), dark);
    t.border = deriveGroup(v.value("palette/border"), dark);
    t.text = deriveGroup(v.value("palette/text"), dark);
    t.mutedText = deriveGroup(v.value("palette/mutedText"), dark);
    t.accent = deriveGroup(v.value("palette/accent"), dark);
    t.accentText = deriveGroup(v.value("palette/accentText"), dark);

    const QColor muted = v.value("palette/mutedText");
    t.text.disabled = muted;
    t.accentText.disabled = muted;
    t.surface.disabled = t.surface.normal;
    t.border.disabled = muted;

    const QColor face = t.surface.normal;
    t.bevelHighlight = face.lighter(160);
    t.bevelLight = face.lighter(115);
    t.bevelDark = face.darker(160);
    t.bevelShadow = face.darker(320);

    t.tooltipBase = t.surface.normal;
    t.tooltipText = t.text.normal;
    return t;
}

Theme Theme::loadBuiltin(const QString &name)
{
    const QString path = QStringLiteral(":/themes/engine/") + name + QStringLiteral(".theme");
    if (QFile::exists(path)) {
        return load(path, true);
    }
    QHash<QString, QColor> v
        = {{"palette/background", QColor(0x31, 0x36, 0x3b)},
           {"palette/panel", QColor(0x23, 0x26, 0x29)},
           {"palette/surface", QColor(0x31, 0x36, 0x3b)},
           {"palette/border", QColor(0x4f, 0x56, 0x5c)},
           {"palette/text", QColor(0xef, 0xf0, 0xf1)},
           {"palette/mutedText", QColor(0x80, 0x80, 0x80)},
           {"palette/accent", QColor(0x3d, 0xae, 0xe9)},
           {"palette/accentText", QColor(0xff, 0xff, 0xff)}};
    Theme t = fromChrome(v, true);
    t.name = name;
    return t;
}

Theme Theme::fromPalette(const QPalette &p, bool dark)
{
    QHash<QString, QColor> v
        = {{"palette/background", p.color(QPalette::Active, QPalette::Window)},
           {"palette/panel", p.color(QPalette::Active, QPalette::Base)},
           {"palette/surface", p.color(QPalette::Active, QPalette::Button)},
           {"palette/border", p.color(QPalette::Active, QPalette::Mid)},
           {"palette/text", p.color(QPalette::Active, QPalette::WindowText)},
           {"palette/mutedText", p.color(QPalette::Disabled, QPalette::WindowText)},
           {"palette/accent", p.color(QPalette::Active, QPalette::Highlight)},
           {"palette/accentText", p.color(QPalette::Active, QPalette::HighlightedText)}};
    Theme t = fromChrome(v, dark);
    t.name = QStringLiteral("Native");
    return t;
}

Theme Theme::iaitoDefault()
{
    if (qApp) {
        const QPalette p = qApp->palette();
        const bool dark = p.color(QPalette::Active, QPalette::Base).lightnessF()
                          < p.color(QPalette::Active, QPalette::WindowText).lightnessF();
        return fromPalette(p, dark);
    }
    return loadBuiltin(QStringLiteral("Dark"));
}

Theme Theme::builtin(const QString &name)
{
    return loadBuiltin(name);
}

QHash<QString, QColor> Theme::toChrome() const
{
    return {
        {"palette/background", background.normal},
        {"palette/panel", panel.normal},
        {"palette/surface", surface.normal},
        {"palette/border", border.normal},
        {"palette/text", text.normal},
        {"palette/mutedText", mutedText.normal},
        {"palette/accent", accent.normal},
        {"palette/accentText", accentText.normal}};
}

QPalette Theme::toPalette(const QPalette &base) const
{
    QPalette p = base;
    struct Map
    {
        QPalette::ColorRole role;
        const ColorGroup *group;
    };
    const Map table[]
        = {{QPalette::Window, &background},
           {QPalette::WindowText, &text},
           {QPalette::Base, &panel},
           {QPalette::AlternateBase, &panel},
           {QPalette::Text, &text},
           {QPalette::Button, &surface},
           {QPalette::ButtonText, &text},
           {QPalette::Highlight, &accent},
           {QPalette::HighlightedText, &accentText},
           {QPalette::PlaceholderText, &mutedText}};
    for (const Map &m : table) {
        p.setColor(QPalette::Active, m.role, m.group->normal);
        p.setColor(QPalette::Inactive, m.role, m.group->normal);
        p.setColor(QPalette::Disabled, m.role, m.group->disabled);
    }
    for (QPalette::ColorGroup g : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        p.setColor(g, QPalette::ToolTipBase, tooltipBase);
        p.setColor(g, QPalette::ToolTipText, tooltipText);
    }
    return p;
}

namespace {

const struct
{
    const char *key;
    int ThemeMetrics::*field;
} kMetricFields[] = {
    {"metrics/radius", &ThemeMetrics::borderRadius},
    {"metrics/lineWidth", &ThemeMetrics::borderWidth},
    {"metrics/focusWidth", &ThemeMetrics::focusWidth},
    {"metrics/spacing", &ThemeMetrics::spacing},
    {"metrics/iconSize", &ThemeMetrics::iconSize},
    {"metrics/sliderGroove", &ThemeMetrics::sliderGrooveHeight},
    {"controls/height", &ThemeMetrics::controlHeight},
    {"controls/checkbox", &ThemeMetrics::checkBoxSize},
    {"controls/checkboxRadius", &ThemeMetrics::checkBoxRadius},
    {"controls/menuPadding", &ThemeMetrics::menuItemPadding},
    {"scrollbar/thickness", &ThemeMetrics::scrollBarThickness},
    {"tabs/border", &ThemeMetrics::tabBorderWidth},
    {"tabs/indicator", &ThemeMetrics::tabIndicatorWidth},
};

const struct
{
    const char *key;
    QColor Theme::*field;
} kOverrideColors[] = {
    {"bevel/highlight", &Theme::bevelHighlight},
    {"bevel/light", &Theme::bevelLight},
    {"bevel/dark", &Theme::bevelDark},
    {"bevel/shadow", &Theme::bevelShadow},
    {"tooltip/base", &Theme::tooltipBase},
    {"tooltip/text", &Theme::tooltipText},
};

QString skinToString(Skin s)
{
    return s == Skin::Bevel ? QStringLiteral("bevel") : QStringLiteral("modern");
}

Skin skinFromString(const QString &s)
{
    return s == QLatin1String("bevel") ? Skin::Bevel : Skin::Modern;
}

QString chromeToString(ChromeStyle c)
{
    return c == ChromeStyle::Accent ? QStringLiteral("accent") : QStringLiteral("flat");
}

ChromeStyle chromeFromString(const QString &s)
{
    return s == QLatin1String("accent") ? ChromeStyle::Accent : ChromeStyle::Flat;
}

QString hintingToString(int h)
{
    switch (h) {
    case QFont::PreferNoHinting:
        return QStringLiteral("none");
    case QFont::PreferVerticalHinting:
        return QStringLiteral("vertical");
    case QFont::PreferFullHinting:
        return QStringLiteral("full");
    default:
        return QStringLiteral("default");
    }
}

QFont::HintingPreference hintingFromString(const QString &s)
{
    if (s == QLatin1String("none"))
        return QFont::PreferNoHinting;
    if (s == QLatin1String("vertical"))
        return QFont::PreferVerticalHinting;
    if (s == QLatin1String("full"))
        return QFont::PreferFullHinting;
    if (s == QLatin1String("default"))
        return QFont::PreferDefaultHinting;
    bool ok = false;
    const int v = s.toInt(&ok);
    return ok ? QFont::HintingPreference(v) : QFont::PreferDefaultHinting;
}

QString colorString(const QColor &c)
{
    return c.alpha() == 255 ? c.name(QColor::HexRgb) : c.name(QColor::HexArgb);
}

} // namespace

void Theme::applyIni(const QString &path)
{
    QSettings s(path, QSettings::IniFormat);
    for (const auto &e : kMetricFields) {
        metrics.*e.field = s.value(e.key, metrics.*e.field).toInt();
    }
    for (const auto &e : kOverrideColors) {
        const QColor c(s.value(e.key).toString());
        if (c.isValid()) {
            this->*e.field = c;
        }
    }
    skin = skinFromString(s.value("meta/skin", skinToString(skin)).toString());
    chromeStyle = chromeFromString(s.value("meta/chrome", chromeToString(chromeStyle)).toString());
    name = s.value("meta/name", name).toString();

    const QString family = s.value("font/family").toString();
    if (!family.isEmpty()) {
        QFont f(family);
        const QString size = s.value("font/size").toString();
        if (size.endsWith(QLatin1String("px"))) {
            f.setPixelSize(size.left(size.size() - 2).toInt());
        } else if (size.endsWith(QLatin1String("pt"))) {
            f.setPointSizeF(size.left(size.size() - 2).toDouble());
        }
        f.setHintingPreference(hintingFromString(s.value("font/hinting").toString()));
        fonts.base = f;
        fonts.custom = true;
    }
}

Theme Theme::load(const QString &path, bool dark)
{
    QSettings s(path, QSettings::IniFormat);
    QHash<QString, QColor> vars = Theme().toChrome();
    const QStringList keys = vars.keys();
    for (const QString &key : keys) {
        const QColor c(s.value(key).toString());
        if (c.isValid()) {
            vars[key] = c;
        }
    }
    Theme t = fromChrome(vars, s.value("meta/dark", dark).toBool());
    t.applyIni(path);
    return t;
}

void Theme::save(const QString &path) const
{
    QSettings s(path, QSettings::IniFormat);
    const QHash<QString, QColor> chrome = toChrome();
    for (auto it = chrome.constBegin(); it != chrome.constEnd(); ++it) {
        s.setValue(it.key(), colorString(it.value()));
    }
    s.setValue("bevel/highlight", colorString(bevelHighlight));
    s.setValue("bevel/light", colorString(bevelLight));
    s.setValue("bevel/dark", colorString(bevelDark));
    s.setValue("bevel/shadow", colorString(bevelShadow));
    s.setValue("tooltip/base", colorString(tooltipBase));
    s.setValue("tooltip/text", colorString(tooltipText));
    for (const auto &e : kMetricFields) {
        s.setValue(e.key, metrics.*e.field);
    }
    s.setValue("meta/name", name);
    s.setValue("meta/dark", dark);
    s.setValue("meta/skin", skinToString(skin));
    s.setValue("meta/chrome", chromeToString(chromeStyle));
    if (fonts.custom) {
        s.setValue("font/family", fonts.base.family());
        s.setValue(
            "font/size",
            fonts.base.pixelSize() > 0
                ? QString::number(fonts.base.pixelSize()) + QStringLiteral("px")
                : QString::number(fonts.base.pointSizeF()) + QStringLiteral("pt"));
        s.setValue("font/hinting", hintingToString(fonts.base.hintingPreference()));
    }
}
