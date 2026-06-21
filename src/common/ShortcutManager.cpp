#include "common/ShortcutManager.h"
#include "core/Iaito.h"

#include <QAction>
#include <QKeyEvent>
#include <QShortcut>
#include <QWidget>

namespace {

struct ShortcutDescriptor
{
    const char *id;
    ShortcutScope scope;
    const char *displayName;
    QList<QKeySequence> defaults;
    QKeySequence::StandardKey stdKey;
};

const QVector<ShortcutDescriptor> &catalog()
{
    static const QVector<ShortcutDescriptor> c = [] {
        const auto Unknown = QKeySequence::UnknownKey;
        QVector<ShortcutDescriptor> t;
        t.append(
            {"global.open",
             ShortcutScope::Global,
             QT_TR_NOOP("Open file"),
             {QKeySequence(Qt::CTRL | Qt::Key_N)},
             Unknown});
        t.append(
            {"global.mapFile",
             ShortcutScope::Global,
             QT_TR_NOOP("Map file at offset"),
             {QKeySequence(Qt::CTRL | Qt::Key_M)},
             Unknown});
        t.append(
            {"global.saveProject",
             ShortcutScope::Global,
             QT_TR_NOOP("Save project"),
             {QKeySequence(Qt::CTRL | Qt::Key_S)},
             Unknown});
        t.append(
            {"global.quit",
             ShortcutScope::Global,
             QT_TR_NOOP("Quit"),
             {QKeySequence(Qt::CTRL | Qt::Key_Q)},
             Unknown});
        t.append(
            {"global.highlightWord",
             ShortcutScope::Global,
             QT_TR_NOOP("Highlight matching words"),
             {QKeySequence(QStringLiteral("Meta+F")), QKeySequence(QStringLiteral("Ctrl+F"))},
             Unknown});
        t.append(
            {"global.zoomIn",
             ShortcutScope::Global,
             QT_TR_NOOP("Zoom in"),
             {QKeySequence(QStringLiteral("Ctrl++"))},
             Unknown});
        t.append(
            {"global.zoomOut",
             ShortcutScope::Global,
             QT_TR_NOOP("Zoom out"),
             {QKeySequence(QStringLiteral("Ctrl+-"))},
             Unknown});
        t.append(
            {"global.zoomReset",
             ShortcutScope::Global,
             QT_TR_NOOP("Reset zoom"),
             {QKeySequence(QStringLiteral("Ctrl+0"))},
             Unknown});
        t.append(
            {"global.focusConsole",
             ShortcutScope::Global,
             QT_TR_NOOP("Focus console input"),
             {QKeySequence(Qt::Key_Period)},
             Unknown});
        t.append(
            {"global.seekExpression",
             ShortcutScope::Global,
             QT_TR_NOOP("Open goto/seek bar"),
             {QKeySequence(Qt::Key_S)},
             Unknown});
        t.append(
            {"global.seekFunctionEnd",
             ShortcutScope::Global,
             QT_TR_NOOP("Seek to function end"),
             {QKeySequence(Qt::Key_Dollar)},
             Unknown});
        t.append(
            {"global.seekFunctionStart",
             ShortcutScope::Global,
             QT_TR_NOOP("Seek to function start"),
             {QKeySequence(Qt::Key_AsciiCircum)},
             Unknown});
        t.append(
            {"global.refresh",
             ShortcutScope::Global,
             QT_TR_NOOP("Refresh all"),
             {},
             QKeySequence::Refresh});
        t.append(
            {"global.closeTab",
             ShortcutScope::Global,
             QT_TR_NOOP("Close tab"),
             {},
             QKeySequence::Close});
        t.append(
            {"global.back",
             ShortcutScope::Global,
             QT_TR_NOOP("Seek backward"),
             {},
             QKeySequence::Back});
        t.append(
            {"global.forward",
             ShortcutScope::Global,
             QT_TR_NOOP("Seek forward"),
             {},
             QKeySequence::Forward});
        t.append(
            {"global.showCode",
             ShortcutScope::Global,
             QT_TR_NOOP("Show Disassembly view"),
             {QKeySequence(Qt::Key_C)},
             Unknown});
        t.append(
            {"global.showGraph",
             ShortcutScope::Global,
             QT_TR_NOOP("Show Graph view"),
             {QKeySequence(Qt::Key_G)},
             Unknown});
        t.append(
            {"global.showTypes",
             ShortcutScope::Global,
             QT_TR_NOOP("Toggle Types panel"),
             {QKeySequence(Qt::Key_T)},
             Unknown});
        t.append(
            {"widget.toggleStrings",
             ShortcutScope::Global,
             QT_TR_NOOP("Toggle Strings panel"),
             {QKeySequence(Qt::SHIFT | Qt::Key_F12)},
             Unknown});
        t.append(
            {"widget.toggleGraph",
             ShortcutScope::Global,
             QT_TR_NOOP("Toggle Graph panel"),
             {QKeySequence(Qt::SHIFT | Qt::Key_G)},
             Unknown});
        t.append(
            {"widget.toggleImports",
             ShortcutScope::Global,
             QT_TR_NOOP("Toggle Imports panel"),
             {QKeySequence(Qt::SHIFT | Qt::Key_I)},
             Unknown});
        t.append(
            {"widget.toggleExports",
             ShortcutScope::Global,
             QT_TR_NOOP("Toggle Exports panel"),
             {QKeySequence(Qt::SHIFT | Qt::Key_E)},
             Unknown});
        t.append(
            {"marks.set",
             ShortcutScope::Global,
             QT_TR_NOOP("Set mark"),
             {QKeySequence(Qt::Key_M)},
             Unknown});
        t.append(
            {"marks.jump",
             ShortcutScope::Global,
             QT_TR_NOOP("Jump to mark"),
             {QKeySequence(Qt::Key_Apostrophe)},
             Unknown});

        t.append(
            {"console.toggle",
             ShortcutScope::Console,
             QT_TR_NOOP("Toggle Console panel"),
             {QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft), QKeySequence(Qt::Key_Colon)},
             Unknown});
        t.append(
            {"console.clearOutput",
             ShortcutScope::Console,
             QT_TR_NOOP("Clear output"),
             {QKeySequence(Qt::CTRL | Qt::Key_L)},
             Unknown});
        t.append(
            {"console.historyPrev",
             ShortcutScope::Console,
             QT_TR_NOOP("Previous command"),
             {QKeySequence(Qt::Key_Up)},
             Unknown});
        t.append(
            {"console.historyNext",
             ShortcutScope::Console,
             QT_TR_NOOP("Next command"),
             {QKeySequence(Qt::Key_Down)},
             Unknown});
        t.append(
            {"console.complete",
             ShortcutScope::Console,
             QT_TR_NOOP("Trigger completion"),
             {QKeySequence(Qt::Key_Tab)},
             Unknown});
        t.append(
            {"console.clearInput",
             ShortcutScope::Console,
             QT_TR_NOOP("Clear input"),
             {QKeySequence(Qt::Key_Escape)},
             Unknown});

        t.append(
            {"disasm.copy", ShortcutScope::Disassembly, QT_TR_NOOP("Copy"), {}, QKeySequence::Copy});
        t.append(
            {"disasm.comment",
             ShortcutScope::Disassembly,
             QT_TR_NOOP("Add comment"),
             {QKeySequence(Qt::Key_Semicolon)},
             Unknown});
        t.append(
            {"disasm.copyAddress",
             ShortcutScope::Disassembly,
             QT_TR_NOOP("Copy address"),
             {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C)},
             Unknown});
        t.append(
            {"disasm.setToCode",
             ShortcutScope::Disassembly,
             QT_TR_NOOP("Set to code"),
             {QKeySequence(Qt::Key_C)},
             Unknown});
        t.append(
            {"disasm.setAsString",
             ShortcutScope::Disassembly,
             QT_TR_NOOP("Set as string"),
             {QKeySequence(Qt::Key_A)},
             Unknown});
        t.append(
            {"disasm.setAsStringAdvanced",
             ShortcutScope::Disassembly,
             QT_TR_NOOP("Set as string (advanced)"),
             {QKeySequence(Qt::SHIFT | Qt::Key_A)},
             Unknown});
        t.append(
            {"disasm.setToData",
             ShortcutScope::Disassembly,
             QT_TR_NOOP("Set to data"),
             {QKeySequence(Qt::Key_D)},
             Unknown});
        t.append(
            {"disasm.setToDataEx",
             ShortcutScope::Disassembly,
             QT_TR_NOOP("Set to data (resize)"),
             {QKeySequence(Qt::Key_Asterisk)},
             Unknown});
        t.append(
            {"disasm.rename",
             ShortcutScope::Disassembly,
             QT_TR_NOOP("Rename"),
             {QKeySequence(Qt::Key_N)},
             Unknown});
        t.append(
            {"disasm.retype",
             ShortcutScope::Disassembly,
             QT_TR_NOOP("Edit variables/retype"),
             {QKeySequence(Qt::Key_Y)},
             Unknown});
        t.append(
            {"disasm.xrefs",
             ShortcutScope::Disassembly,
             QT_TR_NOOP("Show X-Refs"),
             {QKeySequence(Qt::Key_X)},
             Unknown});
        t.append(
            {"disasm.linkType",
             ShortcutScope::Disassembly,
             QT_TR_NOOP("Link type"),
             {QKeySequence(Qt::Key_L)},
             Unknown});
        t.append(
            {"disasm.addBreakpoint",
             ShortcutScope::Disassembly,
             QT_TR_NOOP("Add breakpoint"),
             {QKeySequence(Qt::Key_F2), QKeySequence(Qt::CTRL | Qt::Key_B)},
             Unknown});
        t.append(
            {"disasm.defineFunction",
             ShortcutScope::Disassembly,
             QT_TR_NOOP("Define new function"),
             {QKeySequence(Qt::Key_P)},
             Unknown});
        t.append(
            {"disasm.editFunction",
             ShortcutScope::Disassembly,
             QT_TR_NOOP("Edit function"),
             {QKeySequence(Qt::SHIFT | Qt::Key_P)},
             Unknown});
        t.append(
            {"disasm.undefineFunction",
             ShortcutScope::Disassembly,
             QT_TR_NOOP("Undefine function"),
             {QKeySequence(Qt::Key_U)},
             Unknown});
        t.append(
            {"disasm.switchToGraph",
             ShortcutScope::Disassembly,
             QT_TR_NOOP("Switch to graph view"),
             {QKeySequence(Qt::Key_Space)},
             Unknown});
        t.append(
            {"disasm.seekPrev",
             ShortcutScope::Disassembly,
             QT_TR_NOOP("Seek to previous"),
             {QKeySequence(Qt::Key_Escape)},
             Unknown});
        t.append(
            {"disasm.cursorDown",
             ShortcutScope::Disassembly,
             QT_TR_NOOP("Move cursor down"),
             {QKeySequence(Qt::Key_J), QKeySequence(Qt::Key_Down)},
             Unknown});
        t.append(
            {"disasm.cursorUp",
             ShortcutScope::Disassembly,
             QT_TR_NOOP("Move cursor up"),
             {QKeySequence(Qt::Key_K), QKeySequence(Qt::Key_Up)},
             Unknown});
        t.append(
            {"disasm.cursorPageDown",
             ShortcutScope::Disassembly,
             QT_TR_NOOP("Cursor page down"),
             {},
             QKeySequence::MoveToNextPage});
        t.append(
            {"disasm.cursorPageUp",
             ShortcutScope::Disassembly,
             QT_TR_NOOP("Cursor page up"),
             {},
             QKeySequence::MoveToPreviousPage});
        t.append(
            {"disasm.jumpToOffset",
             ShortcutScope::Disassembly,
             QT_TR_NOOP("Jump to offset under cursor"),
             {QKeySequence(Qt::Key_Return)},
             Unknown});

