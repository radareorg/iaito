#include "dialogs/ScriptManagerWidget.h"

#include "common/RunScriptTask.h"
#include "common/TypeScriptHighlighter.h"
#include "core/Iaito.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSplitter>
#include <QSyntaxHighlighter>
#include <QTemporaryFile>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextStream>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

enum class ScriptLanguage { Radare2, JavaScript, Python };

ScriptLanguage detectLanguage(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("js") || suffix == QLatin1String("ts")) {
        return ScriptLanguage::JavaScript;
    }
    if (suffix == QLatin1String("py")) {
        return ScriptLanguage::Python;
    }
    return ScriptLanguage::Radare2;
}

class ScriptSyntaxHighlighter : public QSyntaxHighlighter
{
public:
    ScriptSyntaxHighlighter(QTextDocument *document, ScriptLanguage language)
        : QSyntaxHighlighter(document)
    {
        QTextCharFormat keywordFormat;
        keywordFormat.setForeground(QColor(53, 132, 228));
        keywordFormat.setFontWeight(QFont::Bold);

        QTextCharFormat commentFormat;
        commentFormat.setForeground(QColor(119, 118, 123));

        QTextCharFormat stringFormat;
        stringFormat.setForeground(QColor(38, 162, 105));

        QTextCharFormat numberFormat;
        numberFormat.setForeground(QColor(230, 97, 0));

        addRule(QStringLiteral(R"(\b0x[0-9a-fA-F]+\b)"), numberFormat);
        addRule(QStringLiteral(R"("[^"]*"|'[^']*')"), stringFormat);

        switch (language) {
        case ScriptLanguage::JavaScript:
            addRules(
                {QStringLiteral(
                    R"(\b(let|const|var|function|return|if|else|for|while|class|import|from|new|async|await|try|catch)\b)")},
                keywordFormat);
            addRule(QStringLiteral(R"(//[^\n]*)"), commentFormat);
            break;
        case ScriptLanguage::Python:
            addRules(
                {QStringLiteral(
                    R"(\b(def|class|return|if|elif|else|for|while|try|except|finally|with|pass|import|from|as|yield|async|await)\b)")},
                keywordFormat);
            addRule(QStringLiteral(R"(#[^\n]*)"), commentFormat);
            break;
        case ScriptLanguage::Radare2:
            addRules(
                {QStringLiteral(R"(^\s*[.?]?[A-Za-z][A-Za-z0-9._-]*)"),
                 QStringLiteral(
                     R"(\b(af|afl|afr|aaa|aae|aap|pd|pdf|px|izz|is|ii|f|fs|s|e|wx|wv|ood|oodf|db|dc|dr|afvd)\b)")},
                keywordFormat);
            addRule(QStringLiteral(R"(#[^\n]*)"), commentFormat);
            addRule(QStringLiteral(R"(;[^\n]*)"), commentFormat);
            break;
        }
    }

protected:
    void highlightBlock(const QString &text) override
    {
        for (const Rule &rule : rules) {
            auto matchIterator = rule.pattern.globalMatch(text);
            while (matchIterator.hasNext()) {
                const auto match = matchIterator.next();
                setFormat(match.capturedStart(), match.capturedLength(), rule.format);
            }
        }
    }

private:
    struct Rule
    {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    void addRule(const QString &pattern, const QTextCharFormat &format)
    {
        rules.append({QRegularExpression(pattern), format});
    }

    void addRules(const QStringList &patterns, const QTextCharFormat &format)
    {
        for (const QString &pattern : patterns) {
            addRule(pattern, format);
        }
    }

    QVector<Rule> rules;
};

} // namespace

ScriptManagerWidget::ScriptManagerWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);

    auto *descriptionLabel = new QLabel(
        tr("Manage reusable scripts for the current session. "
           "<b>rc</b> runs on startup and <b>rc2</b> runs when a file is loaded."),
        this);
    descriptionLabel->setWordWrap(true);

