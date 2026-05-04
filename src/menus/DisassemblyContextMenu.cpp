#include "DisassemblyContextMenu.h"
#include "ColorPickerMenu.h"
#include "MainWindow.h"
#include "dialogs/BreakpointsDialog.h"
#include "dialogs/CommentsDialog.h"
#include "dialogs/EditFunctionDialog.h"
#include "dialogs/EditInstructionDialog.h"
#include "dialogs/EditStringDialog.h"
#include "dialogs/EditVariablesDialog.h"
#include "dialogs/FlagDialog.h"
#include "dialogs/LinkTypeDialog.h"
#include "dialogs/SetToDataDialog.h"
#include "dialogs/XrefsDialog.h"
#include "dialogs/settings/SettingsDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QInputDialog>
#include <QJsonArray>
#include <QPainter>
#include <QPolygonF>
#include <QPushButton>
#include <QShortcut>
#include <QVector>
#include <QtCore>

DisassemblyContextMenu::DisassemblyContextMenu(QWidget *parent, MainWindow *mainWindow)
    : QMenu(parent)
    , offset(0)
    , canCopy(false)
    , mainWindow(mainWindow)
    , actionEditInstruction(this)
    , actionNopInstruction(this)
    , actionJmpReverse(this)
    , actionEditBytes(this)
    , actionCopy(this)
    , actionCopyAddr(this)
    , actionCopyInstruction(this)
    , actionCopyInstructionBytes(this)
    , actionCopyFunctionDisasm(this)
    , actionCopyFunctionBytes(this)
    , actionCopyBytes(this)
    , actionSetProgramCounter(this)
    , actionAddComment(this)
    , actionAnalyzeFunction(this)
    , actionEditFunction(this)
    , actionRename(this)
    , actionSetFunctionVarTypes(this)
    , actionXRefs(this)
    , actionXRefsForVariables(this)
    , actionDisplayOptions(this)
    , actionDeleteComment(this)
    , actionDeleteFlag(this)
    , actionDeleteFunction(this)
    , actionLinkType(this)
    , actionSetBaseBinary(this)
    , actionSetBaseOctal(this)
    , actionSetBaseDecimal(this)
    , actionSetBaseHexadecimal(this)
    , actionSetBasePort(this)
    , actionSetBaseIPAddr(this)
    , actionSetBaseSyscall(this)
    , actionSetBaseString(this)
    , actionSetBits16(this)
    , actionSetBits32(this)
    , actionSetBits64(this)
    , actionContinueUntil(this)
    , actionEditAnnotation(this)
    , actionAddBreakpoint(this)
    , actionAdvancedBreakpoint(this)
    , actionSetToCode(this)
    , actionSetAsStringAuto(this)
    , actionSetAsStringRemove(this)
    , actionSetAsStringAdvanced(this)
    , actionSetToDataEx(this)
    , actionSetToDataByte(this)
    , actionSetToDataWord(this)
    , actionSetToDataDword(this)
    , actionSetToDataQword(this)
    , showInSubmenu(this)
    , actionToggleBBLines(this)
{
    const QColor annotationColor(67, 160, 71);
    const QColor analysisColor(0, 137, 190);
    const QColor debugColor(216, 88, 64);
    const QColor copyColor(56, 142, 60);
    const QColor viewColor(191, 128, 32);

    initAction(
        &actionAddComment,
        tr("Add / Edit Comment"),
        SLOT(on_actionAddComment_triggered()),
        getCommentSequence());
    setActionIcon(&actionAddComment, MenuIcon::Comment, annotationColor);
    addAction(&actionAddComment);

    initAction(
        &actionEditAnnotation,
        tr("Add / Edit Annotation"),
        SLOT(on_actionEditAnnotation_triggered()));
    setActionIcon(&actionEditAnnotation, MenuIcon::Edit, annotationColor);
    addAction(&actionEditAnnotation);

    addSeparator();

    buildRepresentationMenu();
    setMenuIcon(representationMenu, MenuIcon::View, viewColor);

    addSetAsMenu();

    addEditMenu();

    buildCopyMenu();
    setMenuIcon(copyMenu, MenuIcon::Copy, copyColor);

    addSeparator();

    buildAnalysisMenu();
    setMenuIcon(analysisMenu, MenuIcon::Analysis, analysisColor);

    buildDebugMenu();
    setMenuIcon(debugMenu, MenuIcon::Debug, debugColor);

    if (mainWindow) {
        pluginMenu = mainWindow->getContextMenuExtensions(MainWindow::ContextMenuType::Disassembly);
        pluginActionMenuAction = addMenu(pluginMenu);
    }

    connect(
        this, &DisassemblyContextMenu::aboutToShow, this, &DisassemblyContextMenu::aboutToShowSlot);
    connect(
        this, &DisassemblyContextMenu::aboutToHide, this, &DisassemblyContextMenu::aboutToHideSlot);
}

DisassemblyContextMenu::~DisassemblyContextMenu() {}