        t.append(
            {"decompiler.copy",
             ShortcutScope::Decompiler,
             QT_TR_NOOP("Copy"),
             {},
             QKeySequence::Copy});
        t.append(
            {"decompiler.comment",
             ShortcutScope::Decompiler,
             QT_TR_NOOP("Add comment"),
             {QKeySequence(Qt::Key_Semicolon)},
             Unknown});
        t.append(
            {"decompiler.copyAddress",
             ShortcutScope::Decompiler,
             QT_TR_NOOP("Copy address"),
             {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C)},
             Unknown});
        t.append(
            {"decompiler.xrefs",
             ShortcutScope::Decompiler,
             QT_TR_NOOP("Show X-Refs"),
             {QKeySequence(Qt::Key_X)},
             Unknown});
        t.append(
            {"decompiler.rename",
             ShortcutScope::Decompiler,
             QT_TR_NOOP("Rename"),
             {QKeySequence(Qt::Key_N)},
             Unknown});
        t.append(
            {"decompiler.editVariables",
             ShortcutScope::Decompiler,
             QT_TR_NOOP("Edit variables"),
             {QKeySequence(Qt::Key_Y)},
             Unknown});
        t.append(
            {"decompiler.toggleBreakpoint",
             ShortcutScope::Decompiler,
             QT_TR_NOOP("Toggle breakpoint"),
             {QKeySequence(Qt::Key_F2), QKeySequence(Qt::CTRL | Qt::Key_B)},
             Unknown});
        t.append(
            {"decompiler.advancedBreakpoint",
             ShortcutScope::Decompiler,
             QT_TR_NOOP("Advanced breakpoint"),
             {QKeySequence(Qt::CTRL | Qt::Key_F2)},
             Unknown});
        t.append(
            {"decompiler.seekPrev",
             ShortcutScope::Decompiler,
             QT_TR_NOOP("Seek to previous"),
             {QKeySequence(Qt::Key_Escape)},
             Unknown});

        t.append(
            {"addressable.copyAddress",
             ShortcutScope::AddressableList,
             QT_TR_NOOP("Copy address"),
             {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C)},
             Unknown});
        t.append(
            {"addressable.xrefs",
             ShortcutScope::AddressableList,
             QT_TR_NOOP("Show X-Refs"),
             {QKeySequence(Qt::Key_X)},
             Unknown});
        t.append(
            {"addressable.comment",
             ShortcutScope::AddressableList,
             QT_TR_NOOP("Add comment"),
             {QKeySequence(Qt::Key_Semicolon)},
             Unknown});
        t.append(
            {"list.showFilter",
             ShortcutScope::AddressableList,
             QT_TR_NOOP("Show filter"),
             {},
             QKeySequence::Find});
        t.append(
            {"list.clearFilter",
             ShortcutScope::AddressableList,
             QT_TR_NOOP("Clear filter"),
             {QKeySequence(Qt::Key_Escape)},
             Unknown});
        t.append(
            {"functions.rename",
             ShortcutScope::AddressableList,
             QT_TR_NOOP("Rename function"),
             {QKeySequence(Qt::Key_N)},
             Unknown});
        t.append(
            {"search.run",
             ShortcutScope::AddressableList,
             QT_TR_NOOP("Run search"),
             {QKeySequence(Qt::Key_Return)},
             Unknown});

        t.append(
            {"hex.navLeft",
             ShortcutScope::Hexdump,
             QT_TR_NOOP("Move left"),
             {QKeySequence(Qt::Key_H)},
             Unknown});
        t.append(
            {"hex.navDown",
             ShortcutScope::Hexdump,
             QT_TR_NOOP("Move down"),
             {QKeySequence(Qt::Key_J)},
             Unknown});
        t.append(
            {"hex.navUp",
             ShortcutScope::Hexdump,
             QT_TR_NOOP("Move up"),
             {QKeySequence(Qt::Key_K)},
             Unknown});
        t.append(
            {"hex.navRight",
             ShortcutScope::Hexdump,
             QT_TR_NOOP("Move right"),
             {QKeySequence(Qt::Key_L)},
             Unknown});
        t.append(
            {"hex.navLeftSelect",
             ShortcutScope::Hexdump,
             QT_TR_NOOP("Select left"),
             {QKeySequence(Qt::SHIFT | Qt::Key_H)},
             Unknown});
        t.append(
            {"hex.navDownSelect",
             ShortcutScope::Hexdump,
             QT_TR_NOOP("Select down"),
             {QKeySequence(Qt::SHIFT | Qt::Key_J)},
             Unknown});
        t.append(
            {"hex.navUpSelect",
             ShortcutScope::Hexdump,
             QT_TR_NOOP("Select up"),
             {QKeySequence(Qt::SHIFT | Qt::Key_K)},
             Unknown});
        t.append(
            {"hex.navRightSelect",
             ShortcutScope::Hexdump,
             QT_TR_NOOP("Select right"),
             {QKeySequence(Qt::SHIFT | Qt::Key_L)},
             Unknown});
        t.append({"hex.copy", ShortcutScope::Hexdump, QT_TR_NOOP("Copy"), {}, QKeySequence::Copy});
        t.append(
            {"hex.copyAddress",
             ShortcutScope::Hexdump,
             QT_TR_NOOP("Copy address"),
             {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C)},
             Unknown});

        t.append(
            {"graph.zoomIn",
             ShortcutScope::Graph,
             QT_TR_NOOP("Zoom in"),
             {QKeySequence(QStringLiteral("Ctrl++")), QKeySequence(QStringLiteral("Ctrl+Shift++"))},
             Unknown});
        t.append(
            {"graph.zoomOut",
             ShortcutScope::Graph,
             QT_TR_NOOP("Zoom out"),
             {QKeySequence(QStringLiteral("Ctrl+-"))},
             Unknown});
        t.append(
            {"graph.zoomReset",
             ShortcutScope::Graph,
             QT_TR_NOOP("Reset zoom"),
             {QKeySequence(QStringLiteral("Ctrl+0"))},
             Unknown});
        t.append(
            {"graph.takeTrue",
             ShortcutScope::Graph,
             QT_TR_NOOP("Take true branch"),
             {QKeySequence(Qt::Key_T)},
             Unknown});
        t.append(
            {"graph.takeFalse",
             ShortcutScope::Graph,
             QT_TR_NOOP("Take false branch"),
             {QKeySequence(Qt::Key_F)},
             Unknown});
        t.append(
            {"graph.nextInstr",
             ShortcutScope::Graph,
             QT_TR_NOOP("Next instruction"),
             {QKeySequence(Qt::Key_J), QKeySequence(Qt::Key_Down)},
             Unknown});
        t.append(
            {"graph.prevInstr",
             ShortcutScope::Graph,
             QT_TR_NOOP("Previous instruction"),
             {QKeySequence(Qt::Key_K), QKeySequence(Qt::Key_Up)},
             Unknown});
        t.append(
            {"graph.seekPrevBlock",
             ShortcutScope::Graph,
             QT_TR_NOOP("Seek to previous block"),
             {QKeySequence(Qt::Key_U)},
             Unknown});
        t.append(
            {"graph.switchView",
             ShortcutScope::Graph,
             QT_TR_NOOP("Switch view mode"),
             {QKeySequence(Qt::Key_Space)},
             Unknown});
        t.append({"graph.copy", ShortcutScope::Graph, QT_TR_NOOP("Copy"), {}, QKeySequence::Copy});
        t.append(
            {"graph.seekPrev",
             ShortcutScope::Graph,
             QT_TR_NOOP("Seek to previous"),
             {QKeySequence(Qt::Key_Escape)},
             Unknown});

        t.append(
            {"graph.overviewZoomIn",
             ShortcutScope::GraphOverview,
             QT_TR_NOOP("Zoom in"),
             {QKeySequence(Qt::Key_Plus)},
             Unknown});
        t.append(
            {"graph.overviewZoomOut",
             ShortcutScope::GraphOverview,
             QT_TR_NOOP("Zoom out"),
             {QKeySequence(Qt::Key_Minus)},
             Unknown});
        t.append(
            {"graph.panUp",
             ShortcutScope::GraphOverview,
             QT_TR_NOOP("Pan up"),
             {QKeySequence(Qt::Key_Up)},
             Unknown});
        t.append(
            {"graph.panDown",
             ShortcutScope::GraphOverview,
             QT_TR_NOOP("Pan down"),
             {QKeySequence(Qt::Key_Down)},
             Unknown});
        t.append(
            {"graph.panLeft",
             ShortcutScope::GraphOverview,
             QT_TR_NOOP("Pan left"),
             {QKeySequence(Qt::Key_Left)},
             Unknown});
        t.append(
            {"graph.panRight",
             ShortcutScope::GraphOverview,
             QT_TR_NOOP("Pan right"),
             {QKeySequence(Qt::Key_Right)},
             Unknown});

        t.append(
            {"overview.left",
             ShortcutScope::Overview,
             QT_TR_NOOP("Move left"),
             {QKeySequence(Qt::Key_H), QKeySequence(Qt::Key_Left)},
             Unknown});
        t.append(
            {"overview.right",
             ShortcutScope::Overview,
             QT_TR_NOOP("Move right"),
             {QKeySequence(Qt::Key_L), QKeySequence(Qt::Key_Right)},
             Unknown});
        t.append(
            {"overview.up",
             ShortcutScope::Overview,
             QT_TR_NOOP("Move up"),
             {QKeySequence(Qt::Key_K), QKeySequence(Qt::Key_Up)},
             Unknown});
        t.append(
            {"overview.down",
             ShortcutScope::Overview,
             QT_TR_NOOP("Move down"),
             {QKeySequence(Qt::Key_J), QKeySequence(Qt::Key_Down)},
             Unknown});
        t.append(
            {"overview.leftFast",
             ShortcutScope::Overview,
             QT_TR_NOOP("Jump left"),
             {QKeySequence(Qt::SHIFT | Qt::Key_H)},
             Unknown});
        t.append(
            {"overview.rightFast",
             ShortcutScope::Overview,
             QT_TR_NOOP("Jump right"),
             {QKeySequence(Qt::SHIFT | Qt::Key_L)},
             Unknown});
        t.append(
            {"overview.upFast",
             ShortcutScope::Overview,
             QT_TR_NOOP("Jump up"),
             {QKeySequence(Qt::SHIFT | Qt::Key_K)},
             Unknown});
        t.append(
            {"overview.downFast",
             ShortcutScope::Overview,
             QT_TR_NOOP("Jump down"),
             {QKeySequence(Qt::SHIFT | Qt::Key_J)},
             Unknown});
        t.append(
            {"overview.select",
             ShortcutScope::Overview,
             QT_TR_NOOP("Select block"),
             {QKeySequence(Qt::Key_Space)},
             Unknown});
        t.append(
            {"overview.moreBlocks",
             ShortcutScope::Overview,
             QT_TR_NOOP("More blocks"),
             {QKeySequence(Qt::Key_Plus), QKeySequence(Qt::Key_Equal)},
             Unknown});
        t.append(
            {"overview.fewerBlocks",
             ShortcutScope::Overview,
             QT_TR_NOOP("Fewer blocks"),
             {QKeySequence(Qt::Key_Minus)},
             Unknown});
        t.append(
            {"overview.moreColumns",
             ShortcutScope::Overview,
             QT_TR_NOOP("More columns"),
             {QKeySequence(Qt::Key_BracketRight), QKeySequence(Qt::Key_ParenRight)},
             Unknown});
        t.append(
            {"overview.fewerColumns",
             ShortcutScope::Overview,
             QT_TR_NOOP("Fewer columns"),
             {QKeySequence(Qt::Key_BracketLeft), QKeySequence(Qt::Key_ParenLeft)},
             Unknown});

        t.append(
            {"debug.start",
             ShortcutScope::Debug,
             QT_TR_NOOP("Start debugging"),
             {QKeySequence(Qt::Key_F9)},
             Unknown});
        t.append(
            {"debug.continue",
             ShortcutScope::Debug,
             QT_TR_NOOP("Continue"),
             {QKeySequence(Qt::Key_F5)},
             Unknown});
        t.append(
            {"debug.stepInto",
             ShortcutScope::Debug,
             QT_TR_NOOP("Step into"),
             {QKeySequence(Qt::Key_F7)},
             Unknown});
        t.append(
            {"debug.stepOver",
             ShortcutScope::Debug,
             QT_TR_NOOP("Step over"),
             {QKeySequence(Qt::Key_F8)},
             Unknown});
        t.append(
            {"debug.stepOut",
             ShortcutScope::Debug,
             QT_TR_NOOP("Step out"),
             {QKeySequence(Qt::CTRL | Qt::Key_F8)},
             Unknown});
        t.append(
            {"breakpoint.delete",
             ShortcutScope::Debug,
             QT_TR_NOOP("Delete breakpoint"),
             {QKeySequence(Qt::Key_Delete)},
             Unknown});
        t.append(
            {"breakpoint.toggle",
             ShortcutScope::Debug,
             QT_TR_NOOP("Toggle breakpoint enabled"),
             {QKeySequence(Qt::Key_Space)},
             Unknown});
        return t;
    }();
    return c;
}

