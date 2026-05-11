#ifndef R2MCPWIDGET_H
#define R2MCPWIDGET_H

#include "IaitoDockWidget.h"

#include <QString>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTimer;
class QWidget;

class R2McpWidget : public IaitoDockWidget
{
    Q_OBJECT

public:
    explicit R2McpWidget(MainWindow *main);

private slots:
    void applyConfig();
    void refreshStatus();
    void restartServer();
    void startServer();
    void stopServer();

private:
    void setupUI();
    void ensureConfigKeys();
    void loadConfig();
    void applyConfig(bool appendOutput);
    void updateAvailability();
    void updateStatus(bool appendOutput);
    void setControlsEnabled(bool enabled);
    void appendCommandResult(const QString &command, const QString &result);
    void appendLog(const QString &text);
    void trimLog();
    void readLogFile();
    QString runCommand(const QString &command, bool appendOutput = true);
    QWidget *widgetToFocusOnRaise() override;

    QWidget *settingsWidget = nullptr;
    QLabel *statusLabel = nullptr;
    QPushButton *startButton = nullptr;
    QPushButton *stopButton = nullptr;
    QPushButton *restartButton = nullptr;
    QPushButton *statusButton = nullptr;
    QPushButton *applyButton = nullptr;

    QSpinBox *portSpinBox = nullptr;
    QComboBox *contentComboBox = nullptr;
    QLineEdit *baseUrlLineEdit = nullptr;
    QCheckBox *logCheckBox = nullptr;
    QLineEdit *logFileLineEdit = nullptr;
    QCheckBox *approveCheckBox = nullptr;
    QLineEdit *approvalUrlLineEdit = nullptr;
    QCheckBox *yoloCheckBox = nullptr;
    QCheckBox *miniCheckBox = nullptr;
    QCheckBox *permissiveCheckBox = nullptr;
    QCheckBox *runCommandCheckBox = nullptr;
    QCheckBox *readonlyCheckBox = nullptr;
    QCheckBox *ignoreAnalysisCheckBox = nullptr;
    QCheckBox *promptsCheckBox = nullptr;
    QLineEdit *promptsDirLineEdit = nullptr;
    QLineEdit *sandboxLineEdit = nullptr;
    QLineEdit *sandboxGrainLineEdit = nullptr;
    QLineEdit *enabledToolsLineEdit = nullptr;
    QLineEdit *disabledToolsLineEdit = nullptr;
    QLineEdit *decompilerLineEdit = nullptr;
    QCheckBox *sessionToolsCheckBox = nullptr;
    QCheckBox *sessionsCheckBox = nullptr;
    QSpinBox *sessionsMaxSpinBox = nullptr;
    QSpinBox *sessionsTimeoutSpinBox = nullptr;
    QPlainTextEdit *logTextEdit = nullptr;
    QTimer *pollTimer = nullptr;

    QString currentLogFilePath;
    qint64 currentLogFileOffset = -1;
    bool r2mcpAvailable = false;
    bool loadingConfig = false;
};

#endif // R2MCPWIDGET_H