QIcon DisassemblyContextMenu::makeMenuIcon(MenuIcon icon, const QColor &color) const
{
    const qreal ratio = devicePixelRatioF();
    const int size = 16;
    QPixmap pixmap(size * ratio, size * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor accent = color;
    accent.setAlpha(205);
    QPen pen(accent, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    QRectF box(3, 3, 10, 10);
    switch (icon) {
    case MenuIcon::XRefs:
        painter.drawEllipse(QPointF(5, 8), 2.0, 2.0);
        painter.drawEllipse(QPointF(11, 5), 2.0, 2.0);
        painter.drawEllipse(QPointF(11, 11), 2.0, 2.0);
        painter.drawLine(QPointF(6.7, 7), QPointF(9.3, 5.8));
        painter.drawLine(QPointF(6.7, 9), QPointF(9.3, 10.2));
        break;
    case MenuIcon::Navigation:
        painter.drawPolyline(QPolygonF(QVector<QPointF>{QPointF(4, 8), QPointF(11, 8)}));
        painter.drawPolyline(
            QPolygonF(QVector<QPointF>{QPointF(8, 5), QPointF(11, 8), QPointF(8, 11)}));
        break;
    case MenuIcon::Rename:
        painter.drawLine(QPointF(4, 12), QPointF(12, 4));
        painter.drawLine(QPointF(10, 4), QPointF(12, 6));
        painter.drawLine(QPointF(4, 12), QPointF(7, 11));
        break;
    case MenuIcon::Comment:
        painter.drawRoundedRect(QRectF(3.5, 4, 9, 7), 1.5, 1.5);
        painter.drawLine(QPointF(6, 11), QPointF(4.5, 13));
        break;
    case MenuIcon::Analysis:
        painter.drawEllipse(box);
        painter.drawLine(QPointF(10.8, 10.8), QPointF(13, 13));
        break;
    case MenuIcon::Debug:
        painter.drawLine(QPointF(8, 3), QPointF(8, 13));
        painter.drawLine(QPointF(4, 5), QPointF(12, 5));
        painter.drawLine(QPointF(4, 11), QPointF(12, 11));
        painter.drawRoundedRect(QRectF(5, 4, 6, 8), 1.5, 1.5);
        break;
    case MenuIcon::Copy:
        painter.drawRect(QRectF(5, 3.5, 7, 9));
        painter.drawRect(QRectF(3, 5.5, 7, 7));
        break;
    case MenuIcon::View:
        painter.drawEllipse(QRectF(3, 5, 10, 6));
        painter.drawEllipse(QPointF(8, 8), 1.8, 1.8);
        break;
    case MenuIcon::Bookmark:
        painter.drawPolyline(QPolygonF(
            QVector<QPointF>{
                QPointF(5, 3),
                QPointF(11, 3),
                QPointF(11, 13),
                QPointF(8, 10.5),
                QPointF(5, 13),
                QPointF(5, 3)}));
        break;
    case MenuIcon::Tag:
        painter.drawPolyline(QPolygonF(
            QVector<QPointF>{
                QPointF(4, 5),
                QPointF(8, 3),
                QPointF(13, 8),
                QPointF(8, 13),
                QPointF(3, 8),
                QPointF(4, 5)}));
        painter.drawEllipse(QPointF(7, 6.5), 0.8, 0.8);
        break;
    case MenuIcon::Function:
        painter.drawText(QRectF(3, 2, 10, 12), Qt::AlignCenter, QStringLiteral("f"));
        break;
    case MenuIcon::Breakpoint:
        painter.setBrush(accent);
        painter.drawEllipse(QPointF(8, 8), 4.0, 4.0);
        break;
    case MenuIcon::Edit:
        painter.drawRect(QRectF(4, 4, 8, 8));
        painter.drawLine(QPointF(6, 7), QPointF(10, 7));
        painter.drawLine(QPointF(6, 9), QPointF(9, 9));
        break;
    }

    return QIcon(pixmap);
}

void DisassemblyContextMenu::setActionIcon(QAction *action, MenuIcon icon, const QColor &color)
{
    if (action) {
        action->setIcon(makeMenuIcon(icon, color));
    }
}

void DisassemblyContextMenu::setMenuIcon(QMenu *menu, MenuIcon icon, const QColor &color)
{
    if (menu) {
        setActionIcon(menu->menuAction(), icon, color);
    }
}

void DisassemblyContextMenu::buildNavigationMenu()
{
    initAction(&showInSubmenu, tr("Follow in ..."), nullptr);
}

void DisassemblyContextMenu::buildAnalysisMenu()
{
    analysisMenu = addMenu(tr("Analysis"));

    initAction(&actionXRefs, tr("Show X-Refs"), SLOT(on_actionXRefs_triggered()), getXRefSequence());
    setActionIcon(&actionXRefs, MenuIcon::XRefs, QColor(0, 137, 190));
    analysisMenu->addAction(&actionXRefs);

    analysisMenu->addSeparator();

    initAction(
        &actionAnalyzeFunction,
        tr("Define function here"),
        SLOT(on_actionAnalyzeFunction_triggered()),
        getDefineNewFunctionSequence());
    setActionIcon(&actionAnalyzeFunction, MenuIcon::Function, QColor(0, 137, 190));
    analysisMenu->addAction(&actionAnalyzeFunction);

    initAction(
        &actionDeleteFunction,
        tr("Undefine function"),
        SLOT(on_actionDeleteFunction_triggered()),
        getUndefineFunctionSequence());
    analysisMenu->addAction(&actionDeleteFunction);

    initAction(
        &actionEditFunction,
        tr("Edit function metadata..."),
        SLOT(on_actionEditFunction_triggered()),
        getEditFunctionSequence());
    analysisMenu->addAction(&actionEditFunction);

    analysisMenu->addSeparator();

    initAction(
        &actionSetFunctionVarTypes,
        tr("Re-type Local Variables"),
        SLOT(on_actionSetFunctionVarTypes_triggered()),
        getRetypeSequence());
    analysisMenu->addAction(&actionSetFunctionVarTypes);

    initAction(
        &actionXRefsForVariables,
        tr("X-Refs for local variables"),
        SLOT(on_actionXRefsForVariables_triggered()),
        QKeySequence(Qt::SHIFT | Qt::Key_X));
    setActionIcon(&actionXRefsForVariables, MenuIcon::XRefs, QColor(0, 137, 190));
    analysisMenu->addAction(&actionXRefsForVariables);

    initAction(
        &actionLinkType,
        tr("Link Type to Address"),
        SLOT(on_actionLinkType_triggered()),
        getLinkTypeSequence());
    setActionIcon(&actionLinkType, MenuIcon::Analysis, QColor(0, 137, 190));
    analysisMenu->addAction(&actionLinkType);
}

void DisassemblyContextMenu::buildDebugMenu()
{
    debugMenu = addMenu(tr("Debug"));

    addBreakpointActions();
    debugMenu->addSeparator();

    initAction(&actionSetProgramCounter, tr("Set Program Counter"), SLOT(on_actionSetPC_triggered()));
    setActionIcon(&actionSetProgramCounter, MenuIcon::Debug, QColor(216, 88, 64));
    debugMenu->addAction(&actionSetProgramCounter);

    initAction(&actionContinueUntil, tr("Run to here"), SLOT(on_actionContinueUntil_triggered()));
    debugMenu->addAction(&actionContinueUntil);
}

void DisassemblyContextMenu::buildCopyMenu()
{
    copyMenu = addMenu(tr("Copy"));

    initAction(
        &actionCopyAddr,
        tr("Address"),
        SLOT(on_actionCopyAddr_triggered()),
        getCopyAddressSequence());
    setActionIcon(&actionCopyAddr, MenuIcon::Copy, QColor(56, 142, 60));
    copyMenu->addAction(&actionCopyAddr);

    initAction(&actionCopyInstruction, tr("Instruction"), SLOT(on_actionCopyInstruction_triggered()));
    copyMenu->addAction(&actionCopyInstruction);

    initAction(
        &actionCopyInstructionBytes,
        tr("Instruction bytes"),
        SLOT(on_actionCopyInstructionBytes_triggered()));
    copyMenu->addAction(&actionCopyInstructionBytes);

    initAction(
        &actionCopyFunctionDisasm,
        tr("Function disassembly"),
        SLOT(on_actionCopyFunctionDisasm_triggered()));
    copyMenu->addAction(&actionCopyFunctionDisasm);

    initAction(
        &actionCopyFunctionBytes,
        tr("Function bytes"),
        SLOT(on_actionCopyFunctionBytes_triggered()));
    copyMenu->addAction(&actionCopyFunctionBytes);

    copySeparator = copyMenu->addSeparator();

    initAction(&actionCopy, tr("Selection"), SLOT(on_actionCopy_triggered()), getCopySequence());
    copyMenu->addAction(&actionCopy);

    initAction(&actionCopyBytes, tr("Selection bytes"), SLOT(on_actionCopyBytes_triggered()));
    copyMenu->addAction(&actionCopyBytes);
}

void DisassemblyContextMenu::buildRepresentationMenu()
{
    representationMenu = addMenu(tr("View"));

    buildNavigationMenu();
    setActionIcon(&showInSubmenu, MenuIcon::Navigation, QColor(103, 80, 164));
    representationMenu->addAction(&showInSubmenu);
    representationMenu->addSeparator();

    addSetBaseMenu();
    addSetBitsMenu();
    addSetColorMenu();

    actionToggleBBLines.setText(tr("Basic Block boundaries"));
    actionToggleBBLines.setCheckable(true);
    actionToggleBBLines.setChecked(Config()->getConfigBool("asm.lines.bb"));
    setActionIcon(&actionToggleBBLines, MenuIcon::View, QColor(191, 128, 32));
    representationMenu->addAction(&actionToggleBBLines);
    connect(&actionToggleBBLines, &QAction::toggled, this, [](bool checked) {
        Config()->setConfig("asm.lines.bb", checked);
        Core()->triggerAsmOptionsChanged();
    });

    structureOffsetMenu = representationMenu->addMenu(tr("Structure offset"));
    setMenuIcon(structureOffsetMenu, MenuIcon::View, QColor(191, 128, 32));
    connect(
        structureOffsetMenu,
        &QMenu::triggered,
        this,
        &DisassemblyContextMenu::on_actionStructureOffsetMenu_triggered);

    relativeToMenu = representationMenu->addMenu(tr("Relative to"));
    setMenuIcon(relativeToMenu, MenuIcon::View, QColor(191, 128, 32));
    connect(
        relativeToMenu,
        &QMenu::triggered,
        this,
        &DisassemblyContextMenu::on_actionRelativeTo_triggered);

    initAction(
        &actionDisplayOptions,
        tr("Disassembly options"),
        SLOT(on_actionDisplayOptions_triggered()),
        getDisplayOptionsSequence());
    representationMenu->addAction(&actionDisplayOptions);
}

void DisassemblyContextMenu::addSetBaseMenu()
{
    QMenu *parentMenu = representationMenu ? representationMenu : this;
    setBaseMenu = parentMenu->addMenu(tr("Set immediate base to..."));
    setMenuIcon(setBaseMenu, MenuIcon::View, QColor(191, 128, 32));

    initAction(&actionSetBaseBinary, tr("Binary"));
    setBaseMenu->addAction(&actionSetBaseBinary);
    connect(&actionSetBaseBinary, &QAction::triggered, this, [this] { setBase("b"); });

    initAction(&actionSetBaseOctal, tr("Octal"));
    setBaseMenu->addAction(&actionSetBaseOctal);
    connect(&actionSetBaseOctal, &QAction::triggered, this, [this] { setBase("o"); });

    initAction(&actionSetBaseDecimal, tr("Decimal"));
    setBaseMenu->addAction(&actionSetBaseDecimal);
    connect(&actionSetBaseDecimal, &QAction::triggered, this, [this] { setBase("d"); });

    initAction(&actionSetBaseHexadecimal, tr("Hexadecimal"));
    setBaseMenu->addAction(&actionSetBaseHexadecimal);
    connect(&actionSetBaseHexadecimal, &QAction::triggered, this, [this] { setBase("h"); });

    initAction(&actionSetBasePort, tr("Network Port"));
    setBaseMenu->addAction(&actionSetBasePort);
    connect(&actionSetBasePort, &QAction::triggered, this, [this] { setBase("p"); });

    initAction(&actionSetBaseIPAddr, tr("IP Address"));
    setBaseMenu->addAction(&actionSetBaseIPAddr);
    connect(&actionSetBaseIPAddr, &QAction::triggered, this, [this] { setBase("i"); });

    initAction(&actionSetBaseSyscall, tr("Syscall"));
    setBaseMenu->addAction(&actionSetBaseSyscall);
    connect(&actionSetBaseSyscall, &QAction::triggered, this, [this] { setBase("S"); });

    initAction(&actionSetBaseString, tr("String"));
    setBaseMenu->addAction(&actionSetBaseString);
    connect(&actionSetBaseString, &QAction::triggered, this, [this] { setBase("s"); });
}

void DisassemblyContextMenu::addSetBitsMenu()
{
    QMenu *parentMenu = representationMenu ? representationMenu : this;
    setBitsMenu = parentMenu->addMenu(tr("Set current bits to..."));
    setMenuIcon(setBitsMenu, MenuIcon::View, QColor(191, 128, 32));

    initAction(&actionSetBits16, "16");
    setBitsMenu->addAction(&actionSetBits16);
    connect(&actionSetBits16, &QAction::triggered, this, [this] { setBits(16); });

    initAction(&actionSetBits32, "32");
    setBitsMenu->addAction(&actionSetBits32);
    connect(&actionSetBits32, &QAction::triggered, this, [this] { setBits(32); });

    initAction(&actionSetBits64, "64");
    setBitsMenu->addAction(&actionSetBits64);
    connect(&actionSetBits64, &QAction::triggered, this, [this] { setBits(64); });
}

void DisassemblyContextMenu::addSetColorMenu()
{
    auto *picker = new ColorPickerMenu(tr("Set basic block color..."), this);
    setColorMenu = picker;
    setMenuIcon(setColorMenu, MenuIcon::View, QColor(191, 128, 32));
    QMenu *parentMenu = representationMenu ? representationMenu : this;
    parentMenu->addMenu(picker);
    connect(picker, &ColorPickerMenu::colorPicked, this, [this](const QString &c) { setColor(c); });
}

void DisassemblyContextMenu::addSetAsMenu()
{
    QMenu *parentMenu = this;
    setAsMenu = parentMenu->addMenu(tr("Set as..."));
    setMenuIcon(setAsMenu, MenuIcon::View, QColor(191, 128, 32));

    initAction(
        &actionSetToCode, tr("Code"), SLOT(on_actionSetToCode_triggered()), getSetToCodeSequence());
    setAsMenu->addAction(&actionSetToCode);

    setAsString = setAsMenu->addMenu(tr("String..."));

    initAction(
        &actionSetAsStringAuto,
        tr("Auto-detect"),
        SLOT(on_actionSetAsString_triggered()),
        getSetAsStringSequence());
    initAction(&actionSetAsStringRemove, tr("Remove"), SLOT(on_actionSetAsStringRemove_triggered()));
    initAction(
        &actionSetAsStringAdvanced,
        tr("Advanced"),
        SLOT(on_actionSetAsStringAdvanced_triggered()),
        getSetAsStringAdvanced());

    setAsString->addAction(&actionSetAsStringAuto);
    setAsString->addAction(&actionSetAsStringRemove);
    setAsString->addAction(&actionSetAsStringAdvanced);

    addSetToDataMenu();
}

void DisassemblyContextMenu::addSetToDataMenu()
{
    setToDataMenu = setAsMenu->addMenu(tr("Data..."));

    initAction(&actionSetToDataByte, tr("Byte"));
    setToDataMenu->addAction(&actionSetToDataByte);
    connect(&actionSetToDataByte, &QAction::triggered, this, [this] { setToData(1); });

    initAction(&actionSetToDataWord, tr("Word"));
    setToDataMenu->addAction(&actionSetToDataWord);
    connect(&actionSetToDataWord, &QAction::triggered, this, [this] { setToData(2); });

    initAction(&actionSetToDataDword, tr("Dword"));
    setToDataMenu->addAction(&actionSetToDataDword);
    connect(&actionSetToDataDword, &QAction::triggered, this, [this] { setToData(4); });

    initAction(&actionSetToDataQword, tr("Qword"));
    setToDataMenu->addAction(&actionSetToDataQword);
    connect(&actionSetToDataQword, &QAction::triggered, this, [this] { setToData(8); });

    initAction(
        &actionSetToDataEx, "...", SLOT(on_actionSetToDataEx_triggered()), getSetToDataExSequence());
    setToDataMenu->addAction(&actionSetToDataEx);

    auto switchAction = new QAction(this);
    initAction(
        switchAction, "Switch Data", SLOT(on_actionSetToData_triggered()), getSetToDataSequence());
}

void DisassemblyContextMenu::addEditMenu()
{
    QMenu *parentMenu = this;
    editMenu = parentMenu->addMenu(tr("Edit"));
    setMenuIcon(editMenu, MenuIcon::Edit, QColor(117, 117, 117));

    initAction(&actionRename, tr("Rename..."), SLOT(on_actionRename_triggered()), getRenameSequence());
    setActionIcon(&actionRename, MenuIcon::Rename, QColor(67, 160, 71));
    editMenu->addAction(&actionRename);

    initAction(&actionDeleteComment, tr("Delete comment"), SLOT(on_actionDeleteComment_triggered()));
    setActionIcon(&actionDeleteComment, MenuIcon::Comment, QColor(67, 160, 71));
    editMenu->addAction(&actionDeleteComment);

    initAction(&actionDeleteFlag, tr("Delete flag"), SLOT(on_actionDeleteFlag_triggered()));
    setActionIcon(&actionDeleteFlag, MenuIcon::Tag, QColor(117, 117, 117));
    editMenu->addAction(&actionDeleteFlag);
    editMenu->addSeparator();

    initAction(&actionEditInstruction, tr("Instruction"), SLOT(on_actionEditInstruction_triggered()));
    editMenu->addAction(&actionEditInstruction);

    initAction(
        &actionNopInstruction, tr("Nop Instruction"), SLOT(on_actionNopInstruction_triggered()));
    editMenu->addAction(&actionNopInstruction);

    initAction(&actionEditBytes, tr("Bytes"), SLOT(on_actionEditBytes_triggered()));
    editMenu->addAction(&actionEditBytes);

    initAction(&actionJmpReverse, tr("Reverse Jump"), SLOT(on_actionJmpReverse_triggered()));
    editMenu->addAction(&actionJmpReverse);
}

void DisassemblyContextMenu::addBreakpointActions()
{
    QMenu *parentMenu = debugMenu ? debugMenu : this;

    initAction(
        &actionAddBreakpoint,
        tr("Add/remove breakpoint"),
        SLOT(on_actionAddBreakpoint_triggered()),
        getAddBPSequence());
    setActionIcon(&actionAddBreakpoint, MenuIcon::Breakpoint, QColor(216, 88, 64));
    parentMenu->addAction(&actionAddBreakpoint);
    initAction(
        &actionAdvancedBreakpoint,
        tr("Advanced breakpoint"),
        SLOT(on_actionAdvancedBreakpoint_triggered()),
        QKeySequence(Qt::CTRL | Qt::Key_F2));
    parentMenu->addAction(&actionAdvancedBreakpoint);
}

QVector<DisassemblyContextMenu::ThingUsedHere> DisassemblyContextMenu::getThingUsedHere(RVA offset)
{
    QVector<ThingUsedHere> result;
    const QJsonArray array = Core()->cmdj("anj @ " + QString::number(offset)).array();
    result.reserve(array.size());
    for (const auto &thing : array) {
        auto obj = thing.toObject();
        RVA offset = 0;
        if (obj.contains("offset")) {
            offset = obj["offset"].toVariant().toULongLong();
        } else if (obj.contains("addr")) {
            offset = obj["addr"].toVariant().toULongLong();
        }
        QString name;

        // If real names display is enabled, show flag's real name instead of
        // full flag name
        if (Config()->getConfigBool("asm.flags.real") && obj.contains("realname")) {
            name = obj["realname"].toString();
        } else {
            name = obj["name"].toString();
        }

        QString typeString = obj["type"].toString();
        ThingUsedHere::Type type = ThingUsedHere::Type::Address;
        if (typeString == "var") {
            type = ThingUsedHere::Type::Var;
        } else if (typeString == "flag") {
            type = ThingUsedHere::Type::Flag;
        } else if (typeString == "function") {
            type = ThingUsedHere::Type::Function;
        } else if (typeString == "address") {
            type = ThingUsedHere::Type::Address;
        }
        result.push_back(ThingUsedHere{name, offset, type});
    }
    return result;
}

void DisassemblyContextMenu::setOffset(RVA offset)
{
    this->offset = offset;
    this->actionSetFunctionVarTypes.setVisible(true);
}

void DisassemblyContextMenu::setCanCopy(bool enabled)
{
    this->canCopy = enabled;
}

void DisassemblyContextMenu::setCurHighlightedWord(const QString &text)
{
    this->curHighlightedWord = text;
    // Update the renaming options only when a new word is selected
    setupRenaming();
}

DisassemblyContextMenu::ThingUsedHere DisassemblyContextMenu::getThingAt(ut64 address)
{
    ThingUsedHere tuh;
    RAnalFunction *fcn = Core()->functionAt(address);
    RFlagItem *flag = r_flag_get_i(Core()->core()->flags, address);

    // We will lookup through existing r2 types to find something relevant

    if (fcn != nullptr) {
        // It is a function
        tuh.type = ThingUsedHere::Type::Function;
        tuh.name = fcn->name;
    } else if (flag != nullptr) {
        // It is a flag
        tuh.type = ThingUsedHere::Type::Flag;
        if (Config()->getConfigBool("asm.flags.real") && flag->realname) {
            tuh.name = flag->realname;
        } else {
            tuh.name = flag->name;
        }
    } else {
        // Consider it an address
        tuh.type = ThingUsedHere::Type::Address;
    }

    tuh.offset = address;
    return tuh;
}

void DisassemblyContextMenu::buildRenameMenu(ThingUsedHere *tuh)
{
    if (!tuh) {
        qWarning() << "Unexpected behavior null pointer passed to "
                      "DisassemblyContextMenu::buildRenameMenu";
        doRenameAction = RENAME_DO_NOTHING;
        return;
    }

    actionDeleteFlag.setVisible(false);
    // TODO: use switch
    if (tuh->type == ThingUsedHere::Type::Address) {
        doRenameAction = RENAME_ADD_FLAG;
        doRenameInfo.name = RAddressString(tuh->offset);
        doRenameInfo.addr = tuh->offset;
        actionRename.setText(tr("Add flag at %1 (used here)").arg(doRenameInfo.name));
    } else if (tuh->type == ThingUsedHere::Type::Function) {
        doRenameAction = RENAME_FUNCTION;
        doRenameInfo.name = tuh->name;
        doRenameInfo.addr = tuh->offset;
        actionRename.setText(tr("Rename \"%1\"").arg(doRenameInfo.name));
    } else if (tuh->type == ThingUsedHere::Type::Var) {
        doRenameAction = RENAME_LOCAL;
        doRenameInfo.name = tuh->name;
        doRenameInfo.addr = tuh->offset;
        actionRename.setText(tr("Rename local \"%1\"").arg(tuh->name));
    } else if (tuh->type == ThingUsedHere::Type::Flag) {
        doRenameAction = RENAME_FLAG;
        doRenameInfo.name = tuh->name;
        doRenameInfo.addr = tuh->offset;
        actionRename.setText(tr("Rename flag \"%1\" (used here)").arg(doRenameInfo.name));
        actionDeleteFlag.setVisible(true);
    } else {
        qWarning() << "Unexpected renaming type";
        doRenameAction = RENAME_DO_NOTHING;
    }
}

void DisassemblyContextMenu::setupRenaming()
{
    // We parse our highlighted word as an address
    ut64 selection = Core()->num(curHighlightedWord);

    // First, let's try to see if current line (offset) contains a local
    // variable or a function
    ThingUsedHere *tuh = nullptr;
    ThingUsedHere thingAt;
    auto things = getThingUsedHere(offset);
    for (auto &thing : things) {
        if (thing.offset == selection || thing.name == curHighlightedWord) {
            // We matched something on current line
            tuh = &thing;
            break;
        }
    }

    if (!tuh) {
        // Nothing matched on current line, is there anything valid coming from
        // our selection?
        thingAt = getThingAt(selection);

        if (thingAt.offset == 0) {
            // We parsed something which resolved to 0, it's very likely nothing
            // interesting was selected So we fallback on current line offset
            thingAt = getThingAt(offset);
        }

        // However, since for the moment selection selects *every* lines which
        // match a specific offset, make sure we didn't want to select a local
        // variable rather than the function itself
        if (thingAt.type == ThingUsedHere::Type::Function) {
            auto vars = Core()->getVariables(offset);
            for (auto v : vars) {
                if (v.name == curHighlightedWord) {
                    // This is a local variable
                    thingAt.type = ThingUsedHere::Type::Var;
                    thingAt.name = v.name;
                    break;
                }
            }
        }

        // In any case, thingAt will contain something we can rename
        tuh = &thingAt;
    }

    // Now, build the renaming menu and show it
    buildRenameMenu(tuh);
    actionRename.setVisible(true);
}

void DisassemblyContextMenu::aboutToShowSlot()
{
    // check if set immediate base menu makes sense
    QJsonObject instObject
        = Core()->cmdj("aoj @ " + QString::number(offset)).array().first().toObject();
    auto keys = instObject.keys();
    bool immBase = keys.contains("val") || keys.contains("ptr");
    setBaseMenu->menuAction()->setVisible(immBase);
    setBitsMenu->menuAction()->setVisible(true);
    // Populate "Relative to" submenu with asm.addr.relto options
    relativeToMenu->clear();
    // Query possible values via radare2 config
    QStringList relVals = Core()->cmdList("e asm.addr.relto=?");
    for (const QString &val : relVals) {
        if (val.isEmpty()) {
            continue;
        }
        QAction *act = relativeToMenu->addAction(val);
        act->setData(val);
    }
    // Show submenu only if options are available
    relativeToMenu->menuAction()->setVisible(!relVals.isEmpty());

    // Create structure offset menu if it makes sense
    QString memBaseReg; // Base register
    QVariant memDisp;   // Displacement
    if (instObject.contains("opex") && instObject["opex"].toObject().contains("operands")) {
        // Loop through both the operands of the instruction
        for (const QJsonValue value : instObject["opex"].toObject()["operands"].toArray()) {
            QJsonObject operand = value.toObject();
            if (operand.contains("type") && operand["type"].toString() == "mem"
                && operand.contains("base") && !operand["base"].toString().contains("bp")
                && operand.contains("disp") && operand["disp"].toVariant().toLongLong() > 0) {
                // The current operand is the one which has an immediate
                // displacement
                memBaseReg = operand["base"].toString();
                memDisp = operand["disp"].toVariant();
                break;
            }
        }
    }
    if (memBaseReg.isEmpty()) {
        // hide structure offset menu
        structureOffsetMenu->menuAction()->setVisible(false);
    } else {
        // show structure offset menu
        structureOffsetMenu->menuAction()->setVisible(true);
        structureOffsetMenu->clear();

        // Get the possible offsets using the "ahts" command
        // TODO: add ahtj command to radare2 and then use it here
        QStringList ret = Core()->cmdList("ahts " + memDisp.toString());
        for (const QString &val : ret) {
            if (val.isEmpty()) {
                continue;
            }
            structureOffsetMenu->addAction("[" + memBaseReg + " + " + val + "]")->setData(val);
        }
        if (structureOffsetMenu->isEmpty()) {
            // No possible offset was found so hide the menu
            structureOffsetMenu->menuAction()->setVisible(false);
        }
    }

    actionAnalyzeFunction.setVisible(true);

    actionToggleBBLines.blockSignals(true);
    actionToggleBBLines.setChecked(Config()->getConfigBool("asm.lines.bb"));
    actionToggleBBLines.blockSignals(false);

    // Show the option to remove a defined string only if a string is defined in
    // this address
    QString stringDefinition = Core()->cmdRawAt("Cs.", offset);
    actionSetAsStringRemove.setVisible(!stringDefinition.isEmpty());

    QString comment = Core()->cmdRawAt("CC.", offset);

    if (comment.isNull() || comment.isEmpty()) {
        actionDeleteComment.setVisible(false);
        actionAddComment.setText(tr("Add Comment"));
    } else {
        actionDeleteComment.setVisible(true);
        actionAddComment.setText(tr("Edit Comment"));
    }

    actionCopy.setVisible(canCopy);
    actionCopyBytes.setVisible(canCopy);
    copySeparator->setVisible(canCopy);

    // Handle renaming of variable, function, flag, ...
    // Note: This might be useless if we consider setCurrentHighlightedWord is
    // always called before
    setupRenaming();

    // Only show retype for local vars if in a function
    RAnalFunction *in_fcn = Core()->functionIn(offset);
    if (in_fcn) {
        auto vars = Core()->getVariables(offset);
        actionCopyFunctionDisasm.setVisible(true);
        actionCopyFunctionBytes.setVisible(true);
        actionSetFunctionVarTypes.setVisible(!vars.empty());
        actionEditFunction.setVisible(true);
        actionEditFunction.setText(tr("Edit function \"%1\"").arg(in_fcn->name));
    } else {
        actionCopyFunctionDisasm.setVisible(false);
        actionCopyFunctionBytes.setVisible(false);
        actionSetFunctionVarTypes.setVisible(false);
        actionEditFunction.setVisible(false);
    }

    // Decide to show Reverse jmp option
    showReverseJmpQuery();

    if (showInSubmenu.menu() != nullptr) {
        showInSubmenu.menu()->deleteLater();
    }
    if (mainWindow) {
        showInSubmenu.setMenu(mainWindow->createShowInMenu(this, offset));
    }

    // Keep debug affordances discoverable, but disable actions that require an
    // active debug session.
    const bool debugging = Core()->currentlyDebugging;
    const QString debugDisabledTip = tr("Start a debug session to use this action");
    actionSetProgramCounter.setEnabled(debugging);
    actionSetProgramCounter.setToolTip(debugging ? QString() : debugDisabledTip);
    actionSetProgramCounter.setStatusTip(debugging ? QString() : debugDisabledTip);
    actionContinueUntil.setEnabled(debugging);
    actionContinueUntil.setToolTip(debugging ? QString() : debugDisabledTip);
    actionContinueUntil.setStatusTip(debugging ? QString() : debugDisabledTip);
    bool hasBreakpoint = Core()->breakpointIndexAt(offset) > -1;
    actionAddBreakpoint.setText(hasBreakpoint ? tr("Remove breakpoint") : tr("Add breakpoint"));
    actionAdvancedBreakpoint.setText(
        hasBreakpoint ? tr("Edit breakpoint") : tr("Advanced breakpoint"));
    QString progCounterName = Core()->getRegisterName("PC").toUpper();
    actionSetProgramCounter.setText(tr("Set %1 here").arg(progCounterName));

    if (pluginMenu) {
        pluginActionMenuAction->setVisible(!pluginMenu->isEmpty());
        for (QAction *pluginAction : pluginMenu->actions()) {
            pluginAction->setData(QVariant::fromValue(offset));
        }
    }

    bool isLocalVar = isHighlightedWordLocalVar();
    actionXRefsForVariables.setVisible(isLocalVar);
    if (isLocalVar) {
        actionXRefsForVariables.setText(tr("X-Refs for %1").arg(curHighlightedWord));
    }
}

void DisassemblyContextMenu::aboutToHideSlot()
{
    actionXRefsForVariables.setVisible(true);
}

QKeySequence DisassemblyContextMenu::getCopySequence() const
{
    return QKeySequence::Copy;
}

QKeySequence DisassemblyContextMenu::getCommentSequence() const
{
    return {Qt::Key_Semicolon};
}

QKeySequence DisassemblyContextMenu::getCopyAddressSequence() const
{
    return {Qt::CTRL | Qt::SHIFT | Qt::Key_C};
}

QKeySequence DisassemblyContextMenu::getSetToCodeSequence() const
{
    return {Qt::Key_C};
}

QKeySequence DisassemblyContextMenu::getSetAsStringSequence() const
{
    return {Qt::Key_A};
}

QKeySequence DisassemblyContextMenu::getSetAsStringAdvanced() const
{
    return {Qt::SHIFT | Qt::Key_A};
}

QKeySequence DisassemblyContextMenu::getSetToDataSequence() const
{
    return {Qt::Key_D};
}

QKeySequence DisassemblyContextMenu::getSetToDataExSequence() const
{
    return {Qt::Key_Asterisk};
}

QKeySequence DisassemblyContextMenu::getRenameSequence() const
{
    return {Qt::Key_N};
}

QKeySequence DisassemblyContextMenu::getRetypeSequence() const
{
    return {Qt::Key_Y};
}

QKeySequence DisassemblyContextMenu::getXRefSequence() const
{
    return {Qt::Key_X};
}

QKeySequence DisassemblyContextMenu::getDisplayOptionsSequence() const
{
    return {}; // TODO insert correct sequence
}

QKeySequence DisassemblyContextMenu::getLinkTypeSequence() const
{
    return {Qt::Key_L};
}

QList<QKeySequence> DisassemblyContextMenu::getAddBPSequence() const
{
    return {Qt::Key_F2, Qt::CTRL | Qt::Key_B};
}

QKeySequence DisassemblyContextMenu::getDefineNewFunctionSequence() const
{
    return {Qt::Key_P};
}

QKeySequence DisassemblyContextMenu::getEditFunctionSequence() const
{
    return {Qt::SHIFT | Qt::Key_P};
}

QKeySequence DisassemblyContextMenu::getUndefineFunctionSequence() const
{
    return {Qt::Key_U};
}

void DisassemblyContextMenu::on_actionEditInstruction_triggered()
{
    if (!ioModesController.prepareForWriting()) {
        return;
    }
    EditInstructionDialog e(EDIT_TEXT, this);
    e.setWindowTitle(tr("Edit Instruction at %1").arg(RAddressString(offset)));

    QString oldInstructionOpcode = Core()->getInstructionOpcode(offset);
    QString oldInstructionBytes = Core()->getInstructionBytes(offset);

    e.setInstruction(oldInstructionOpcode);

    if (e.exec()) {
        QString userInstructionOpcode = e.getInstruction();
        if (userInstructionOpcode != oldInstructionOpcode) {
            Core()->editInstruction(offset, userInstructionOpcode);
        }
    }
}

void DisassemblyContextMenu::on_actionNopInstruction_triggered()
{
    if (!ioModesController.prepareForWriting()) {
        return;
    }
    Core()->nopInstruction(offset);
}

void DisassemblyContextMenu::showReverseJmpQuery()
{
    QString type;

    QJsonArray array = Core()->cmdj("pdj 1 @ " + RAddressString(offset)).array();
    if (array.isEmpty()) {
        return;
    }

    type = array.first().toObject()["type"].toString();
    if (type == "cjmp") {
        actionJmpReverse.setVisible(true);
    } else {
        actionJmpReverse.setVisible(false);
    }
}

void DisassemblyContextMenu::on_actionJmpReverse_triggered()
{
    if (!ioModesController.prepareForWriting()) {
        return;
    }
    Core()->jmpReverse(offset);
}

void DisassemblyContextMenu::on_actionEditBytes_triggered()
{
    if (!ioModesController.prepareForWriting()) {
        return;
    }
    EditInstructionDialog e(EDIT_BYTES, this);
    e.setWindowTitle(tr("Edit Bytes at %1").arg(RAddressString(offset)));

    QString oldBytes = Core()->getInstructionBytes(offset);
    e.setInstruction(oldBytes);

    if (e.exec()) {
        QString bytes = e.getInstruction();
        if (bytes != oldBytes) {
            Core()->editBytes(offset, bytes);
        }
    }
}

void DisassemblyContextMenu::on_actionCopy_triggered()
{
    emit copy();
}

void DisassemblyContextMenu::on_actionCopyAddr_triggered()
{
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(RAddressString(offset));
}

void DisassemblyContextMenu::on_actionCopyInstruction_triggered()
{
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(Core()->getInstructionOpcode(offset));
}

void DisassemblyContextMenu::on_actionCopyInstructionBytes_triggered()
{
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(Core()->getInstructionBytes(offset));
}

void DisassemblyContextMenu::on_actionCopyFunctionDisasm_triggered()
{
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(Core()->cmdRawAt("pdr", offset));
}

void DisassemblyContextMenu::on_actionCopyFunctionBytes_triggered()
{
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(Core()->cmdRawAt("p8f", offset).trimmed());
}

// Slot triggered by context menu to request copying raw bytes of the selected instructions
void DisassemblyContextMenu::on_actionCopyBytes_triggered()
{
    emit copyBytes();
}

void DisassemblyContextMenu::on_actionAddBreakpoint_triggered()
{
    Core()->toggleBreakpoint(offset);
}

void DisassemblyContextMenu::on_actionAdvancedBreakpoint_triggered()
{
    int index = Core()->breakpointIndexAt(offset);
    if (index >= 0) {
        BreakpointsDialog::editBreakpoint(Core()->getBreakpointAt(offset), this);
    } else {
        BreakpointsDialog::createNewBreakpoint(offset, this);
    }
}

void DisassemblyContextMenu::on_actionContinueUntil_triggered()
{
    Core()->continueUntilDebug(RAddressString(offset));
}

void DisassemblyContextMenu::on_actionSetPC_triggered()
{
    QString progCounterName = Core()->getRegisterName("PC");
    Core()->setRegister(progCounterName, RAddressString(offset).toUpper());
}

void DisassemblyContextMenu::on_actionAddComment_triggered()
{
    CommentsDialog::addOrEditComment(offset, this);
}

void DisassemblyContextMenu::on_actionAnalyzeFunction_triggered()
{
    bool ok;
    // Create dialog
    QString functionName = QInputDialog::getText(
        this,
        tr("New function %1").arg(RAddressString(offset)),
        tr("Function name:"),
        QLineEdit::Normal,
        QString(),
        &ok);

    // If user accepted
    if (ok && !functionName.isEmpty()) {
        Core()->createFunctionAt(offset, functionName);
    }
}

void DisassemblyContextMenu::on_actionEditAnnotation_triggered()
{
    QString os = Core()->cmdRaw("anos");
    QString *s = openTextEditDialog(os, this);
    if (s != nullptr) {
        Core()->cmdRaw(QStringLiteral("ano=base64:%1").arg(QString(s->toLocal8Bit().toBase64())));
        this->mainWindow->refreshAll();
    }
}

void DisassemblyContextMenu::on_actionRename_triggered()
{
    bool ok = false;
    switch (doRenameAction) {
    case RENAME_FUNCTION: {
        QString newName = QInputDialog::getText(
            this->mainWindow,
            tr("Rename function %2").arg(doRenameInfo.name),
            tr("Function name:"),
            QLineEdit::Normal,
            doRenameInfo.name,
            &ok);
        if (ok && !newName.isEmpty()) {
            Core()->renameFunction(doRenameInfo.addr, newName);
        }
        break;
    }
    case RENAME_FLAG:
    case RENAME_ADD_FLAG: {
        // defaultSize unused for existing flags, use 1
        FlagDialog dialog(doRenameInfo.addr, 1, this->mainWindow);
        ok = (dialog.exec() == QDialog::Accepted);
    } break;
    case RENAME_LOCAL: {
        RAnalFunction *fcn = Core()->functionIn(offset);
        if (fcn) {
            EditVariablesDialog dialog(fcn->addr, curHighlightedWord, this->mainWindow);
            if (!dialog.empty()) {
                // Don't show the dialog if there are no variables
                ok = dialog.exec();
            }
        }
        break;
    }
    case RENAME_DO_NOTHING:
        // Do nothing
        break;
    default:
        qWarning() << "Unhandled renaming action: " << doRenameAction;
        // assert(false);
        break;
    }

    if (ok) {
        // Rebuild menu in case the user presses the rename shortcut directly
        // before clicking
        setupRenaming();
    }
}

void DisassemblyContextMenu::on_actionSetFunctionVarTypes_triggered()
{
    RAnalFunction *fcn = Core()->functionIn(offset);

    if (!fcn) {
        QMessageBox::critical(
            this,
            tr("Re-type Local Variables"),
            tr("You must be in a function to define variable types."));
        return;
    }

    EditVariablesDialog dialog(fcn->addr, curHighlightedWord, this);
    if (dialog.empty()) { // don't show the dialog if there are no variables
        return;
    }
    dialog.exec();
}

void DisassemblyContextMenu::on_actionXRefs_triggered()
{
    XrefsDialog dialog(mainWindow, nullptr);
    dialog.fillRefsForAddress(offset, RAddressString(offset), false);
    dialog.exec();
}

void DisassemblyContextMenu::on_actionXRefsForVariables_triggered()
{
    if (isHighlightedWordLocalVar()) {
        XrefsDialog dialog(mainWindow, nullptr);
        dialog.fillRefsForVariable(curHighlightedWord, offset);
        dialog.exec();
    }
}

void DisassemblyContextMenu::on_actionDisplayOptions_triggered()
{
    QWidget *dialogParent = this->window();
    SettingsDialog *dialog = dialogParent->findChild<SettingsDialog *>();
    if (!dialog) {
        dialog = new SettingsDialog(dialogParent);
    }
    dialog->showSection(SettingsDialog::Section::Disassembly);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void DisassemblyContextMenu::on_actionSetToCode_triggered()
{
    Core()->setToCode(offset);
}

void DisassemblyContextMenu::on_actionSetAsString_triggered()
{
    Core()->setAsString(offset);
}

void DisassemblyContextMenu::on_actionSetAsStringRemove_triggered()
{
    Core()->removeString(offset);
}

void DisassemblyContextMenu::on_actionSetAsStringAdvanced_triggered()
{
    EditStringDialog dialog(parentWidget());
    const int predictedStrSize = Core()->getString(offset).size();
    dialog.setStringSizeValue(predictedStrSize);
    dialog.setStringStartAddress(offset);

    if (!dialog.exec()) {
        return;
    }

    uint64_t strAddr = 0U;
    if (!dialog.getStringStartAddress(strAddr)) {
        QMessageBox::critical(
            this->window(), tr("Wrong address"), tr("Can't edit string at this address"));
        return;
    }
    IaitoCore::StringTypeFormats coreStringType = IaitoCore::StringTypeFormats::s_None;

    const auto strSize = dialog.getStringSizeValue();
    const auto strType = dialog.getStringType();
    switch (strType) {
    case EditStringDialog::StringType::s_Auto:
        coreStringType = IaitoCore::StringTypeFormats::s_None;
        break;
    case EditStringDialog::StringType::s_ASCII_LATIN1:
        coreStringType = IaitoCore::StringTypeFormats::s_ASCII_LATIN1;
        break;
    case EditStringDialog::StringType::s_UTF8:
        coreStringType = IaitoCore::StringTypeFormats::s_UTF8;
        break;
    case EditStringDialog::StringType::s_UTF16:
        coreStringType = IaitoCore::StringTypeFormats::s_UTF16;
        break;
    case EditStringDialog::StringType::s_PASCAL:
        coreStringType = IaitoCore::StringTypeFormats::s_PASCAL;
        break;
    };

    Core()->setAsString(strAddr, strSize, coreStringType);
}

void DisassemblyContextMenu::on_actionSetToData_triggered()
{
    int size = Core()->sizeofDataMeta(offset);
    if (size > 8 || (size && (size & (size - 1)))) {
        return;
    }
    if (size == 0 || size == 8) {
        size = 1;
    } else {
        size *= 2;
    }
    setToData(size);
}

void DisassemblyContextMenu::on_actionSetToDataEx_triggered()
{
    SetToDataDialog dialog(offset, this->window());
    if (!dialog.exec()) {
        return;
    }
    setToData(dialog.getItemSize(), dialog.getItemCount());
}

void DisassemblyContextMenu::on_actionStructureOffsetMenu_triggered(QAction *action)
{
    Core()->applyStructureOffset(action->data().toString(), offset);
}

void DisassemblyContextMenu::on_actionLinkType_triggered()
{
    LinkTypeDialog dialog(mainWindow);
    if (!dialog.setDefaultAddress(curHighlightedWord)) {
        dialog.setDefaultAddress(RAddressString(offset));
    }
    dialog.exec();
}
// Slot to handle selection of "Relative to" values
void DisassemblyContextMenu::on_actionRelativeTo_triggered(QAction *action)
{
    const QString val = action->data().toString();
    // Set the asm.addr.relto configuration and refresh disassembly view
    Core()->setConfig("asm.addr.relto", val);
    Core()->triggerAsmOptionsChanged();
}

void DisassemblyContextMenu::on_actionDeleteComment_triggered()
{
    Core()->delComment(offset);
}

void DisassemblyContextMenu::on_actionDeleteFlag_triggered()
{
    Core()->delFlag(offset);
}

void DisassemblyContextMenu::on_actionDeleteFunction_triggered()
{
    Core()->delFunction(offset);
}

void DisassemblyContextMenu::on_actionEditFunction_triggered()
{
    RCore *core = Core()->core();
    EditFunctionDialog dialog(mainWindow);
    RAnalFunction *fcn = r_anal_get_fcn_in(core->anal, offset, 0);

    if (fcn) {
        dialog.setWindowTitle(tr("Edit function %1").arg(fcn->name));
        dialog.setNameText(fcn->name);

        QString startAddrText = "0x" + QString::number(fcn->addr, 16);
        dialog.setStartAddrText(startAddrText);

        dialog.setStackSizeText(QString::number(fcn->stack));

        QStringList callConList = Core()->cmdRaw("afcl").split("\n");
        callConList.removeLast();
        dialog.setCallConList(callConList);
        dialog.setCallConSelected(fcn->callconv);

        if (dialog.exec()) {
            QString new_name = dialog.getNameText();
            Core()->renameFunction(fcn->addr, new_name);
            QString new_start_addr = dialog.getStartAddrText();
            ut64 newAddr = Core()->math(new_start_addr);
            if (newAddr != fcn->addr) {
                r_anal_function_relocate(fcn, newAddr);
            }
            QString new_stack_size = dialog.getStackSizeText();
            Core()->cmdRawAt(QStringLiteral("afS %1").arg(Core()->math(new_stack_size)), newAddr);
            Core()->cmdRaw("afc " + dialog.getCallConSelected());
            emit Core() -> functionsChanged();
        }
    }
}

void DisassemblyContextMenu::setBase(QString base)
{
    Core()->setImmediateBase(base, offset);
}

void DisassemblyContextMenu::setBits(int bits)
{
    Core()->setCurrentBits(bits, offset);
}

void DisassemblyContextMenu::setColor(const QString &color)
{
    // r2's `abc` keys the color by the exact address passed, while the graph
    // reads it back keyed by the basic block entry. Resolve the block entry
    // for the current offset so picking a color on any instruction of the
    // block still colors the block. Remove once r2 resolves this on its side.
    RVA target = offset;
    QJsonArray bbs = Core()->cmdj(QStringLiteral("afbj. @ %1").arg(offset)).array();
    if (!bbs.isEmpty()) {
        RVA entry = bbs.first().toObject().value("addr").toVariant().toULongLong();
        if (entry != 0) {
            target = entry;
        }
    }
    if (color.isEmpty()) {
        Core()->cmd(QStringLiteral("abc- @ %1").arg(target));
    } else {
        Core()->cmd(QStringLiteral("abc %1 @ %2").arg(color).arg(target));
    }
    emit Config() -> colorsUpdated();
}

void DisassemblyContextMenu::setToData(int size, int repeat)
{
    Core()->setToData(offset, size, repeat);
}

QAction *DisassemblyContextMenu::addAnonymousAction(
    QString name, const char *slot, QKeySequence keySequence)
{
    auto action = new QAction(this);
    addAction(action);
    anonymousActions.append(action);
    initAction(action, name, slot, keySequence);
    return action;
}

void DisassemblyContextMenu::initAction(QAction *action, QString name, const char *slot)
{
    action->setParent(this);
    parentWidget()->addAction(action);
    action->setText(name);
    if (slot) {
        connect(action, SIGNAL(triggered(bool)), this, slot);
    }
}

void DisassemblyContextMenu::initAction(
    QAction *action, QString name, const char *slot, QKeySequence keySequence)
{
    initAction(action, name, slot);
    if (keySequence.isEmpty()) {
        return;
    }
    action->setShortcut(keySequence);
    action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
}

void DisassemblyContextMenu::initAction(
    QAction *action, QString name, const char *slot, QList<QKeySequence> keySequenceList)
{
    initAction(action, name, slot);
    if (keySequenceList.empty()) {
        return;
    }
    action->setShortcuts(keySequenceList);
    action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
}

bool DisassemblyContextMenu::isHighlightedWordLocalVar()
{
    QList<VariableDescription> variables = Core()->getVariables(offset);
    for (const VariableDescription &var : variables) {
        if (var.name == curHighlightedWord) {
            return true;
        }
    }
    return false;
}
