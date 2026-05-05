#include "R2AIWidget.h"
#include "core/Iaito.h"

#include <utility>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextBrowser>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int ACTIVE_POLL_INTERVAL_MS = 1000;
constexpr int IDLE_POLL_INTERVAL_MS = 5000;
constexpr int MAX_TAB_TITLE_LENGTH = 36;

QString stripAnsi(QString text)
{
    static const QRegularExpression ansiPattern(QStringLiteral("\x1b\\[[0-?]*[ -/]*[@-~]"));
    text.remove(ansiPattern);
    return text;
}

QString normalizeNewlines(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text;
}

QString quoteMarkdown(QString text)
{
    text = normalizeNewlines(stripAnsi(text)).trimmed();
    if (text.isEmpty()) {
        return QString();
    }
    text.replace(QLatin1Char('\n'), QStringLiteral("\n> "));
    return QStringLiteral("> %1").arg(text);
}

QString fencedMarkdown(QString text, const QString &language = QStringLiteral("text"))
{
    text = normalizeNewlines(stripAnsi(text)).trimmed();
    text.replace(QStringLiteral("```"), QStringLiteral("'''"));
    return QStringLiteral("```%1\n%2\n```").arg(language, text);
}

QString normalizeThinkingTags(QString text)
{
    text = normalizeNewlines(stripAnsi(text));
    static const QRegularExpression thinkingPattern(
        QStringLiteral("<thinking>\\s*([\\s\\S]*?)\\s*</thinking>"),
        QRegularExpression::DotMatchesEverythingOption);

    QString result;
    int last = 0;
    auto it = thinkingPattern.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        result += text.mid(last, match.capturedStart() - last);

        const QString thinking = match.captured(1).trimmed();
        if (!thinking.isEmpty()) {
            result += QStringLiteral("\n\n**Thinking**\n\n%1\n\n").arg(quoteMarkdown(thinking));
        }
        last = match.capturedEnd();
    }
    result += text.mid(last);
    return result.trimmed();
}

QString formatR2AiOutput(QString text)
{
    text = normalizeNewlines(stripAnsi(text));
    static const QRegularExpression toolResultPattern(
        QStringLiteral("(^|\\n)Tool result \\(([^\\n\\)]+)\\):\\n"));

    QString result;
    auto appendBlock = [&result](const QString &block) {
        const QString trimmed = block.trimmed();
        if (trimmed.isEmpty()) {
            return;
        }
        if (!result.trimmed().isEmpty()) {
            result += QStringLiteral("\n\n");
        }
        result += trimmed;
    };

    int pos = 0;
    while (pos < text.size()) {
        const QRegularExpressionMatch match = toolResultPattern.match(text, pos);
        if (!match.hasMatch()) {
            appendBlock(normalizeThinkingTags(text.mid(pos)));
            break;
        }

        appendBlock(normalizeThinkingTags(text.mid(pos, match.capturedStart() - pos)));

        const int bodyStart = match.capturedEnd();
        int bodyEnd = text.size();
        const int nextThinking = text.indexOf(QStringLiteral("<thinking>"), bodyStart);
        if (nextThinking >= 0) {
            bodyEnd = nextThinking;
        }
        const QRegularExpressionMatch nextToolMatch = toolResultPattern.match(text, bodyStart);
        if (nextToolMatch.hasMatch()) {
            bodyEnd = qMin(bodyEnd, nextToolMatch.capturedStart());
        }

        const QString toolName = match.captured(2).trimmed();
        const QString body = text.mid(bodyStart, bodyEnd - bodyStart).trimmed();
        appendBlock(
            QStringLiteral("**Tool result (%1)**\n\n%2")
                .arg(toolName.isEmpty() ? QStringLiteral("?") : toolName, fencedMarkdown(body)));
        pos = bodyEnd;
    }
    return result.trimmed();
}

void setBrowserMarkdown(QTextBrowser *browser, const QString &markdown)
{
    QScrollBar *bar = browser->verticalScrollBar();
    const bool wasAtBottom = !bar || bar->value() >= bar->maximum() - 4;
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    browser->setMarkdown(markdown);
#else
    browser->setHtml(QStringLiteral("<pre>%1</pre>").arg(markdown.toHtmlEscaped()));
#endif
    if (bar && wasAtBottom) {
        bar->setValue(bar->maximum());
    }
}

