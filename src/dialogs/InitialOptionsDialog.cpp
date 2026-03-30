#include "InitialOptionsDialog.h"
#include "common/AsyncTask.h"
#include "ui_InitialOptionsDialog.h"

#include "common/Helpers.h"
#include "core/MainWindow.h"
#include "dialogs/AsyncTaskDialog.h"
#include "dialogs/NewFileDialog.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

#include "common/AnalTask.h"
#include "core/Iaito.h"

static QPlainTextEdit *s_analysisLogWidget = nullptr;

// Called from r_cons_is_breaked() which fires very frequently during analysis.
// Pumps the Qt event loop so the UI stays responsive (progress bar, interrupt button).
// Throttled to avoid slowing down analysis.
static void analysisBreakCallback(void *user)
{
    Q_UNUSED(user);
    static qint64 lastPump = 0;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastPump > 100) {
        lastPump = now;
        QApplication::processEvents();
    }
}

// Captures R_LOG messages from radare2 into the progress dialog log area.
static bool analysisLogCallback(void *user, int type, const char *origin, const char *msg)
{
    Q_UNUSED(user);
    Q_UNUSED(type);
    Q_UNUSED(origin);
    if (s_analysisLogWidget && msg) {
        QString text = QString::fromUtf8(msg).trimmed();
        if (!text.isEmpty()) {
            s_analysisLogWidget->appendPlainText(text);
            QScrollBar *sb = s_analysisLogWidget->verticalScrollBar();
            sb->setValue(sb->maximum());
        }
    }
    return false;
}

InitialOptionsDialog::InitialOptionsDialog(MainWindow *main)
    : QDialog(nullptr)
    , // parent must not be main
    ui(new Ui::InitialOptionsDialog)
    , main(main)
    , core(Core())
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() & (~Qt::WindowContextHelpButtonHint));
    ui->logoSvgWidget->load(Config()->getLogoFile());

    // Fill the plugins combo
    asmPlugins = core->getRAsmPluginDescriptions();
    for (const auto &plugin : asmPlugins) {
        ui->archComboBox->addItem(plugin.name, plugin.name);
    }
    auto analPlugins = core->getRAnalPluginDescriptions();
    for (const auto &plugin : analPlugins) {
        bool found = false;
        // check if it doesnt exist
        for (const auto &ap : asmPlugins) {
            if (ap.name == plugin.name) {
                found = true;
                break;
            }
        }
        if (!found) {
            ui->archComboBox->addItem(plugin.name, plugin.name);
        }
    }

    setTooltipWithConfigHelp(ui->archComboBox, "asm.arch");
    ui->archComboBox->model()->sort(0);

    // cpu combo box
    ui->cpuComboBox->lineEdit()->setPlaceholderText(tr("Auto"));
    setTooltipWithConfigHelp(ui->cpuComboBox, "asm.cpu");

    updateCPUComboBox();

    // os combo box
    for (const auto &plugin : core->cmdList("e asm.os=?")) {
        ui->kernelComboBox->addItem(plugin, plugin);
    }

    setTooltipWithConfigHelp(ui->kernelComboBox, "asm.os");
    setTooltipWithConfigHelp(ui->bitsComboBox, "asm.bits");

    for (const auto &plugin : core->getRBinPluginDescriptions("bin")) {
        ui->formatComboBox->addItem(plugin.name, QVariant::fromValue(plugin));
    }

    analysisCommands = {
        {{"aa", tr("Analyze all symbols")}, new QCheckBox(), true},
        {{"aar", tr("Analyze instructions for references")}, new QCheckBox(), true},
        {{"aac", tr("Analyze function calls")}, new QCheckBox(), true},
        {{"aab", tr("Analyze all basic blocks")}, new QCheckBox(), false},
        {{"aao", tr("Analyze all objc references")}, new QCheckBox(), false},
        {{"avrr", tr("Recover class information from RTTI")}, new QCheckBox(), false},
        {{"aan", tr("Autoname functions based on context")}, new QCheckBox(), false},
        {{"aae", tr("Linearly emulate code to find computed references")}, new QCheckBox(), false},
        {{"aaef", tr("Emulate all functions to find computed references")}, new QCheckBox(), false},
        {{"aafr", tr("Analyze all consecutive functions")}, new QCheckBox(), false},
        {{"aaft", tr("Type and Argument matching analysis")}, new QCheckBox(), false},
        {{"aaT", tr("Analyze code after trap-sleds")}, new QCheckBox(), false},
        {{"aap", tr("Analyze function preludes")}, new QCheckBox(), false},
        {{"aaw", tr("Analyze data accesses as dwords")}, new QCheckBox(), false},
        {{"e! anal.jmp.tbl", tr("Analyze jump tables in switch statements")},
         new QCheckBox(),
         false},
        {{"e! anal.pushret", tr("Analyze PUSH+RET as JMP")}, new QCheckBox(), false},
        {{"e! anal.hasnext", tr("Continue analysis after each function")}, new QCheckBox(), false}};

    // Per each checkbox, set a tooltip desccribing it
    AnalysisCommands item;
    foreach (item, analysisCommands) {
        item.checkbox->setText(item.commandDesc.description);
        item.checkbox->setToolTip(item.commandDesc.command);
        item.checkbox->setChecked(item.checked);
        ui->verticalLayout_7->addWidget(item.checkbox);
    }

    ui->hideFrame->setVisible(false);
    ui->analoptionsFrame->setVisible(false);
    ui->advancedAnlysisLine->setVisible(false);

    updatePDBLayout();

    connect(ui->pdbCheckBox, &QCheckBox::toggled, this, [this](bool) { updatePDBLayout(); });

    updateScriptLayout();

    connect(ui->scriptCheckBox, &QCheckBox::toggled, this, [this](bool) { updateScriptLayout(); });

    connect(ui->cancelButton, &QPushButton::clicked, this, &InitialOptionsDialog::reject);

    ui->programLineEdit->setText(main->getFilename());
}

