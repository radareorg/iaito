#include "BackgroundAnalTask.h"
#include "core/Iaito.h"
#include <QDebug>
#include <QJsonArray>

BackgroundAnalTask::BackgroundAnalTask(const InitialOptions &options)
    : Task(tr("Analysis"), TaskPriority::Normal)
    , options(options)
{
    setCancellable(true);
}

BackgroundAnalTask::~BackgroundAnalTask()
{
}

void BackgroundAnalTask::cancel()
{
    Task::cancel();
    // Break radare2 operations
    r_cons_singleton()->context->breaked = true;
}

void BackgroundAnalTask::run()
{
    setProgress(0);
    setDescription(tr("Loading and analyzing binary..."));
    
    // Fast operations to update progress feedback
    setProgress(5);
    
    int perms = R_PERM_RX;
    if (options.writeEnabled) {
        perms |= R_PERM_W;
        emit Core()->ioModeChanged();
    }

    // Demangle (must be before file Core()->loadFile)
    Core()->setConfig("bin.demangle", options.demangle);
    
    setProgress(10);
    
    // Do not reload the file if already loaded
    QJsonArray openedFiles = Core()->getOpenedFiles();
    if (!openedFiles.size() && options.filename.length()) {
        setDescription(tr("Loading binary file..."));
        openFailed = false;
        bool fileLoaded = Core()->loadFile(
            options.filename,
            options.binLoadAddr,
            options.mapAddr,
            perms,
            options.useVA,
            options.loadBinCache,
            options.loadBinInfo,
            options.forceBinPlugin);
        if (!fileLoaded) {
            setDescription(tr("File loading failed"));
            openFailed = true;
            emit openFileFailed();
            setState(TaskState::Failed);
            return;
        }
        
        setProgress(20);
    }

    // r_core_bin_load might change asm.bits, so let's set that after the bin is loaded
    Core()->setCPU(options.arch, options.cpu, options.bits);

    if (shouldCancel()) {
        setState(TaskState::Cancelled);
        return;
    }
    
    setProgress(30);
    setDescription(tr("Setting up analysis environment..."));
    
    Core()->setConfig("anal.vars", options.analVars);
    if (!options.os.isNull()) {
        Core()->setConfig("asm.os", options.os);
    }

    if (!options.pdbFile.isNull()) {
        setDescription(tr("Loading PDB file..."));
        Core()->loadPDB(options.pdbFile);
    }

    setProgress(40);
    
    if (shouldCancel()) {
        setState(TaskState::Cancelled);
        return;
    }

    if (!options.shellcode.isNull() && options.shellcode.size() / 2 > 0) {
        setDescription(tr("Loading shellcode..."));
        Core()->cmd("wx " + options.shellcode + " @e:io.va=0@0");
    }

    if (options.endian != InitialOptions::Endianness::Auto) {
        Core()->setEndianness(options.endian == InitialOptions::Endianness::Big);
    }

    setProgress(45);
    
    Core()->cmdRaw("fs *");

    if (!options.script.isNull()) {
        setDescription(tr("Executing script..."));
        Core()->loadScript(options.script);
    }
    
    setProgress(50);

    if (shouldCancel()) {
        setState(TaskState::Cancelled);
        return;
    }

    if (!options.analCmd.empty()) {
        setDescription(tr("Executing analysis..."));
        int count = options.analCmd.size();
        int i = 0;
        for (const CommandDescription &cmd : options.analCmd) {
            if (shouldCancel()) {
                setState(TaskState::Cancelled);
                return;
            }
            
            // Calculate and emit progress from 50-100%
            int progress = 50 + ((i + 1) * 50) / count;
            setProgress(progress);
            
            setDescription(tr("Analysis: %1").arg(cmd.description));
            Core()->cmd(cmd.command);
            i++;
        }
        
        setProgress(100);
        setDescription(tr("Analysis complete!"));
    } else {
        setProgress(100);
        setDescription(tr("Skipping Analysis."));
    }
}