QString compactTitle(QString title)
{
    title = title.simplified();
    if (title.isEmpty()) {
        title = QStringLiteral("Task");
    }
    if (title.size() > MAX_TAB_TITLE_LENGTH) {
        title = title.left(MAX_TAB_TITLE_LENGTH - 3) + QStringLiteral("...");
    }
    return title;
}

bool isLiveState(const QString &state)
{
    return state == QLatin1String("pending") || state == QLatin1String("running")
           || state == QLatin1String("wait-approve") || state == QLatin1String("wait-input");
}

bool isFinalState(const QString &state)
{
    return state == QLatin1String("complete") || state == QLatin1String("error")
           || state == QLatin1String("cancelled") || state == QLatin1String("stopped");
}

QJsonDocument parseJsonObjectResult(const QString &result)
{
    QByteArray json = result.toUtf8().trimmed();
    const int begin = json.indexOf('{');
    const int end = json.lastIndexOf('}');
    if (begin > 0 && end > begin) {
        json = json.mid(begin, end - begin + 1);
    }

    QJsonParseError error;
    QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return QJsonDocument();
    }
    return document;
}

int queuedTaskId(const QString &result)
{
    static const QRegularExpression queuedPattern(
        QStringLiteral("\\[async\\]\\s+task\\s+(\\d+)\\s+queued"));
    const QRegularExpressionMatch match = queuedPattern.match(stripAnsi(result));
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

QString toolDetailsText(const QString &toolName, const QString &args)
{
    QStringList lines;
    lines << QStringLiteral("Tool: %1").arg(toolName.isEmpty() ? QStringLiteral("?") : toolName);

    const QJsonDocument document = QJsonDocument::fromJson(args.toUtf8());
    const QJsonObject object = document.object();
    if (!object.isEmpty()) {
        const QString primaryKey = toolName == QLatin1String("execute_js")
                                       ? QStringLiteral("script")
                                       : QStringLiteral("command");
        const QString primaryValue = object.value(primaryKey).toString();
        if (!primaryValue.isEmpty()) {
            lines << QStringLiteral("%1: %2").arg(primaryKey, primaryValue);
        }
    }

    if (!args.isEmpty()) {
        lines << QString();
        lines << QStringLiteral("Arguments:");
        lines << args;
    }
    return lines.join(QLatin1Char('\n'));
}

QString statusText(const QString &kind, const QString &state, int taskId, int age, int steps)
{
    QStringList parts;
    if (taskId > 0) {
        parts << QStringLiteral("#%1").arg(taskId);
    }
    if (!kind.isEmpty()) {
        parts << kind;
    }
    parts << (state.isEmpty() ? QStringLiteral("idle") : state);
    if (age >= 0) {
        parts << QStringLiteral("%1s").arg(age);
    }
    if (steps > 0) {
        parts << QStringLiteral("%1 steps").arg(steps);
    }
    return parts.join(QStringLiteral(" | "));
}

QString runR2AiCoreCommand(const QString &command)
{
    return Core()->cmd(command);
}

} // namespace

R2AIWidget::R2AIWidget(MainWindow *main)
    : IaitoDockWidget(main)
{
    setObjectName("r2aiWidget");
    setWindowTitle(tr("r2ai"));
    setupUI();
    updateAvailability();
}

R2AIWidget::~R2AIWidget()
{
    qDeleteAll(taskViews);
}

void R2AIWidget::ensureAsyncConfig()
{
    Core()->setConfig("r2ai.async", true);
    Core()->setConfig("r2ai.async.purge", true);
}

void R2AIWidget::updateAvailability()
{
    r2aiAvailable = !Core()->cmd("Lc~^r2ai").trimmed().isEmpty();
    if (r2aiAvailable) {
        ensureAsyncConfig();
    }

    inputLineEdit->setEnabled(r2aiAvailable);
    autoModeCheckBox->setEnabled(r2aiAvailable);
    sendButton->setEnabled(r2aiAvailable && !actionInProgress);
    newTabButton->setEnabled(r2aiAvailable);
    settingsButton->setEnabled(r2aiAvailable);
    updateCurrentControls();
    if (QAction *act = toggleViewAction()) {
        act->setEnabled(r2aiAvailable);
    }
}