    locationLabel = new QLabel(this);
    locationLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    openDirectoryButton = new QToolButton(this);
    openDirectoryButton->setText(tr("Open Folder"));
    connect(
        openDirectoryButton,
        &QToolButton::clicked,
        this,
        &ScriptManagerWidget::openScriptsDirectory);

    auto *locationLayout = new QHBoxLayout();
    locationLayout->addWidget(locationLabel, 1);
    locationLayout->addWidget(openDirectoryButton);

    scriptTree = new QTreeWidget(this);
    scriptTree->setColumnCount(1);
    scriptTree->setHeaderHidden(true);
    scriptTree->setMinimumWidth(240);
    connect(scriptTree, &QTreeWidget::itemActivated, this, &ScriptManagerWidget::selectScriptFromTree);
    connect(scriptTree, &QTreeWidget::itemClicked, this, &ScriptManagerWidget::selectScriptFromTree);

    currentFileLabel = new QLabel(this);
    currentFileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    editor = new QPlainTextEdit(this);
    editor->setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);
    editor->setPlaceholderText(
        tr("Write radare2, JavaScript, Python, or r2pipe helper scripts here."));
    editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    outputTextEdit = new QPlainTextEdit(this);
    outputTextEdit->setReadOnly(true);
    outputTextEdit->setMaximumBlockCount(5000);
    outputTextEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    outputTextEdit->setPlaceholderText(tr("Script output will appear here."));

    auto *editorContainer = new QWidget(this);
    auto *editorLayout = new QVBoxLayout(editorContainer);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->addWidget(currentFileLabel);
    editorLayout->addWidget(editor, 1);

    auto *outputLabel = new QLabel(tr("Output"), this);
    auto *outputContainer = new QWidget(this);
    auto *outputLayout = new QVBoxLayout(outputContainer);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    outputLayout->addWidget(outputLabel);
    outputLayout->addWidget(outputTextEdit, 1);

    auto *rightSplitter = new QSplitter(Qt::Vertical, this);
    rightSplitter->addWidget(editorContainer);
    rightSplitter->addWidget(outputContainer);
    rightSplitter->setStretchFactor(0, 4);
    rightSplitter->setStretchFactor(1, 2);

    auto *contentSplitter = new QSplitter(Qt::Horizontal, this);
    contentSplitter->addWidget(scriptTree);
    contentSplitter->addWidget(rightSplitter);
    contentSplitter->setStretchFactor(0, 1);
    contentSplitter->setStretchFactor(1, 3);

    newButton = new QPushButton(tr("New"), this);
    openButton = new QPushButton(tr("Open..."), this);
    saveButton = new QPushButton(tr("Save"), this);
    saveAsButton = new QPushButton(tr("Save As..."), this);
    reloadButton = new QPushButton(tr("Reload"), this);
    runButton = new QPushButton(tr("Run"), this);
    stopButton = new QPushButton(tr("Stop"), this);

    connect(newButton, &QPushButton::clicked, this, &ScriptManagerWidget::newScript);
    connect(openButton, &QPushButton::clicked, this, &ScriptManagerWidget::openScript);
    connect(saveButton, &QPushButton::clicked, this, &ScriptManagerWidget::saveScript);
    connect(saveAsButton, &QPushButton::clicked, this, &ScriptManagerWidget::saveScriptAs);
    connect(reloadButton, &QPushButton::clicked, this, &ScriptManagerWidget::reloadCurrentScript);
    connect(runButton, &QPushButton::clicked, this, &ScriptManagerWidget::runCurrentScript);
    connect(stopButton, &QPushButton::clicked, this, &ScriptManagerWidget::stopRunningScript);
    connect(editor->document(), &QTextDocument::modificationChanged, this, [this](bool) {
        updateCurrentFileLabel();
        updateButtonState();
    });

    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(newButton);
    buttonLayout->addWidget(openButton);
    buttonLayout->addWidget(saveButton);
    buttonLayout->addWidget(saveAsButton);
    buttonLayout->addWidget(reloadButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(runButton);
    buttonLayout->addWidget(stopButton);

    mainLayout->addWidget(descriptionLabel);
    mainLayout->addLayout(locationLayout);
    mainLayout->addWidget(contentSplitter, 1);
    mainLayout->addLayout(buttonLayout);

    locationLabel->setText(
        tr("Scripts directory: %1").arg(QDir::toNativeSeparators(scriptsDirectoryPath())));
    refreshScriptList();
    updateHighlighter();
    updateCurrentFileLabel();
    updateButtonState();
}

