#ifndef DISASSEMBLYCONTEXTMENU_H
#define DISASSEMBLYCONTEXTMENU_H

#include "common/IOModesController.h"
#include "common/TextEditDialog.h"
#include "core/Iaito.h"
#include <QKeySequence>
#include <QMenu>

class MainWindow;

class IAITO_EXPORT DisassemblyContextMenu : public QMenu
{
    Q_OBJECT

public:
    DisassemblyContextMenu(QWidget *parent, MainWindow *mainWindow);
    ~DisassemblyContextMenu();

signals:
    void copy();
    /**
     * @brief Signal emitted to copy raw bytes of selected instructions
     */
    void copyBytes();

public slots:
    void setOffset(RVA offset);
    void setCanCopy(bool enabled);

    /**
     * @brief Sets the value of curHighlightedWord
     * @param text The current highlighted word
     */
    void setCurHighlightedWord(const QString &text);

private slots:
    void aboutToShowSlot();
    void aboutToHideSlot();

    void on_actionEditFunction_triggered();
    void on_actionEditInstruction_triggered();
    void on_actionNopInstruction_triggered();
    void on_actionJmpReverse_triggered();
    void on_actionEditBytes_triggered();
    void showReverseJmpQuery();

    void on_actionCopy_triggered();
    void on_actionCopyAddr_triggered();
    /**
     * @brief Slot triggered to copy raw bytes of selected instructions
     */
    void on_actionCopyBytes_triggered();
    void on_actionAddComment_triggered();
    void on_actionSetProgramCounter_triggered();
    void on_actionAnalyzeFunction_triggered();
    void on_actionRename_triggered();
    void on_actionSetFunctionVarTypes_triggered();
    void on_actionXRefs_triggered();
    void on_actionXRefsForVariables_triggered();
    void on_actionDisplayOptions_triggered();

    void on_actionDeleteComment_triggered();
    void on_actionDeleteFlag_triggered();
    void on_actionDeleteFunction_triggered();

    void on_actionAddBreakpoint_triggered();
    void on_actionAdvancedBreakpoint_triggered();
    void on_actionContinueUntil_triggered();
    void on_actionSetPC_triggered();
    void on_actionEditAnnotation_triggered();

    void on_actionSetToCode_triggered();
    void on_actionSetAsString_triggered();
    void on_actionSetAsStringRemove_triggered();
    void on_actionSetAsStringAdvanced_triggered();
    void on_actionSetToData_triggered();
    void on_actionSetToDataEx_triggered();

    /**
     * @brief Executed on selecting an offset from the structureOffsetMenu
     * Uses the applyStructureOffset() function of IaitoCore to apply the
     * structure offset
     * \param action The action which trigered the event
     */
    void on_actionStructureOffsetMenu_triggered(QAction *action);

    /**
     * @brief Executed on selecting the "Link Type to Address" option
     * Opens the LinkTypeDialog box from where the user can link the address
     * to a type
     */
    void on_actionLinkType_triggered();
    /**
     * @brief Executed on selecting the "Relative to" option
     * Sets the asm.addr.relto config variable and refreshes disassembly
     */
    void on_actionRelativeTo_triggered(QAction *action);

private:
    QKeySequence getCopySequence() const;
    QKeySequence getCommentSequence() const;
    QKeySequence getCopyAddressSequence() const;
    QKeySequence getSetToCodeSequence() const;
    QKeySequence getSetAsStringSequence() const;
    QKeySequence getSetAsStringAdvanced() const;
    QKeySequence getSetToDataSequence() const;
    QKeySequence getSetToDataExSequence() const;
    QKeySequence getRenameSequence() const;
    QKeySequence getRetypeSequence() const;
    QKeySequence getXRefSequence() const;
    QKeySequence getDisplayOptionsSequence() const;
    QKeySequence getDefineNewFunctionSequence() const;
    QKeySequence getUndefineFunctionSequence() const;
    QKeySequence getEditFunctionSequence() const;
    QList<QKeySequence> getAddBPSequence() const;

    /**
     * @return the shortcut key for "Link Type to Address" option
     */
    QKeySequence getLinkTypeSequence() const;

    RVA offset;
    bool canCopy;
    QString curHighlightedWord; // The current highlighted word
    MainWindow *mainWindow;
    IOModesController ioModesController;

    QList<QAction *> anonymousActions;

    QMenu *editMenu;
    QAction actionEditInstruction;
    QAction actionNopInstruction;
    QAction actionJmpReverse;
    QAction actionEditBytes;

    QAction actionCopy;
    QAction *copySeparator;
    QAction actionCopyAddr;
    /**
     * @brief Action to copy raw bytes of the selected instructions
     */
    QAction actionCopyBytes;

    QAction actionSetProgramCounter;
    QAction actionAddComment;
    QAction actionAnalyzeFunction;
    QAction actionEditFunction;
    QAction actionRename;
    QAction actionSetFunctionVarTypes;
    QAction actionXRefs;
    QAction actionXRefsForVariables;
    QAction actionDisplayOptions;