void R2AIWidget::setupUI()
{
    QWidget *container = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(container);

    tabWidget = new QTabWidget(container);
    tabWidget->setTabsClosable(true);
    newTabButton = new QPushButton(QStringLiteral("+"), tabWidget);
    newTabButton->setToolTip(tr("New r2ai tab"));
    newTabButton->setFlat(true);
    newTabButton->setFixedSize(28, 24);
    tabWidget->setCornerWidget(newTabButton, Qt::TopRightCorner);
    layout->addWidget(tabWidget, 1);

    QHBoxLayout *hLayout = new QHBoxLayout();
    inputLineEdit = new QLineEdit(container);
    autoModeCheckBox = new QCheckBox(tr("Auto"), container);
    sendButton = new QPushButton(tr("Send"), container);
    interruptButton = new QPushButton(tr("Interrupt"), container);
    settingsButton = new QPushButton(tr("Settings"), container);
    hLayout->addWidget(inputLineEdit, 1);
    hLayout->addWidget(autoModeCheckBox);
    hLayout->addWidget(sendButton);
    hLayout->addWidget(interruptButton);
    hLayout->addWidget(settingsButton);
    layout->addLayout(hLayout);

    setWidget(container);

    createTaskTab(tr("New"));

    pollTimer = new QTimer(this);
    pollTimer->setInterval(ACTIVE_POLL_INTERVAL_MS);

    connect(sendButton, &QPushButton::clicked, this, &R2AIWidget::onSendClicked);
    connect(inputLineEdit, &QLineEdit::returnPressed, this, &R2AIWidget::onSendClicked);
    connect(settingsButton, &QPushButton::clicked, this, &R2AIWidget::onSettingsClicked);
    connect(interruptButton, &QPushButton::clicked, this, &R2AIWidget::onInterruptCurrentClicked);
    connect(newTabButton, &QPushButton::clicked, this, &R2AIWidget::onNewTabClicked);
    connect(tabWidget, &QTabWidget::tabCloseRequested, this, &R2AIWidget::onTabCloseRequested);
    connect(tabWidget, &QTabWidget::currentChanged, this, &R2AIWidget::updateCurrentControls);
    connect(pollTimer, &QTimer::timeout, this, &R2AIWidget::pollAsyncTasks);

    if (QAction *act = toggleViewAction()) {
        act->setText(tr("r2ai"));
    }

    pollTimer->start();
}

R2AIWidget::TaskView *R2AIWidget::createTaskTab(const QString &title)
{
    TaskView *view = new TaskView;
    view->title = title.isEmpty() ? tr("New") : title;
    view->state = QStringLiteral("idle");

    view->page = new QWidget(tabWidget);
    QVBoxLayout *layout = new QVBoxLayout(view->page);

    view->statusLabel = new QLabel(view->page);
    layout->addWidget(view->statusLabel);

    QFrame *approvalFrame = new QFrame(view->page);
    approvalFrame->setFrameShape(QFrame::StyledPanel);
    QVBoxLayout *approvalLayout = new QVBoxLayout(approvalFrame);
    view->approvalPanel = approvalFrame;
    view->approvalLabel = new QLabel(approvalFrame);
    view->approvalLabel->setWordWrap(true);
    view->approvalDetails = new QPlainTextEdit(approvalFrame);
    view->approvalDetails->setReadOnly(true);
    view->approvalDetails->setMaximumHeight(120);

    QHBoxLayout *approvalButtons = new QHBoxLayout();
    approvalButtons->addStretch();
    view->approvalInterruptButton = new QPushButton(tr("Interrupt"), approvalFrame);
    view->declineButton = new QPushButton(tr("Decline"), approvalFrame);
    view->approveButton = new QPushButton(tr("Approve"), approvalFrame);
    approvalButtons->addWidget(view->approvalInterruptButton);
    approvalButtons->addWidget(view->declineButton);
    approvalButtons->addWidget(view->approveButton);

    approvalLayout->addWidget(view->approvalLabel);
    approvalLayout->addWidget(view->approvalDetails);
    approvalLayout->addLayout(approvalButtons);
    view->approvalPanel->hide();
    layout->addWidget(view->approvalPanel);

    view->outputBrowser = new QTextBrowser(view->page);
    view->outputBrowser->setReadOnly(true);
    view->outputBrowser->setOpenExternalLinks(true);
    layout->addWidget(view->outputBrowser, 1);

    taskViews.append(view);
    taskByPage.insert(view->page, view);

    const int index = tabWidget->addTab(view->page, compactTitle(view->title));
    tabWidget->setCurrentIndex(index);
    updateTaskStatus(view);

    connect(view->approveButton, &QPushButton::clicked, this, [this, view]() { approveTask(view); });
    connect(view->declineButton, &QPushButton::clicked, this, [this, view]() { declineTask(view); });
    connect(view->approvalInterruptButton, &QPushButton::clicked, this, [this, view]() {
        interruptTask(view);
    });

    return view;
}

