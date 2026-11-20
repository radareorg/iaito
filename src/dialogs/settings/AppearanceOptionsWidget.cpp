#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontDialog>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QToolBar>
#include <QTranslator>
#include <QtSvg/QSvgRenderer>

#include "AppearanceOptionsWidget.h"
#include "SettingsDialog.h"
#include "ui_AppearanceOptionsWidget.h"
#include <QComboBox>
#include <QtWidgets/QSpinBox>

#include "common/Configuration.h"
#include "common/Helpers.h"
#include "common/theme/IaitoStyle.h"
#include "common/theme/Theme.h"

#include "common/ColorThemeWorker.h"
#include "core/MainWindow.h"
#include "dialogs/settings/ColorThemeEditDialog.h"
#include "dialogs/settings/InterfaceThemeEditDialog.h"
#include "widgets/ColorPicker.h"
#include <QPushButton>

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
    {"PT Mono", "PT Mono"},
    {"Windows", "Windows"},
    {"W95FA (iaito)", "Iaito W95FA"},
};

static const char *kCustomLabel = "Custom...";
static const int kDefaultPointSize = 13;
static const int kDisabledNavbarLocation = -1;

static bool interfaceThemeExists(const QString &name)
{
    for (const auto &theme : Configuration::iaitoInterfaceThemesList()) {
        if (theme.name == name) {
            return true;
        }
    }
    return false;
}
} // namespace

AppearanceOptionsWidget::AppearanceOptionsWidget(SettingsDialog *dialog)
    : QDialog(dialog)
    , ui(new Ui::AppearanceOptionsWidget)
{
    ui->setupUi(this);

    auto *interfaceThemeMenu = new QMenu(this);
    interfaceThemeMenu->addActions(
        {ui->ifaceEditThemeAction,
         ui->ifaceRenameThemeAction,
         ui->ifaceCopyThemeAction,
         ui->ifaceDeleteThemeAction});
    interfaceThemeMenu->addSeparator();
    interfaceThemeMenu->addActions({ui->ifaceExportThemeAction, ui->ifaceImportThemeAction});
    ui->interfaceThemeButton->setMenu(interfaceThemeMenu);

    auto *colorThemeMenu = new QMenu(this);
    colorThemeMenu->addActions(
        {ui->editThemeAction, ui->renameThemeAction, ui->copyThemeAction, ui->deleteThemeAction});
    colorThemeMenu->addSeparator();
    colorThemeMenu->addActions({ui->exportThemeAction, ui->importThemeAction});
    ui->colorThemeButton->setMenu(colorThemeMenu);

    for (const auto &entry : kFontFamilies) {
        ui->fontFamilyComboBox
            ->addItem(QString::fromLatin1(entry.label), QString::fromLatin1(entry.family));
    }
    ui->fontFamilyComboBox->addItem(tr(kCustomLabel), QString());
    {
        QSignalBlocker blocker(ui->visualNavbarLocationComboBox);
        ui->visualNavbarLocationComboBox->addItem(tr("Disabled"), kDisabledNavbarLocation);
        ui->visualNavbarLocationComboBox->addItem(
            tr("SuperTop"), static_cast<int>(Configuration::VisualNavbarLocation::SuperTop));
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
    }
    if (dialog && dialog->parentWidget()) {
        auto *navbar = dialog->parentWidget()->findChild<QToolBar *>("visualNavbar");
        if (navbar) {
            visualNavbarToggleAction = navbar->toggleViewAction();
            connect(visualNavbarToggleAction, &QAction::toggled, this, [this]() {
                updateVisualNavbarFromConfig();
            });
        }
    }
    {
        // Filling the list must not write the first entry into the settings
        QSignalBlocker blocker(ui->memoryScrollBarComboBox);
        ui->memoryScrollBarComboBox
            ->addItem(tr("Hidden"), static_cast<int>(Configuration::MemoryScrollBarMode::Hidden));
        ui->memoryScrollBarComboBox->addItem(
            tr("Spring (unbounded)"), static_cast<int>(Configuration::MemoryScrollBarMode::Spring));
        ui->memoryScrollBarComboBox->addItem(
            tr("Bounded (navigation bar range)"),
            static_cast<int>(Configuration::MemoryScrollBarMode::Bounded));
    }
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
        ui->editThemeAction->setIcon(getIconFromSvg(":/img/icons/pencil_thin.svg", textColor));
        ui->deleteThemeAction->setIcon(getIconFromSvg(":/img/icons/trash_bin.svg", textColor));
        ui->copyThemeAction->setIcon(getIconFromSvg(":/img/icons/copy.svg", textColor));
        ui->importThemeAction->setIcon(getIconFromSvg(":/img/icons/download_black.svg", textColor));
        ui->exportThemeAction->setIcon(getIconFromSvg(":/img/icons/upload_black.svg", textColor));
        ui->renameThemeAction->setIcon(getIconFromSvg(":/img/icons/rename.svg", textColor));
        ui->ifaceEditThemeAction->setIcon(getIconFromSvg(":/img/icons/pencil_thin.svg", textColor));
        ui->ifaceRenameThemeAction->setIcon(getIconFromSvg(":/img/icons/rename.svg", textColor));
        ui->ifaceCopyThemeAction->setIcon(getIconFromSvg(":/img/icons/copy.svg", textColor));
        ui->ifaceDeleteThemeAction->setIcon(getIconFromSvg(":/img/icons/trash_bin.svg", textColor));
        ui->ifaceExportThemeAction->setIcon(
            getIconFromSvg(":/img/icons/upload_black.svg", textColor));
        ui->ifaceImportThemeAction->setIcon(
            getIconFromSvg(":/img/icons/download_black.svg", textColor));
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
        &Configuration::interfaceFontChanged,
        this,
        &AppearanceOptionsWidget::updateInterfaceFontFromConfig);
    connect(
        Config(),
        &Configuration::visualNavbarLocationChanged,
        this,
        [this](Configuration::VisualNavbarLocation) { updateVisualNavbarFromConfig(); });
    connect(Config(), &Configuration::visualNavbarThicknessChanged, this, [this](int) {
        updateVisualNavbarFromConfig();
    });
    connect(
        Config(),
        &Configuration::memoryScrollBarModeChanged,
        this,
        [this](Configuration::MemoryScrollBarMode) { updateVisualNavbarFromConfig(); });
    connect(Config(), &Configuration::colorsUpdated, this, [this]() {
        updateVisualNavbarFromConfig();
    });

    connect(
        ui->colorComboBox,
        &QComboBox::currentTextChanged,
        this,
        &AppearanceOptionsWidget::updateModificationActions);

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
    connect(
        ui->interfaceFontSizeSpinBox,
        static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
        Config(),
        &Configuration::setInterfaceFontSize);
    connect(
        ui->interfaceFontResetButton,
        &QPushButton::clicked,
        Config(),
        &Configuration::resetInterfaceFontSize);
}