InitialOptionsDialog::~InitialOptionsDialog() {}

void InitialOptionsDialog::updateCPUComboBox()
{
    QString currentText = ui->cpuComboBox->lineEdit()->text();
    ui->cpuComboBox->clear();

    QString arch = getSelectedArch();
    QStringList cpus;
    if (!arch.isEmpty()) {
        auto pluginDescr = std::find_if(
            asmPlugins.begin(), asmPlugins.end(), [&](const RAsmPluginDescription &plugin) {
                return plugin.name == arch;
            });
        if (pluginDescr != asmPlugins.end()) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
            cpus = pluginDescr->cpus.split(",", Qt::SkipEmptyParts);
#else
            cpus = pluginDescr->cpus.split(",", QString::SkipEmptyParts);
#endif
        }
    }

    ui->cpuComboBox->addItem("");
    ui->cpuComboBox->addItems(cpus);

    ui->cpuComboBox->lineEdit()->setText(currentText);
}

QList<QString> InitialOptionsDialog::getAnalysisCommands(const InitialOptions &options)
{
    QList<QString> commands;
    for (auto &commandDesc : options.analCmd) {
        commands << commandDesc.command;
    }
    return commands;
}

void InitialOptionsDialog::loadOptions(const InitialOptions &options)
{
    if (options.analCmd.isEmpty()) {
        analLevel = 0;
    } else if (options.analCmd.first().command == "aa") {
        analLevel = 1;
    } else if (options.analCmd.first().command == "aac") {
        analLevel = 2;
    } else if (options.analCmd.first().command == "aaa") {
        analLevel = 3;
    } else if (options.analCmd.first().command == "aaaa") {
        analLevel = 4;
    } else if (options.analCmd.first().command == "aaaaa") {
        analLevel = 5;
    } else {
        analLevel = 6;
        AnalysisCommands item;
        QList<QString> commands = getAnalysisCommands(options);
        foreach (item, analysisCommands) {
            qInfo() << item.commandDesc.command;
            item.checkbox->setChecked(commands.contains(item.commandDesc.command));
        }
    }

    if (!options.script.isEmpty()) {
        ui->scriptCheckBox->setChecked(true);
        ui->scriptLineEdit->setText(options.script);
        analLevel = 0;
    } else {
        ui->scriptCheckBox->setChecked(false);
        ui->scriptLineEdit->setText("");
    }

    ui->analSlider->setValue(analLevel);

    shellcode = options.shellcode;

    if (!options.forceBinPlugin.isEmpty()) {
        ui->formatComboBox->setCurrentText(options.forceBinPlugin);
    } else {
        ui->formatComboBox->setCurrentIndex(0);
    }

    if (!options.arch.isEmpty()) {
        ui->archComboBox->setCurrentText(options.arch);
        updateCPUComboBox();
    }

    if (!options.cpu.isEmpty()) {
        ui->cpuComboBox->setCurrentText(options.cpu);
    } else {
        ui->cpuComboBox->setCurrentIndex(0);
    }

    ui->bitsComboBox->setCurrentText(options.bits > 0 ? QString::number(options.bits) : tr("Auto"));

    if (!options.os.isEmpty()) {
        ui->kernelComboBox->setCurrentText(options.os);
    }

    switch (options.endian) {
    case InitialOptions::Endianness::Little:
        ui->endiannessComboBox->setCurrentIndex(1);
        break;
    case InitialOptions::Endianness::Big:
        ui->endiannessComboBox->setCurrentIndex(2);
        break;
    default:
        ui->endiannessComboBox->setCurrentIndex(0);
        break;
    }

    if (options.binLoadAddr != RVA_INVALID) {
        ui->entry_loadOffset->setText(RAddressString(options.binLoadAddr));
    }

    if (options.mapAddr != RVA_INVALID) {
        ui->entry_mapOffset->setText(RAddressString(options.mapAddr));
    }

    ui->writeCheckBox->setChecked(options.writeEnabled);
    ui->binCheckBox->setChecked(!options.loadBinInfo);
    ui->binCacheCheckBox->setChecked(options.loadBinCache);
    ui->demangleCheckBox->setChecked(options.demangle);
    ui->varCheckBox->setChecked(options.analVars);
}