const QHash<QString, int> &catalogIndex()
{
    static const QHash<QString, int> idx = [] {
        QHash<QString, int> m;
        const auto &c = catalog();
        for (int i = 0; i < c.size(); ++i) {
            m.insert(QString::fromLatin1(c[i].id), i);
        }
        return m;
    }();
    return idx;
}

const ShortcutDescriptor *findDescriptor(const QString &id)
{
    const auto &idx = catalogIndex();
    auto it = idx.constFind(id);
    if (it == idx.constEnd()) {
        return nullptr;
    }
    return &catalog()[it.value()];
}

bool eventMatchesSequence(const QKeyEvent *event, const QKeySequence &seq)
{
    if (!event || seq.count() != 1) {
        return false;
    }
    const int combo = static_cast<int>(
                          event->modifiers() & ~(Qt::KeypadModifier | Qt::GroupSwitchModifier))
                      | event->key();
    return seq == QKeySequence(combo);
}

bool scopesCollide(ShortcutScope a, ShortcutScope b)
{
    return a == b;
}

QString commandConflictLabel(const QString &command)
{
    QString c = command.trimmed();
    if (c.length() > 40) {
        c = c.left(37) + QStringLiteral("...");
    }
    return QObject::tr("Custom \"%1\"").arg(c);
}