    QAction actionDeleteComment;
    QAction actionDeleteFlag;
    QAction actionDeleteFunction;

    QMenu *structureOffsetMenu;
    // Menu to select asm.addr.relto values at runtime
    QMenu *relativeToMenu;

    QAction actionLinkType;

    QMenu *setBaseMenu;
    QAction actionSetBaseBinary;
    QAction actionSetBaseOctal;
    QAction actionSetBaseDecimal;
    QAction actionSetBaseHexadecimal;
    QAction actionSetBasePort;
    QAction actionSetBaseIPAddr;
    QAction actionSetBaseSyscall;
    QAction actionSetBaseString;

    QMenu *setBitsMenu;
    QAction actionSetBits16;
    QAction actionSetBits32;
    QAction actionSetBits64;

    QMenu *setColorMenu;
    QAction actionSetColorRed;
    QAction actionSetColorMagenta;
    QAction actionSetColorGreen;
    QAction actionSetColorBlue;
    QAction actionSetColorCyan;
    QAction actionSetColorYellow;
    QAction actionSetColorGray;
    QAction actionSetColorBrown;
    QAction actionSetColorReset;

    QMenu *debugMenu;
    QAction actionContinueUntil;
    QAction actionSetPC;
    QAction actionEditAnnotation;

    QMenu *breakpointMenu;
    QAction actionAddBreakpoint;
    QAction actionAdvancedBreakpoint;

    QAction actionSetToCode;

    QAction actionSetAsStringAuto;
    QAction actionSetAsStringRemove;
    QAction actionSetAsStringAdvanced;

    QMenu *setToDataMenu;
    QMenu *setAsMenu;
    QMenu *setAsString;
    QAction actionSetToDataEx;
    QAction actionSetToDataByte;
    QAction actionSetToDataWord;
    QAction actionSetToDataDword;
    QAction actionSetToDataQword;

    QAction showInSubmenu;
    QList<QAction *> showTargetMenuActions;
    QMenu *pluginMenu = nullptr;
    QAction *pluginActionMenuAction = nullptr;

    // For creating anonymous entries (that are always visible)
    QAction *addAnonymousAction(QString name, const char *slot, QKeySequence shortcut);

    void initAction(QAction *action, QString name, const char *slot = nullptr);
    void initAction(QAction *action, QString name, const char *slot, QKeySequence keySequence);
    void initAction(QAction *action, QString name, const char *slot, QList<QKeySequence> keySequence);

    void setBase(QString base);
    void setToData(int size, int repeat = 1);
    void setBits(int bits);
    void setColor(const char *color);

    void addSetBaseMenu();
    void addSetColorMenu();
    void addSetBitsMenu();
    void addSetAsMenu();
    void addSetToDataMenu();
    void addEditMenu();
    void addBreakpointMenu();
    void addDebugMenu();

    enum DoRenameAction {
        RENAME_FUNCTION,
        RENAME_FLAG,
        RENAME_ADD_FLAG,
        RENAME_LOCAL,
        RENAME_DO_NOTHING,
    };
    struct DoRenameInfo
    {
        ut64 addr;
        QString name;
    };
    DoRenameAction doRenameAction = RENAME_DO_NOTHING;
    DoRenameInfo doRenameInfo = {};

    /*
     * @brief Setups up the "Rename" option in the context menu
     *
     * This function takes into account cursor location so it can choose between
     * current address and pointed value i.e. `0x000040f3  lea rdi,
     * [0x000199b1]` -> does the user want to add a flag at 0x40f3 or at
     * 0x199b1? and for that we will rely on |curHighlightedWord| which is the
     * currently selected word.
     */
    void setupRenaming();

    /**
     * @brief Checks if the currently highlighted word in the disassembly widget
     * is a local variable or function paramter.
     * @return Return true if the highlighted word is the name of a local
     * variable or function parameter, return false otherwise.
     */
    bool isHighlightedWordLocalVar();
    struct ThingUsedHere
    {
        QString name;
        RVA offset;
        enum class Type { Var, Function, Flag, Address };
        Type type;
    };
    QVector<ThingUsedHere> getThingUsedHere(RVA offset);

    /*
     * @brief This function checks if the given address contains a function,
     * a flag or if it is just an address.
     */
    ThingUsedHere getThingAt(ut64 address);

    /*
     * @brief This function will set the text for the renaming menu given a
     * ThingUsedHere and provide information on how to handle the renaming of
     * this specific thing. Indeed, selected dialogs are different when it comes
     * to adding a flag, renaming an existing function, renaming a local
     * variable...
     *
     * This function handles every possible object.
     */
    void buildRenameMenu(ThingUsedHere *tuh);
};
#endif // DISASSEMBLYCONTEXTMENU_H
