/** \file IaitoDescriptions.h
 * This file contains every structure description that are used in widgets.
 * The descriptions are used for the Qt metatypes.
 */
#ifndef DESCRIPTIONS_H
#define DESCRIPTIONS_H

#include "core/IaitoCommon.h"
#include <QColor>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

struct FunctionDescription
{
    RVA offset = 0;
    RVA linearSize = 0;
    RVA realSize = 0;
    RVA nargs = 0;
    RVA nbbs = 0;
    RVA nlocals = 0;
    QString calltype;
    QString name;
    RVA edges = 0;
    RVA stackframe = 0;

    bool contains(RAnal *anal, RVA addr) const
    {
        return r_anal_get_functions_in(anal, addr) != nullptr;
        // TODO: this is not exactly correct in edge cases.
        // r_anal_function_contains() does it right.
        // return addr >= offset && addr < offset + linearSize;
    }
};

struct ImportDescription
{
    RVA plt = 0;
    int ordinal = 0;
    QString bind;
    QString type;
    QString name;
    QString libname;
};

struct ExportDescription
{
    RVA vaddr = 0;
    RVA paddr = 0;
    RVA size = 0;
    QString type;
    QString name;
    QString flag_name;
};

struct HeaderDescription
{
    RVA vaddr = 0;
    RVA paddr = 0;
    QString value;
    QString name;
};

struct ZignatureDescription
{
    QString name;
    QString bytes;
    RVA cc = 0;
    RVA nbbs = 0;
    RVA edges = 0;
    RVA ebbs = 0;
    RVA offset = 0;
    QStringList refs;
};

struct TypeDescription
{
    QString type;
    int size = 0;
    QString format;
    QString category;
};

struct SearchDescription
{
    RVA offset = 0;
    int size = 0;
    QString code;
    QString data;
};

struct SymbolDescription
{
    RVA vaddr = 0;
    QString bind;
    QString type;
    QString name;
};

struct CommentDescription
{
    RVA offset = 0;
    QString name;
};

struct RelocDescription
{
    RVA vaddr = 0;
    RVA paddr = 0;
    QString type;
    QString name;
};

struct StringDescription
{
    RVA vaddr = 0;
    QString string;
    QString type;
    QString section;
    ut32 length = 0;
    ut32 size = 0;
};

struct FlagspaceDescription
{
    QString name;
};

struct FlagDescription
{
    RVA offset = 0;
    RVA size = 0;
    QString name;
    QString realname;
};

struct SectionDescription
{
    RVA vaddr = 0;
    RVA paddr = 0;
    RVA size = 0;
    RVA vsize = 0;
    QString name;
    QString perm;
    QString entropy;
};

struct SegmentDescription
{
    RVA vaddr = 0;
    RVA paddr = 0;
    RVA size = 0;
    RVA vsize = 0;
    QString name;
    QString perm;
};

struct EntrypointDescription
{
    RVA vaddr = 0;
    RVA paddr = 0;
    RVA baddr = 0;
    RVA laddr = 0;
    RVA haddr = 0;
    QString type;
};

struct XrefDescription
{
    RVA from = 0;
    QString from_str;
    RVA to = 0;
    QString to_str;
    QString type;
};

struct RBinPluginDescription
{
    QString name;
    QString description;
    QString license;
    QString type;
};

struct RIOPluginDescription
{
    QString name;
    QString description;
    QString license;
    QString permissions;
    QList<QString> uris;
};

struct RCorePluginDescription
{
    QString name;
    QString license;
    QString author;
    QString description;
};

struct RAsmPluginDescription
{
    QString name;
    QString architecture;
    QString author;
    QString version;
    QString cpus;
    QString description;
    QString license;
};

struct RLangPluginDescription
{
    QString name;
    QString description;
    QString license;
    QString alias;
    QString ext;
};

struct RFSPluginDescription
{
    QString name;
    QString description;
    QString license;
    QString author;
};

struct RDebugPluginDescription
{
    QString name;
    QString description;
    QString license;
    QString author;
    QString arch;
};

struct RMutaPluginDescription
{
    QString name;
    QString description;
    QString license;
    QString author;
    QString type;
};

struct DisassemblyLine
{
    RVA offset = 0;
    QString text;
    RVA arrow = 0;
};

struct BinClassBaseClassDescription
{
    QString name;
    RVA offset = 0;
};

struct BinClassMethodDescription
{
    QString name;
    RVA addr = RVA_INVALID;
    st64 vtableOffset = -1;
};

struct BinClassFieldDescription
{
    QString name;
    RVA addr = RVA_INVALID;
};

