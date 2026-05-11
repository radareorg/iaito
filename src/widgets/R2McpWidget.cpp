#include "R2McpWidget.h"

#include "common/Helpers.h"
#include "core/Iaito.h"
#include "core/MainWindow.h"

#include <algorithm>
#include <limits>

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int STATUS_POLL_INTERVAL_MS = 3000;
constexpr int MAX_LOG_CHARS = 60000;
constexpr qint64 INITIAL_LOG_BYTES = 32768;

bool isR2McpAvailable()
{
    return !Core()->cmd("Lc~^r2mcp").trimmed().isEmpty();
}

QString normalizedMultiline(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text.trimmed();
}

void addCheckBox(QGridLayout *layout, QCheckBox *checkBox, int index)
{
    layout->addWidget(checkBox, index / 2, index % 2);
}

int boundedConfigInt(const QString &key, int minimum, int maximum)
{
    return std::max(minimum, std::min(maximum, Core()->getConfigi(key)));
}

} // namespace

R2McpWidget::R2McpWidget(MainWindow *main)
    : IaitoDockWidget(main)
{
    setObjectName(
        main ? main->getUniqueObjectName(QStringLiteral("R2McpWidget"))
             : QStringLiteral("R2McpWidget"));
    setWindowTitle(tr("r2mcp"));
    toggleViewAction()->setText(tr("r2mcp..."));

    setupUI();
    updateAvailability();
    ensureConfigKeys();
    loadConfig();
    updateStatus(false);

    pollTimer = new QTimer(this);
    pollTimer->setInterval(STATUS_POLL_INTERVAL_MS);
    connect(pollTimer, &QTimer::timeout, this, [this]() {
        updateAvailability();
        updateStatus(false);
        readLogFile();
    });
    pollTimer->start();
}