R2AIWidget::TaskView *R2AIWidget::currentTaskView() const
{
    return taskByPage.value(tabWidget->currentWidget(), nullptr);
}

void R2AIWidget::onNewTabClicked()
{
    createTaskTab(tr("New %1").arg(nextUntitledTab++));
    updateCurrentControls();
}

void R2AIWidget::onTabCloseRequested(int index)
{
    QWidget *page = tabWidget->widget(index);
    closeTaskTab(taskByPage.value(page, nullptr));
}

void R2AIWidget::closeTaskTab(TaskView *view)
{
    if (!view) {
        return;
    }

    if (view->taskId > 0 && isLiveState(view->state)) {
        interruptTask(view);
    }

    const int index = tabWidget->indexOf(view->page);
    if (index >= 0) {
        tabWidget->removeTab(index);
    }
    taskByPage.remove(view->page);
    if (view->taskId > 0) {
        taskById.remove(view->taskId);
    }
    taskViews.removeOne(view);
    view->page->deleteLater();
    delete view;

    if (tabWidget->count() == 0) {
        createTaskTab(tr("New"));
    }
}

QString R2AIWidget::buildPromptCommand(const QString &input) const
{
    if (autoModeCheckBox->isChecked() && !input.startsWith(QLatin1Char('-'))) {
        return QStringLiteral("r2ai -a %1").arg(input);
    }
    return QStringLiteral("r2ai %1").arg(input);
}

void R2AIWidget::onSendClicked()
{
    if (!r2aiAvailable || actionInProgress) {
        return;
    }

    const QString input = inputLineEdit->text().trimmed();
    if (input.isEmpty()) {
        return;
    }

    ensureAsyncConfig();

    TaskView *view = currentTaskView();
    if (!view || view->taskId > 0 || !view->transcript.isEmpty()) {
        view = createTaskTab(autoModeCheckBox->isChecked() ? tr("Auto") : tr("Query"));
    }

    appendMarkdown(view, QStringLiteral("**User**\n\n%1").arg(quoteMarkdown(input)));
    view->title = input;
    updateTaskTabTitle(view);
    inputLineEdit->clear();

    executeCommand(buildPromptCommand(input), view, CommandKind::Submit, input);
}

void R2AIWidget::executeCommand(
    const QString &command, TaskView *view, CommandKind kind, const QString &prompt)
{
    if (actionInProgress) {
        return;
    }

    QPointer<QWidget> page = view ? view->page : nullptr;
    actionInProgress = true;
    updateCurrentControls();
    // r2ai async tasks run their model/network work in r2ai's worker thread, but r2 core
    // commands and approved tools must execute on this main thread.
    const QString result = runR2AiCoreCommand(command);
    actionInProgress = false;
    TaskView *target = page ? taskByPage.value(page.data(), nullptr) : nullptr;
    handleCommandFinished(result, target, kind, prompt, command);
}

