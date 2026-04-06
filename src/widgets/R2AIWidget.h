#ifndef R2AIWIDGET_H
#define R2AIWIDGET_H

#include "IaitoDockWidget.h"
#include "common/CommandTask.h"
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSharedPointer>

class MainWindow;

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
    void updateAvailability();

    QPlainTextEdit *outputTextEdit;
    QLineEdit *inputLineEdit;
    QPushButton *sendButton;
    QPushButton *settingsButton;
    QSharedPointer<CommandTask> commandTask;
};

#endif // R2AIWIDGET_H