void R2McpWidget::setupUI()
{
    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);

    auto *topLayout = new QHBoxLayout;
    statusLabel = new QLabel(tr("Checking r2mcp..."), container);
    statusLabel->setMinimumWidth(220);
    startButton = new QPushButton(tr("Start"), container);
    stopButton = new QPushButton(tr("Stop"), container);
    restartButton = new QPushButton(tr("Restart"), container);
    statusButton = new QPushButton(tr("Status"), container);
    applyButton = new QPushButton(tr("Apply"), container);
    topLayout->addWidget(statusLabel, 1);
    topLayout->addWidget(startButton);
    topLayout->addWidget(stopButton);
    topLayout->addWidget(restartButton);
    topLayout->addWidget(statusButton);
    topLayout->addWidget(applyButton);
    layout->addLayout(topLayout);

    auto *scrollArea = new QScrollArea(container);
    scrollArea->setWidgetResizable(true);
    settingsWidget = new QWidget(scrollArea);
    auto *settingsLayout = new QVBoxLayout(settingsWidget);

    auto *httpGroup = new QGroupBox(tr("HTTP"), settingsWidget);
    auto *httpForm = new QFormLayout(httpGroup);
    portSpinBox = new QSpinBox(httpGroup);
    portSpinBox->setRange(1, 65535);
    contentComboBox = new QComboBox(httpGroup);
    contentComboBox->setEditable(true);
    contentComboBox->addItems(
        {QStringLiteral("text"),
         QStringLiteral("json"),
         QStringLiteral("structured"),
         QStringLiteral("both")});
    baseUrlLineEdit = new QLineEdit(httpGroup);
    httpForm->addRow(tr("Port"), portSpinBox);
    httpForm->addRow(tr("Content"), contentComboBox);
    httpForm->addRow(tr("Proxy base URL"), baseUrlLineEdit);
    settingsLayout->addWidget(httpGroup);

    auto *accessGroup = new QGroupBox(tr("Access"), settingsWidget);
    auto *accessLayout = new QVBoxLayout(accessGroup);
    auto *accessGrid = new QGridLayout;
    logCheckBox = new QCheckBox(tr("Debug logs"), accessGroup);
    approveCheckBox = new QCheckBox(tr("Supervisor approval"), accessGroup);
    yoloCheckBox = new QCheckBox(tr("YOLO mode"), accessGroup);
    readonlyCheckBox = new QCheckBox(tr("Read only"), accessGroup);
    addCheckBox(accessGrid, logCheckBox, 0);
    addCheckBox(accessGrid, approveCheckBox, 1);
    addCheckBox(accessGrid, yoloCheckBox, 2);
    addCheckBox(accessGrid, readonlyCheckBox, 3);
    accessLayout->addLayout(accessGrid);
    auto *accessForm = new QFormLayout;
    logFileLineEdit = new QLineEdit(accessGroup);
    approvalUrlLineEdit = new QLineEdit(accessGroup);
    sandboxLineEdit = new QLineEdit(accessGroup);
    sandboxGrainLineEdit = new QLineEdit(accessGroup);
    accessForm->addRow(tr("Log file"), logFileLineEdit);
    accessForm->addRow(tr("Approval URL"), approvalUrlLineEdit);
    accessForm->addRow(tr("Sandbox"), sandboxLineEdit);
    accessForm->addRow(tr("Sandbox grain"), sandboxGrainLineEdit);
    accessLayout->addLayout(accessForm);
    settingsLayout->addWidget(accessGroup);

    auto *toolsGroup = new QGroupBox(tr("Tools"), settingsWidget);
    auto *toolsLayout = new QVBoxLayout(toolsGroup);
    auto *toolsGrid = new QGridLayout;
    miniCheckBox = new QCheckBox(tr("Mini tool set"), toolsGroup);
    permissiveCheckBox = new QCheckBox(tr("Permissive tools"), toolsGroup);
    runCommandCheckBox = new QCheckBox(tr("Run command tools"), toolsGroup);
    ignoreAnalysisCheckBox = new QCheckBox(tr("Ignore analysis level"), toolsGroup);
    promptsCheckBox = new QCheckBox(tr("Prompts"), toolsGroup);
    sessionToolsCheckBox = new QCheckBox(tr("Session tools"), toolsGroup);
    addCheckBox(toolsGrid, miniCheckBox, 0);
    addCheckBox(toolsGrid, permissiveCheckBox, 1);
    addCheckBox(toolsGrid, runCommandCheckBox, 2);
    addCheckBox(toolsGrid, ignoreAnalysisCheckBox, 3);
    addCheckBox(toolsGrid, promptsCheckBox, 4);
    addCheckBox(toolsGrid, sessionToolsCheckBox, 5);
    toolsLayout->addLayout(toolsGrid);
    auto *toolsForm = new QFormLayout;
    promptsDirLineEdit = new QLineEdit(toolsGroup);
    enabledToolsLineEdit = new QLineEdit(toolsGroup);
    disabledToolsLineEdit = new QLineEdit(toolsGroup);
    decompilerLineEdit = new QLineEdit(toolsGroup);
    toolsForm->addRow(tr("Prompts dir"), promptsDirLineEdit);
    toolsForm->addRow(tr("Enabled tools"), enabledToolsLineEdit);
    toolsForm->addRow(tr("Disabled tools"), disabledToolsLineEdit);
    toolsForm->addRow(tr("Decompiler"), decompilerLineEdit);
    toolsLayout->addLayout(toolsForm);
    settingsLayout->addWidget(toolsGroup);

    auto *sessionsGroup = new QGroupBox(tr("HTTP sessions"), settingsWidget);
    auto *sessionsForm = new QFormLayout(sessionsGroup);
    sessionsCheckBox = new QCheckBox(tr("X-Session-ID multiplexing"), sessionsGroup);
    sessionsMaxSpinBox = new QSpinBox(sessionsGroup);
    sessionsMaxSpinBox->setRange(0, std::numeric_limits<int>::max());
    sessionsTimeoutSpinBox = new QSpinBox(sessionsGroup);
    sessionsTimeoutSpinBox->setRange(0, std::numeric_limits<int>::max());
    sessionsTimeoutSpinBox->setSuffix(tr(" s"));
    sessionsForm->addRow(sessionsCheckBox);
    sessionsForm->addRow(tr("Max sessions"), sessionsMaxSpinBox);
    sessionsForm->addRow(tr("Idle timeout"), sessionsTimeoutSpinBox);
    settingsLayout->addWidget(sessionsGroup);

    settingsLayout->addStretch();
    scrollArea->setWidget(settingsWidget);
    layout->addWidget(scrollArea, 3);

    logTextEdit = new QPlainTextEdit(container);
    logTextEdit->setReadOnly(true);
    logTextEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    logTextEdit->setMinimumHeight(150);
    qhelpers::bindFont(logTextEdit, false, true);
    layout->addWidget(new QLabel(tr("Logs"), container));
    layout->addWidget(logTextEdit, 1);

    setWidget(container);

    connect(startButton, &QPushButton::clicked, this, &R2McpWidget::startServer);
    connect(stopButton, &QPushButton::clicked, this, &R2McpWidget::stopServer);
    connect(restartButton, &QPushButton::clicked, this, &R2McpWidget::restartServer);
    connect(statusButton, &QPushButton::clicked, this, &R2McpWidget::refreshStatus);
    connect(applyButton, &QPushButton::clicked, this, [this]() { applyConfig(); });
    connect(approveCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (loadingConfig || !checked) {
            return;
        }
        QSignalBlocker blocker(yoloCheckBox);
        yoloCheckBox->setChecked(false);
    });
    connect(yoloCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (loadingConfig || !checked) {
            return;
        }
        QSignalBlocker blocker(approveCheckBox);
        approveCheckBox->setChecked(false);
    });
    connect(logFileLineEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        if (loadingConfig || text.trimmed().isEmpty()) {
            return;
        }
        logCheckBox->setChecked(true);
    });
}