void R2AIWidget::handleCommandFinished(
    const QString &result,
    TaskView *view,
    CommandKind kind,
    const QString &prompt,
    const QString &command)
{
    switch (kind) {
    case CommandKind::Submit: {
        const int taskId = queuedTaskId(result);
        if (taskId > 0 && view) {
            attachTaskId(view, taskId);
            view->state = QStringLiteral("pending");
            view->title = prompt;
            updateTaskStatus(view);
            updateTaskTabTitle(view);
            appendMarkdown(view, tr("**Queued**\n\nTask #%1").arg(taskId));
        } else if (view) {
            appendMarkdown(view, formatR2AiOutput(result));
        }
        break;
    }
    case CommandKind::Approve:
        if (view) {
            view->state = QStringLiteral("running");
            view->approvalPanel->hide();
            const QString toolName = view->pendingTool.isEmpty() ? tr("tool") : view->pendingTool;
            appendMarkdown(view, tr("**Approved %1**").arg(toolName));
            updateTaskStatus(view);
        }
        break;
    case CommandKind::Decline:
        if (view) {
            view->state = QStringLiteral("running");
            view->approvalPanel->hide();
            const QString toolName = view->pendingTool.isEmpty() ? tr("tool") : view->pendingTool;
            appendMarkdown(view, tr("**Declined %1**").arg(toolName));
            updateTaskStatus(view);
        }
        break;
    case CommandKind::Interrupt:
        if (view) {
            view->state = QStringLiteral("stopped");
            view->finished = true;
            view->approvalPanel->hide();
            if (view->taskId > 0) {
                taskById.remove(view->taskId);
            }
            appendMarkdown(view, tr("**Interrupted**"));
            updateTaskStatus(view);
            updateTaskTabTitle(view);
        }
        break;
    }

    updateCurrentControls();
    if (r2aiAvailable) {
        pollAsyncTasks();
    }
    Q_UNUSED(command);
}

void R2AIWidget::attachTaskId(TaskView *view, int taskId)
{
    if (!view || taskId <= 0) {
        return;
    }
    if (view->taskId > 0) {
        taskById.remove(view->taskId);
    }
    view->taskId = taskId;
    taskById.insert(taskId, view);
}

void R2AIWidget::pollAsyncTasks()
{
    if (!r2aiAvailable) {
        updateAvailability();
        if (!r2aiAvailable) {
            pollTimer->setInterval(IDLE_POLL_INTERVAL_MS);
            return;
        }
    }
    if (pollInProgress || actionInProgress) {
        return;
    }

    ensureAsyncConfig();
    pollInProgress = true;
    const QString result = runR2AiCoreCommand(QStringLiteral("r2ai -sj"));
    pollInProgress = false;
    onPollFinished(result);
}

void R2AIWidget::onPollFinished(const QString &result)
{
    const QJsonDocument document = parseJsonObjectResult(result);
    if (!document.isObject()) {
        pollTimer->setInterval(IDLE_POLL_INTERVAL_MS);
        return;
    }

    const QJsonArray tasks = document.object().value(QStringLiteral("tasks")).toArray();
    QHash<int, bool> seenTasks;
    bool hasLiveTasks = false;
    for (const QJsonValue &value : tasks) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject task = value.toObject();
        const int taskId = task.value(QStringLiteral("id")).toInt();
        if (taskId > 0) {
            seenTasks.insert(taskId, true);
        }
        const QString state = task.value(QStringLiteral("state")).toString();
        hasLiveTasks = hasLiveTasks || isLiveState(state);
        updateTaskFromJson(task);
    }

    for (TaskView *view : std::as_const(taskViews)) {
        if (view->taskId <= 0 || view->finished || seenTasks.contains(view->taskId)) {
            continue;
        }
        if (isLiveState(view->state)) {
            view->state = QStringLiteral("stopped");
            view->finished = true;
            view->approvalPanel->hide();
            updateTaskStatus(view);
        }
    }

    pollTimer->setInterval(hasLiveTasks ? ACTIVE_POLL_INTERVAL_MS : IDLE_POLL_INTERVAL_MS);
    updateCurrentControls();
}

