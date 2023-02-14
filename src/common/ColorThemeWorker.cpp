#include "ColorThemeWorker.h"

#include <QDir>
#include <QFile>
#include <QColor>
#include <QJsonArray>
#include <QStandardPaths>
#include <QRegularExpression>

#include "common/Configuration.h"

const QStringList ColorThemeWorker::cutterSpecificOptions = {
    "wordHighlight",
    "lineHighlight",
    "gui.main",
    "gui.imports",
    "highlightPC",
    "gui.navbar.err",
    "gui.navbar.seek",
    "gui.navbar.pc",
    "gui.navbar.sym",
    "gui.dataoffset",
    "gui.navbar.code",
    "gui.navbar.empty",
    "angui.navbar.str",
    "gui.disass_selected",
    "gui.breakpoint_background",
    "gui.overview.node",
    "gui.overview.fill",
    "gui.overview.border",
    "gui.border",
    "gui.background",
    "gui.alt_background",
    "gui.disass_selected"
};

const QStringList ColorThemeWorker::radare2UnusedOptions = {
    "linehl",
    "wordhl",
    "graph.box",
    "graph.box2",
    "graph.box3",
    "graph.box4",
    "graph.current",
    "graph.box2",
    "widget_sel",
    "widget_bg",
    "label",
    "ai.write",
    "invalid",
    "ai.seq",
    "args",
    "ai.read",
    "ai.exec",
    "ai.ascii",
    "prompt",
    "graph.traced"
};

ColorThemeWorker::ColorThemeWorker(QObject *parent) : QObject (parent)
{
#if R2_VERSION_NUMBER < 50709
    char* szThemes = r_str_home (R2_HOME_THEMES);
#else
    char* szThemes = r_xdg_datadir ("cons");
#endif
    customR2ThemesLocationPath = szThemes;
    r_mem_free (szThemes);
    if (!QDir (customR2ThemesLocationPath).exists ()) {
        QDir().mkpath (customR2ThemesLocationPath);
    }

    QDir currDir {
	QStringLiteral("%1%2%3")
		.arg(r_sys_prefix(nullptr))
		.arg(R_SYS_DIR)
		.arg(R2_THEMES)
    };
    if (currDir.exists ()) {
        standardR2ThemesLocationPath = currDir.absolutePath ();
    } else {
        QMessageBox::critical (nullptr,
            tr("Standard themes not found"),
            tr("The radare2 standard themes could not be found in '%1'. "
               "Most likely, radare2 is not properly installed.")
                .arg(currDir.path())
        );
    }
}

QColor ColorThemeWorker::mergeColors(const QColor& upper, const QColor& lower) const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // not implemented Qt::SHIFT doesnt exist
    float r1, g1, b1, a1;
    float r2, g2, b2, a2;
    float r, g, b, a;
#else
    qreal r1, g1, b1, a1;
    qreal r2, g2, b2, a2;
    qreal r, g, b, a;
#endif

    upper.getRgbF(&r1, &g1, &b1, &a1);
    lower.getRgbF(&r2, &g2, &b2, &a2);

    a = (1.0 - a1) * a2 + a1;
    r = ((1.0 - a1) * a2 * r2 + a1 * r1) / a;
    g = ((1.0 - a1) * a2 * g2 + a1 * g1) / a;
    b = ((1.0 - a1) * a2 * b2 + a1 * b1) / a;

    QColor res;
    res.setRgbF(r, g, b, a);
    return res;
}

QString ColorThemeWorker::copy(const QString &srcThemeName,
                                   const QString &copyThemeName) const
{
    if (!isThemeExist(srcThemeName)) {
        return tr("Theme <b>%1</b> does not exist.")
                .arg(srcThemeName);
    }

    return save(getTheme(srcThemeName), copyThemeName);
}