void R2McpWidget::ensureConfigKeys()
{
    if (r2mcpAvailable) {
        Core()->cmd("r2mcp config");
    }
}

void R2McpWidget::loadConfig()
{
    if (!r2mcpAvailable) {
        return;
    }

    loadingConfig = true;
    portSpinBox->setValue(boundedConfigInt(QStringLiteral("r2mcp.port"), 1, 65535));
    const QString content = Core()->getConfig("r2mcp.content");
    int contentIndex = contentComboBox->findText(content);
    if (contentIndex >= 0) {
        contentComboBox->setCurrentIndex(contentIndex);
    } else {
        contentComboBox->setEditText(content);
    }
    baseUrlLineEdit->setText(Core()->getConfig("r2mcp.baseurl"));

    const QString logFile = Core()->getConfig("r2mcp.logfile");
    logCheckBox->setChecked(Core()->getConfigb("r2mcp.log") || !logFile.isEmpty());
    logFileLineEdit->setText(logFile);
    approveCheckBox->setChecked(Core()->getConfigb("r2mcp.approve"));
    approvalUrlLineEdit->setText(Core()->getConfig("r2mcp.svc"));
    yoloCheckBox->setChecked(Core()->getConfigb("r2mcp.yolo"));
    readonlyCheckBox->setChecked(Core()->getConfigb("r2mcp.readonly"));
    sandboxLineEdit->setText(Core()->getConfig("r2mcp.sandbox"));
    sandboxGrainLineEdit->setText(Core()->getConfig("r2mcp.sandbox.grain"));

    miniCheckBox->setChecked(Core()->getConfigb("r2mcp.mini"));
    permissiveCheckBox->setChecked(Core()->getConfigb("r2mcp.permissive"));
    runCommandCheckBox->setChecked(Core()->getConfigb("r2mcp.run"));
    ignoreAnalysisCheckBox->setChecked(Core()->getConfigb("r2mcp.ignore_analysis"));
    promptsCheckBox->setChecked(Core()->getConfigb("r2mcp.prompts"));
    sessionToolsCheckBox->setChecked(Core()->getConfigb("r2mcp.session_tools"));
    promptsDirLineEdit->setText(Core()->getConfig("r2mcp.prompts.dir"));
    enabledToolsLineEdit->setText(Core()->getConfig("r2mcp.enabled"));
    disabledToolsLineEdit->setText(Core()->getConfig("r2mcp.disabled"));
    decompilerLineEdit->setText(Core()->getConfig("r2mcp.decompiler"));

    sessionsCheckBox->setChecked(Core()->getConfigb("r2mcp.sessions"));
    sessionsMaxSpinBox->setValue(
        boundedConfigInt(QStringLiteral("r2mcp.sessions.max"), 0, std::numeric_limits<int>::max()));
    sessionsTimeoutSpinBox->setValue(boundedConfigInt(
        QStringLiteral("r2mcp.sessions.timeout"), 0, std::numeric_limits<int>::max()));
    loadingConfig = false;
}