void R2AIWidget::updateTaskFromJson(const QJsonObject &task)
{
    const int taskId = task.value(QStringLiteral("id")).toInt();
    if (taskId <= 0) {
        return;
    }

    TaskView *view = taskById.value(taskId, nullptr);
    if (!view) {
        view = createTaskTab(task.value(QStringLiteral("title")).toString());
        attachTaskId(view, taskId);
    }

    view->kind = task.value(QStringLiteral("kind")).toString();
    view->state = task.value(QStringLiteral("state")).toString();
    const QString title = task.value(QStringLiteral("title")).toString();
    if (!title.isEmpty()) {
        view->title = title;
    }

    const QString output = task.value(QStringLiteral("output")).toString();
    if (!output.isEmpty() && output != view->lastOutput) {
        QString delta = output;
        if (!view->lastOutput.isEmpty() && output.startsWith(view->lastOutput)) {
            delta = output.mid(view->lastOutput.size());
        }
        appendMarkdown(view, formatR2AiOutput(delta));
        view->lastOutput = output;
    }

    const QString error = task.value(QStringLiteral("error")).toString();
    if (!error.isEmpty() && error != view->lastError) {
        appendMarkdown(view, QStringLiteral("**Error**\n\n%1").arg(fencedMarkdown(error)));
        view->lastError = error;
    }

    view->pendingTool = task.value(QStringLiteral("pending_tool")).toString();
    const QString pendingToolArgs = task.value(QStringLiteral("pending_tool_args")).toString();
    if (!pendingToolArgs.isEmpty()) {
        view->pendingToolArgs = pendingToolArgs;
    }
    const QString pendingToolCallId = task.value(QStringLiteral("pending_tool_call_id")).toString();
    const QString pendingToolCallKey
        = pendingToolCallId.isEmpty()
              ? QStringLiteral("%1\n%2").arg(view->pendingTool, view->pendingToolArgs)
              : pendingToolCallId;
    if (view->state == QLatin1String("wait-approve") && !pendingToolCallKey.trimmed().isEmpty()
        && pendingToolCallKey != view->lastPendingToolCall) {
        const QString toolName = view->pendingTool.isEmpty() ? QStringLiteral("?")
                                                             : view->pendingTool;
        appendMarkdown(
            view,
            QStringLiteral("**Tool call (%1)**\n\n%2")
                .arg(
                    toolName,
                    fencedMarkdown(toolDetailsText(view->pendingTool, view->pendingToolArgs))));
        view->lastPendingToolCall = pendingToolCallKey;
    }
    updateApprovalPanel(view);

    if (isFinalState(view->state)) {
        view->finished = true;
        view->approvalPanel->hide();
    }

    updateTaskStatus(
        view,
        task.value(QStringLiteral("age")).toInt(-1),
        task.value(QStringLiteral("steps")).toInt(-1));
    updateTaskTabTitle(view);
}

void R2AIWidget::updateTaskStatus(TaskView *view, int age, int steps)
{
    if (!view || !view->statusLabel) {
        return;
    }
    view->statusLabel->setText(statusText(view->kind, view->state, view->taskId, age, steps));
}

void R2AIWidget::updateTaskTabTitle(TaskView *view)
{
    if (!view || !view->page) {
        return;
    }
    QString title = view->title;
    if (view->taskId > 0) {
        title = QStringLiteral("#%1 %2").arg(view->taskId).arg(title);
    }
    const int index = tabWidget->indexOf(view->page);
    if (index >= 0) {
        tabWidget->setTabText(index, compactTitle(title));
    }
}

void R2AIWidget::appendMarkdown(TaskView *view, const QString &markdown)
{
    if (!view) {
        return;
    }
    const QString normalized = normalizeThinkingTags(markdown);
    if (normalized.isEmpty()) {
        return;
    }
    if (!view->transcript.isEmpty()) {
        view->transcript += QStringLiteral("\n\n");
    }
    view->transcript += normalized;
    renderTask(view);
}

void R2AIWidget::renderTask(TaskView *view)
{
    if (!view || !view->outputBrowser) {
        return;
    }
    setBrowserMarkdown(view->outputBrowser, view->transcript);
}