AppearanceOptionsWidget::~AppearanceOptionsWidget() {}

void AppearanceOptionsWidget::updateInterfaceFontFromConfig()
{
    const QFont font = Config()->getInterfaceFont();
    const bool pixels = font.pixelSize() > 0;
    QSignalBlocker blocker(ui->interfaceFontSizeSpinBox);
    ui->interfaceFontFamilyLabel->setText(font.family());
    ui->interfaceFontSizeSpinBox->setSuffix(pixels ? tr(" px") : tr(" pt"));
    ui->interfaceFontSizeSpinBox->setValue(pixels ? font.pixelSize() : font.pointSize());
    ui->interfaceFontResetButton->setEnabled(Config()->hasCustomInterfaceFontSize());
}

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

    const bool pixels = currentFont.pixelSize() > 0;
    const int size = pixels ? currentFont.pixelSize() : currentFont.pointSize();
    const QString unit = pixels ? QStringLiteral("px") : QStringLiteral("pt");
    ui->fontSizeSpinBox->setSuffix(QLatin1Char(' ') + unit);
    ui->fontSizeSpinBox->setValue(size > 0 ? size : kDefaultPointSize);

    bool isCustom = ui->fontFamilyComboBox->itemData(matchIndex).toString().isEmpty();
    ui->fontSelectionButton->setEnabled(isCustom);
    ui->fontSelectionLabel->setText(
        QStringLiteral("%1 %2%3").arg(currentFont.family()).arg(size).arg(unit));
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
    updateInterfaceModificationActions(ui->themeComboBox->currentText());
    ui->colorComboBox->updateFromConfig(interfaceThemeChanged);
    updateModificationActions(ui->colorComboBox->currentText());
}