ScriptManagerWidget::~ScriptManagerWidget() = default;

bool ScriptManagerWidget::maybeSave()
{
    if (!editor->document()->isModified()) {
        return true;
    }

    const auto choice = QMessageBox::warning(
        this,
        tr("Unsaved script"),
        tr("Save changes to %1 before continuing?").arg(displayNameForPath(currentFilePath)),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (choice == QMessageBox::Cancel) {
        return false;
    }
    if (choice == QMessageBox::Discard) {
        return true;
    }

    if (currentFilePath.isEmpty()) {
        saveScriptAs();
    } else {
        saveScriptFile(currentFilePath);
    }
    return !editor->document()->isModified();
}

void ScriptManagerWidget::shutdown()
{
    if (runningTask && runningTask->isRunning()) {
        runningTask->interrupt();
        runningTask->wait();
        runningTask.clear();
    }
    temporaryRunFile.reset();
}

void ScriptManagerWidget::refreshScriptList()
{
    const QString selectedPath = currentFilePath;
    const QSignalBlocker blocker(scriptTree);
    scriptTree->clear();

    const QDir scriptsDir(scriptsDirectoryPath());

    auto *specialScripts = new QTreeWidgetItem(scriptTree, {tr("Session Scripts")});
    addScriptEntry(
        specialScripts, tr("Startup script (rc)"), scriptsDir.filePath(QStringLiteral("rc")));
    addScriptEntry(
        specialScripts, tr("On-load script (rc2)"), scriptsDir.filePath(QStringLiteral("rc2")));

    auto *savedScripts = new QTreeWidgetItem(
        scriptTree, {tr("Saved Scripts (%1)").arg(QDir::toNativeSeparators(scriptsDir.path()))});
    if (scriptsDir.exists()) {
        const QFileInfoList entries
            = scriptsDir
                  .entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &entry : entries) {
            const QString fileName = entry.fileName();
            if (fileName == QLatin1String("rc") || fileName == QLatin1String("rc2")) {
                continue;
            }
            addScriptEntry(savedScripts, fileName, entry.absoluteFilePath());
        }
    }

    auto *homeScripts = new QTreeWidgetItem(scriptTree, {tr("Legacy Home Scripts")});
    addScriptEntry(homeScripts, tr(".iaitorc"), QDir::home().filePath(QStringLiteral(".iaitorc")));
    addScriptEntry(homeScripts, tr(".iaitorc2"), QDir::home().filePath(QStringLiteral(".iaitorc2")));

    scriptTree->expandAll();
    selectPathInTree(selectedPath);
}

void ScriptManagerWidget::selectScriptFromTree(QTreeWidgetItem *item, int)
{
    const QString path = item ? item->data(0, ScriptPathRole).toString() : QString();
    if (path.isEmpty()) {
        return;
    }
    if (path == currentFilePath) {
        return;
    }
    if (!maybeSave()) {
        selectPathInTree(currentFilePath);
        return;
    }
    loadScriptFile(path);
}

void ScriptManagerWidget::newScript()
{
    if (!maybeSave()) {
        return;
    }

    currentFilePath.clear();
    editor->clear();
    editor->document()->setModified(false);
    outputTextEdit->clear();
    updateHighlighter();
    updateCurrentFileLabel();
    updateButtonState();
}

void ScriptManagerWidget::openScript()
{
    if (!maybeSave()) {
        return;
    }

    const QString fileName = QFileDialog::getOpenFileName(
        this,
        tr("Open script"),
        defaultSavePath(),
        tr("Script files (*.r2 *.js *.ts *.py *.lua *.sh);;All files (*)"),
        nullptr,
        QFILEDIALOG_FLAGS);
    if (fileName.isEmpty()) {
        return;
    }

    loadScriptFile(fileName);
}