void R2AIWidget::updateApprovalPanel(TaskView *view)
{
    if (!view || !view->approvalPanel) {
        return;
    }

    const bool waitingForApproval = view->state == QLatin1String("wait-approve");
    view->approvalPanel->setVisible(waitingForApproval);
    if (!waitingForApproval) {
        return;
    }

    const QString toolName = view->pendingTool.isEmpty() ? QStringLiteral("?") : view->pendingTool;
    view->approvalLabel->setText(tr("Task #%1 wants to run %2.").arg(view->taskId).arg(toolName));
    view->approvalDetails->setPlainText(toolDetailsText(view->pendingTool, view->pendingToolArgs));
    view->approveButton->setEnabled(!actionInProgress);
    view->declineButton->setEnabled(!actionInProgress);
    view->approvalInterruptButton->setEnabled(!actionInProgress);
}

void R2AIWidget::approveTask(TaskView *view)
{
    if (!view || view->taskId <= 0 || actionInProgress) {
        return;
    }
    executeCommand(QStringLiteral("r2ai -sy %1").arg(view->taskId), view, CommandKind::Approve);
}

void R2AIWidget::declineTask(TaskView *view)
{
    if (!view || view->taskId <= 0 || actionInProgress) {
        return;
    }
    executeCommand(QStringLiteral("r2ai -sn %1").arg(view->taskId), view, CommandKind::Decline);
}

void R2AIWidget::interruptTask(TaskView *view)
{
    if (!view || view->taskId <= 0 || actionInProgress) {
        return;
    }
    executeCommand(QStringLiteral("r2ai -sk %1").arg(view->taskId), view, CommandKind::Interrupt);
}

void R2AIWidget::onInterruptCurrentClicked()
{
    interruptTask(currentTaskView());
}

void R2AIWidget::updateCurrentControls()
{
    TaskView *view = currentTaskView();
    const bool canAct = r2aiAvailable && !actionInProgress;
    sendButton->setEnabled(canAct);
    interruptButton->setEnabled(canAct && view && view->taskId > 0 && isLiveState(view->state));

    for (TaskView *taskView : std::as_const(taskViews)) {
        updateApprovalPanel(taskView);
    }
}

static void setComboToValue(QComboBox *combo, const QString &value)
{
    int i = combo->findText(value);
    if (i >= 0) {
        combo->setCurrentIndex(i);
    }
}

