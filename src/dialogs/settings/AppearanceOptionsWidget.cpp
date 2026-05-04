#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontDialog>
#include <QInputDialog>
#include <QLabel>
#include <QPainter>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTranslator>
#include <QtSvg/QSvgRenderer>

#include "AppearanceOptionsWidget.h"
#include "SettingsDialog.h"
#include "ui_AppearanceOptionsWidget.h"
#include <QComboBox>
#include <QtWidgets/QSpinBox>

#include "common/Configuration.h"
#include "common/Helpers.h"

#include "common/ColorThemeWorker.h"
#include "core/MainWindow.h"
#include "dialogs/settings/ColorThemeEditDialog.h"
#include "widgets/ColorPicker.h"

namespace {
struct FontFamilyEntry
{
    const char *label;
    const char *family;
};

static const FontFamilyEntry kFontFamilies[] = {
    {"AnonymousPro", "Anonymous Pro"},
    {"AgaveRegular", "Agave"},
    {"Inconsolata", "Inconsolata"},
    {"IBM Plex", "IBM Plex Mono"},
    {"Windows", "Windows"},
};

static const char *kCustomLabel = "Custom...";
static const char *kDefaultFamily = "IBM Plex Mono";
static const int kDefaultPointSize = 13;
} // namespace

AppearanceOptionsWidget::AppearanceOptionsWidget(SettingsDialog *dialog)
    : QDialog(dialog)
    , ui(new Ui::AppearanceOptionsWidget)
{
    ui->setupUi(this);
    for (const auto &entry : kFontFamilies) {
        ui->fontFamilyComboBox
            ->addItem(QString::fromLatin1(entry.label), QString::fromLatin1(entry.family));
    }
    ui->fontFamilyComboBox->addItem(tr(kCustomLabel), QString());
    ui->visualNavbarLocationComboBox
        ->addItem(tr("SuperTop"), static_cast<int>(Configuration::VisualNavbarLocation::SuperTop));
    ui->visualNavbarLocationComboBox
        ->addItem(tr("Top"), static_cast<int>(Configuration::VisualNavbarLocation::Top));
    ui->visualNavbarLocationComboBox
        ->addItem(tr("Left"), static_cast<int>(Configuration::VisualNavbarLocation::Left));
    ui->visualNavbarLocationComboBox
        ->addItem(tr("Right"), static_cast<int>(Configuration::VisualNavbarLocation::Right));
    ui->visualNavbarLocationComboBox
        ->addItem(tr("Bottom"), static_cast<int>(Configuration::VisualNavbarLocation::Bottom));
    ui->visualNavbarLocationComboBox->addItem(
        tr("SuperBottom"), static_cast<int>(Configuration::VisualNavbarLocation::SuperBottom));
    updateFromConfig();

    QStringList langs = Config()->getAvailableTranslations();
    ui->languageComboBox->addItems(langs);

    QString curr = Config()->getCurrLocale().nativeLanguageName();
    if (!curr.isEmpty()) {
        curr = curr.at(0).toUpper() + curr.right(curr.length() - 1);
    }
    if (!langs.contains(curr)) {
        curr = "English";
    }
    ui->languageComboBox->setCurrentText(curr);

    auto setIcons = [this]() {
        QColor textColor = palette().text().color();
        ui->editButton->setIcon(getIconFromSvg(":/img/icons/pencil_thin.svg", textColor));
        ui->deleteButton->setIcon(getIconFromSvg(":/img/icons/trash_bin.svg", textColor));
        ui->copyButton->setIcon(getIconFromSvg(":/img/icons/copy.svg", textColor));
        ui->importButton->setIcon(getIconFromSvg(":/img/icons/download_black.svg", textColor));
        ui->exportButton->setIcon(getIconFromSvg(":/img/icons/upload_black.svg", textColor));
        ui->renameButton->setIcon(getIconFromSvg(":/img/icons/rename.svg", textColor));
    };
    setIcons();
    connect(Config(), &Configuration::interfaceThemeChanged, this, setIcons);

    connect(
        ui->languageComboBox,
        static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
        this,
        &AppearanceOptionsWidget::onLanguageComboBoxCurrentIndexChanged);

    connect(
        Config(),
        &Configuration::fontsUpdated,
        this,
        &AppearanceOptionsWidget::updateFontFromConfig);
    connect(
        Config(),
        &Configuration::visualNavbarLocationChanged,
        this,
        [this](Configuration::VisualNavbarLocation) { updateVisualNavbarFromConfig(); });
    connect(Config(), &Configuration::visualNavbarThicknessChanged, this, [this](int) {
        updateVisualNavbarFromConfig();
    });
    connect(Config(), &Configuration::colorsUpdated, this, [this]() {
        updateVisualNavbarFromConfig();
    });

    connect(
        ui->colorComboBox,
        &QComboBox::currentTextChanged,
        this,
        &AppearanceOptionsWidget::updateModificationButtons);

    connect(
        ui->fontZoomBox,
        static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
        this,
        &AppearanceOptionsWidget::onFontZoomBoxValueChanged);

    connect(
        ui->fontFamilyComboBox,
        static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
        this,
        &AppearanceOptionsWidget::onFontFamilyComboBoxCurrentIndexChanged);

    connect(
        ui->fontSizeSpinBox,
        static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
        this,
        &AppearanceOptionsWidget::onFontSizeSpinBoxValueChanged);

    connect(
        ui->fontResetButton,
        &QPushButton::clicked,
        this,
        &AppearanceOptionsWidget::on_fontResetButton_clicked);

    ui->useDecompilerHighlighter->setChecked(!Config()->isDecompilerAnnotationHighlighterEnabled());
    connect(ui->useDecompilerHighlighter, &QCheckBox::toggled, this, [](bool checked) {
        Config()->enableDecompilerAnnotationHighlighter(checked);
    });

    // Decompiler background execution option (disabled by default for safety)
    ui->decompilerRunInBackground->setChecked(Config()->getDecompilerRunInBackground());
    connect(ui->decompilerRunInBackground, &QCheckBox::toggled, this, [](bool checked) {
        Config()->setDecompilerRunInBackground(checked);
    });
}