void R2McpWidget::applyConfig()
{
    applyConfig(true);
}

void R2McpWidget::applyConfig(bool appendOutput)
{
    if (!r2mcpAvailable) {
        return;
    }

    const bool yolo = yoloCheckBox->isChecked();
    const bool approve = !yolo && approveCheckBox->isChecked();
    const bool logEnabled = logCheckBox->isChecked();
    const QString logFile = logEnabled ? logFileLineEdit->text().trimmed() : QString();

    Core()->setConfig("r2mcp.port", portSpinBox->value());
    Core()->setConfig("r2mcp.content", contentComboBox->currentText().trimmed());
    Core()->setConfig("r2mcp.baseurl", baseUrlLineEdit->text().trimmed());
    Core()->setConfig("r2mcp.log", logEnabled);
    Core()->setConfig("r2mcp.logfile", logFile);
    Core()->setConfig("r2mcp.approve", approve);
    Core()->setConfig("r2mcp.svc", approvalUrlLineEdit->text().trimmed());
    Core()->setConfig("r2mcp.yolo", yolo);
    Core()->setConfig("r2mcp.readonly", readonlyCheckBox->isChecked());
    Core()->setConfig("r2mcp.sandbox", sandboxLineEdit->text().trimmed());
    Core()->setConfig("r2mcp.sandbox.grain", sandboxGrainLineEdit->text().trimmed());
    Core()->setConfig("r2mcp.mini", miniCheckBox->isChecked());
    Core()->setConfig("r2mcp.permissive", permissiveCheckBox->isChecked());
    Core()->setConfig("r2mcp.run", runCommandCheckBox->isChecked());
    Core()->setConfig("r2mcp.ignore_analysis", ignoreAnalysisCheckBox->isChecked());
    Core()->setConfig("r2mcp.prompts", promptsCheckBox->isChecked());
    Core()->setConfig("r2mcp.session_tools", sessionToolsCheckBox->isChecked());
    Core()->setConfig("r2mcp.prompts.dir", promptsDirLineEdit->text().trimmed());
    Core()->setConfig("r2mcp.enabled", enabledToolsLineEdit->text().trimmed());
    Core()->setConfig("r2mcp.disabled", disabledToolsLineEdit->text().trimmed());
    Core()->setConfig("r2mcp.decompiler", decompilerLineEdit->text().trimmed());
    Core()->setConfig("r2mcp.sessions", sessionsCheckBox->isChecked());
    Core()->setConfig("r2mcp.sessions.max", sessionsMaxSpinBox->value());
    Core()->setConfig("r2mcp.sessions.timeout", sessionsTimeoutSpinBox->value());

    runCommand(
        logEnabled ? QStringLiteral("r2mcp logs on") : QStringLiteral("r2mcp logs off"),
        appendOutput);
    if (yolo) {
        runCommand(QStringLiteral("r2mcp approve off"), appendOutput);
        runCommand(QStringLiteral("r2mcp yolo on"), appendOutput);
    } else {
        runCommand(QStringLiteral("r2mcp yolo off"), appendOutput);
        runCommand(
            approve ? QStringLiteral("r2mcp approve on") : QStringLiteral("r2mcp approve off"),
            appendOutput);
    }

    if (appendOutput) {
        appendLog(
            tr("Applied r2mcp configuration. Restart the server to apply HTTP, session, tool-set, "
               "sandbox, prompt, decompiler, content, and proxy URL changes."));
    }
    if (currentLogFilePath != logFile) {
        currentLogFilePath = logFile;
        currentLogFileOffset = -1;
    }
    updateStatus(false);
    readLogFile();
}

void R2McpWidget::updateAvailability()
{
    r2mcpAvailable = isR2McpAvailable();
    setControlsEnabled(r2mcpAvailable);
    if (!r2mcpAvailable) {
        statusLabel->setText(tr("r2mcp is not loaded"));
    }
}

