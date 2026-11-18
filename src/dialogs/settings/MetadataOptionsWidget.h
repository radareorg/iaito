#ifndef METADATAOPTIONSWIDGET_H
#define METADATAOPTIONSWIDGET_H

#include <QDialog>
#include <QList>
#include <QString>

#include <memory>

class QAbstractButton;
class QCheckBox;
class SettingsDialog;

namespace Ui {
class MetadataOptionsWidget;
}

class MetadataOptionsWidget : public QDialog
{
    Q_OBJECT

public:
    explicit MetadataOptionsWidget(SettingsDialog *dialog);
    ~MetadataOptionsWidget();

private:
    std::unique_ptr<Ui::MetadataOptionsWidget> ui;
    struct ConfigCheckbox
    {
        QCheckBox *checkBox;
        QString config;
    };
    QList<ConfigCheckbox> checkboxes;

    void triggerAsmOptionsChanged();

private slots:
    void resetToDefault();
    void updateAsmOptionsFromVars();
    void on_jumpLineSpacingComboBox_currentIndexChanged(int index);
    void on_adaptiveJumpLineSpacingCheckBox_toggled(bool checked);
    void on_followJumpLineCheckBox_toggled(bool checked);
    void on_curvedJumpLineCheckBox_toggled(bool checked);
    void on_colorizeJumpLineCheckBox_toggled(bool checked);
    void on_varsubCheckBox_toggled(bool checked);
    void on_buttonBox_clicked(QAbstractButton *button);
};

#endif // METADATAOPTIONSWIDGET_H