AppearanceOptionsWidget::~AppearanceOptionsWidget() {}

void AppearanceOptionsWidget::updateFontFromConfig()
{
    QFont currentFont = Config()->getBaseFont();

    QSignalBlocker familyBlocker(ui->fontFamilyComboBox);
    QSignalBlocker sizeBlocker(ui->fontSizeSpinBox);

    int matchIndex = -1;
    for (int i = 0; i < ui->fontFamilyComboBox->count(); ++i) {
        QString family = ui->fontFamilyComboBox->itemData(i).toString();
        if (!family.isEmpty() && family == currentFont.family()) {
            matchIndex = i;
            break;
        }
    }
    if (matchIndex < 0) {
        matchIndex = ui->fontFamilyComboBox->count() - 1;
    }
    ui->fontFamilyComboBox->setCurrentIndex(matchIndex);

    int pointSize = currentFont.pointSize();
    if (pointSize <= 0) {
        int pixelSize = currentFont.pixelSize();
        pointSize = pixelSize > 0 ? pixelSize : kDefaultPointSize;
    }
    ui->fontSizeSpinBox->setValue(pointSize);

    bool isCustom = ui->fontFamilyComboBox->itemData(matchIndex).toString().isEmpty();
    ui->fontSelectionButton->setEnabled(isCustom);
    ui->fontSelectionLabel
        ->setText(QStringLiteral("%1 %2pt").arg(currentFont.family()).arg(pointSize));
    ui->fontSelectionLabel->setVisible(isCustom);
}

void AppearanceOptionsWidget::updateThemeFromConfig(bool interfaceThemeChanged)
{
    // Disconnect currentIndexChanged because clearing the comboxBox and
    // refiling it causes its index to change.
    QSignalBlocker signalBlockerThemeBox(ui->themeComboBox);

    ui->themeComboBox->clear();
    for (auto &it : Configuration::iaitoInterfaceThemesList()) {
        ui->themeComboBox->addItem(it.name);
    }
    int currInterfaceThemeIndex = Config()->getInterfaceTheme();
    if (currInterfaceThemeIndex >= Configuration::iaitoInterfaceThemesList().size()) {
        currInterfaceThemeIndex = 0;
        Config()->setInterfaceTheme(currInterfaceThemeIndex);
    }
    ui->themeComboBox->setCurrentIndex(currInterfaceThemeIndex);
    ui->colorComboBox->updateFromConfig(interfaceThemeChanged);
    updateModificationButtons(ui->colorComboBox->currentText());
}

