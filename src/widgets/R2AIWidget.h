#ifndef R2AIWIDGET_H
#define R2AIWIDGET_H

#include "IaitoDockWidget.h"
#include "common/CommandTask.h"
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSharedPointer>
#include <QString>

class MainWindow;
class QDialog;
class QComboBox;
class QTableWidget;
class QTableWidgetItem;

class R2AIWidget : public IaitoDockWidget
{
    Q_OBJECT

public:
    explicit R2AIWidget(MainWindow *main);
    ~R2AIWidget() override;

private slots:
    void onSendClicked();
    void onSettingsClicked();
    void onCommandFinished(const QString &result);

private:
    void setupUI();
    void executeCommand(const QString &command);
    /**
     * Disable UI elements and menu entry if the r2ai plugin is not available.
     */
    void updateAvailability();

    QComboBox *modelCombo;
    QPlainTextEdit *outputTextEdit;
    QLineEdit *inputLineEdit;
    QPushButton *sendButton;
    QPushButton *settingsButton;
    QSharedPointer<CommandTask> commandTask;
};

#endif // R2AIWIDGET_H
