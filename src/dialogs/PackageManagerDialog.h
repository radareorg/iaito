// PackageManagerDialog provides a Qt-based frontend for r2pm package management
#ifndef PACKAGEMANAGERDIALOG_H
#define PACKAGEMANAGERDIALOG_H

#include <QDialog>
#include <QProcess>
#include <QSet>
#include <QString>

class QLineEdit;
class QTableWidget;
class QPushButton;
class QTextEdit;

class PackageManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PackageManagerDialog(QWidget *parent = nullptr);
    ~PackageManagerDialog() override;

private slots:
    void refreshPackages();
    void installPackage();
    void uninstallPackage();
    void filterPackages(const QString &text);
    void processReadyRead();
    void processFinished(int exitCode, QProcess::ExitStatus status);

private:
    QLineEdit *m_filterLineEdit;
    QTableWidget *m_tableWidget;
    QPushButton *m_refreshButton;
    QPushButton *m_installButton;
    QPushButton *m_uninstallButton;
    QTextEdit *m_logTextEdit;
    QProcess *m_process;
    QSet<QString> m_installedPackages;
    void populateInstalledPackages();
};

#endif // PACKAGEMANAGERDIALOG_H