QString ColorThemeWorker::save(const QJsonDocument &theme, const QString &themeName) const
{
    QFile fOut(QDir(customR2ThemesLocationPath).filePath(themeName));
    if (!fOut.open(QFile::WriteOnly | QFile::Truncate)) {
        return tr("The file <b>%1</b> cannot be opened.")
                .arg(QFileInfo(fOut).filePath());
    }

    QJsonObject obj = theme.object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); it++) {

        QJsonArray arr = it.value().toArray();
        QColor color;
        if (arr.isEmpty()) {
            color = it.value().toVariant().value<QColor>();
        } else if (arr.size() == 4) {
            color = QColor(arr[0].toInt(), arr[1].toInt(), arr[2].toInt(), arr[3].toInt());
        } else if (arr.size() == 3) {
            color = QColor(arr[0].toInt(), arr[1].toInt(), arr[2].toInt());
        } else {
            continue;
        }
        if (cutterSpecificOptions.contains(it.key())) {
            fOut.write(QString("#~%1 rgb:%2\n")
                       .arg(it.key(), color.name(QColor::HexArgb).remove('#'))
                       .toUtf8());
        } else {
            fOut.write(QString("ec %1 rgb:%2\n")
                       .arg(it.key(), color.name(QColor::HexRgb).remove('#'))
                       .toUtf8());
        }
    }

    fOut.close();
    return "";
}

bool ColorThemeWorker::isCustomTheme(const QString &themeName) const
{
    return QFile::exists(QDir(customR2ThemesLocationPath).filePath(themeName));
}

bool ColorThemeWorker::isThemeExist(const QString &name) const
{
    QStringList themes = Core()->getColorThemes();
    return themes.contains(name);
}

QJsonDocument ColorThemeWorker::getTheme(const QString& themeName) const
{
    int r, g, b, a;
    QVariantMap theme;
    QString curr = Config()->getColorTheme();

    if (themeName != curr) {
        Core()->cmdRaw(QString("eco %1").arg(themeName));
        theme = Core()->cmdj("ecj").object().toVariantMap();
        Core()->cmdRaw(QString("eco %1").arg(curr));
    } else {
        theme = Core()->cmdj("ecj").object().toVariantMap();
    }

    for (auto it = theme.begin(); it != theme.end(); it++) {
        auto arr = it.value().toList();
        QColor(arr[0].toInt(), arr[1].toInt(), arr[2].toInt()).getRgb(&r, &g, &b, &a);
        theme[it.key()] = QJsonArray({r, g, b, a});
    }

    ColorFlags colorFlags = ColorFlags::DarkFlag;
    if (Configuration::relevantThemes.contains(themeName)) {
        colorFlags = Configuration::relevantThemes[themeName];
    }

    for (auto& it : cutterSpecificOptions) {
        Configuration::cutterOptionColors[it][colorFlags].getRgb(&r, &g, &b, &a);
        theme.insert(it, QJsonArray{r, g, b, a});
    }

    if (isCustomTheme(themeName)) {
        QFile src(QDir(customR2ThemesLocationPath).filePath(themeName));
        if (!src.open(QFile::ReadOnly)) {
            return QJsonDocument();
        }
        QStringList sl;
        for (auto &line : QString(src.readAll()).split('\n', IAITO_QT_SKIP_EMPTY_PARTS)) {
            sl = line.replace("#~", "ec ").replace("rgb:", "#").split(' ', IAITO_QT_SKIP_EMPTY_PARTS);
            if (sl.size() != 3 || sl[0][0] == '#') {
                continue;
            }
            QColor(sl[2]).getRgb(&r, &g, &b, &a);
            theme.insert(sl[1], QJsonArray({r, g, b, a}));
        }
    }

    for (auto &key : radare2UnusedOptions) {
        theme.remove(key);
    }

    // manualy converting instead of using QJsonObject::fromVariantMap because
    // Qt < 5.6 QJsonValue.fromVariant doesn't expect QVariant to already contain
    // QJson values like QJsonArray. https://github.com/qt/qtbase/commit/26237f0a2d8db80024b601f676bbce54d483e672
    QJsonObject obj;
    for (auto it = theme.begin(); it != theme.end(); it++) {
        auto &value = it.value();
        if (value.canConvert<QJsonArray>()) {
            obj[it.key()] = it.value().value<QJsonArray>();
        } else {
            obj[it.key()] = QJsonValue::fromVariant(value);
        }

    }

    return QJsonDocument(obj);
}

