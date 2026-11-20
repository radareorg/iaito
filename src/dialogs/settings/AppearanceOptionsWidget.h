
#ifndef AppearanceOptionsWidget_H
#define AppearanceOptionsWidget_H

#include <memory>
#include <QDialog>
#include <QPointer>
#include <QPushButton>

#include "core/Iaito.h"

class SettingsDialog;
class QAction;

namespace Ui {
class AppearanceOptionsWidget;
}

class AppearanceOptionsWidget : public QDialog
{
    Q_OBJECT

public:
    explicit AppearanceOptionsWidget(SettingsDialog *dialog);
    ~AppearanceOptionsWidget();

private:
    std::unique_ptr<Ui::AppearanceOptionsWidget> ui;
    QPointer<QAction> visualNavbarToggleAction;

private slots:
    void updateFontFromConfig();
    void updateInterfaceFontFromConfig();
    void updateThemeFromConfig(bool interfaceThemeChanged = true);
    void updateVisualNavbarFromConfig();

    void on_fontSelectionButton_clicked();
    void on_fontResetButton_clicked();
    void onFontFamilyComboBoxCurrentIndexChanged(int index);
    void onFontSizeSpinBoxValueChanged(int size);
    void onFontZoomBoxValueChanged(int zoom);
    void on_themeComboBox_currentIndexChanged(int index);
    void on_visualNavbarLocationComboBox_currentIndexChanged(int index);
    void on_visualNavbarThicknessSpinBox_valueChanged(int value);
    void on_memoryScrollBarComboBox_currentIndexChanged(int index);
    void on_copyThemeAction_triggered();
    void on_deleteThemeAction_triggered();

    /**
     * @brief Imports theme file specified by user to custom themes
     * directory.
     */
    void on_importThemeAction_triggered();

    /**
     * @brief Exports current color theme to file
     * specified by user.
     */
    void on_exportThemeAction_triggered();

    /**
     * @brief Shows dialog to rename current color theme.
     */
    void on_renameThemeAction_triggered();
    void on_editThemeAction_triggered();

    void on_ifaceEditThemeAction_triggered();
    void on_ifaceCopyThemeAction_triggered();
    void on_ifaceRenameThemeAction_triggered();
    void on_ifaceDeleteThemeAction_triggered();
    void on_ifaceExportThemeAction_triggered();
    void on_ifaceImportThemeAction_triggered();

    void onLanguageComboBoxCurrentIndexChanged(int index);

private:
    bool confirmSystemThemeCopy(const QString &theme);
    bool copyColorTheme(const QString &theme);
    bool copyInterfaceTheme(const QString &theme);
    void updateModificationActions(const QString &theme);
    void updateInterfaceModificationActions(const QString &theme);
    void selectInterfaceThemeByName(const QString &name);
    void updateFromConfig();

    /**
     * @brief Changes all @a before colors in given @a fileName svg file to @a
     * after and returns result icon. If @a before is not specified, changes all
     * colors.
     * @param fileName
     * Path to svg file.
     * @param after
     * What color should be inserted instead of old one.
     * @param before
     * Color that should be repalced.
     */
    QIcon getIconFromSvg(
        const QString &fileName, const QColor &after, const QColor &before = QColor());
};

#endif // ASMOPTIONSDIALOG_H
