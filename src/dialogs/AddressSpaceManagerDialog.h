#ifndef ADDRESSSPACEMANAGERDIALOG_H
#define ADDRESSSPACEMANAGERDIALOG_H

#include <QDialog>
#include <QString>
#include <QVector>

class IaitoDockWidget;
class MainWindow;
class QCheckBox;
class QTabWidget;
class QWidget;

class AddressSpaceManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AddressSpaceManagerDialog(MainWindow *mainWindow);
    ~AddressSpaceManagerDialog() override;

private slots:
    void refreshOptions();

private:
    struct ConfigOption
    {
        QString key;
        QCheckBox *checkBox;
    };

    void addDockTab(IaitoDockWidget *dock, const QString &title, const QString &objectName);
    QWidget *createOptionsTab();
    void setOption(const QString &key, bool enabled);

    QTabWidget *tabs;
    QVector<ConfigOption> options;
};

#endif // ADDRESSSPACEMANAGERDIALOG_H