void AppearanceOptionsWidget::updateVisualNavbarFromConfig()
{
    QSignalBlocker locationBlocker(ui->visualNavbarLocationComboBox);
    QSignalBlocker thicknessBlocker(ui->visualNavbarThicknessSpinBox);
    QSignalBlocker scrollBarBlocker(ui->memoryScrollBarComboBox);

    const int location = visualNavbarToggleAction && !visualNavbarToggleAction->isChecked()
                             ? kDisabledNavbarLocation
                             : static_cast<int>(Config()->getVisualNavbarLocation());
    int index = ui->visualNavbarLocationComboBox->findData(location);
    if (index >= 0) {
        ui->visualNavbarLocationComboBox->setCurrentIndex(index);
    }

    ui->visualNavbarThicknessSpinBox->setValue(Config()->getVisualNavbarThickness());
    const int scrollBarMode = static_cast<int>(Config()->getMemoryScrollBarMode());
    const int scrollBarIndex = ui->memoryScrollBarComboBox->findData(scrollBarMode);
    if (scrollBarIndex >= 0) {
        ui->memoryScrollBarComboBox->setCurrentIndex(scrollBarIndex);
    }
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
    if (family == QLatin1String("Iaito W95FA")) {
        font.setStyleStrategy(QFont::NoAntialias);
        font.setHintingPreference(QFont::PreferNoHinting);
    } else if (font.family() == QLatin1String("Iaito W95FA")) {
        font.setStyleStrategy(QFont::PreferDefault);
        font.setHintingPreference(QFont::PreferDefaultHinting);
    }
    font.setFamily(family);
    Config()->setFont(font);
}

void AppearanceOptionsWidget::onFontSizeSpinBoxValueChanged(int size)
{
    QFont font = Config()->getBaseFont();
    if (font.pixelSize() > 0) {
        font.setPixelSize(size);
    } else {
        font.setPointSize(size);
    }
    Config()->setFont(font);
}

void AppearanceOptionsWidget::on_fontResetButton_clicked()
{
    Config()->resetFont();
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
    if (index < 0) {
        return;
    }
    const int location = ui->visualNavbarLocationComboBox->itemData(index).toInt();
    const bool visible = location != kDisabledNavbarLocation;
    if (visible) {
        Config()->setVisualNavbarLocation(
            static_cast<Configuration::VisualNavbarLocation>(location));
    }
    if (visualNavbarToggleAction && visualNavbarToggleAction->isChecked() != visible) {
        visualNavbarToggleAction->trigger();
    }
}

void AppearanceOptionsWidget::on_visualNavbarThicknessSpinBox_valueChanged(int value)
{
    Config()->setVisualNavbarThickness(value);
}

void AppearanceOptionsWidget::on_memoryScrollBarComboBox_currentIndexChanged(int index)
{
    Config()->setMemoryScrollBarMode(
        static_cast<Configuration::MemoryScrollBarMode>(
            ui->memoryScrollBarComboBox->itemData(index).toInt()));
}

void AppearanceOptionsWidget::on_editThemeAction_triggered()
{
    QString theme = ui->colorComboBox->currentText();
    if (!ThemeWorker().isCustomTheme(theme)) {
        if (!confirmSystemThemeCopy(theme) || !copyColorTheme(theme)) {
            return;
        }
        theme = ui->colorComboBox->currentText();
    }

    ColorThemeEditDialog dial;
    dial.setWindowTitle(tr("Theme Editor - <%1>").arg(theme));
    dial.exec();
    ui->colorComboBox->updateFromConfig(false);
}

void AppearanceOptionsWidget::selectInterfaceThemeByName(const QString &name)
{
    const auto &themes = Configuration::iaitoInterfaceThemesList();
    for (int i = 0; i < themes.size(); ++i) {
        if (themes[i].name == name) {
            Config()->setInterfaceTheme(i);
            break;
        }
    }
    updateThemeFromConfig(false);
}

