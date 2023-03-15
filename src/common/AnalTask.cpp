#include "core/Iaito.h"
#include "common/AnalTask.h"
#include "core/MainWindow.h"
#include "dialogs/InitialOptionsDialog.h"
#include <QJsonArray>
#include <QDebug>
#include <QCheckBox>

AnalTask::AnalTask() :
    AsyncTask()
{
}

AnalTask::~AnalTask()
{
}

void AnalTask::interrupt()
{
    AsyncTask::interrupt();
    r_cons_singleton()->context->breaked = true;
}

QString AnalTask::getTitle() {
    // If no file is loaded we consider it's Initial Analysis
    QJsonArray openedFiles = Core()->getOpenedFiles();
    if (!openedFiles.size()) {
        return tr("Initial Analysis");
    }
    return tr("Analyzing Program");
}

void AnalTask::runTask()
{
    int perms = R_PERM_RX;
    if (options.writeEnabled) {
        perms |= R_PERM_W;
        emit Core()->ioModeChanged();
    }

    // Demangle (must be before file Core()->loadFile)
    Core()->setConfig("bin.demangle", options.demangle);

    // Do not reload the file if already loaded
    QJsonArray openedFiles = Core()->getOpenedFiles();
    if (!openedFiles.size() && options.filename.length()) {
        log(tr("Loading the file..."));
        openFailed = false;
        bool fileLoaded = Core()->loadFile(options.filename,
                                           options.binLoadAddr,
                                           options.mapAddr,
                                           perms,
                                           options.useVA,
                                           options.loadBinCache,
                                           options.loadBinInfo,
                                           options.forceBinPlugin);
        if (!fileLoaded) {
            // Something wrong happened, fallback to open dialog
            openFailed = true;
            emit openFileFailed();
            interrupt();
            return;
        }
    }

    // r_core_bin_load might change asm.bits, so let's set that after the bin is loaded
    Core()->setCPU(options.arch, options.cpu, options.bits);

    if (isInterrupted()) {
        return;
    }
    Core()->setConfig("anal.vars", options.analVars);
    if (!options.os.isNull()) {
        Core()->setConfig("asm.os", options.os);
    }

    if (!options.pdbFile.isNull()) {
        log(tr("Loading PDB file..."));
        Core()->loadPDB(options.pdbFile);
    }

    if (isInterrupted()) {
        return;
    }

    if (!options.shellcode.isNull() && options.shellcode.size() / 2 > 0) {
        log(tr("Loading shellcode..."));
        Core()->cmdRaw("wx " + options.shellcode);
    }

    if (options.endian != InitialOptions::Endianness::Auto) {
        Core()->setEndianness(options.endian == InitialOptions::Endianness::Big);
    }

    Core()->cmdRaw("fs *");

    if (!options.script.isNull()) {
        log(tr("Executing script..."));
        Core()->loadScript(options.script);
    }

    if (isInterrupted()) {
        return;
    }

    if (!options.analCmd.empty()) {
        log(tr("Executing analysis..."));
        for (const CommandDescription &cmd : options.analCmd) {
            if (isInterrupted()) {
                return;
            }
            // log(cmd.description);
            log(cmd.command + " : " + cmd.description);
            // use cmd instead of cmdRaw because commands can be unexpected
            Core()->cmd(cmd.command);
        }
        log(tr("Analysis complete!"));
    } else {
        log(tr("Skipping Analysis."));
    }
}