void R2AIWidget::onSettingsClicked()
{
    QDialog *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("r2ai Settings"));
    QVBoxLayout *dlgLayout = new QVBoxLayout(dlg);

    QString apiVal = Core()->getConfig("r2ai.api");
    QString modelVal = Core()->getConfig("r2ai.model");

    QComboBox *providerCombo = new QComboBox(dlg);
    providerCombo->addItems(Core()->cmd("r2ai -e api=?").split('\n', Qt::SkipEmptyParts));
    setComboToValue(providerCombo, apiVal);

    QComboBox *modelCombo = new QComboBox(dlg);
    auto loadModels = [&]() {
        modelCombo->clear();
        Core()->setConfig("r2ai.api", providerCombo->currentText());
        modelCombo->addItems(Core()->cmd("r2ai -e model=?").split('\n', Qt::SkipEmptyParts));
    };
    loadModels();
    setComboToValue(modelCombo, modelVal);
    connect(providerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int) {
        loadModels();
    });

    QPlainTextEdit *sysEdit = new QPlainTextEdit(dlg);
    sysEdit->setPlainText(Core()->getConfig("r2ai.system"));

    QPlainTextEdit *promptEdit = new QPlainTextEdit(dlg);
    promptEdit->setPlainText(Core()->getConfig("r2ai.prompt"));

    QTableWidget *table = new QTableWidget(dlg);
    table->verticalHeader()->setVisible(false);
    table->setColumnCount(2);
    table->horizontalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    QStringList lines = Core()->cmd("r2ai -e").split('\n', Qt::SkipEmptyParts);
    int row = 0;
    for (const QString &ln : lines) {
        QStringList p = ln.split('=', Qt::KeepEmptyParts);
        if (p.size() != 2) {
            continue;
        }
        const QString &v = p[1].trimmed();
        const QString k = p[0].replace("r2ai.", "").trimmed();
        if (k == "api" || k == "model" || k == "system" || k == "prompt" || k == "async"
            || k == "async.purge") {
            continue;
        }
        table->insertRow(row);
        auto *ki = new QTableWidgetItem(k);
        ki->setFlags(ki->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, 0, ki);

        if (v == "true" || v == "false") {
            auto *cb = new QCheckBox(dlg);
            cb->setChecked(v == "true");
            table->setCellWidget(row, 1, cb);
        } else {
            table->setItem(row, 1, new QTableWidgetItem(v));
        }
        row++;
    }

    QHBoxLayout *panelsLayout = new QHBoxLayout();
    QWidget *leftPanel = new QWidget(dlg);
    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
    QWidget *rightPanel = new QWidget(dlg);
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);

    leftLayout->addWidget(new QLabel(tr("Provider:"), dlg));
    leftLayout->addWidget(providerCombo);
    leftLayout->addWidget(new QLabel(tr("Model:"), dlg));
    leftLayout->addWidget(modelCombo);

    QPushButton *apiKeyBtn = new QPushButton(tr("API Key"), dlg);
    leftLayout->addWidget(apiKeyBtn);
    connect(apiKeyBtn, &QPushButton::clicked, this, [=]() {
        QString provider = providerCombo->currentText();
        QString filePath = QDir::home().filePath(QString(".r2ai.%1-key").arg(provider));
        QString key;
        QFile keyFile(filePath);
        if (keyFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&keyFile);
            key = in.readLine();
            keyFile.close();
        }
        QDialog keyDlg(dlg);
        keyDlg.setWindowTitle(provider);
        QVBoxLayout *keyDlgLayout = new QVBoxLayout(&keyDlg);
        QLineEdit *keyEdit = new QLineEdit(&keyDlg);
        keyEdit->setText(key);
        keyDlgLayout->addWidget(keyEdit);
        QHBoxLayout *keyBtnLayout = new QHBoxLayout();
        QPushButton *cancelKeyBtn = new QPushButton(tr("Cancel"), &keyDlg);
        QPushButton *saveKeyBtn = new QPushButton(tr("Save"), &keyDlg);
        keyBtnLayout->addWidget(cancelKeyBtn);
        keyBtnLayout->addWidget(saveKeyBtn);
        keyDlgLayout->addLayout(keyBtnLayout);
        connect(cancelKeyBtn, &QPushButton::clicked, &keyDlg, &QDialog::reject);
        connect(saveKeyBtn, &QPushButton::clicked, &keyDlg, &QDialog::accept);
        if (keyDlg.exec() == QDialog::Accepted) {
            QFile outFile(filePath);
            if (outFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                QTextStream out(&outFile);
                out << keyEdit->text();
                outFile.close();
            }
        }
    });
    leftLayout->addWidget(new QLabel(tr("System Prompt:"), dlg));
    leftLayout->addWidget(sysEdit);
    leftLayout->addWidget(new QLabel(tr("User Prompt:"), dlg));
    leftLayout->addWidget(promptEdit);

    rightLayout->addWidget(new QLabel(tr("Other Options:"), dlg));
    rightLayout->addWidget(table);

    panelsLayout->addWidget(leftPanel);
    panelsLayout->addWidget(rightPanel);
    dlgLayout->addLayout(panelsLayout);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto *saveBtn = new QPushButton(tr("Save"), dlg);
    auto *cancelBtn = new QPushButton(tr("Cancel"), dlg);
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(saveBtn);
    dlgLayout->addLayout(btnLayout);
    connect(saveBtn, &QPushButton::clicked, dlg, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);

    if (dlg->exec() == QDialog::Accepted) {
        Core()->setConfig("r2ai.api", providerCombo->currentText());
        Core()->setConfig("r2ai.model", modelCombo->currentText());
        Core()->setConfig("r2ai.system", sysEdit->toPlainText());
        Core()->setConfig("r2ai.prompt", promptEdit->toPlainText());
        for (int i = 0; i < table->rowCount(); i++) {
            QString key = table->item(i, 0)->text();
            QString val;
            if (auto *cb = qobject_cast<QCheckBox *>(table->cellWidget(i, 1))) {
                val = cb->isChecked() ? "true" : "false";
            } else if (auto *it = table->item(i, 1)) {
                val = it->text();
            }
            Core()->setConfig(QString("r2ai.%1").arg(key), val);
        }
        ensureAsyncConfig();
    }
}
