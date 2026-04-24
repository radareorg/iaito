#ifndef SYSCALLSWIDGET_H
#define SYSCALLSWIDGET_H

#include "widgets/IaitoDockWidget.h"

#include <QList>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTableWidget;
class QTableWidgetItem;

class SyscallsWidget : public IaitoDockWidget
{
    Q_OBJECT

public:
    explicit SyscallsWidget(MainWindow *main);

private slots:
    void refreshSyscalls();
    void updateConfigWidgets();
    void showSyscallDetails(QTableWidgetItem *item);
    void applyFilter(const QString &text);

private:
    struct SyscallInfo
    {
        QString name;
        QString vectorText;
        quint64 vector = 0;
        quint64 number = 0;
        int argc = -1;
        QString format;
    };

    QWidget *widgetToFocusOnRaise() override;

    void populateConfigCombos();
    void syncConfigFromSession();
    QList<SyscallInfo> parseSyscalls(const QString &output) const;
    QList<SyscallInfo> parseSyscallList(const QString &output) const;
    QString commandSuffix() const;
    QString selectedArch() const;
    QString selectedBits() const;
    QString selectedOs() const;
    static QString sanitizeConfigValue(QString value);
    static QString numberText(quint64 value);
    static bool parseInteger(const QString &text, quint64 *value);

    QCheckBox *overrideCheckBox = nullptr;
    QComboBox *archComboBox = nullptr;
    QComboBox *bitsComboBox = nullptr;
    QComboBox *osComboBox = nullptr;
    QLabel *summaryLabel = nullptr;
    QLineEdit *filterLineEdit = nullptr;
    QTableWidget *syscallsTable = nullptr;
    QPlainTextEdit *detailTextEdit = nullptr;
};

#endif // SYSCALLSWIDGET_H
