#ifndef SCRIPTMANAGERDIALOG_H
#define SCRIPTMANAGERDIALOG_H

#include <QDialog>
#include <QSharedPointer>

#include <memory>

class QCloseEvent;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSyntaxHighlighter;
class QTreeWidget;
class QTreeWidgetItem;
class QToolButton;
class RunScriptTask;

class ScriptManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ScriptManagerDialog(QWidget *parent = nullptr);
    ~ScriptManagerDialog() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void refreshScriptList();
    void selectScriptFromTree(QTreeWidgetItem *item, int column);
    void newScript();
    void openScript();
    void saveScript();
    void saveScriptAs();
    void reloadCurrentScript();
    void runCurrentScript();
    void stopRunningScript();
    void openScriptsDirectory();

private:
    static constexpr int ScriptPathRole = Qt::UserRole;

    bool maybeSave();
    bool loadScriptFile(const QString &path);
    bool saveScriptFile(const QString &path);
    void addScriptEntry(QTreeWidgetItem *parent, const QString &label, const QString &path);
    void updateCurrentFileLabel();
    void updateButtonState();
    void updateHighlighter();
    void selectPathInTree(const QString &path);
    QString displayNameForPath(const QString &path) const;
    QString scriptsDirectoryPath() const;
    QString defaultSavePath() const;
    QString prepareRunnableScriptFile();

    QTreeWidget *scriptTree;
    QLabel *locationLabel;
    QLabel *currentFileLabel;
    QPlainTextEdit *editor;
    QPlainTextEdit *outputTextEdit;
    QPushButton *newButton;
    QPushButton *openButton;
    QPushButton *saveButton;
    QPushButton *saveAsButton;
    QPushButton *reloadButton;
    QPushButton *runButton;
    QPushButton *stopButton;
    QToolButton *openDirectoryButton;

    QString currentFilePath;
    QSyntaxHighlighter *highlighter = nullptr;
    QSharedPointer<RunScriptTask> runningTask;
    std::unique_ptr<class QTemporaryFile> temporaryRunFile;
};

#endif // SCRIPTMANAGERDIALOG_H
