#ifndef INTERFACETHEMEEDITDIALOG_H
#define INTERFACETHEMEEDITDIALOG_H

#include <QColor>
#include <QDialog>
#include <QHash>
#include <QPalette>
#include <QString>

class ColorThemeListView;
class ColorPicker;
class QLineEdit;
class QTimer;

class InterfaceThemeEditDialog : public QDialog
{
    Q_OBJECT

public:
    explicit InterfaceThemeEditDialog(const QString &themeName = QString(), QWidget *parent = nullptr);

    QString savedName() const { return m_savedName; }

public slots:
    void accept() override;
    void reject() override;

private slots:
    void colorOptionChanged(const QColor &newColor);

private:
    ColorThemeListView *listView;
    ColorPicker *colorPicker;
    QLineEdit *nameEdit;
    QTimer *previewTimer;
    QHash<QString, QColor> m_vars;
    QString m_savedName;
};

#endif // INTERFACETHEMEEDITDIALOG_H