QString ColorThemeWorker::deleteTheme(const QString &themeName) const
{
    if (!isCustomTheme(themeName)) {
        return tr("You can not delete standard radare2 color themes.");
    }
    if (!isThemeExist(themeName)) {
        return tr("Theme <b>%1</b> does not exist.").arg(themeName);
    }

    QFile file(QDir(customR2ThemesLocationPath).filePath(themeName));
    if (file.isWritable()) {
        return tr("You have no permission to write to <b>%1</b>")
                .arg(QFileInfo(file).filePath());
    }
    if (!file.open(QFile::ReadOnly)) {
        return tr("File <b>%1</b> can not be opened.")
                .arg(QFileInfo(file).filePath());
    }
    if (!file.remove()) {
        return tr("File <b>%1</b> can not be removed.")
                .arg(QFileInfo(file).filePath());
    }
    return "";
}

QString ColorThemeWorker::importTheme(const QString& file) const
{
    QFileInfo src(file);
     if (!src.exists()) {
         return tr("File <b>%1</b> does not exist.").arg(file);
     }

    bool ok;
    bool isTheme = isFileTheme(file, &ok);
    if (!ok) {
        return tr("File <b>%1</b> could not be opened. "
                  "Please make sure you have access to it and try again.")
                .arg(file);
    } else if (!isTheme) {
        return tr("File <b>%1</b> is not a Iaito color theme").arg(file);
    }

    QString name = src.fileName();
    if (isThemeExist(name)) {
        return tr("A color theme named <b>%1</b> already exists.").arg(name);
    }

    if (QFile::copy(file, QDir(customR2ThemesLocationPath).filePath(name))) {
         return "";
     } else {
         return tr("Error occurred during importing. "
                   "Please make sure you have an access to "
                   "the directory <b>%1</b> and try again.")
                 .arg(src.dir().path());
    }
}

QString ColorThemeWorker::renameTheme(const QString& themeName, const QString& newName) const
{
    if (isThemeExist(newName)) {
         return tr("A color theme named <b>\"%1\"</b> already exists.").arg(newName);
     }

     if (!isCustomTheme(themeName)) {
         return tr("You can not rename standard radare2 themes.");
     }

     QDir dir = customR2ThemesLocationPath;
     bool ok = QFile::rename(dir.filePath(themeName), dir.filePath(newName));
     if (!ok) {
         return tr("Something went wrong during renaming. "
                   "Please make sure you have access to the directory <b>\"%1\"</b>.").arg(dir.path());
     }
     return "";
}

bool ColorThemeWorker::isFileTheme(const QString& filePath, bool* ok) const
{
    QFile f(filePath);
    if (!f.open(QFile::ReadOnly)) {
        *ok = false;
        return false;
    }

    const QString colors = "black|red|white|green|magenta|yellow|cyan|blue|gray|none";
    QString options = (Core()->cmdj("ecj").object().keys() << cutterSpecificOptions)
                      .join('|')
                      .replace(".", "\\.");

    QString pattern = QString("((ec\\s+(%1)\\s+(((rgb:|#)[0-9a-fA-F]{3,8})|(%2))))\\s*").arg(options).arg(colors);
    // The below construct mimics the behaviour of QRegexP::exactMatch(), which was here before
    QRegularExpression regexp("\\A(?:" + pattern + ")\\z");

    for (auto &line : QString(f.readAll()).split('\n', IAITO_QT_SKIP_EMPTY_PARTS)) {
        line.replace("#~", "ec ");
        if (!line.isEmpty() && !regexp.match(line).hasMatch()) {
            *ok = true;
            return false;
        }
    }

    *ok = true;
    return true;
}

QStringList ColorThemeWorker::customThemes() const
{
    QStringList themes = Core()->getColorThemes();
    QStringList ret;
    for (int i = 0; i < themes.size(); i++) {
        if (isCustomTheme(themes[i])) {
            ret.push_back(themes[i]);
        }
    }
    return ret;
}