void InitialOptionsDialog::setTooltipWithConfigHelp(QWidget *w, const char *config)
{
    w->setToolTip(QStringLiteral("%1 (%2)").arg(core->getConfigDescription(config)).arg(config));
}

QString InitialOptionsDialog::getSelectedArch() const
{
    QVariant archValue = ui->archComboBox->currentData();
    return archValue.isValid() ? archValue.toString() : nullptr;
}

QString InitialOptionsDialog::getSelectedCPU() const
{
    QString cpu = ui->cpuComboBox->currentText();
    if (cpu.isNull() || cpu.isEmpty()) {
        return nullptr;
    }
    return cpu;
}

int InitialOptionsDialog::getSelectedBits() const
{
    QString sel_bits = ui->bitsComboBox->currentText();
    if (sel_bits != "Auto") {
        return sel_bits.toInt();
    }

    return 0;
}

InitialOptions::Endianness InitialOptionsDialog::getSelectedEndianness() const
{
    switch (ui->endiannessComboBox->currentIndex()) {
    case 1:
        return InitialOptions::Endianness::Little;
    case 2:
        return InitialOptions::Endianness::Big;
    default:
        return InitialOptions::Endianness::Auto;
    }
}

QString InitialOptionsDialog::getSelectedOS() const
{
    QVariant os = ui->kernelComboBox->currentData();
    return os.isValid() ? os.toString() : nullptr;
}

QList<CommandDescription> InitialOptionsDialog::getSelectedAdvancedAnalCmds() const
{
    QList<CommandDescription> advanced = QList<CommandDescription>();
    if (ui->analSlider->value() == 6) {
        AnalysisCommands item;
        foreach (item, analysisCommands) {
            if (item.checkbox->isChecked()) {
                advanced << item.commandDesc;
            }
        }
    }
    return advanced;
}

