#ifndef R2AIWIDGET_H
#define R2AIWIDGET_H

#include "IaitoDockWidget.h"

#include <QHash>
#include <QList>
#include <QString>

class MainWindow;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTextBrowser;
class QTimer;
class QWidget;
class QJsonObject;

class R2AIWidget : public IaitoDockWidget
{
    Q_OBJECT

public:
    explicit R2AIWidget(MainWindow *main);
    ~R2AIWidget() override;

private slots:
    void onSendClicked();
    void onSettingsClicked();
    void pollAsyncTasks();
    void onPollFinished(const QString &result);
    void onInterruptCurrentClicked();
    void onNewTabClicked();
    void onTabCloseRequested(int index);
    void updateCurrentControls();

private:
    enum class CommandKind { Submit, Approve, Decline, Interrupt };

    struct TaskView
    {
        int taskId = 0;
        bool finished = false;
        QString title;
        QString kind;
        QString state;
        QString lastOutput;
        QString lastError;
        QString lastPendingToolCall;
        QString transcript;
        QString pendingTool;
        QString pendingToolArgs;
        QWidget *page = nullptr;
        QLabel *statusLabel = nullptr;
        QTextBrowser *outputBrowser = nullptr;
        QWidget *approvalPanel = nullptr;
        QLabel *approvalLabel = nullptr;
        QPlainTextEdit *approvalDetails = nullptr;
        QPushButton *approveButton = nullptr;
        QPushButton *declineButton = nullptr;
        QPushButton *approvalInterruptButton = nullptr;
    };

    void setupUI();
    void ensureAsyncConfig();
    void executeCommand(
        const QString &command, TaskView *view, CommandKind kind, const QString &prompt = QString());
    void handleCommandFinished(
        const QString &result,
        TaskView *view,
        CommandKind kind,
        const QString &prompt,
        const QString &command);
    void updateAvailability();
    QString buildPromptCommand(const QString &input) const;

    TaskView *createTaskTab(const QString &title = QString());
    TaskView *currentTaskView() const;
    void closeTaskTab(TaskView *view);
    void attachTaskId(TaskView *view, int taskId);
    void updateTaskFromJson(const QJsonObject &task);
    void updateTaskStatus(TaskView *view, int age = -1, int steps = -1);
    void updateTaskTabTitle(TaskView *view);
    void appendMarkdown(TaskView *view, const QString &markdown);
    void renderTask(TaskView *view);
    void updateApprovalPanel(TaskView *view);
    void approveTask(TaskView *view);
    void declineTask(TaskView *view);
    void interruptTask(TaskView *view);

    QTabWidget *tabWidget = nullptr;
    QLineEdit *inputLineEdit = nullptr;
    QCheckBox *autoModeCheckBox = nullptr;
    QPushButton *sendButton = nullptr;
    QPushButton *interruptButton = nullptr;
    QPushButton *newTabButton = nullptr;
    QPushButton *settingsButton = nullptr;
    QTimer *pollTimer = nullptr;
    QHash<int, TaskView *> taskById;
    QHash<QWidget *, TaskView *> taskByPage;
    QList<TaskView *> taskViews;
    int nextUntitledTab = 1;
    bool r2aiAvailable = false;
    bool pollInProgress = false;
    bool actionInProgress = false;
};

#endif // R2AIWIDGET_H