struct BinClassDescription
{
    QString name;
    RVA addr = RVA_INVALID;
    RVA vtableAddr = RVA_INVALID;
    ut64 index = 0;
    QList<BinClassBaseClassDescription> baseClasses;
    QList<BinClassMethodDescription> methods;
    QList<BinClassFieldDescription> fields;
};

struct AnalMethodDescription
{
    QString name;
    RVA addr = 0;
    st64 vtableOffset = 0;
};

struct AnalBaseClassDescription
{
    QString id;
    RVA offset = 0;
    QString className;
};

struct AnalVTableDescription
{
    QString id;
    ut64 offset = 0;
    ut64 addr = 0;
};

struct ResourcesDescription
{
    QString name;
    RVA vaddr = 0;
    ut64 index = 0;
    QString type;
    ut64 size = 0;
    QString lang;
};

struct VTableDescription
{
    RVA addr = 0;
    QList<BinClassMethodDescription> methods;
};

struct BlockDescription
{
    RVA addr = 0;
    RVA size = 0;
    int flags = 0;
    int functions = 0;
    int inFunctions = 0;
    int comments = 0;
    int symbols = 0;
    int strings = 0;
    int blocks = 0;
    ut8 rwx = 0;
    QString color;
};

struct BlockStatistics
{
    RVA from = 0;
    RVA to = 0;
    RVA blocksize = 0;
    QList<BlockDescription> blocks;
};

struct MemoryMapDescription
{
    RVA addrStart = 0;
    RVA addrEnd = 0;
    QString name;
    QString fileName;
    QString type;
    QString permission;
};

struct BreakpointDescription
{
    enum PositionType {
        Address,
        Named,
        Module,
    };

    RVA addr = 0;
    int64_t moduleDelta = 0;
    int index = -1;
    PositionType type = Address;
    int size = 0;
    int permission = 0;
    QString positionExpression;
    QString name;
    QString command;
    QString condition;
    bool hw = false;
    bool trace = false;
    bool enabled = true;
};

struct ProcessDescription
{
    int pid = 0;
    int uid = 0;
    QString status;
    QString path;
};

struct RefDescription
{
    QString ref;
    QColor refColor;
};

struct VariableDescription
{
    enum class RefType { SP, BP, Reg };
    RefType refType = RefType::SP;
    QString name;
    QString type;
};

struct RegisterRefValueDescription
{
    QString name;
    QString value;
    QString ref;
};

Q_DECLARE_METATYPE(FunctionDescription)
Q_DECLARE_METATYPE(ImportDescription)
Q_DECLARE_METATYPE(ExportDescription)
Q_DECLARE_METATYPE(SymbolDescription)
Q_DECLARE_METATYPE(CommentDescription)
Q_DECLARE_METATYPE(RelocDescription)
Q_DECLARE_METATYPE(StringDescription)
Q_DECLARE_METATYPE(FlagspaceDescription)
Q_DECLARE_METATYPE(FlagDescription)
Q_DECLARE_METATYPE(XrefDescription)
Q_DECLARE_METATYPE(EntrypointDescription)
Q_DECLARE_METATYPE(RBinPluginDescription)
Q_DECLARE_METATYPE(RIOPluginDescription)
Q_DECLARE_METATYPE(RCorePluginDescription)
Q_DECLARE_METATYPE(RAsmPluginDescription)
Q_DECLARE_METATYPE(BinClassMethodDescription)
Q_DECLARE_METATYPE(BinClassFieldDescription)
Q_DECLARE_METATYPE(BinClassDescription)
Q_DECLARE_METATYPE(const BinClassDescription *)
Q_DECLARE_METATYPE(const BinClassMethodDescription *)
Q_DECLARE_METATYPE(const BinClassFieldDescription *)
Q_DECLARE_METATYPE(AnalBaseClassDescription)
Q_DECLARE_METATYPE(AnalMethodDescription)
Q_DECLARE_METATYPE(AnalVTableDescription)
Q_DECLARE_METATYPE(ResourcesDescription)
Q_DECLARE_METATYPE(VTableDescription)
Q_DECLARE_METATYPE(TypeDescription)
Q_DECLARE_METATYPE(HeaderDescription)
Q_DECLARE_METATYPE(ZignatureDescription)
Q_DECLARE_METATYPE(SearchDescription)
Q_DECLARE_METATYPE(SectionDescription)
Q_DECLARE_METATYPE(SegmentDescription)
Q_DECLARE_METATYPE(MemoryMapDescription)
Q_DECLARE_METATYPE(BreakpointDescription)
Q_DECLARE_METATYPE(BreakpointDescription::PositionType)
Q_DECLARE_METATYPE(ProcessDescription)
Q_DECLARE_METATYPE(RefDescription)
Q_DECLARE_METATYPE(VariableDescription)

#endif // DESCRIPTIONS_H