void AppearanceOptionsWidget::updateVisualNavbarFromConfig()
{
    QSignalBlocker locationBlocker(ui->visualNavbarLocationComboBox);
    QSignalBlocker thicknessBlocker(ui->visualNavbarThicknessSpinBox);
    QSignalBlocker themeColorBlocker(ui->visualNavbarUseThemeColorsCheckBox);

    auto location = static_cast<int>(Config()->getVisualNavbarLocation());
    int index = ui->visualNavbarLocationComboBox->findData(location);
    if (index >= 0) {
        ui->visualNavbarLocationComboBox->setCurrentIndex(index);
    }

    ui->visualNavbarThicknessSpinBox->setValue(Config()->getVisualNavbarThickness());
    ui->visualNavbarUseThemeColorsCheckBox->setChecked(Config()->getVisualNavbarUseThemeColors());
}

void AppearanceOptionsWidget::onFontZoomBoxValueChanged(int zoom)
{
    qreal zoomFactor = zoom / 100.0;
    Config()->setZoomFactor(zoomFactor);
}

void AppearanceOptionsWidget::on_fontSelectionButton_clicked()
{
    QFont currentFont = Config()->getBaseFont();
    bool ok;
    QFont newFont
        = QFontDialog::getFont(&ok, currentFont, this, QString(), QFontDialog::DontUseNativeDialog);
    if (ok) {
        Config()->setFont(newFont);
    }
}

void AppearanceOptionsWidget::onFontFamilyComboBoxCurrentIndexChanged(int index)
{
    QString family = ui->fontFamilyComboBox->itemData(index).toString();
    bool isCustom = family.isEmpty();
    ui->fontSelectionButton->setEnabled(isCustom);
    ui->fontSelectionLabel->setEnabled(isCustom);
    if (isCustom) {
        return;
    }
    QFont font = Config()->getBaseFont();
    font.setFamily(family);
    int size = ui->fontSizeSpinBox->value();
    font.setPointSize(size);
    Config()->setFont(font);
}

void AppearanceOptionsWidget::onFontSizeSpinBoxValueChanged(int size)
{
    QFont font = Config()->getBaseFont();
    font.setPointSize(size);
    Config()->setFont(font);
}

void AppearanceOptionsWidget::on_fontResetButton_clicked()
{
    QFont font(QString::fromLatin1(kDefaultFamily), kDefaultPointSize);
    Config()->setFont(font);
    Config()->setZoomFactor(1.0);
    ui->fontZoomBox->setValue(100);
}

void AppearanceOptionsWidget::on_themeComboBox_currentIndexChanged(int index)
{
    Config()->setInterfaceTheme(index);
    updateThemeFromConfig();
}

void AppearanceOptionsWidget::on_visualNavbarLocationComboBox_currentIndexChanged(int index)
{
    auto location = static_cast<Configuration::VisualNavbarLocation>(
        ui->visualNavbarLocationComboBox->itemData(index).toInt());
    Config()->setVisualNavbarLocation(location);
}

void AppearanceOptionsWidget::on_visualNavbarThicknessSpinBox_valueChanged(int value)
{
    Config()->setVisualNavbarThickness(value);
}

void AppearanceOptionsWidget::on_visualNavbarUseThemeColorsCheckBox_toggled(bool checked)
{
    Config()->setVisualNavbarUseThemeColors(checked);
}

void AppearanceOptionsWidget::on_editButton_clicked()
{
    ColorThemeEditDialog dial;
    dial.setWindowTitle(tr("Theme Editor - <%1>").arg(ui->colorComboBox->currentText()));
    dial.exec();
    ui->colorComboBox->updateFromConfig(false);
}

void AppearanceOptionsWidget::on_copyButton_clicked()
{
    QString currColorTheme = ui->colorComboBox->currentText();

    QString newThemeName;
    do {
        newThemeName = QInputDialog::getText(
                           this,
                           tr("Enter theme name"),
                           tr("Name:"),
                           QLineEdit::Normal,
                           currColorTheme + tr(" - copy"))
                           .trimmed();
        if (newThemeName.isEmpty()) {
            return;
        }
        if (ThemeWorker().isThemeExist(newThemeName)) {
            QMessageBox::information(
                this, tr("Theme Copy"), tr("Theme named %1 already exists.").arg(newThemeName));
        } else {
            break;
        }
    } while (true);

    ThemeWorker().copy(currColorTheme, newThemeName);
    Config()->setColorTheme(newThemeName);
    updateThemeFromConfig(false);
}