void InitialOptionsDialog::setupAndStartAnalysis(
    /*int level, QList<QString> advanced*/)
{
    InitialOptions options;

    options.filename = main->getFilename();
    if (!options.filename.isEmpty()) {
        main->setWindowTitle("iaito – " + options.filename);
    }
    options.shellcode = this->shellcode;

    // Where the bin header is located in the file (-B)
    if (ui->entry_loadOffset->text().length() > 0) {
        options.binLoadAddr = Core()->math(ui->entry_loadOffset->text());
    }

    // Where to map the file once loaded (-m)
    options.mapAddr = Core()->math(ui->entry_mapOffset->text());
    options.arch = getSelectedArch();
    options.cpu = getSelectedCPU();
    options.bits = getSelectedBits();
    options.os = getSelectedOS();
    options.analVars = ui->varCheckBox->isChecked();
    options.writeEnabled = ui->writeCheckBox->isChecked();
    options.loadBinInfo = !ui->binCheckBox->isChecked();
    options.loadBinCache = ui->binCacheCheckBox->isChecked();
    QVariant forceBinPluginData = ui->formatComboBox->currentData();
    if (!forceBinPluginData.isNull()) {
        RBinPluginDescription pluginDesc = forceBinPluginData.value<RBinPluginDescription>();
        options.forceBinPlugin = pluginDesc.name;
    }
    options.demangle = ui->demangleCheckBox->isChecked();
    if (ui->pdbCheckBox->isChecked()) {
        options.pdbFile = ui->pdbLineEdit->text();
    }
    if (ui->scriptCheckBox->isChecked()) {
        options.script = ui->scriptLineEdit->text();
    }

    options.endian = getSelectedEndianness();

    int level = ui->analSlider->value();
    switch (level) {
    case 0:
        options.analCmd = {};
        break;
    case 1:
        options.analCmd = {{"aa", "Auto analysis"}};
        break;
    case 2:
        options.analCmd = {{"aac", "Analyse function calls"}};
        break;
    case 3:
        options.analCmd = {{"aaa", "Advanced Auto analysis"}};
        break;
    case 4:
        options.analCmd = {{"aaaa", "Experimental analysis"}};
        break;
    case 5:
        options.analCmd = {{"aaaaa", "Unstable analysis"}};
        break;
    case 6:
        options.analCmd = getSelectedAdvancedAnalCmds();
        break;
    default:
        options.analCmd = {};
        break;
    }

    // Validate filename
    if (options.filename.endsWith("://") || options.filename.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("Please select a file"));
        return;
    }
    if (options.filename.startsWith("malloc:")) {
        options.loadBinInfo = false;
    }

    MainWindow *main = this->main;
    IaitoCore *iaito = Core();
    bool reuseFile = this->reuseCurrentFile;

    // Build the progress dialog on the stack (lives until this function returns)
    QDialog progressDialog;
    progressDialog.setWindowTitle(tr("Analyzing Program"));
    progressDialog.setMinimumSize(500, 350);
    progressDialog.setWindowFlags(progressDialog.windowFlags()
                                  & ~Qt::WindowContextHelpButtonHint
                                  & ~Qt::WindowCloseButtonHint);

    QVBoxLayout layout(&progressDialog);

    QLabel timeLabel(tr("Running..."));
    layout.addWidget(&timeLabel);

    QProgressBar progressBar;
    progressBar.setRange(0, 0);
    progressBar.setTextVisible(false);
    layout.addWidget(&progressBar);

    QPlainTextEdit logText;
    logText.setReadOnly(true);
    QFont mono("Courier");
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(10);
    logText.setFont(mono);
    logText.setStyleSheet(
        "QPlainTextEdit { background-color: #1a1a1a; color: #cccccc; }");
    layout.addWidget(&logText);

    QPushButton interruptBtn(tr("Interrupt"));
    layout.addWidget(&interruptBtn);

    // Log helper: append text and pump the event loop
    auto appendLog = [&logText](const QString &msg) {
        logText.appendPlainText(msg);
        QScrollBar *sb = logText.verticalScrollBar();
        sb->setValue(sb->maximum());
        QApplication::processEvents();
    };

    // Interrupt handler: set r_cons breaked flag
    bool interrupted = false;
    RCore *rcore = iaito->core_;
    QObject::connect(&interruptBtn, &QPushButton::clicked, &progressDialog, [&]() {
        rcore->cons->context->breaked = true;
        interrupted = true;
        interruptBtn.setEnabled(false);
        interruptBtn.setText(tr("Interrupting..."));
    });

    // Elapsed time timer
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer timeLabelTimer;
    QObject::connect(&timeLabelTimer, &QTimer::timeout, &progressDialog, [&]() {
        int secs = (elapsed.elapsed() + 500) / 1000;
        int mins = secs / 60;
        int hrs = mins / 60;
        QString label = tr("Running for") + " ";
        if (hrs) {
            label += QString::number(hrs) + "h ";
        }
        if (mins) {
            label += QString::number(mins % 60) + "m ";
        }
        label += QString::number(secs % 60) + "s";
        timeLabel.setText(label);
    });
    timeLabelTimer.start(1000);

    // Set up R_LOG callback to capture radare2 log messages and pump event loop
    s_analysisLogWidget = &logText;

    // Close options dialog, show progress dialog
    done(0);
    progressDialog.show();
    QApplication::processEvents();

    // --- Load the file ---
    int perms = R_PERM_RX;
    if (options.writeEnabled) {
        perms |= R_PERM_W;
        emit iaito->ioModeChanged();
    }
    iaito->setConfig("bin.demangle", options.demangle);

    if (!reuseFile && options.filename.length()) {
        appendLog(tr("Loading the file..."));
        bool fileLoaded = iaito->loadFile(
            options.filename,
            options.binLoadAddr,
            options.mapAddr,
            perms,
            options.useVA,
            options.loadBinCache,
            options.loadBinInfo,
            options.forceBinPlugin);
        if (!fileLoaded) {
            progressDialog.close();
            main->openNewFileFailed();
            return;
        }
    }

    iaito->setCPU(options.arch, options.cpu, options.bits);
    iaito->setConfig("anal.vars", options.analVars);
    if (!options.os.isNull()) {
        iaito->setConfig("asm.os", options.os);
    }
    if (!options.pdbFile.isNull()) {
        appendLog(tr("Loading PDB file..."));
        iaito->loadPDB(options.pdbFile);
    }
    if (!options.shellcode.isNull() && options.shellcode.size() / 2 > 0) {
        appendLog(tr("Loading shellcode..."));
        iaito->cmdRaw("wx " + options.shellcode);
    }
    if (options.endian != InitialOptions::Endianness::Auto) {
        iaito->setEndianness(options.endian == InitialOptions::Endianness::Big);
    }
    iaito->cmdRaw("fs *");
    if (!options.script.isNull()) {
        appendLog(tr("Executing script..."));
        iaito->loadScript(options.script);
    }

    // --- Run analysis commands ---
    // Hook into r_cons_is_breaked() to pump the event loop during analysis.
    // This keeps the progress bar animating and the interrupt button clickable.
    RConsBreak oldCbBreak = rcore->cons->cb_break;
    void *oldCbBreakUser = rcore->cons->user;
    rcore->cons->cb_break = analysisBreakCallback;
    rcore->cons->user = nullptr;

    r_log_add_callback(analysisLogCallback, nullptr);

    if (!options.analCmd.empty()) {
        const bool boi = iaito->getConfigb("esil.breakoninvalid");
        iaito->setConfig("esil.breakoninvalid", false);
        for (const CommandDescription &cmd : options.analCmd) {
            if (interrupted) {
                break;
            }
            appendLog(cmd.command + " : " + cmd.description);
            iaito->cmd(cmd.command);
        }
        iaito->setConfig("esil.breakoninvalid", boi);
        if (!interrupted) {
            appendLog(tr("Analysis complete!"));
        }
    } else {
        appendLog(tr("Skipping Analysis."));
    }

    r_log_del_callback(analysisLogCallback);
    s_analysisLogWidget = nullptr;

    rcore->cons->cb_break = oldCbBreak;
    rcore->cons->user = oldCbBreakUser;

    timeLabelTimer.stop();
    progressDialog.close();
    main->finalizeOpen();
}