void R2McpWidget::setControlsEnabled(bool enabled)
{
    settingsWidget->setEnabled(enabled);
    startButton->setEnabled(enabled);
    stopButton->setEnabled(enabled);
    restartButton->setEnabled(enabled);
    statusButton->setEnabled(enabled);
    applyButton->setEnabled(enabled);
}

void R2McpWidget::startServer()
{
    applyConfig(false);
    runCommand(QStringLiteral("r2mcp start"));
    updateStatus(false);
    readLogFile();
}

void R2McpWidget::stopServer()
{
    runCommand(QStringLiteral("r2mcp stop"));
    updateStatus(false);
}

void R2McpWidget::restartServer()
{
    applyConfig(false);
    runCommand(QStringLiteral("r2mcp restart"));
    updateStatus(false);
    readLogFile();
}

void R2McpWidget::refreshStatus()
{
    updateStatus(true);
    readLogFile();
}

void R2McpWidget::updateStatus(bool appendOutput)
{
    if (!r2mcpAvailable) {
        return;
    }

    const QString output = runCommand(QStringLiteral("r2mcp status"), appendOutput);
    bool running = false;
    QString url;
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed == QLatin1String("http: running")) {
            running = true;
        } else if (trimmed.startsWith(QStringLiteral("url:"))) {
            url = trimmed.mid(4).trimmed();
        }
    }

    if (running) {
        statusLabel->setText(url.isEmpty() ? tr("Running") : tr("Running: %1").arg(url));
    } else {
        statusLabel->setText(tr("Stopped"));
    }
    startButton->setEnabled(r2mcpAvailable && !running);
    stopButton->setEnabled(r2mcpAvailable && running);
    restartButton->setEnabled(r2mcpAvailable);
}

QString R2McpWidget::runCommand(const QString &command, bool appendOutput)
{
    if (!r2mcpAvailable) {
        return QString();
    }

    const QString result = Core()->cmd(command);
    if (appendOutput) {
        appendCommandResult(command, result);
    }
    return result;
}

void R2McpWidget::appendCommandResult(const QString &command, const QString &result)
{
    QString text = QStringLiteral("> %1").arg(command);
    const QString output = normalizedMultiline(result);
    if (!output.isEmpty()) {
        text += QLatin1Char('\n') + output;
    }
    appendLog(text);
}

void R2McpWidget::appendLog(const QString &text)
{
    const QString normalized = normalizedMultiline(text);
    if (normalized.isEmpty()) {
        return;
    }
    if (!logTextEdit->toPlainText().isEmpty()) {
        logTextEdit->appendPlainText(QString());
    }
    logTextEdit->appendPlainText(normalized);
    trimLog();
    logTextEdit->verticalScrollBar()->setValue(logTextEdit->verticalScrollBar()->maximum());
}

void R2McpWidget::trimLog()
{
    const QString text = logTextEdit->toPlainText();
    if (text.size() <= MAX_LOG_CHARS) {
        return;
    }

    QString trimmed = text.right(MAX_LOG_CHARS);
    const int firstNewline = trimmed.indexOf(QLatin1Char('\n'));
    if (firstNewline >= 0) {
        trimmed = trimmed.mid(firstNewline + 1);
    }
    QSignalBlocker blocker(logTextEdit);
    logTextEdit->setPlainText(trimmed);
}

void R2McpWidget::readLogFile()
{
    const QString path = logCheckBox->isChecked() ? logFileLineEdit->text().trimmed() : QString();
    if (path.isEmpty()) {
        return;
    }

    if (path != currentLogFilePath) {
        currentLogFilePath = path;
        currentLogFileOffset = -1;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    const qint64 size = file.size();
    if (currentLogFileOffset < 0) {
        currentLogFileOffset = std::max<qint64>(0, size - INITIAL_LOG_BYTES);
    } else if (currentLogFileOffset > size) {
        currentLogFileOffset = 0;
    }
    if (!file.seek(currentLogFileOffset)) {
        return;
    }

    const QByteArray bytes = file.readAll();
    currentLogFileOffset = file.pos();
    if (!bytes.isEmpty()) {
        appendLog(QString::fromLocal8Bit(bytes));
    }
}

QWidget *R2McpWidget::widgetToFocusOnRaise()
{
    return startButton;
}