void AppearanceOptionsWidget::on_deleteButton_clicked()
{
    QString currTheme = ui->colorComboBox->currentText();
    if (!ThemeWorker().isCustomTheme(currTheme)) {
        QMessageBox::critical(nullptr, tr("Error"), ThemeWorker().deleteTheme(currTheme));
        return;
    }
    int ret = QMessageBox::question(
        nullptr, tr("Delete"), tr("Are you sure you want to delete <b>%1</b>?").arg(currTheme));
    if (ret == QMessageBox::Yes) {
        QString err = ThemeWorker().deleteTheme(currTheme);
        updateThemeFromConfig(false);
        if (!err.isEmpty()) {
            QMessageBox::critical(nullptr, tr("Error"), err);
        }
    }
}

void AppearanceOptionsWidget::on_importButton_clicked()
{
    QString fileName = QFileDialog::getOpenFileName(
        this,
        "",
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
        QString(),
        0,
        QFILEDIALOG_FLAGS);
    if (fileName.isEmpty()) {
        return;
    }

    QString err = ThemeWorker().importTheme(fileName);
    QString themeName = QFileInfo(fileName).fileName();
    if (err.isEmpty()) {
        QMessageBox::information(
            this,
            tr("Success"),
            tr("Color theme <b>%1</b> was successfully imported.").arg(themeName));
        Config()->setColorTheme(themeName);
        updateThemeFromConfig(false);
    } else {
        QMessageBox::critical(this, tr("Error"), err);
    }
}

void AppearanceOptionsWidget::on_exportButton_clicked()
{
    QString theme = ui->colorComboBox->currentText();
    QString file = QFileDialog::getSaveFileName(
        this,
        "",
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QDir::separator() + theme);
    if (file.isEmpty()) {
        return;
    }

    // User already gave his consent for this in QFileDialog::getSaveFileName()
    if (QFileInfo(file).exists()) {
        QFile(file).remove();
    }
    QString err = ThemeWorker().save(ThemeWorker().getTheme(theme), file);
    if (err.isEmpty()) {
        QMessageBox::information(
            this, tr("Success"), tr("Color theme <b>%1</b> was successfully exported.").arg(theme));
    } else {
        QMessageBox::critical(this, tr("Error"), err);
    }
}

void AppearanceOptionsWidget::on_renameButton_clicked()
{
    QString currColorTheme = Config()->getColorTheme();
    QString newName = QInputDialog::getText(
        this, tr("Enter new theme name"), tr("Name:"), QLineEdit::Normal, currColorTheme);
    if (newName.isEmpty() || newName == currColorTheme) {
        return;
    }

    QString err = ThemeWorker().renameTheme(currColorTheme, newName);
    if (!err.isEmpty()) {
        QMessageBox::critical(this, tr("Error"), err);
    } else {
        Config()->setColorTheme(newName);
        updateThemeFromConfig(false);
    }
}

void AppearanceOptionsWidget::onLanguageComboBoxCurrentIndexChanged(int index)
{
    QString language = ui->languageComboBox->itemText(index).toLower();
    if (Config()->setLocaleByName(language)) {
        QMessageBox::information(
            this,
            tr("Language settings"),
            tr("Language will be changed after next application start."));
        return;
    }

    qWarning() << tr("Cannot set language, not found in available ones");
}

void AppearanceOptionsWidget::updateModificationButtons(const QString &theme)
{
    bool editable = ThemeWorker().isCustomTheme(theme);
    ui->editButton->setEnabled(editable);
    ui->deleteButton->setEnabled(editable);
    ui->renameButton->setEnabled(editable);
}

void AppearanceOptionsWidget::updateFromConfig()
{
    updateFontFromConfig();
    updateThemeFromConfig(false);
    updateVisualNavbarFromConfig();
    ui->fontZoomBox->setValue(qRound(Config()->getZoomFactor() * 100));
}

QIcon AppearanceOptionsWidget::getIconFromSvg(
    const QString &fileName, const QColor &after, const QColor &before)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        return QIcon();
    }
    QString data = file.readAll();
    data.replace(
        QRegularExpression(QStringLiteral("#%1").arg(
            before.isValid() ? before.name().remove(0, 1) : "[0-9a-fA-F]{6}")),
        QStringLiteral("%1").arg(after.name()));

    QSvgRenderer svgRenderer(data.toUtf8());
    QPixmap pix(svgRenderer.defaultSize());
    pix.fill(Qt::transparent);

    QPainter pixPainter(&pix);
    svgRenderer.render(&pixPainter);

    return pix;
}