void InitialOptionsDialog::on_okButton_clicked()
{
    ui->okButton->setEnabled(false);
    setupAndStartAnalysis();
}

void InitialOptionsDialog::closeEvent(QCloseEvent *event)
{
    event->accept();
}

QString InitialOptionsDialog::analysisDescription(int level)
{
    // TODO: replace this with meaningful descriptions
    switch (level) {
    case 0:
        return tr("No analysis");
    case 1:
        return tr("Auto-Analysis (aa)");
    case 2:
        return tr("Call-Analysis (aac)");
    case 3:
        return tr("Advanced Analysis (aaa)");
    case 4:
        return tr("Experimental Analysis (aaaa)");
    case 5:
        return tr("Cutting Edge Analysis (aaaaa)");
    case 6:
        return tr("Custom Analysis Steps");
    default:
        return tr("Unknown");
    }
}

void InitialOptionsDialog::on_analSlider_valueChanged(int value)
{
    ui->analDescription->setText(
        tr("Level") + QStringLiteral(": %1").arg(analysisDescription(value)));
    if (value == 0) {
        ui->analCheckBox->setChecked(false);
        ui->analCheckBox->setText(tr("Analysis: Disabled"));
    } else {
        ui->analCheckBox->setChecked(true);
        ui->analCheckBox->setText(tr("Run Analysis"));
        if (value == 6) {
            ui->analoptionsFrame->setVisible(true);
            ui->advancedAnlysisLine->setVisible(true);
        } else {
            ui->analoptionsFrame->setVisible(false);
            ui->advancedAnlysisLine->setVisible(false);
        }
    }
}