const char *kSettingsGroup = "Shortcuts";
const QString kDisabledSentinel = QStringLiteral("@@disabled@@");

} // namespace

ShortcutManager *ShortcutManager::instance()
{
    static ShortcutManager *s_instance = new ShortcutManager();
    return s_instance;
}

ShortcutManager::ShortcutManager(QObject *parent)
    : QObject(parent)
{
    load();
}

void ShortcutManager::load()
{
    m_overrides.clear();
    s.beginGroup(QString::fromLatin1(kSettingsGroup));
    const QStringList keys = s.childKeys();
    for (const QString &key : keys) {
        if (!catalogIndex().contains(key)) {
            continue;
        }
        QList<QKeySequence> seqs;
        const QStringList parts = s.value(key).toStringList();
        for (const QString &p : parts) {
            if (p == kDisabledSentinel) {
                continue;
            }
            QKeySequence seq = QKeySequence::fromString(p, QKeySequence::PortableText);
            if (!seq.isEmpty()) {
                seqs.append(seq);
            }
        }
        m_overrides.insert(key, seqs);
    }
    s.endGroup();

    m_commandShortcuts.clear();
    const int count = s.beginReadArray(QStringLiteral("CommandShortcuts"));
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        CommandShortcut cs;
        cs.key = QKeySequence::fromString(
            s.value(QStringLiteral("key")).toString(), QKeySequence::PortableText);
        cs.command = s.value(QStringLiteral("command")).toString();
        cs.needsInput = s.value(QStringLiteral("needsInput"), false).toBool();
        m_commandShortcuts.append(cs);
    }
    s.endArray();
}