void AppearanceOptionsWidget::on_ifaceEditThemeAction_triggered()
{
    QString theme = ui->themeComboBox->currentText();
    if (!Configuration::isCustomInterfaceTheme(theme)) {
        if (!confirmSystemThemeCopy(theme) || !copyInterfaceTheme(theme)) {
            return;
        }
        theme = ui->themeComboBox->currentText();
    }

    InterfaceThemeEditDialog dialog(theme, this);
    if (dialog.exec() == QDialog::Accepted) {
        selectInterfaceThemeByName(dialog.savedName());
    }
}

void AppearanceOptionsWidget::on_ifaceCopyThemeAction_triggered()
{
    copyInterfaceTheme(ui->themeComboBox->currentText());
}

bool AppearanceOptionsWidget::copyInterfaceTheme(const QString &theme)
{
    QString newName;
    do {
        newName = QInputDialog::getText(
                      this,
                      tr("Enter theme name"),
                      tr("Name:"),
                      QLineEdit::Normal,
                      theme + tr(" - copy"))
                      .trimmed();
        if (newName.isEmpty()) {
            return false;
        }
        if (!Configuration::isValidThemeName(newName)) {
            QMessageBox::warning(this, tr("Theme Copy"), tr("Invalid theme name."));
        } else if (interfaceThemeExists(newName)) {
            QMessageBox::information(
                this, tr("Theme Copy"), tr("Theme named %1 already exists.").arg(newName));
        } else {
            break;
        }
    } while (true);

    const QString dst
        = QDir(Configuration::userThemesDir()).filePath(newName + QStringLiteral(".theme"));
    bool copied = false;
    if (Configuration::isCustomInterfaceTheme(theme)) {
        copied = QFile::copy(
            QDir(Configuration::userThemesDir()).filePath(theme + QStringLiteral(".theme")), dst);
    } else {
        Theme t = theme == QStringLiteral("Native") ? IaitoStyle::instance()->theme()
                                                    : Theme::builtin(theme);
        t.name = newName;
        t.save(dst);
        copied = QFileInfo::exists(dst);
    }
    if (!copied) {
        QMessageBox::critical(this, tr("Error"), tr("Cannot copy theme."));
        return false;
    }
    selectInterfaceThemeByName(newName);
    return true;
}

void AppearanceOptionsWidget::on_ifaceRenameThemeAction_triggered()
{
    const QString current = ui->themeComboBox->currentText();
    if (!Configuration::isCustomInterfaceTheme(current)) {
        return;
    }
    const QString newName
        = QInputDialog::getText(
              this, tr("Enter new theme name"), tr("Name:"), QLineEdit::Normal, current)
              .trimmed();
    if (newName.isEmpty() || newName == current) {
        return;
    }
    if (!Configuration::isValidThemeName(newName)) {
        QMessageBox::critical(this, tr("Error"), tr("Invalid theme name."));
        return;
    }
    if (interfaceThemeExists(newName)) {
        QMessageBox::critical(this, tr("Error"), tr("Theme named %1 already exists.").arg(newName));
        return;
    }
    QDir dir(Configuration::userThemesDir());
    QFile::rename(
        dir.filePath(current + QStringLiteral(".theme")),
        dir.filePath(newName + QStringLiteral(".theme")));
    selectInterfaceThemeByName(newName);
}

void AppearanceOptionsWidget::on_ifaceDeleteThemeAction_triggered()
{
    const QString current = ui->themeComboBox->currentText();
    if (!Configuration::isCustomInterfaceTheme(current)) {
        return;
    }
    int ret = QMessageBox::question(
        this, tr("Delete"), tr("Are you sure you want to delete <b>%1</b>?").arg(current));
    if (ret != QMessageBox::Yes) {
        return;
    }
    QFile::remove(QDir(Configuration::userThemesDir()).filePath(current + QStringLiteral(".theme")));
    Config()->setInterfaceTheme(0);
    updateThemeFromConfig(false);
}

void AppearanceOptionsWidget::on_ifaceExportThemeAction_triggered()
{
    const QString current = ui->themeComboBox->currentText();
    QString file = QFileDialog::getSaveFileName(
        this,
        "",
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QDir::separator() + current
            + QStringLiteral(".theme"));
    if (file.isEmpty()) {
        return;
    }
    const Theme theme = Configuration::isCustomInterfaceTheme(current)
                            ? Theme::load(
                                  QDir(Configuration::userThemesDir())
                                      .filePath(current + QStringLiteral(".theme")),
                                  Configuration::interfaceThemeIsDark(current))
                        : current == QStringLiteral("Native") ? IaitoStyle::instance()->theme()
                                                              : Theme::builtin(current);
    theme.save(file);
    QSettings out(file, QSettings::IniFormat);
    out.setValue(QStringLiteral("meta/name"), current);
    out.sync();
    if (out.status() != QSettings::NoError) {
        QMessageBox::critical(this, tr("Error"), tr("Cannot write file."));
    }
}

