
#ifndef IAITO_INITIALOPTIONS_H
#define IAITO_INITIALOPTIONS_H

#include "core/Iaito.h"

/**
 * @brief The CommandDescription struct is a pair of a radare2 command and its
 * description
 */
struct CommandDescription
{
    QString command;
    QString description;
};

struct InitialOptions
{
    enum class Endianness { Auto, Little, Big };

    QString filename;

    bool useVA = true;
    RVA binLoadAddr = RVA_INVALID;
    RVA mapAddr = RVA_INVALID;

    QString arch;
    QString cpu;
    int bits = 0;
    QString os;
    bool analVars = true;

    Endianness endian;

    bool writeEnabled = false;
    bool loadBinInfo = true;
    bool loadBinCache = false;
    /**
     * @brief When true, iaito is opening the target as a live debug session.
     * setupAndStartAnalysis() will set cfg.debug=true before loadFile so the
     * rest of the pipeline (Dashboard, MainWindow, widgets, etc.) treats the
     * session as a debug one even before currentlyDebugging has been set.
     */
    bool debug = false;
    QString forceBinPlugin;

    bool demangle = true;

    QString pdbFile;
    QString script;

    QList<CommandDescription> analCmd = {{"aaa", "Auto analysis"}};

    QString shellcode;
};

#endif // IAITO_INITIALOPTIONS_H