QStringList ShortcutManager::ids() const
{
    QStringList out;
    const auto &c = catalog();
    out.reserve(c.size());
    for (const auto &d : c) {
        out.append(QString::fromLatin1(d.id));
    }
    return out;
}

QList<ShortcutScope> ShortcutManager::scopes()
{
    return {
        ShortcutScope::Global,
        ShortcutScope::Disassembly,
        ShortcutScope::Decompiler,
        ShortcutScope::Hexdump,
        ShortcutScope::Graph,
        ShortcutScope::GraphOverview,
        ShortcutScope::Console,
        ShortcutScope::AddressableList,
        ShortcutScope::Debug,
        ShortcutScope::Overview};
}

QStringList ShortcutManager::idsForScope(ShortcutScope sc) const
{
    QStringList out;
    for (const auto &d : catalog()) {
        if (d.scope == sc) {
            out.append(QString::fromLatin1(d.id));
        }
    }
    return out;
}

QString ShortcutManager::scopeName(ShortcutScope sc)
{
    switch (sc) {
    case ShortcutScope::Global:
        return tr("Global");
    case ShortcutScope::Disassembly:
        return tr("Disassembly");
    case ShortcutScope::Decompiler:
        return tr("Decompiler");
    case ShortcutScope::Hexdump:
        return tr("Hexdump");
    case ShortcutScope::Graph:
        return tr("Graph");
    case ShortcutScope::GraphOverview:
        return tr("Graph Overview");
    case ShortcutScope::Console:
        return tr("Console");
    case ShortcutScope::AddressableList:
        return tr("Lists");
    case ShortcutScope::Debug:
        return tr("Debug");
    case ShortcutScope::Overview:
        return tr("Overview");
    }
    return QString();
}