void ScriptManagerWidget::saveScript()
{
    if (currentFilePath.isEmpty()) {
        saveScriptAs();
        return;
    }

    saveScriptFile(currentFilePath);
}

void ScriptManagerWidget::saveScriptAs()
{
    const QString path = QFileDialog::getSaveFileName(
        this,
        tr("Save script"),
        defaultSavePath(),
        tr("Script files (*.r2 *.js *.ts *.py *.lua *.sh);;All files (*)"),
        nullptr,
        QFILEDIALOG_FLAGS);
    if (path.isEmpty()) {
        return;
    }

    saveScriptFile(path);
}

void ScriptManagerWidget::reloadCurrentScript()
{
    if (currentFilePath.isEmpty()) {
        return;
    }

    if (editor->document()->isModified()) {
        const auto ret = QMessageBox::question(
            this,
            tr("Reload script"),
            tr("Discard local changes and reload %1?")
                .arg(QDir::toNativeSeparators(currentFilePath)));
        if (ret != QMessageBox::Yes) {
            return;
        }
    }

    loadScriptFile(currentFilePath);
}

void ScriptManagerWidget::runCurrentScript()
{
    if (runningTask && runningTask->isRunning()) {
        return;
    }

    const QString path = prepareRunnableScriptFile();
    if (path.isEmpty()) {
        return;
    }

    outputTextEdit->clear();
    outputTextEdit->appendPlainText(
        tr("Running %1").arg(QDir::toNativeSeparators(displayNameForPath(path))));

    auto task = QSharedPointer<RunScriptTask>::create();
    task->setFileName(path);
    connect(task.data(), &AsyncTask::logChanged, this, [this](const QString &log) {
        outputTextEdit->setPlainText(log);
        outputTextEdit->moveCursor(QTextCursor::End);
    });
    connect(task.data(), &AsyncTask::finished, this, [this]() {
        runningTask.clear();
        temporaryRunFile.reset();
        updateButtonState();
    });

    runningTask = task;
    updateButtonState();
    Core()->getAsyncTaskManager()->start(task);
}

void ScriptManagerWidget::stopRunningScript()
{
    if (!runningTask || !runningTask->isRunning()) {
        return;
    }

    runningTask->interrupt();
    outputTextEdit->appendPlainText(tr("\nStopping script..."));
    updateButtonState();
}

void ScriptManagerWidget::openScriptsDirectory()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(scriptsDirectoryPath()));
}

bool ScriptManagerWidget::loadScriptFile(const QString &path)
{
    QFile file(path);
    QString contents;

    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::warning(
                this, tr("Open script"), tr("Cannot open %1").arg(QDir::toNativeSeparators(path)));
            return false;
        }
        contents = QString::fromUtf8(file.readAll());
    }

    currentFilePath = path;
    editor->setPlainText(contents);
    editor->document()->setModified(false);
    updateHighlighter();
    updateCurrentFileLabel();
    updateButtonState();
    selectPathInTree(path);
    return true;
}

bool ScriptManagerWidget::saveScriptFile(const QString &path)
{
    const QFileInfo fileInfo(path);
    if (!fileInfo.dir().exists()) {
        fileInfo.dir().mkpath(QStringLiteral("."));
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(
            this, tr("Save script"), tr("Cannot save %1").arg(QDir::toNativeSeparators(path)));
        return false;
    }

    QTextStream stream(&file);
    stream << editor->toPlainText();
    if (!file.commit()) {
        QMessageBox::warning(
            this, tr("Save script"), tr("Cannot write %1").arg(QDir::toNativeSeparators(path)));
        return false;
    }

    currentFilePath = path;
    editor->document()->setModified(false);
    refreshScriptList();
    updateHighlighter();
    updateCurrentFileLabel();
    updateButtonState();
    return true;
}