void AppearanceOptionsWidget::on_ifaceImportThemeAction_triggered()
{
    QString fileName = QFileDialog::getOpenFileName(
        this,
        "",
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
        tr("Interface theme (*.theme)"),
        nullptr,
        QFILEDIALOG_FLAGS);
    if (fileName.isEmpty()) {
        return;
    }
    const QString name = QFileInfo(fileName).completeBaseName();
    const QString dst
        = QDir(Configuration::userThemesDir()).filePath(name + QStringLiteral(".theme"));
    if (QFileInfo::exists(dst)) {
        QFile::remove(dst);
    }
    if (!QFile::copy(fileName, dst)) {
        QMessageBox::critical(this, tr("Error"), tr("Cannot import theme."));
        return;
    }
    selectInterfaceThemeByName(name);
}

void AppearanceOptionsWidget::updateInterfaceModificationActions(const QString &theme)
{
    bool editable = Configuration::isCustomInterfaceTheme(theme);
    ui->ifaceEditThemeAction->setEnabled(!theme.isEmpty());
    ui->ifaceRenameThemeAction->setEnabled(editable);
    ui->ifaceDeleteThemeAction->setEnabled(editable);
}

void AppearanceOptionsWidget::on_copyThemeAction_triggered()
{
    copyColorTheme(ui->colorComboBox->currentText());
}

bool AppearanceOptionsWidget::copyColorTheme(const QString &theme)
{
    QString newThemeName;
    do {
        newThemeName = QInputDialog::getText(
                           this,
                           tr("Enter theme name"),
                           tr("Name:"),
                           QLineEdit::Normal,
                           theme + tr(" - copy"))
                           .trimmed();
        if (newThemeName.isEmpty()) {
            return false;
        }
        if (ThemeWorker().isThemeExist(newThemeName)) {
            QMessageBox::information(
                this, tr("Theme Copy"), tr("Theme named %1 already exists.").arg(newThemeName));
        } else {
            break;
        }
    } while (true);

    QString err = ThemeWorker().copy(theme, newThemeName);
    if (!err.isEmpty()) {
        QMessageBox::critical(this, tr("Error"), err);
        return false;
    }
    Config()->setColorTheme(newThemeName);
    updateThemeFromConfig(false);
    return true;
}

void AppearanceOptionsWidget::on_deleteThemeAction_triggered()
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

void AppearanceOptionsWidget::on_importThemeAction_triggered()
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

void AppearanceOptionsWidget::on_exportThemeAction_triggered()
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

void AppearanceOptionsWidget::on_renameThemeAction_triggered()
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

void AppearanceOptionsWidget::updateModificationActions(const QString &theme)
{
    bool editable = ThemeWorker().isCustomTheme(theme);
    ui->editThemeAction->setEnabled(!theme.isEmpty());
    ui->deleteThemeAction->setEnabled(editable);
    ui->renameThemeAction->setEnabled(editable);
}

bool AppearanceOptionsWidget::confirmSystemThemeCopy(const QString &theme)
{
    QMessageBox prompt(this);
    prompt.setIcon(QMessageBox::Question);
    prompt.setWindowTitle(tr("Edit Theme"));
    prompt.setText(
        tr("The system theme <b>%1</b> cannot be edited directly.").arg(theme.toHtmlEscaped()));
    prompt.setInformativeText(tr("Would you like to make a copy and start editing it?"));
    QPushButton *copyButton = prompt.addButton(tr("Make a Copy"), QMessageBox::AcceptRole);
    prompt.addButton(QMessageBox::Cancel);
    prompt.setDefaultButton(copyButton);
    prompt.exec();
    return prompt.clickedButton() == copyButton;
}

void AppearanceOptionsWidget::updateFromConfig()
{
    updateFontFromConfig();
    updateInterfaceFontFromConfig();
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