QString ShortcutManager::displayName(const QString &id) const
{
    const ShortcutDescriptor *d = findDescriptor(id);
    return d ? tr(d->displayName) : id;
}

ShortcutScope ShortcutManager::scope(const QString &id) const
{
    const ShortcutDescriptor *d = findDescriptor(id);
    return d ? d->scope : ShortcutScope::Global;
}

bool ShortcutManager::contains(const QString &id) const
{
    return catalogIndex().contains(id);
}

QList<QKeySequence> ShortcutManager::sequences(const QString &id) const
{
    auto it = m_overrides.constFind(id);
    if (it != m_overrides.constEnd()) {
        return it.value();
    }
    return defaults(id);
}

QKeySequence ShortcutManager::sequence(const QString &id) const
{
    const QList<QKeySequence> seqs = sequences(id);
    return seqs.isEmpty() ? QKeySequence() : seqs.first();
}

QList<QKeySequence> ShortcutManager::defaults(const QString &id) const
{
    const ShortcutDescriptor *d = findDescriptor(id);
    if (!d) {
        return {};
    }
    if (d->stdKey != QKeySequence::UnknownKey) {
        return QKeySequence::keyBindings(d->stdKey);
    }
    return d->defaults;
}

bool ShortcutManager::isOverridden(const QString &id) const
{
    return m_overrides.contains(id);
}

