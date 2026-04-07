#ifndef SCRIPTMANAGERWIDGET_H
#define SCRIPTMANAGERWIDGET_H

#include <QSharedPointer>
#include <QWidget>

#include <memory>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSyntaxHighlighter;
class QTreeWidget;
class QTreeWidgetItem;
class QToolButton;
class RunScriptTask;

class ScriptManagerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ScriptManagerWidget(QWidget *parent = nullptr);
    ~ScriptManagerWidget() override;

    bool maybeSave();
    void shutdown();

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

#endif // SCRIPTMANAGERWIDGET_H
