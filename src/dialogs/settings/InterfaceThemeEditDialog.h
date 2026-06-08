#ifndef INTERFACETHEMEEDITDIALOG_H
#define INTERFACETHEMEEDITDIALOG_H

#include "common/theme/Theme.h"

#include <QColor>
#include <QDialog>
#include <QHash>
#include <QString>
#include <QVector>

class ColorThemeListView;
class ColorPicker;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QTimer;

class InterfaceThemeEditDialog : public QDialog
{
    Q_OBJECT

public:
    explicit InterfaceThemeEditDialog(
        const QString &themeName = QString(), QWidget *parent = nullptr);

    QString savedName() const { return m_savedName; }

public slots:
    void accept() override;
    void reject() override;

private slots:
    void colorOptionChanged(const QColor &newColor);

private:
    Theme buildTheme() const;

    ColorThemeListView *listView;
    ColorPicker *colorPicker;
    QLineEdit *nameEdit;
    QComboBox *modeCombo;
    QComboBox *skinCombo;
    QComboBox *chromeCombo;
    QVector<QSpinBox *> metricSpins;
    QTimer *previewTimer;
    QHash<QString, QColor> m_vars;
    Theme m_base;
    QString m_savedName;
    bool m_dark;
};

#endif // INTERFACETHEMEEDITDIALOG_H