void ScriptManagerWidget::addScriptEntry(
    QTreeWidgetItem *parent, const QString &label, const QString &path)
{
    auto *item = new QTreeWidgetItem(parent, {label});
    item->setData(0, ScriptPathRole, path);
    item->setToolTip(0, QDir::toNativeSeparators(path));
}

void ScriptManagerWidget::updateCurrentFileLabel()
{
    const QString pathLabel = currentFilePath.isEmpty() ? tr("Unsaved script")
                                                        : QDir::toNativeSeparators(currentFilePath);
    const QString modified = editor->document()->isModified() ? tr(" (modified)") : QString();
    currentFileLabel->setText(tr("Current script: %1%2").arg(pathLabel, modified));
}

void ScriptManagerWidget::updateButtonState()
{
    const bool isRunning = runningTask && runningTask->isRunning();
    saveButton->setEnabled(editor->document()->isModified() || !currentFilePath.isEmpty());
    saveAsButton->setEnabled(true);
    reloadButton->setEnabled(!currentFilePath.isEmpty());
    runButton->setEnabled(!isRunning && !editor->toPlainText().trimmed().isEmpty());
    stopButton->setEnabled(isRunning);
}

void ScriptManagerWidget::updateHighlighter()
{
    delete highlighter;
    if (detectLanguage(currentFilePath) == ScriptLanguage::JavaScript) {
        highlighter = new TypeScriptHighlighter(editor->document());
        return;
    }

    highlighter = new ScriptSyntaxHighlighter(editor->document(), detectLanguage(currentFilePath));
}

void ScriptManagerWidget::selectPathInTree(const QString &path)
{
    if (path.isEmpty()) {
        scriptTree->clearSelection();
        return;
    }

    const QList<QTreeWidgetItem *> items
        = scriptTree->findItems(QStringLiteral("*"), Qt::MatchWildcard | Qt::MatchRecursive, 0);
    for (QTreeWidgetItem *item : items) {
        if (item->data(0, ScriptPathRole).toString() == path) {
            scriptTree->setCurrentItem(item);
            return;
        }
    }
    scriptTree->clearSelection();
}

QString ScriptManagerWidget::displayNameForPath(const QString &path) const
{
    if (path.isEmpty()) {
        return tr("the current buffer");
    }

    const QFileInfo info(path);
    const QString scriptsDir = scriptsDirectoryPath();
    if (path == QDir(scriptsDir).filePath(QStringLiteral("rc"))) {
        return tr("Startup script (rc)");
    }
    if (path == QDir(scriptsDir).filePath(QStringLiteral("rc2"))) {
        return tr("On-load script (rc2)");
    }
    return info.fileName().isEmpty() ? path : info.fileName();
}

QString ScriptManagerWidget::scriptsDirectoryPath() const
{
    return Core()->getIaitoRCDefaultDirectory().absolutePath();
}

QString ScriptManagerWidget::defaultSavePath() const
{
    if (!currentFilePath.isEmpty()) {
        return currentFilePath;
    }
    return QDir(scriptsDirectoryPath()).filePath(QStringLiteral("script.r2"));
}

QString ScriptManagerWidget::prepareRunnableScriptFile()
{
    if (!editor->document()->isModified() && !currentFilePath.isEmpty()) {
        temporaryRunFile.reset();
        return currentFilePath;
    }

    QString templatePath = QDir(QDir::tempPath()).filePath(QStringLiteral("iaito-script-XXXXXX"));
    const QString suffix = QFileInfo(currentFilePath).suffix();
    if (!suffix.isEmpty()) {
        templatePath += QLatin1Char('.');
        templatePath += suffix;
    } else {
        templatePath += QStringLiteral(".r2");
    }

    auto tempFile = std::make_unique<QTemporaryFile>(templatePath);
    tempFile->setAutoRemove(true);
    if (!tempFile->open()) {
        QMessageBox::warning(this, tr("Run script"), tr("Cannot create a temporary script file."));
        return QString();
    }

    tempFile->write(editor->toPlainText().toUtf8());
    tempFile->flush();

    const QString filePath = tempFile->fileName();
    temporaryRunFile = std::move(tempFile);
    return filePath;
}