void ShortcutManager::setSequences(const QString &id, const QList<QKeySequence> &seqs)
{
    if (!contains(id)) {
        return;
    }
    if (seqs == defaults(id)) {
        resetToDefault(id);
        return;
    }
    m_overrides.insert(id, seqs);
    persist(id);
    applyTo(id);
    emit shortcutChanged(id);
}

void ShortcutManager::setSequence(const QString &id, const QKeySequence &seq)
{
    if (seq.isEmpty()) {
        setSequences(id, {});
    } else {
        setSequences(id, {seq});
    }
}

void ShortcutManager::resetToDefault(const QString &id)
{
    if (!m_overrides.contains(id)) {
        return;
    }
    m_overrides.remove(id);
    persist(id);
    applyTo(id);
    emit shortcutChanged(id);
}

void ShortcutManager::resetAll()
{
    m_overrides.clear();
    s.remove(QString::fromLatin1(kSettingsGroup));
    for (const auto &d : catalog()) {
        applyTo(QString::fromLatin1(d.id));
    }
    emit shortcutsReset();
}

QShortcut *ShortcutManager::registerShortcut(
    const QString &id, QWidget *parent, Qt::ShortcutContext context)
{
    QShortcut *sc = new QShortcut(sequence(id), parent);
    sc->setContext(context);
    m_bindings[id].append(Binding{sc, nullptr});
    return sc;
}

void ShortcutManager::bindAction(const QString &id, QAction *action)
{
    if (!action) {
        return;
    }
    action->setShortcuts(sequences(id));
    m_bindings[id].append(Binding{nullptr, action});
}

void ShortcutManager::applyTo(const QString &id)
{
    auto it = m_bindings.find(id);
    if (it == m_bindings.end()) {
        return;
    }
    const QList<QKeySequence> seqs = sequences(id);
    QVector<Binding> &vec = it.value();
    for (int i = vec.size() - 1; i >= 0; --i) {
        if (vec[i].shortcut) {
            vec[i].shortcut->setKey(seqs.isEmpty() ? QKeySequence() : seqs.first());
        } else if (vec[i].action) {
            vec[i].action->setShortcuts(seqs);
        } else {
            vec.remove(i);
        }
    }
}

void ShortcutManager::persist(const QString &id)
{
    s.beginGroup(QString::fromLatin1(kSettingsGroup));
    auto it = m_overrides.constFind(id);
    if (it != m_overrides.constEnd()) {
        QStringList out;
        for (const QKeySequence &seq : it.value()) {
            out.append(seq.toString(QKeySequence::PortableText));
        }
        if (out.isEmpty()) {
            out.append(kDisabledSentinel);
        }
        s.setValue(id, out);
    } else {
        s.remove(id);
    }
    s.endGroup();
}