void InitialOptionsDialog::on_AdvOptButton_clicked()
{
    if (ui->AdvOptButton->isChecked()) {
        ui->hideFrame->setVisible(true);
        ui->AdvOptButton->setArrowType(Qt::DownArrow);
    } else {
        ui->hideFrame->setVisible(false);
        ui->AdvOptButton->setArrowType(Qt::RightArrow);
    }
}

void InitialOptionsDialog::on_analCheckBox_clicked(bool checked)
{
    if (!checked) {
        analLevel = ui->analSlider->value();
    }
    ui->analSlider->setValue(checked ? analLevel : 0);
}

void InitialOptionsDialog::on_archComboBox_currentIndexChanged(int)
{
    updateCPUComboBox();
}

void InitialOptionsDialog::updatePDBLayout()
{
    ui->pdbWidget->setEnabled(ui->pdbCheckBox->isChecked());
}

void InitialOptionsDialog::on_pdbSelectButton_clicked()
{
    QFileDialog dialog(this);
    dialog.setWindowTitle(tr("Select PDB file"));
    dialog.setNameFilters({tr("PDB file (*.pdb)"), tr("All files (*)")});

    if (!dialog.exec()) {
        return;
    }

    const QString &fileName = QDir::toNativeSeparators(dialog.selectedFiles().first());

    if (!fileName.isEmpty()) {
        ui->pdbLineEdit->setText(fileName);
    }
}

void InitialOptionsDialog::updateScriptLayout()
{
    ui->scriptWidget->setEnabled(ui->scriptCheckBox->isChecked());
}

void InitialOptionsDialog::on_scriptSelectButton_clicked()
{
    QFileDialog dialog(this);
    dialog.setWindowTitle(tr("Select radare2 script file"));
    dialog.setNameFilters({tr("Script file (*.r2)"), tr("All files (*)")});

    if (!dialog.exec()) {
        return;
    }

    const QString &fileName = QDir::toNativeSeparators(dialog.selectedFiles().first());

    if (!fileName.isEmpty()) {
        ui->scriptLineEdit->setText(fileName);
    }
}

void InitialOptionsDialog::reject()
{
    done(0);
    if (!reuseCurrentFile) {
        main->displayNewFileDialog();
    }
}