bool ShortcutManager::matches(const QString &id, const QKeyEvent *event) const
{
    const ShortcutDescriptor *d = findDescriptor(id);
    if (!d || !event) {
        return false;
    }
    if (d->stdKey != QKeySequence::UnknownKey && !isOverridden(id)) {
        return const_cast<QKeyEvent *>(event)->matches(d->stdKey);
    }
    for (const QKeySequence &seq : sequences(id)) {
        if (eventMatchesSequence(event, seq)) {
            return true;
        }
    }
    return false;
}

QVector<CommandShortcut> ShortcutManager::commandShortcuts() const
{
    return m_commandShortcuts;
}

void ShortcutManager::setCommandShortcuts(const QVector<CommandShortcut> &list)
{
    m_commandShortcuts = list;
    s.remove(QStringLiteral("CommandShortcuts"));
    s.beginWriteArray(QStringLiteral("CommandShortcuts"), m_commandShortcuts.size());
    for (int i = 0; i < m_commandShortcuts.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue(
            QStringLiteral("key"), m_commandShortcuts[i].key.toString(QKeySequence::PortableText));
        s.setValue(QStringLiteral("command"), m_commandShortcuts[i].command);
        s.setValue(QStringLiteral("needsInput"), m_commandShortcuts[i].needsInput);
    }
    s.endArray();
    emit commandShortcutsChanged();
}

int ShortcutManager::matchCommandShortcut(const QKeyEvent *event) const
{
    for (int i = 0; i < m_commandShortcuts.size(); ++i) {
        const CommandShortcut &cs = m_commandShortcuts[i];
        if (!cs.command.trimmed().isEmpty() && eventMatchesSequence(event, cs.key)) {
            return i;
        }
    }
    return -1;
}

void ShortcutManager::migrateLegacyCommandKeys()
{
    QStringList imported = s.value(QStringLiteral("ImportedLegacyKeys")).toStringList();
    QVector<CommandShortcut> list = m_commandShortcuts;
    bool changed = false;
    for (int i = 1; i <= 12; ++i) {
        const QString cmd = Core()->getConfig(QStringLiteral("key.f%1").arg(i)).trimmed();
        if (cmd.isEmpty()) {
            continue;
        }
        const QString token = QStringLiteral("f%1=%2").arg(i).arg(cmd);
        if (imported.contains(token)) {
            continue;
        }
        imported.append(token);
        CommandShortcut cs;
        cs.key = QKeySequence(Qt::Key_F1 + (i - 1));
        cs.command = cmd;
        cs.needsInput = false;
        list.append(cs);
        changed = true;
    }
    if (changed) {
        s.setValue(QStringLiteral("ImportedLegacyKeys"), imported);
        setCommandShortcuts(list);
    }
}

QList<ShortcutConflict> ShortcutManager::conflicts() const
{
    QHash<QString, QStringList> bySeq;
    for (const auto &d : catalog()) {
        const QString id = QString::fromLatin1(d.id);
        for (const QKeySequence &seq : sequences(id)) {
            if (seq.isEmpty()) {
                continue;
            }
            bySeq[seq.toString(QKeySequence::PortableText)].append(id);
        }
    }

    QList<ShortcutConflict> out;
    for (auto it = bySeq.constBegin(); it != bySeq.constEnd(); ++it) {
        const QStringList &group = it.value();
        if (group.size() < 2) {
            continue;
        }
        for (int i = 0; i < group.size(); ++i) {
            for (int j = i + 1; j < group.size(); ++j) {
                if (scopesCollide(scope(group[i]), scope(group[j]))) {
                    out.append(
                        {group[i],
                         group[j],
                         QKeySequence::fromString(it.key(), QKeySequence::PortableText)});
                }
            }
        }
    }

    for (int i = 0; i < m_commandShortcuts.size(); ++i) {
        const CommandShortcut &ci = m_commandShortcuts[i];
        if (ci.key.isEmpty() || ci.command.trimmed().isEmpty()) {
            continue;
        }
        const QString pk = ci.key.toString(QKeySequence::PortableText);
        const QString labelI = commandConflictLabel(ci.command);
        for (int j = i + 1; j < m_commandShortcuts.size(); ++j) {
            const CommandShortcut &cj = m_commandShortcuts[j];
            if (cj.key.isEmpty() || cj.command.trimmed().isEmpty()) {
                continue;
            }
            if (cj.key.toString(QKeySequence::PortableText) == pk) {
                out.append({labelI, commandConflictLabel(cj.command), ci.key});
            }
        }
        QStringList seenBuiltins;
        for (const QString &bid : bySeq.value(pk)) {
            if (seenBuiltins.contains(bid)) {
                continue;
            }
            seenBuiltins.append(bid);
            out.append({labelI, bid, ci.key});
        }
    }
    return out;
}
