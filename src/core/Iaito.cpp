#include "GuiCorePlugin.cpp"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QVector>

#include <cassert>
#include <memory>

#include "Decompiler.h"
#include "common/AsyncTask.h"
#include "common/BasicInstructionHighlighter.h"
#include "common/Configuration.h"
#include "common/Json.h"
#include "common/R2Shims.h"
#include "common/R2Task.h"
#include "common/TempConfig.h"
#include "core/Iaito.h"
#include "plugins/PluginManager.h"

#include <r_asm.h>
#include <r_cmd.h>
#include <r_core.h>

#if R2_VERSION_NUMBER >= 50809 // reverse compatability
#define BO bo
#else
#define BO o
#endif

Q_GLOBAL_STATIC(IaitoCore, uniqueInstance)

#define R_JSON_KEY(name) static const QString name = QStringLiteral(#name)
#define CORE_LOCK() RCoreLocked core(this)

namespace RJsonKey {
R_JSON_KEY(addr);
R_JSON_KEY(addrs);
R_JSON_KEY(addr_end);
R_JSON_KEY(arrow);
R_JSON_KEY(baddr);
R_JSON_KEY(bind);
R_JSON_KEY(blocks);
R_JSON_KEY(blocksize);
R_JSON_KEY(bytes);
R_JSON_KEY(calltype);
R_JSON_KEY(cc);
R_JSON_KEY(classname);
R_JSON_KEY(code);
R_JSON_KEY(comment);
R_JSON_KEY(comments);
R_JSON_KEY(cost);
R_JSON_KEY(data);
R_JSON_KEY(description);
R_JSON_KEY(ebbs);
R_JSON_KEY(edges);
R_JSON_KEY(enabled);
R_JSON_KEY(entropy);
R_JSON_KEY(fcn_addr);
R_JSON_KEY(fcn_name);
R_JSON_KEY(fields);
R_JSON_KEY(file);
R_JSON_KEY(flags);
R_JSON_KEY(flagname);
R_JSON_KEY(format);
R_JSON_KEY(from);
R_JSON_KEY(functions);
R_JSON_KEY(graph);
R_JSON_KEY(haddr);
R_JSON_KEY(hw);
R_JSON_KEY(in_functions);
R_JSON_KEY(index);
R_JSON_KEY(jump);
R_JSON_KEY(laddr);
R_JSON_KEY(lang);
R_JSON_KEY(len);
R_JSON_KEY(length);
R_JSON_KEY(license);
R_JSON_KEY(methods);
R_JSON_KEY(name);
R_JSON_KEY(realname);
R_JSON_KEY(nargs);
R_JSON_KEY(nbbs);
R_JSON_KEY(nlocals);
R_JSON_KEY(offset);
R_JSON_KEY(opcode);
R_JSON_KEY(opcodes);
R_JSON_KEY(ordinal);
R_JSON_KEY(libname);
R_JSON_KEY(outdegree);
R_JSON_KEY(paddr);
R_JSON_KEY(path);
R_JSON_KEY(perm);
R_JSON_KEY(pid);
R_JSON_KEY(plt);
R_JSON_KEY(prot);
R_JSON_KEY(ref);
R_JSON_KEY(refs);
R_JSON_KEY(reg);
R_JSON_KEY(rwx);
R_JSON_KEY(section);
R_JSON_KEY(sections);
R_JSON_KEY(size);
R_JSON_KEY(stackframe);
R_JSON_KEY(status);
R_JSON_KEY(string);
R_JSON_KEY(strings);
R_JSON_KEY(symbols);
R_JSON_KEY(text);
R_JSON_KEY(to);
R_JSON_KEY(trace);
R_JSON_KEY(type);
R_JSON_KEY(uid);
R_JSON_KEY(vaddr);
R_JSON_KEY(value);
R_JSON_KEY(vsize);
} // namespace RJsonKey

#undef R_JSON_KEY

static void updateOwnedCharPtr(char *&variable, const QString &newValue)
{
    auto data = newValue.toUtf8();
    r_mem_free(variable);
    variable = strdup(data.data());
}

static QString fromOwnedCharPtr(char *str)
{
    QString result(str ? str : "");
    r_mem_free(str);
    return result;
}

RCoreLocked::RCoreLocked(IaitoCore *core)
    : core(core)
{
    core->coreMutex.lock();
#if R2_VERSION_NUMBER < 50609
    assert(core->coreLockDepth >= 0);
    core->coreLockDepth++;
    if (core->coreLockDepth == 1) {
        if (core->coreBed) {
            r_cons_sleep_end(core->coreBed);
            core->coreBed = nullptr;
        }
    }
#endif
}

RCoreLocked::~RCoreLocked()
{
    core->coreLockDepth--;
#if R2_VERSION_NUMBER < 50609
    assert(core->coreLockDepth >= 0);
    if (core->coreLockDepth == 0) {
        core->coreBed = r_cons_sleep_begin();
    }
#endif
    core->coreMutex.unlock();
}

RCoreLocked::operator RCore *() const
{
    return core->core_;
}

RCore *RCoreLocked::operator->() const
{
    return core->core_;
}

static void cutterREventCallback(REvent *, int type, void *user, void *data)
{
    auto core = reinterpret_cast<IaitoCore *>(user);
    core->handleREvent(type, data);
}

IaitoCore::IaitoCore(QObject *parent)
    : QObject(parent)
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
    , coreMutex(QMutex::Recursive)
#else
    , coreMutex()
#endif
{}

IaitoCore *IaitoCore::instance()
{
    return uniqueInstance;
}

void IaitoCore::initialize(bool loadPlugins)
{
    RCore *kore = iaitoPluginCore();
    if (kore != nullptr) {
        core_ = kore;
    } else {
        core_ = r_core_new();
    }
#if R2_VERSION_NUMBER < 50609
    r_core_task_sync_begin(&core_->tasks);
    coreBed = r_cons_sleep_begin();
#endif
    CORE_LOCK();
    setConfig("dbg.wrap", true);

    r_event_hook(core_->anal->ev, R_EVENT_ALL, cutterREventCallback, this);
#if 0
#ifdef APPIMAGE
	auto prefix = QDir(QCoreApplication::applicationDirPath());
	// Executable is in appdir/bin
	prefix.cdUp();
	qInfo() << "Setting r2 prefix =" << prefix.absolutePath() << " for AppImage.";
	setConfig("dir.prefix", prefix.absolutePath());

	auto pluginsDir = prefix;
	if (pluginsDir.cd("share/radare2/plugins")) {
		qInfo() << "Setting r2 plugins dir =" << pluginsDir.absolutePath();
		setConfig("dir.plugins", pluginsDir.absolutePath());
	} else {
		qInfo() << "r2 plugins dir =" << pluginsDir.absolutePath() << "does not exist!";
	}
#endif
#endif

    if (loadPlugins) {
        QString pluginsDir = QDir(Plugins()->getUserPluginsDirectory()).absolutePath();
        auto iaitoPluginsDirectory = pluginsDir.toStdString();
        r_lib_opendir (core->lib, iaitoPluginsDirectory.c_str());
    } else {
        setConfig("cfg.plugins", false);
    }
    if (getConfigi("cfg.plugins")) {
        r_core_loadlibs(this->core_, R_CORE_LOADLIBS_ALL, nullptr);
    }
    r_lib_open_ptr(this->core_->lib, "uiaito", this, &uiaito_radare_plugin);
    // IMPLICIT r_bin_iobind (core_->bin, core_->io);

    // Otherwise r2 may ask the user for input and Iaito would freeze
    setConfig("scr.interactive", false);

    // Initialize graph node highlighter
    bbHighlighter = new BasicBlockHighlighter();

    // Initialize Async tasks manager
    asyncTaskManager = new AsyncTaskManager(this);
}

IaitoCore::~IaitoCore()
{
#if R2_VERSION_NUMBER < 50609
    r_cons_sleep_end(coreBed);
    r_core_task_sync_end(&core_->tasks);
#endif
    RCore *kore = iaitoPluginCore();
    if (kore != nullptr) {
        // leave qt
        QCoreApplication::exit();
    } else {
        // 	r_core_free (core_);
        r_cons_free();
    }
    delete bbHighlighter;
}

RCoreLocked IaitoCore::core()
{
    return RCoreLocked(this);
}

QDir IaitoCore::getIaitoRCDefaultDirectory() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}

QVector<QString> IaitoCore::getIaitoRCFilePaths(int n) const
{
    QVector<QString> result;
    auto filename = (n == 0) ? ".iaitorc" : ".iaitorc2";
    result.push_back(QFileInfo(QDir::home(), filename).absoluteFilePath());
    QStringList locations = QStandardPaths::standardLocations(QStandardPaths::AppConfigLocation);
    for (auto &location : locations) {
        result.push_back(QFileInfo(QDir(location), filename).absoluteFilePath());
    }
    // File in config editor is from this path
    result.push_back(QFileInfo(getIaitoRCDefaultDirectory(), "rc").absoluteFilePath());
    return result;
}

void IaitoCore::loadIaitoRC(int n)
{
    CORE_LOCK();
    const auto result = getIaitoRCFilePaths(n);
    for (auto &cutterRCFilePath : result) {
        auto cutterRCFileInfo = QFileInfo(cutterRCFilePath);
        if (!cutterRCFileInfo.exists() || !cutterRCFileInfo.isFile()) {
            continue;
        }
        qInfo() << "Loading " << n << " file from " << cutterRCFilePath;
        r_core_cmd_file(core, cutterRCFilePath.toUtf8().constData());
    }
}

void IaitoCore::loadDefaultIaitoRC()
{
    CORE_LOCK();
    auto cutterRCFilePath = QFileInfo(getIaitoRCDefaultDirectory(), "rc").absoluteFilePath();
    const auto cutterRCFileInfo = QFileInfo(cutterRCFilePath);
    if (!cutterRCFileInfo.exists() || !cutterRCFileInfo.isFile()) {
        return;
    }
    qInfo() << "Loading initialization script from " << cutterRCFilePath;
    r_core_cmd_file(core, cutterRCFilePath.toUtf8().constData());
}

void IaitoCore::loadSecondaryIaitoRC()
{
    CORE_LOCK();
    auto cutterRCFilePath = QFileInfo(getIaitoRCDefaultDirectory(), "rc2").absoluteFilePath();
    const auto cutterRCFileInfo = QFileInfo(cutterRCFilePath);
    if (!cutterRCFileInfo.exists() || !cutterRCFileInfo.isFile()) {
        return;
    }
    qInfo() << "Loading secondary script from " << cutterRCFilePath;
    r_core_cmd_file(core, cutterRCFilePath.toUtf8().constData());
}

QList<QString> IaitoCore::sdbList(QString path)
{
    CORE_LOCK();
    QList<QString> list = QList<QString>();
    Sdb *root = sdb_ns_path(core->sdb, path.toUtf8().constData(), 0);
    if (root) {
        void *vsi;
        ls_iter_t *iter;
        ls_foreach(root->ns, iter, vsi)
        {
            SdbNs *nsi = (SdbNs *) vsi;
            list << nsi->name;
        }
    }
    return list;
}

using SdbListPtr = std::unique_ptr<SdbList, decltype(&ls_free)>;
static SdbListPtr makeSdbListPtr(SdbList *list)
{
    return {list, ls_free};
}

QList<QString> IaitoCore::sdbListKeys(QString path)
{
    CORE_LOCK();
    QList<QString> list = QList<QString>();
    Sdb *root = sdb_ns_path(core->sdb, path.toUtf8().constData(), 0);
    if (root) {
        void *vsi;
        ls_iter_t *iter;
        SdbListPtr l = makeSdbListPtr(sdb_foreach_list(root, false));
        ls_foreach(l, iter, vsi)
        {
            SdbKv *nsi = (SdbKv *) vsi;
            list << reinterpret_cast<char *>(nsi->base.key);
        }
    }
    return list;
}

QString IaitoCore::sdbGet(QString path, QString key)
{
    CORE_LOCK();
    Sdb *db = sdb_ns_path(core->sdb, path.toUtf8().constData(), 0);
    if (db) {
        const char *val = sdb_const_get(db, key.toUtf8().constData(), 0);
        if (val && *val)
            return val;
    }
    return QString();
}

bool IaitoCore::sdbSet(QString path, QString key, QString val)
{
    CORE_LOCK();
    Sdb *db = sdb_ns_path(core->sdb, path.toUtf8().constData(), 1);
    return db && sdb_set(db, key.toUtf8().constData(), val.toUtf8().constData(), 0);
}

QString IaitoCore::sanitizeStringForCommand(QString s)
{
    static const QRegularExpression regexp(";|@`");
    return s.replace(regexp, QStringLiteral("_"));
}

QString IaitoCore::cmdHtml(const char *str)
{
    CORE_LOCK();

    RVA offset = core->offset;
    r_core_cmd0(core, "e scr.html=true;e scr.color=2");
    char *res = r_core_cmd_str(core, str);
    r_core_cmd0(core, "e scr.html=false;e scr.color=0");
    QString o = fromOwnedCharPtr(res);

    if (offset != core->offset) {
        updateSeek();
    }
    return o;
}

QString IaitoCore::cmd(const char *str)
{
    CORE_LOCK();

    RVA offset = core->offset;
    char *res = r_core_cmd_str(core, str);
    QString o = fromOwnedCharPtr(res);

    if (offset != core->offset) {
        updateSeek();
    }
    return o;
}

bool IaitoCore::isRedirectableDebugee()
{
    if (!currentlyDebugging || currentlyAttachedToPID != -1) {
        return false;
    }

    // We are only able to redirect locally debugged unix processes
    QJsonArray openFilesArray = cmdj("oj").array();
    for (QJsonValue value : openFilesArray) {
        QJsonObject openFile = value.toObject();
        QString URI = openFile["uri"].toString();
        if (URI.contains("ptrace") || URI.contains("mach")) {
            return true;
        }
    }
    return false;
}

bool IaitoCore::isDebugTaskInProgress()
{
    if (!debugTask.isNull()) {
        return true;
    }

    return false;
}

bool IaitoCore::asyncCmdEsil(const char *command, QSharedPointer<R2Task> &task)
{
    asyncCmd(command, task);

    if (task.isNull()) {
        return false;
    }

    connect(task.data(), &R2Task::finished, task.data(), [this, task]() {
        QString res = task.data()->getResult();

        if (res.contains(QStringLiteral("[ESIL] Stopped execution in an invalid instruction"))) {
            msgBox.showMessage("Stopped when attempted to run an invalid instruction. You can "
                               "disable this in Preferences");
        }
    });

    return true;
}

bool IaitoCore::asyncCmd(const char *str, QSharedPointer<R2Task> &task)
{
    if (!task.isNull()) {
        return false;
    }

    CORE_LOCK();

    RVA offset = core->offset;

    task = QSharedPointer<R2Task>(new R2Task(str, true));
    connect(task.data(), &R2Task::finished, task.data(), [this, offset, task]() {
        CORE_LOCK();

        if (offset != core->offset) {
            updateSeek();
        }
    });

    return true;
}

QString IaitoCore::cmdRawAt(const char *cmd, RVA address)
{
    RVA oldOffset = getOffset();
    seekSilent(address);
    QString res = cmdRaw(cmd);
    seekSilent(oldOffset);
    return res;
}

bool IaitoCore::cmdRaw0(const QString &s)
{
    (void) r_core_cmd0(core_, s.toStdString().c_str());
    return core_->rc == 0;
}

QString IaitoCore::cmdRaw(const char *rcmd)
{
    QString res;
#if 1
    res = cmd(rcmd);
#else
    CORE_LOCK();
    r_cons_push();
    // r_cmd_call does not return the output of the command
    r_cmd_call(core->rcmd, cmd);

    // we grab the output straight from r_cons
    res = r_cons_get_buffer();

    // cleaning up
    r_cons_pop();
    r_cons_echo(NULL);
#endif
    return res;
}

QJsonDocument IaitoCore::cmdj(const char *str)
{
    CORE_LOCK();
    char *res = r_core_cmd_str(core, str);
    QJsonDocument doc = parseJson(res, str);
    r_mem_free(res);
    return doc;
}

QJsonDocument IaitoCore::cmdjAt(const char *str, RVA address)
{
    RVA oldOffset = getOffset();
    seekSilent(address);
    QJsonDocument res = cmdj(str);
    seekSilent(oldOffset);
    return res;
}

QString IaitoCore::cmdTask(const QString &str)
{
    R2Task task(str);
    task.startTask();
    task.joinTask();
    return task.getResult();
}

QJsonDocument IaitoCore::cmdjTask(const QString &str)
{
#if MONOTHREAD
    return cmdj(str);
#else
    R2Task task(str);
    task.startTask();
    task.joinTask();
    return parseJson(task.getResultRaw(), str);
#endif
}

QJsonDocument IaitoCore::parseJson(const char *res, const char *cmd)
{
    QByteArray json(res);

    if (json.isEmpty()) {
        return QJsonDocument();
    }

    QJsonParseError jsonError;
    QJsonDocument doc = QJsonDocument::fromJson(json, &jsonError);

    if (jsonError.error != QJsonParseError::NoError) {
        if (cmd) {
            R_LOG_ERROR(
                "Failed to parse JSON for command \"%s\": %s",
                cmd,
                jsonError.errorString().toLocal8Bit().constData());
        } else {
            R_LOG_ERROR("Failed to parse JSON: %s", jsonError.errorString().toLocal8Bit().constData());
        }
        const int MAX_JSON_DUMP_SIZE = 8 * 1024;
        if (json.length() > MAX_JSON_DUMP_SIZE) {
            int originalSize = json.length();
            json.resize(MAX_JSON_DUMP_SIZE);
            R_LOG_INFO("%d bytes total: %s", originalSize, json.constData());
        } else {
            R_LOG_INFO("%s", json.constData());
        }
    }

    return doc;
}

QStringList IaitoCore::autocomplete(const QString &cmd, RLinePromptType promptType, size_t limit)
{
    RLineBuffer buf;
    int c = snprintf(buf.data, sizeof(buf.data), "%s", cmd.toUtf8().constData());
    if (c < 0) {
        return {};
    }
    buf.index = buf.length = std::min((int) (sizeof(buf.data) - 1), c);

    RLineCompletion completion;
    r_line_completion_init(&completion, limit);
    r_core_autocomplete(core(), &completion, &buf, promptType);

    QStringList r;
#if R2_VERSION_NUMBER >= 50709
    int amount = r_pvector_length(&completion.args);
#else
    int amount = r_pvector_len(&completion.args);
#endif
    r.reserve(amount);
    for (int i = 0; i < amount; i++) {
        r.push_back(
            QString::fromUtf8(reinterpret_cast<const char *>(r_pvector_at(&completion.args, i))));
    }

    r_line_completion_fini(&completion);
    return r;
}

/**
 * @brief IaitoCore::loadFile
 * Load initial file. TODO Maybe use the "o" commands?
 * @param path File path
 * @param baddr Base (RBin) address
 * @param mapaddr Map address
 * @param perms
 * @param va
 * @param loadbin Load RBin information
 * @param forceBinPlugin
 * @return
 */
bool IaitoCore::loadFile(
    QString path,
    ut64 baddr,
    ut64 mapaddr,
    int perms,
    int va,
    bool bincache,
    bool loadbin,
    const QString &forceBinPlugin)
{
    CORE_LOCK();
    r_config_set_i(core->config, "io.va", va);
    r_config_set_b(core->config, "bin.cache", bincache);

    Core()->loadIaitoRC(0);
    RIODesc *f = r_core_file_open(core, path.toUtf8().constData(), perms, mapaddr);
    if (!f) {
        R_LOG_ERROR("r_core_file_open failed");
        return false;
    }

    if (!forceBinPlugin.isNull()) {
        r_bin_force_plugin(r_core_get_bin(core), forceBinPlugin.toUtf8().constData());
    }

    if (loadbin && va) {
        if (!r_core_bin_load(core, path.toUtf8().constData(), baddr)) {
            R_LOG_ERROR("Cannot find rbin information");
        }

#if HAVE_MULTIPLE_RBIN_FILES_INSIDE_SELECT_WHICH_ONE
        if (!r_core_file_open(core, path.toUtf8(), R_IO_READ | (rw ? R_IO_WRITE : 0, mapaddr))) {
            R_LOG_ERROR("Cannot open file");
        } else {
            // load RBin information
            // XXX only for sub-bins
            r_core_bin_load(core, path.toUtf8(), baddr);
            r_bin_select_idx(core->bin, NULL, idx);
        }
#endif
    } else {
        // Not loading RBin info coz va = false
    }
    r_core_bin_export_info(core, R_MODE_SET);

    /*
        auto iod = core->io ? core->io->desc : NULL;
        auto debug = core->file && iod && (core->file->fd == iod->fd) &&
       iod->plugin && \ iod->plugin->isdbg;
    */
    auto debug = r_config_get_b(core->config, "cfg.debug");

    if (!debug && r_flag_get(core->flags, "entry0")) {
        r_core_cmd0(core, "s entry0");
    }

    if (perms & R_PERM_W) {
        r_core_cmd0(core, "omfg+w");
    }
    // run script
    // Core()->loadIaitoRC(1);
    Core()->loadSecondaryIaitoRC();

    fflush(stdout);
    return true;
}

bool IaitoCore::tryFile(QString path, bool rw)
{
    if (path == "") {
        return false;
    }
    CORE_LOCK();
    int flags = rw ? R_PERM_RW : R_PERM_R;
    RIODesc *cf = r_core_file_open(core, path.toUtf8().constData(), flags, 0LL);
    if (!cf) {
        return false;
    }

    r_core_cmdf(core, "o-%d", cf->fd);

    return true;
}

/**
 * @brief Maps a file using r2 API
 * @param path Path to file
 * @param mapaddr Map Address
 * @return bool
 */
bool IaitoCore::mapFile(QString path, RVA mapaddr)
{
    CORE_LOCK();
    RVA addr = mapaddr != RVA_INVALID ? mapaddr : 0;
    ut64 baddr = Core()->getFileInfo().object()["bin"].toObject()["baddr"].toVariant().toULongLong();
    if (r_core_file_open(core, path.toUtf8().constData(), R_PERM_RX, addr)) {
        r_core_bin_load(core, path.toUtf8().constData(), baddr);
    } else {
        return false;
    }
    return true;
}

void IaitoCore::renameFunction(const RVA offset, const QString &newName)
{
    cmdRaw("afn " + newName + " " + RAddressString(offset));
    emit functionRenamed(offset, newName);
}

void IaitoCore::delFunction(RVA addr)
{
    cmdRaw("af- " + RAddressString(addr));
    emit functionsChanged();
}

void IaitoCore::renameFlag(QString old_name, QString new_name)
{
    cmdRaw("fr " + old_name + " " + new_name);
    emit flagsChanged();
}

void IaitoCore::renameFunctionVariable(QString newName, QString oldName, RVA functionAddress)
{
    CORE_LOCK();
    RAnalFunction *function = r_anal_get_function_at(core->anal, functionAddress);
    RAnalVar *variable = r_anal_function_get_var_byname(function, oldName.toUtf8().constData());
    if (variable) {
        r_anal_var_rename(variable, newName.toUtf8().constData(), true);
    }
    emit refreshCodeViews();
}

void IaitoCore::delFlag(RVA addr)
{
    cmdRawAt("f-", addr);
    emit flagsChanged();
}

void IaitoCore::delFlag(const QString &name)
{
    cmdRaw("f-" + name);
    emit flagsChanged();
}

QString IaitoCore::getInstructionBytes(RVA addr)
{
    return cmdj("aoj @ " + RAddressString(addr))
        .array()
        .first()
        .toObject()[RJsonKey::bytes]
        .toString();
}

QString IaitoCore::getInstructionOpcode(RVA addr)
{
    return cmdj("aoj @ " + RAddressString(addr))
        .array()
        .first()
        .toObject()[RJsonKey::opcode]
        .toString();
}

void IaitoCore::editInstruction(RVA addr, const QString &inst)
{
    cmdRawAt(QStringLiteral("wa %1").arg(inst), addr);
    emit instructionChanged(addr);
}

void IaitoCore::nopInstruction(RVA addr)
{
    cmdRawAt("wao nop", addr);
    emit instructionChanged(addr);
}

void IaitoCore::jmpReverse(RVA addr)
{
    cmdRawAt("wao recj", addr);
    emit instructionChanged(addr);
}

void IaitoCore::editBytes(RVA addr, const QString &bytes)
{
    cmdRawAt(QStringLiteral("wx %1").arg(bytes), addr);
    emit instructionChanged(addr);
}

void IaitoCore::editBytesEndian(RVA addr, const QString &bytes)
{
    cmdRawAt(QStringLiteral("wv %1").arg(bytes), addr);
    emit stackChanged();
}

void IaitoCore::setToCode(RVA addr)
{
    cmdRawAt("Cd-", addr);
    emit instructionChanged(addr);
}

void IaitoCore::setAsString(RVA addr, int size, StringTypeFormats type)
{
    if (RVA_INVALID == addr) {
        return;
    }

    QString command;

    switch (type) {
    case StringTypeFormats::s_None: {
        command = "Cs";
        break;
    }
    case StringTypeFormats::s_ASCII_LATIN1: {
        command = "Csa";
        break;
    }
    case StringTypeFormats::s_UTF8: {
        command = "Cs8";
        break;
    }
    case StringTypeFormats::s_PASCAL: {
        command = "Csp";
        break;
    }
    case StringTypeFormats::s_UTF16: {
        command = "Csw";
        break;
    }
    default:
        return;
    }

    seekAndShow(addr);

    cmdRawAt(QStringLiteral("%1 %2").arg(command).arg(size), addr);
    emit instructionChanged(addr);
}

void IaitoCore::removeString(RVA addr)
{
    cmdRawAt("Cs-", addr);
    emit instructionChanged(addr);
}

QString IaitoCore::getString(RVA addr)
{
    return cmdRawAt("ps", addr);
}

void IaitoCore::setToData(RVA addr, int size, int repeat)
{
    if (size <= 0 || repeat <= 0) {
        return;
    }
    cmdRawAt("Cd-", addr);
    cmdRawAt(QStringLiteral("Cd %1 %2").arg(size).arg(repeat), addr);
    emit instructionChanged(addr);
}

int IaitoCore::sizeofDataMeta(RVA addr)
{
    bool ok;
    int size = cmdRawAt("Cd.", addr).toInt(&ok);
    return (ok ? size : 0);
}

void IaitoCore::setComment(RVA addr, const QString &cmt)
{
    cmdRawAt(QStringLiteral("CCu base64:%1").arg(QString(cmt.toLocal8Bit().toBase64())), addr);
    emit commentsChanged(addr);
}

void IaitoCore::delComment(RVA addr)
{
    cmdRawAt("CC-", addr);
    emit commentsChanged(addr);
}

/**
 * @brief Gets the comment present at a specific address
 * @param addr The address to be checked
 * @return String containing comment
 */
QString IaitoCore::getCommentAt(RVA addr)
{
    CORE_LOCK();
    return r_meta_get_string(core->anal, R_META_TYPE_COMMENT, addr);
}

void IaitoCore::setImmediateBase(const QString &r2BaseName, RVA offset)
{
    if (offset == RVA_INVALID) {
        offset = getOffset();
    }

    this->cmdRawAt(QStringLiteral("ahi %1").arg(r2BaseName), offset);
    emit instructionChanged(offset);
}

void IaitoCore::setCurrentBits(int bits, RVA offset)
{
    if (offset == RVA_INVALID) {
        offset = getOffset();
    }

    this->cmdRawAt(QStringLiteral("ahb %1").arg(bits), offset);
    emit instructionChanged(offset);
}

void IaitoCore::applyStructureOffset(const QString &structureOffset, RVA offset)
{
    if (offset == RVA_INVALID) {
        offset = getOffset();
    }

    this->cmdRawAt("aht " + structureOffset, offset);
    emit instructionChanged(offset);
}

void IaitoCore::seekSilent(ut64 offset)
{
    CORE_LOCK();
    if (offset == RVA_INVALID) {
        return;
    }
    r_core_seek(core, offset, true);
}

void IaitoCore::seek(ut64 offset)
{
    // Slower than using the API, but the API is not complete
    // which means we either have to duplicate code from radare2
    // here, or refactor radare2 API.
    CORE_LOCK();
    if (offset == RVA_INVALID) {
        return;
    }

    // use cmd and not cmdRaw to make sure seekChanged is emitted
    cmd(QStringLiteral("s %1").arg(offset));
    // cmd already does emit seekChanged(core_->offset);
}

void IaitoCore::showMemoryWidget()
{
    emit showMemoryWidgetRequested();
}

void IaitoCore::seekAndShow(ut64 offset)
{
    seek(offset);
    showMemoryWidget();
}

void IaitoCore::seekAndShow(QString offset)
{
    seek(offset);
    showMemoryWidget();
}

void IaitoCore::seek(QString thing)
{
    cmdRaw(QStringLiteral("s %1").arg(thing));
    updateSeek();
}

void IaitoCore::seekPrev()
{
    // Use cmd because cmdRaw does not work with seek history
    cmd("s-");
}

void IaitoCore::seekNext()
{
    // Use cmd because cmdRaw does not work with seek history
    cmd("s+");
}

void IaitoCore::updateSeek()
{
    emit seekChanged(getOffset());
}

RVA IaitoCore::prevOpAddr(RVA startAddr, int count)
{
    CORE_LOCK();
    bool ok;
    RVA offset = cmdRawAt(QStringLiteral("/O %1").arg(count), startAddr).toULongLong(&ok, 16);
    return ok ? offset : startAddr - count;
}

RVA IaitoCore::nextOpAddr(RVA startAddr, int count)
{
    CORE_LOCK();

    QJsonArray array
        = Core()
              ->cmdj("pdj " + QString::number(count + 1) + "@" + QString::number(startAddr))
              .array();
    if (array.isEmpty()) {
        return startAddr + 1;
    }

    QJsonValue instValue = array.last();
    if (!instValue.isObject()) {
        return startAddr + 1;
    }

    bool ok;
    RVA offset = instValue.toObject()[RJsonKey::offset].toVariant().toULongLong(&ok);
    if (!ok) {
        return startAddr + 1;
    }

    return offset;
}

RVA IaitoCore::getOffset()
{
    return core_->offset;
}

ut64 IaitoCore::math(const QString &expr)
{
    CORE_LOCK();
    return r_num_math(core ? core->num : NULL, expr.toUtf8().constData());
}

ut64 IaitoCore::num(const QString &expr)
{
    CORE_LOCK();
    return r_num_get(core ? core->num : NULL, expr.toUtf8().constData());
}

QString IaitoCore::itoa(ut64 num, int rdx)
{
    return QString::number(num, rdx);
}

void IaitoCore::setConfig(const char *k, const char *v)
{
    CORE_LOCK();
    r_config_set(core->config, k, v);
}

void IaitoCore::setConfig(const QString &k, const char *v)
{
    CORE_LOCK();
    r_config_set(core->config, k.toUtf8().constData(), v);
}

void IaitoCore::setConfig(const char *k, const QString &v)
{
    CORE_LOCK();
    r_config_set(core->config, k, v.toUtf8().constData());
}

void IaitoCore::setConfig(const char *k, int v)
{
    CORE_LOCK();
    r_config_set_i(core->config, k, static_cast<ut64>(v));
}

void IaitoCore::setConfig(const char *k, bool v)
{
    CORE_LOCK();
    r_config_set_b(core->config, k, v);
}

int IaitoCore::getConfigi(const char *k)
{
    CORE_LOCK();
    return static_cast<int>(r_config_get_i(core->config, k));
}

ut64 IaitoCore::getConfigut64(const char *k)
{
    CORE_LOCK();
    return r_config_get_i(core->config, k);
}

bool IaitoCore::getConfigb(const char *k)
{
    CORE_LOCK();
    return r_config_get_i(core->config, k) != 0;
}

QString IaitoCore::getConfigDescription(const char *k)
{
    CORE_LOCK();
    RConfigNode *node = r_config_node_get(core->config, k);
    return node ? QString(node->desc) : QStringLiteral("Unrecognized configuration key");
}

void IaitoCore::triggerRefreshAll()
{
    emit refreshAll();
}

void IaitoCore::triggerAsmOptionsChanged()
{
    emit asmOptionsChanged();
}

void IaitoCore::triggerGraphOptionsChanged()
{
    emit graphOptionsChanged();
}

void IaitoCore::message(const QString &msg, bool debug)
{
    if (msg.isEmpty())
        return;
    if (debug) {
        qDebug() << msg;
        emit newDebugMessage(msg);
        return;
    }
    emit newMessage(msg);
}

QString IaitoCore::getFilePath()
{
    CORE_LOCK();
    char *o = r_core_cmd_str(core, "o.");
    r_str_trim_tail(o);
    auto os = QString(o);
    free(o);
    return os;
}

QString IaitoCore::getConfig(const char *k)
{
    CORE_LOCK();
    return QString(r_config_get(core->config, k));
}

void IaitoCore::setConfig(const char *k, const QVariant &v)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    switch (v.typeId()) {
    case QMetaType::Bool:
        setConfig(k, v.toBool());
        break;
    case QMetaType::Int:
        setConfig(k, v.toInt());
        break;
    default:
        setConfig(k, v.toString());
        break;
    }
#else
    switch (v.type()) {
    case QVariant::Type::Bool:
        setConfig(k, v.toBool());
        break;
    case QVariant::Type::Int:
        setConfig(k, v.toInt());
        break;
    default:
        setConfig(k, v.toString());
        break;
    }
#endif
}

void IaitoCore::setCPU(QString arch, QString cpu, int bits)
{
    if (arch != nullptr) {
        setConfig("asm.arch", arch);
    }
    if (cpu != nullptr) {
        setConfig("asm.cpu", cpu);
    }
    setConfig("asm.bits", bits);
}

void IaitoCore::setEndianness(bool big)
{
    setConfig("cfg.bigendian", big);
}

QByteArray IaitoCore::assemble(const QString &code)
{
    CORE_LOCK();
    RAsmCode *ac = r_asm_massemble(core->rasm, code.toUtf8().constData());
    QByteArray res;
    if (ac && ac->bytes) {
        res = QByteArray(reinterpret_cast<const char *>(ac->bytes), ac->len);
    }
    r_asm_code_free(ac);
    return res;
}

QString IaitoCore::disassemble(const QByteArray &data)
{
    CORE_LOCK();
    RAsmCode *ac = r_asm_mdisassemble(
        core->rasm, reinterpret_cast<const ut8 *>(data.constData()), data.length());
    QString code;
    if (ac && ac->assembly) {
        code = QString::fromUtf8(ac->assembly);
    }
    r_asm_code_free(ac);
    return code;
}

QString IaitoCore::disassembleSingleInstruction(RVA addr)
{
    return cmdRawAt("pi 1", addr).simplified();
}

RAnalFunction *IaitoCore::functionIn(ut64 addr)
{
    CORE_LOCK();
    RList *fcns = r_anal_get_functions_in(core->anal, addr);
    RAnalFunction *fcn = !r_list_empty(fcns) ? reinterpret_cast<RAnalFunction *>(r_list_first(fcns))
                                             : nullptr;
    r_list_free(fcns);
    return fcn;
}

RAnalFunction *IaitoCore::functionAt(ut64 addr)
{
    CORE_LOCK();
    return r_anal_get_function_at(core->anal, addr);
}

/**
 * @brief finds the start address of a function in a given address
 * @param addr - an address which belongs to a function
 * @returns if function exists, return its start address. Otherwise return
 * RVA_INVALID
 */
RVA IaitoCore::getFunctionStart(RVA addr)
{
    CORE_LOCK();
    RAnalFunction *fcn = Core()->functionIn(addr);
    return fcn ? fcn->addr : RVA_INVALID;
}

/**
 * @brief finds the end address of a function in a given address
 * @param addr - an address which belongs to a function
 * @returns if function exists, return its end address. Otherwise return
 * RVA_INVALID
 */
RVA IaitoCore::getFunctionEnd(RVA addr)
{
    CORE_LOCK();
    RAnalFunction *fcn = Core()->functionIn(addr);
    return fcn ? fcn->addr : RVA_INVALID;
}

/**
 * @brief finds the last instruction of a function in a given address
 * @param addr - an address which belongs to a function
 * @returns if function exists, return the address of its last instruction.
 * Otherwise return RVA_INVALID
 */
RVA IaitoCore::getLastFunctionInstruction(RVA addr)
{
    CORE_LOCK();
    RAnalFunction *fcn = Core()->functionIn(addr);
    if (!fcn) {
        return RVA_INVALID;
    }
    RAnalBlock *lastBB = (RAnalBlock *) r_list_last(fcn->bbs);
    return lastBB ? lastBB->addr + r_anal_bb_offset_inst(lastBB, lastBB->ninstr - 1) : RVA_INVALID;
}

QString IaitoCore::cmdFunctionAt(QString addr)
{
    QString ret;
    // Use cmd because cmdRaw would not work with grep
    ret = cmd(QStringLiteral("fd @ %1~[0]").arg(addr));
    return ret.trimmed();
}

QString IaitoCore::cmdFunctionAt(RVA addr)
{
    return cmdFunctionAt(QString::number(addr));
}

void IaitoCore::cmdEsil(const char *command)
{
    // use cmd and not cmdRaw because of unexpected commands
    QString res = cmd(command);
    if (res.contains(QStringLiteral("[ESIL] Stopped execution in an invalid instruction"))) {
        msgBox.showMessage("Stopped when attempted to run an invalid "
                           "instruction. You can disable this in Preferences");
    }
}

QString IaitoCore::createFunctionAt(RVA addr)
{
    QString ret = cmdRaw(QStringLiteral("af %1").arg(addr));
    emit functionsChanged();
    return ret;
}

QString IaitoCore::createFunctionAt(RVA addr, QString name)
{
    static const QRegularExpression regExp("[^a-zA-Z0-9_]");
    name.remove(regExp);
    QString ret = cmdRawAt(QStringLiteral("af %1").arg(name), addr);
    emit functionsChanged();
    return ret;
}

QJsonDocument IaitoCore::getRegistersInfo()
{
    return cmdj("aeafj");
}

RVA IaitoCore::getOffsetJump(RVA addr)
{
    bool ok;
    RVA value = cmdj("aoj @" + QString::number(addr))
                    .array()
                    .first()
                    .toObject()
                    .value(RJsonKey::jump)
                    .toVariant()
                    .toULongLong(&ok);

    if (!ok) {
        return RVA_INVALID;
    }

    return value;
}

QList<Decompiler *> IaitoCore::getDecompilers()
{
    return decompilers;
}

Decompiler *IaitoCore::getDecompilerById(const QString &id)
{
    for (Decompiler *dec : decompilers) {
        if (dec->getId() == id) {
            return dec;
        }
    }
    return nullptr;
}

bool IaitoCore::registerDecompiler(Decompiler *decompiler)
{
    if (getDecompilerById(decompiler->getId())) {
        return false;
    }
    decompiler->setParent(this);
    decompilers.push_back(decompiler);
    return true;
}

QJsonDocument IaitoCore::getFileInfo()
{
    return cmdj("ij");
}

QJsonDocument IaitoCore::getFileVersionInfo()
{
    return cmdj("iVj");
}

QJsonDocument IaitoCore::getSignatureInfo()
{
    return cmdj("iCj");
}

// Utility function to check if a telescoped item exists and add it with
// prefixes to the desc
static inline const QString appendVar(
    QString &dst, const QString val, const QString prepend_val, const QString append_val)
{
    if (!val.isEmpty()) {
        dst += prepend_val + val + append_val;
    }
    return val;
}

RefDescription IaitoCore::formatRefDesc(QJsonObject refItem)
{
    RefDescription desc;

    // Ignore empty refs and refs that only contain addr
    if (refItem.size() <= 1) {
        return desc;
    }

    QString str = refItem["string"].toVariant().toString();
    if (!str.isEmpty()) {
        char *s = strdup(str.toStdString().c_str());
        desc.ref = s; // str;
        free(s);
        desc.refColor = ConfigColor("comment");
    } else {
        QString type, string;
        do {
            desc.ref += " ->";
            appendVar(desc.ref, refItem["reg"].toVariant().toString(), " @", "");
            appendVar(desc.ref, refItem["mapname"].toVariant().toString(), " (", ")");
            appendVar(desc.ref, refItem["section"].toVariant().toString(), " (", ")");
            appendVar(desc.ref, refItem["func"].toVariant().toString(), " ", "");
            type = appendVar(desc.ref, refItem["type"].toVariant().toString(), " ", "");
            appendVar(desc.ref, refItem["perms"].toVariant().toString(), " ", "");
            appendVar(desc.ref, refItem["asm"].toVariant().toString(), " \"", "\"");
            string = appendVar(desc.ref, refItem["string"].toVariant().toString(), " ", "");
            if (!string.isNull()) {
                // There is no point in adding ascii and addr info after a
                // string
                break;
            }
            if (!refItem["value"].isNull()) {
                appendVar(
                    desc.ref, RAddressString(refItem["value"].toVariant().toULongLong()), " ", "");
            }
            refItem = refItem["ref"].toObject();
        } while (!refItem.empty());

        // Set the ref's color according to the last item type
        if (type == "ascii" || !string.isEmpty()) {
            desc.refColor = ConfigColor("comment");
        } else if (type == "program") {
            desc.refColor = ConfigColor("fname");
        } else if (type == "library") {
            desc.refColor = ConfigColor("floc");
        } else if (type == "stack") {
            desc.refColor = ConfigColor("offset");
        }
    }

    return desc;
}

QList<QJsonObject> IaitoCore::getRegisterRefs(int depth)
{
    QList<QJsonObject> ret;
    if (!currentlyDebugging) {
        return ret;
    }

    QJsonObject registers = cmdj("drj").object();

    for (const QString &key : registers.keys()) {
        QJsonObject reg;
        reg["value"] = registers.value(key);
        reg["ref"] = getAddrRefs(registers.value(key).toVariant().toULongLong(), depth);
        reg["name"] = key;
        ret.append(reg);
    }

    return ret;
}

QList<QJsonObject> IaitoCore::getStack(int size, int depth)
{
    QList<QJsonObject> stack;
    if (!currentlyDebugging) {
        return stack;
    }

    CORE_LOCK();
    bool ret;
    RVA addr = cmdRaw("dr SP").toULongLong(&ret, 16);
    if (!ret) {
        return stack;
    }

    int base = r_config_get_i(core->config, "asm.bits");
    for (int i = 0; i < size; i += base / 8) {
        if ((base == 32 && addr + i >= UT32_MAX) || (base == 16 && addr + i >= UT16_MAX)) {
            break;
        }

        stack.append(getAddrRefs(addr + i, depth));
    }

    return stack;
}

QJsonObject IaitoCore::getAddrRefs(RVA addr, int depth)
{
    QJsonObject json;
    if (depth < 1 || addr == UT64_MAX) {
        return json;
    }

    CORE_LOCK();
    int bits = r_config_get_i(core->config, "asm.bits");
    QByteArray buf = QByteArray();
    ut64 type = r_core_anal_address(core, addr);

    json["addr"] = QString::number(addr);

    // Search for the section the addr is in, avoid duplication for heap/stack
    // with type
    if (!(type & R_ANAL_ADDR_TYPE_HEAP || type & R_ANAL_ADDR_TYPE_STACK)) {
        // Attempt to find the address within a map
        RDebugMap *map = r_debug_map_get(core->dbg, addr);
        if (map && map->name && map->name[0]) {
            json["mapname"] = map->name;
        }

        RBinSection *sect = r_bin_get_section_at(r_bin_cur_object(core->bin), addr, true);
        if (sect && sect->name[0]) {
            json["section"] = sect->name;
        }
    }

    // Check if the address points to a register
    RFlagItem *fi = r_flag_get_i(core->flags, addr);
    if (fi) {
        RRegItem *r = r_reg_get(core->dbg->reg, fi->name, -1);
        if (r) {
            json["reg"] = r->name;
#if R2_VERSION_NUMBER >= 50709
            r_unref(r);
#endif
        }
    }

    // Attempt to find the address within a function
    RAnalFunction *fcn = r_anal_get_fcn_in(core->anal, addr, 0);
    if (fcn) {
        json["fcn"] = fcn->name;
    }

    // Update type and permission information
    if (type != 0) {
        if (type & R_ANAL_ADDR_TYPE_HEAP) {
            json["type"] = "heap";
        } else if (type & R_ANAL_ADDR_TYPE_STACK) {
            json["type"] = "stack";
        } else if (type & R_ANAL_ADDR_TYPE_PROGRAM) {
            json["type"] = "program";
        } else if (type & R_ANAL_ADDR_TYPE_LIBRARY) {
            json["type"] = "library";
        } else if (type & R_ANAL_ADDR_TYPE_ASCII) {
            json["type"] = "ascii";
        } else if (type & R_ANAL_ADDR_TYPE_SEQUENCE) {
            json["type"] = "sequence";
        }

        QString perms = "";
        if (type & R_ANAL_ADDR_TYPE_READ) {
            perms += "r";
        }
        if (type & R_ANAL_ADDR_TYPE_WRITE) {
            perms += "w";
        }
        if (type & R_ANAL_ADDR_TYPE_EXEC) {
#if R2_VERSION_NUMBER >= 50709
            RAnalOp op;
            buf.resize(32);
            perms += "x";
            // Instruction disassembly
            r_io_read_at(core->io, addr, (unsigned char *) buf.data(), buf.size());
            r_asm_set_pc(core->rasm, addr);
            r_anal_op_init(&op);
            r_asm_disassemble(core->rasm, &op, (unsigned char *) buf.data(), buf.size());
            json["asm"] = op.mnemonic;
            r_anal_op_fini(&op);
#else
            RAsmOp op;
            buf.resize(32);
            perms += "x";
            // Instruction disassembly
            r_io_read_at(core->io, addr, (unsigned char *) buf.data(), buf.size());
            r_asm_set_pc(core->rasm, addr);
            r_asm_disassemble(core->rasm, &op, (unsigned char *) buf.data(), buf.size());
            json["asm"] = r_asm_op_get_asm(&op);
#endif
        }

        if (!perms.isEmpty()) {
            json["perms"] = perms;
        }
    }

    // Try to telescope further if depth permits it
    if ((type & R_ANAL_ADDR_TYPE_READ) && !(type & R_ANAL_ADDR_TYPE_EXEC)) {
        buf.resize(64);
        ut32 *n32 = (ut32 *) buf.data();
        ut64 *n64 = (ut64 *) buf.data();
        r_io_read_at(core->io, addr, (unsigned char *) buf.data(), buf.size());
        ut64 n = (bits == 64) ? *n64 : *n32;
        // The value of the next address will serve as an indication that
        // there's more to telescope if we have reached the depth limit
        json["value"] = QString::number(n);
        if (depth && n != addr) {
            // Make sure we aren't telescoping the same address
            QJsonObject ref = getAddrRefs(n, depth - 1);
            if (!ref.empty() && !ref["type"].isNull()) {
                // If the dereference of the current pointer is an ascii
                // character we might have a string in this address
                if (ref["type"].toString().contains("ascii")) {
                    buf.resize(128);
                    r_io_read_at(core->io, addr, (unsigned char *) buf.data(), buf.size());
                    QString strVal = QString(buf);
                    // Indicate that the string is longer than the printed value
                    if (strVal.size() == buf.size()) {
                        strVal += "...";
                    }
                    json["string"] = strVal;
                }
                json["ref"] = ref;
            }
        }
    }
    return json;
}

QJsonDocument IaitoCore::getProcessThreads(int pid)
{
    if (-1 == pid) {
        // Return threads list of the currently debugged PID
        return cmdj("dptj");
    } else {
        return cmdj("dptj " + QString::number(pid));
    }
}

QJsonDocument IaitoCore::getChildProcesses(int pid)
{
    // Return the currently debugged process and it's children
    if (-1 == pid) {
        return cmdj("dpj");
    }
    // Return the given pid and it's child processes
    return cmdj("dpj " + QString::number(pid));
}

QJsonDocument IaitoCore::getRegisterValues()
{
    return cmdj("drj");
}

QList<VariableDescription> IaitoCore::getVariables(RVA at)
{
    QList<VariableDescription> ret;
    QJsonObject varsObject = cmdj(QStringLiteral("afvj @ %1").arg(at)).object();

    auto addVars = [&](VariableDescription::RefType refType, const QJsonArray &array) {
        for (const QJsonValue varValue : array) {
            QJsonObject varObject = varValue.toObject();
            VariableDescription desc;
            desc.refType = refType;
            desc.name = varObject["name"].toString();
            desc.type = varObject["type"].toString();
            ret << desc;
        }
    };

    addVars(VariableDescription::RefType::SP, varsObject["sp"].toArray());
    addVars(VariableDescription::RefType::BP, varsObject["bp"].toArray());
    addVars(VariableDescription::RefType::Reg, varsObject["reg"].toArray());

    return ret;
}

QVector<RegisterRefValueDescription> IaitoCore::getRegisterRefValues()
{
    QJsonArray registerRefArray = cmdj("drrj").array();
    QVector<RegisterRefValueDescription> result;

    for (const QJsonValue value : registerRefArray) {
        QJsonObject regRefObject = value.toObject();

        RegisterRefValueDescription desc;
        desc.name = regRefObject[RJsonKey::reg].toString();
        desc.value = regRefObject[RJsonKey::value].toString();
        desc.ref = regRefObject[RJsonKey::ref].toString();

        result.push_back(desc);
    }
    return result;
}

QString IaitoCore::getRegisterName(QString registerRole)
{
    return cmdRaw("drn " + registerRole).trimmed();
}

RVA IaitoCore::getProgramCounterValue()
{
    bool ok;
    if (currentlyDebugging) {
        // Use cmd because cmdRaw would not work with inner command backticked
        // TODO: Risky command due to changes in API, search for something safer
        RVA addr = cmd("dr `drn PC`").toULongLong(&ok, 16);
        if (ok) {
            return addr;
        }
    }
    return RVA_INVALID;
}

void IaitoCore::setRegister(QString regName, QString regValue)
{
    cmdRaw(QStringLiteral("dr %1=%2").arg(regName).arg(regValue));
    emit registersChanged();
    emit refreshCodeViews();
}

void IaitoCore::setCurrentDebugThread(int tid)
{
    if (!asyncCmd("dpt=" + QString::number(tid), debugTask)) {
        return;
    }

    emit debugTaskStateChanged();
    connect(debugTask.data(), &R2Task::finished, this, [this]() {
        debugTask.clear();
        emit registersChanged();
        emit refreshCodeViews();
        emit stackChanged();
        syncAndSeekProgramCounter();
        emit switchedThread();
        emit debugTaskStateChanged();
    });

    debugTask->startTask();
}

void IaitoCore::setCurrentDebugProcess(int pid)
{
    if (!currentlyDebugging || !asyncCmd("dp=" + QString::number(pid), debugTask)) {
        return;
    }

    emit debugTaskStateChanged();
    connect(debugTask.data(), &R2Task::finished, this, [this]() {
        debugTask.clear();
        emit registersChanged();
        emit refreshCodeViews();
        emit stackChanged();
        emit flagsChanged();
        syncAndSeekProgramCounter();
        emit switchedProcess();
        emit debugTaskStateChanged();
    });

    debugTask->startTask();
}

void IaitoCore::startDebug()
{
    if (!currentlyDebugging) {
        offsetPriorDebugging = getOffset();
    }
    currentlyOpenFile = getFilePath();

    if (!asyncCmd("ood", debugTask)) {
        return;
    }

    emit debugTaskStateChanged();

    connect(debugTask.data(), &R2Task::finished, this, [this]() {
        if (debugTaskDialog) {
            delete debugTaskDialog;
        }
        debugTask.clear();

        emit registersChanged();
        if (!currentlyDebugging) {
            setConfig("asm.flags", false);
            currentlyDebugging = true;
            emit toggleDebugView();
            emit refreshCodeViews();
        }

        emit codeRebased();
        emit stackChanged();
        emit debugTaskStateChanged();
    });

    debugTaskDialog = new R2TaskDialog(debugTask);
    debugTaskDialog->setBreakOnClose(true);
    debugTaskDialog->setAttribute(Qt::WA_DeleteOnClose);
    debugTaskDialog->setDesc(tr("Starting native debug..."));
    debugTaskDialog->show();

    debugTask->startTask();
}

void IaitoCore::startEmulation()
{
    if (!currentlyDebugging) {
        offsetPriorDebugging = getOffset();
    }

    // clear registers, init esil state, stack, progcounter at current seek
    asyncCmd("aei; aeim; aeip", debugTask);

    emit debugTaskStateChanged();

    connect(debugTask.data(), &R2Task::finished, this, [this]() {
        if (debugTaskDialog) {
            delete debugTaskDialog;
        }
        debugTask.clear();

        if (!currentlyDebugging || !currentlyEmulating) {
            // prevent register flags from appearing during debug/emul
            setConfig("asm.flags", false);
            // allows to view self-modifying code changes or other binary
            // changes
            setConfig("io.cache", true);
            currentlyDebugging = true;
            currentlyEmulating = true;
            emit toggleDebugView();
        }

        emit registersChanged();
        emit stackChanged();
        emit codeRebased();
        emit refreshCodeViews();
        emit debugTaskStateChanged();
    });

    debugTaskDialog = new R2TaskDialog(debugTask);
    debugTaskDialog->setBreakOnClose(true);
    debugTaskDialog->setAttribute(Qt::WA_DeleteOnClose);
    debugTaskDialog->setDesc(tr("Starting emulation..."));
    debugTaskDialog->show();

    debugTask->startTask();
}

void IaitoCore::attachRemote(const QString &uri)
{
    if (!currentlyDebugging) {
        offsetPriorDebugging = getOffset();
    }

    // connect to a debugger with the given plugin
    asyncCmd("e cfg.debug = true; oodf " + uri, debugTask);
    emit debugTaskStateChanged();

    connect(debugTask.data(), &R2Task::finished, this, [this, uri]() {
        if (debugTaskDialog) {
            delete debugTaskDialog;
        }
        debugTask.clear();
        // Check if we actually connected
        bool connected = false;
        QJsonArray openFilesArray = getOpenedFiles();
        for (QJsonValue value : openFilesArray) {
            QJsonObject openFile = value.toObject();
            QString fileUri = openFile["uri"].toString();
            if (!fileUri.compare(uri)) {
                connected = true;
            }
        }
        seekAndShow(getProgramCounterValue());
        if (!connected) {
            emit attachedRemote(false);
            emit debugTaskStateChanged();
            return;
        }

        emit registersChanged();
        if (!currentlyDebugging || !currentlyEmulating) {
            // prevent register flags from appearing during debug/emul
            setConfig("asm.flags", false);
            currentlyDebugging = true;
            emit toggleDebugView();
        }

        emit codeRebased();
        emit attachedRemote(true);
        emit debugTaskStateChanged();
    });

    debugTaskDialog = new R2TaskDialog(debugTask);
    debugTaskDialog->setBreakOnClose(true);
    debugTaskDialog->setAttribute(Qt::WA_DeleteOnClose);
    debugTaskDialog->setDesc(tr("Connecting to: ") + uri);
    debugTaskDialog->show();

    debugTask->startTask();
}

void IaitoCore::attachDebug(int pid)
{
    if (!currentlyDebugging) {
        offsetPriorDebugging = getOffset();
    }

    // attach to process with dbg plugin
    asyncCmd("e cfg.debug = true; oodf dbg://" + QString::number(pid), debugTask);
    emit debugTaskStateChanged();

    connect(debugTask.data(), &R2Task::finished, this, [this, pid]() {
        if (debugTaskDialog) {
            delete debugTaskDialog;
        }
        debugTask.clear();

        syncAndSeekProgramCounter();
        if (!currentlyDebugging || !currentlyEmulating) {
            // prevent register flags from appearing during debug/emul
            setConfig("asm.flags", false);
            currentlyDebugging = true;
            currentlyOpenFile = getFilePath();
            currentlyAttachedToPID = pid;
            emit toggleDebugView();
        }

        emit codeRebased();
        emit debugTaskStateChanged();
    });

    debugTaskDialog = new R2TaskDialog(debugTask);
    debugTaskDialog->setBreakOnClose(true);
    debugTaskDialog->setAttribute(Qt::WA_DeleteOnClose);
    debugTaskDialog->setDesc(tr("Attaching to process (") + QString::number(pid) + ")...");
    debugTaskDialog->show();

    debugTask->startTask();
}

void IaitoCore::suspendDebug()
{
    debugTask->breakTask();
}

void IaitoCore::stopDebug()
{
    if (!currentlyDebugging) {
        return;
    }

    if (!debugTask.isNull()) {
        suspendDebug();
    }

    currentlyDebugging = false;
    emit debugTaskStateChanged();

    if (currentlyEmulating) {
        cmdEsil("aeim-; aei-; wcr; .ar-");
        currentlyEmulating = false;
    } else if (currentlyAttachedToPID != -1) {
        // Use cmd because cmdRaw would not work with command concatenation
        cmd(QStringLiteral("dp- %1; o %2; .ar-")
                .arg(QString::number(currentlyAttachedToPID), currentlyOpenFile));
        currentlyAttachedToPID = -1;
    } else {
        QString ptraceFiles = "";
        // close ptrace file descriptors left open
        QJsonArray openFilesArray = cmdj("oj").array();
        for (QJsonValue value : openFilesArray) {
            QJsonObject openFile = value.toObject();
            QString URI = openFile["uri"].toString();
            if (URI.contains("ptrace")) {
                ptraceFiles += "o-" + QString::number(openFile["fd"].toInt()) + ";";
            }
        }
        // Use cmd because cmdRaw would not work with command concatenation
        cmd("doc" + ptraceFiles);
    }

    syncAndSeekProgramCounter();
    setConfig("asm.flags", true);
    setConfig("io.cache", false);
    emit codeRebased();
    emit toggleDebugView();
    offsetPriorDebugging = getOffset();
    emit debugTaskStateChanged();
}

void IaitoCore::syncAndSeekProgramCounter()
{
    seekAndShow(getProgramCounterValue());
    emit registersChanged();
}

void IaitoCore::continueDebug()
{
    if (!currentlyDebugging) {
        return;
    }

    if (currentlyEmulating) {
        if (!asyncCmdEsil("aec", debugTask)) {
            return;
        }
    } else {
        if (!asyncCmd("dc", debugTask)) {
            return;
        }
    }

    emit debugTaskStateChanged();
    connect(debugTask.data(), &R2Task::finished, this, [this]() {
        debugTask.clear();
        syncAndSeekProgramCounter();
        emit registersChanged();
        emit refreshCodeViews();
        emit debugTaskStateChanged();
    });

    debugTask->startTask();
}

void IaitoCore::continueUntilDebug(QString offset)
{
    if (!currentlyDebugging) {
        return;
    }

    if (currentlyEmulating) {
        if (!asyncCmdEsil("aecu " + offset, debugTask)) {
            return;
        }
    } else {
        if (!asyncCmd("dcu " + offset, debugTask)) {
            return;
        }
    }

    emit debugTaskStateChanged();
    connect(debugTask.data(), &R2Task::finished, this, [this]() {
        debugTask.clear();
        syncAndSeekProgramCounter();
        emit registersChanged();
        emit stackChanged();
        emit refreshCodeViews();
        emit debugTaskStateChanged();
    });

    debugTask->startTask();
}

void IaitoCore::continueUntilCall()
{
    if (!currentlyDebugging) {
        return;
    }

    if (currentlyEmulating) {
        if (!asyncCmdEsil("aecc", debugTask)) {
            return;
        }
    } else {
        if (!asyncCmd("dcc", debugTask)) {
            return;
        }
    }

    emit debugTaskStateChanged();
    connect(debugTask.data(), &R2Task::finished, this, [this]() {
        debugTask.clear();
        syncAndSeekProgramCounter();
        emit debugTaskStateChanged();
    });

    debugTask->startTask();
}

void IaitoCore::continueUntilSyscall()
{
    if (!currentlyDebugging) {
        return;
    }

    if (currentlyEmulating) {
        if (!asyncCmdEsil("aecs", debugTask)) {
            return;
        }
    } else {
        if (!asyncCmd("dcs", debugTask)) {
            return;
        }
    }

    emit debugTaskStateChanged();
    connect(debugTask.data(), &R2Task::finished, this, [this]() {
        debugTask.clear();
        syncAndSeekProgramCounter();
        emit debugTaskStateChanged();
    });

    debugTask->startTask();
}

void IaitoCore::stepDebug()
{
    if (!currentlyDebugging) {
        return;
    }

    if (currentlyEmulating) {
        if (!asyncCmdEsil("aes", debugTask)) {
            return;
        }
    } else {
        if (!asyncCmd("ds", debugTask)) {
            return;
        }
    }

    emit debugTaskStateChanged();
    connect(debugTask.data(), &R2Task::finished, this, [this]() {
        debugTask.clear();
        syncAndSeekProgramCounter();
        emit debugTaskStateChanged();
    });

    debugTask->startTask();
}

void IaitoCore::stepOverDebug()
{
    if (!currentlyDebugging) {
        return;
    }

    if (currentlyEmulating) {
        if (!asyncCmdEsil("aeso", debugTask)) {
            return;
        }
    } else {
        if (!asyncCmd("dso", debugTask)) {
            return;
        }
    }

    emit debugTaskStateChanged();
    connect(debugTask.data(), &R2Task::finished, this, [this]() {
        debugTask.clear();
        syncAndSeekProgramCounter();
        emit debugTaskStateChanged();
    });

    debugTask->startTask();
}

void IaitoCore::stepOutDebug()
{
    if (!currentlyDebugging) {
        return;
    }

    emit debugTaskStateChanged();
    if (!asyncCmd("dsf", debugTask)) {
        return;
    }

    connect(debugTask.data(), &R2Task::finished, this, [this]() {
        debugTask.clear();
        syncAndSeekProgramCounter();
        emit debugTaskStateChanged();
    });

    debugTask->startTask();
}

QStringList IaitoCore::getDebugPlugins()
{
    QStringList plugins;
    QJsonArray pluginArray = cmdj("dLj").array();

    for (const QJsonValue value : pluginArray) {
        QJsonObject pluginObject = value.toObject();

        QString plugin = pluginObject[RJsonKey::name].toString();

        plugins << plugin;
    }
    return plugins;
}

QString IaitoCore::getActiveDebugPlugin()
{
    return getConfig("dbg.backend");
}

void IaitoCore::setDebugPlugin(QString plugin)
{
    setConfig("dbg.backend", plugin);
}

void IaitoCore::toggleBreakpoint(RVA addr)
{
    cmdRaw(QStringLiteral("dbs %1").arg(addr));
    emit breakpointsChanged(addr);
}

void IaitoCore::addBreakpoint(const BreakpointDescription &config)
{
    CORE_LOCK();
    RBreakpointItem *breakpoint = nullptr;
    int watchpoint_prot = 0;
    if (config.hw) {
        watchpoint_prot = config.permission & ~(R_BP_PROT_EXEC);
    }

    auto address = config.addr;
    char *module = nullptr;
    QByteArray moduleNameData;
    if (config.type == BreakpointDescription::Named) {
        address = Core()->math(config.positionExpression);
    } else if (config.type == BreakpointDescription::Module) {
        address = 0;
        moduleNameData = config.positionExpression.toUtf8();
        module = moduleNameData.data();
    }
    breakpoint = r_debug_bp_add(
        core->dbg,
        address,
        (config.hw && watchpoint_prot == 0),
        watchpoint_prot,
        watchpoint_prot,
        module,
        config.moduleDelta);
    if (!breakpoint) {
        QMessageBox::critical(nullptr, tr("Breakpoint error"), tr("Failed to create breakpoint"));
        return;
    }
    if (config.type == BreakpointDescription::Named) {
        updateOwnedCharPtr(breakpoint->expr, config.positionExpression);
    }

    if (config.hw) {
        breakpoint->size = config.size;
    }
    if (config.type == BreakpointDescription::Named) {
        updateOwnedCharPtr(breakpoint->name, config.positionExpression);
    }

    int index = std::find(
                    core->dbg->bp->bps_idx,
                    core->dbg->bp->bps_idx + core->dbg->bp->bps_idx_count,
                    breakpoint)
                - core->dbg->bp->bps_idx;

    breakpoint->enabled = config.enabled;
    if (config.trace) {
        setBreakpointTrace(index, config.trace);
    }
    if (!config.condition.isEmpty()) {
        updateOwnedCharPtr(breakpoint->cond, config.condition);
    }
    if (!config.command.isEmpty()) {
        updateOwnedCharPtr(breakpoint->data, config.command);
    }
    emit breakpointsChanged(breakpoint->addr);
}

void IaitoCore::updateBreakpoint(int index, const BreakpointDescription &config)
{
    CORE_LOCK();
    if (auto bp = r_bp_get_index(core->dbg->bp, index)) {
        r_bp_del(core->dbg->bp, bp->addr);
    }
    // Delete by index currently buggy,
    // required for breakpoints with non address based position
    // r_bp_del_index(core->dbg->bp, index);
    addBreakpoint(config);
}

void IaitoCore::delBreakpoint(RVA addr)
{
    cmdRaw("db- " + RAddressString(addr));
    emit breakpointsChanged(addr);
}

void IaitoCore::delAllBreakpoints()
{
    cmdRaw("db-*");
    emit refreshCodeViews();
}

void IaitoCore::enableBreakpoint(RVA addr)
{
    cmdRaw("dbe " + RAddressString(addr));
    emit breakpointsChanged(addr);
}

void IaitoCore::disableBreakpoint(RVA addr)
{
    cmdRaw("dbd " + RAddressString(addr));
    emit breakpointsChanged(addr);
}

void IaitoCore::setBreakpointTrace(int index, bool enabled)
{
    if (enabled) {
        cmdRaw(QStringLiteral("dbite %1").arg(index));
    } else {
        cmdRaw(QStringLiteral("dbitd %1").arg(index));
    }
}

static BreakpointDescription breakpointDescriptionFromR2(int index, r_bp_item_t *bpi)
{
    BreakpointDescription bp;
    bp.addr = bpi->addr;
    bp.index = index;
    bp.size = bpi->size;
    if (bpi->expr) {
        bp.positionExpression = bpi->expr;
        bp.type = BreakpointDescription::Named;
    }
    bp.name = bpi->name;
    bp.permission = bpi->perm;
    bp.command = bpi->data;
    bp.condition = bpi->cond;
    bp.hw = bpi->hw;
    bp.trace = bpi->trace;
    bp.enabled = bpi->enabled;
    return bp;
}

int IaitoCore::breakpointIndexAt(RVA addr)
{
    CORE_LOCK();
    return r_bp_get_index_at(core->dbg->bp, addr);
}

BreakpointDescription IaitoCore::getBreakpointAt(RVA addr)
{
    CORE_LOCK();
    int index = breakpointIndexAt(addr);
    auto bp = r_bp_get_index(core->dbg->bp, index);
    if (bp) {
        return breakpointDescriptionFromR2(index, bp);
    }
    return BreakpointDescription();
}

QList<BreakpointDescription> IaitoCore::getBreakpoints()
{
    CORE_LOCK();
    QList<BreakpointDescription> ret;
    for (int i = 0; i < core->dbg->bp->bps_idx_count; i++) {
        RBreakpointItem *bpi = r_bp_get_index(core->dbg->bp, i);
        if (!bpi)
            continue;
        ret.push_back(breakpointDescriptionFromR2(i, bpi));
    }

    return ret;
}

QList<RVA> IaitoCore::getBreakpointsAddresses()
{
    QList<RVA> bpAddresses;
    for (const BreakpointDescription &bp : getBreakpoints()) {
        bpAddresses << bp.addr;
    }

    return bpAddresses;
}

QList<RVA> IaitoCore::getBreakpointsInFunction(RVA funcAddr)
{
    QList<RVA> allBreakpoints = getBreakpointsAddresses();
    QList<RVA> functionBreakpoints;

    // Use std manipulations to take only the breakpoints that belong to this
    // function
    std::copy_if(
        allBreakpoints.begin(),
        allBreakpoints.end(),
        std::back_inserter(functionBreakpoints),
        [this, funcAddr](RVA BPadd) { return getFunctionStart(BPadd) == funcAddr; });
    return functionBreakpoints;
}

bool IaitoCore::isBreakpoint(const QList<RVA> &breakpoints, RVA addr)
{
    return breakpoints.contains(addr);
}

QJsonDocument IaitoCore::getBacktrace()
{
    return cmdj("dbtj");
}

QList<ProcessDescription> IaitoCore::getAllProcesses()
{
    QList<ProcessDescription> ret;
    QJsonArray processArray = cmdj("dplj").array();

    for (const QJsonValue value : processArray) {
        QJsonObject procObject = value.toObject();

        ProcessDescription proc;

        proc.pid = procObject[RJsonKey::pid].toInt();
        proc.uid = procObject[RJsonKey::uid].toInt();
        proc.status = procObject[RJsonKey::status].toString();
        proc.path = procObject[RJsonKey::path].toString();

        ret << proc;
    }

    return ret;
}

QList<MemoryMapDescription> IaitoCore::getMemoryMap()
{
    QList<MemoryMapDescription> ret;
    QJsonArray memoryMapArray = cmdj("dmj").array();

    for (const QJsonValue value : memoryMapArray) {
        QJsonObject memMapObject = value.toObject();

        MemoryMapDescription memMap;

        memMap.name = memMapObject[RJsonKey::name].toString();
        memMap.fileName = memMapObject[RJsonKey::file].toString();
        memMap.addrStart = memMapObject[RJsonKey::addr].toVariant().toULongLong();
        memMap.addrEnd = memMapObject[RJsonKey::addr_end].toVariant().toULongLong();
        memMap.type = memMapObject[RJsonKey::type].toString();
        memMap.permission = memMapObject[RJsonKey::perm].toString();

        ret << memMap;
    }

    return ret;
}

QStringList IaitoCore::getStats()
{
    QStringList stats;
    cmdRaw("fs functions");

    // The cmd coomand is frequently used in this function because
    // cmdRaw would not work with grep
    stats << cmd("f~?").trimmed();

    QString imps = cmd("ii~?").trimmed();
    stats << imps;

    cmdRaw("fs symbols");
    stats << cmd("f~?").trimmed();
    cmdRaw("fs strings");
    stats << cmd("f~?").trimmed();
    cmdRaw("fs relocs");
    stats << cmd("f~?").trimmed();
    cmdRaw("fs sections");
    stats << cmd("f~?").trimmed();
    cmdRaw("fs *");
    stats << cmd("f~?").trimmed();

    return stats;
}

void IaitoCore::setGraphEmpty(bool empty)
{
    emptyGraph = empty;
}

bool IaitoCore::isGraphEmpty()
{
    return emptyGraph;
}

void IaitoCore::getOpcodes()
{
    this->opcodes = cmdList("aoml");
    this->regs = cmdList("drp~[1]");
}

void IaitoCore::setSettings()
{
    setConfig("scr.interactive", false);

    setConfig("hex.pairs", false);
    setConfig("asm.xrefs", false);

    setConfig("asm.tabs.once", false);
    setConfig("asm.flags.middle", 2);

    setConfig("anal.hasnext", false);
    setConfig("asm.lines.call", false);

    setConfig("cfg.fortunes.tts", false);
    setConfig("cfg.fortunes.type", "tips");

    // Colors
    setConfig("scr.color", COLOR_MODE_DISABLED);

    // Don't show hits
    setConfig("search.flags", false);
}

QList<RVA> IaitoCore::getSeekHistory()
{
    CORE_LOCK();
    QList<RVA> ret;

    QJsonArray jsonArray = cmdj("sj").array();
    for (const QJsonValue value : jsonArray)
        ret << value.toVariant().toULongLong();

    return ret;
}

QStringList IaitoCore::getAsmPluginNames()
{
    CORE_LOCK();
    RListIter *it;
    QStringList ret;

#if R2_VERSION_NUMBER >= 50809
    RArchPlugin *ap;
    IaitoRListForeach(core->anal->arch->plugins, it, RArchPlugin, ap)
    {
        ret << ap->meta.name;
    }
#elif R2_VERSION_NUMBER >= 50709
    RArchPlugin *ap;
    IaitoRListForeach(core->anal->arch->plugins, it, RArchPlugin, ap)
    {
        ret << ap->name;
    }
#else
    RAsmPlugin *ap;
    IaitoRListForeach(core->rasm->plugins, it, RAsmPlugin, ap)
    {
        ret << ap->name;
    }
#endif

    return ret;
}

QStringList IaitoCore::getAnalPluginNames()
{
    CORE_LOCK();
    RListIter *it;
    QStringList ret;

    RAnalPlugin *ap;
    IaitoRListForeach(core->anal->plugins, it, RAnalPlugin, ap)
    {
#if R2_VERSION_NUMBER >= 50809
        ret << ap->meta.name;
#else
        ret << ap->name;
#endif
    }

    return ret;
}

QStringList IaitoCore::getProjectNames()
{
    CORE_LOCK();
    QStringList ret;

    QJsonArray jsonArray = cmdj("Pj").array();
    for (const QJsonValue value : jsonArray) {
        ret.append(value.toString());
    }

    return ret;
}

QList<RBinPluginDescription> IaitoCore::getRBinPluginDescriptions(const QString &type)
{
    QList<RBinPluginDescription> ret;

    QJsonObject jsonRoot = cmdj("iLj").object();
    for (const QString &key : jsonRoot.keys()) {
        if (!type.isNull() && key != type)
            continue;

        QJsonArray pluginArray = jsonRoot[key].toArray();

        for (const QJsonValue pluginValue : pluginArray) {
            QJsonObject pluginObject = pluginValue.toObject();

            RBinPluginDescription desc;

            desc.name = pluginObject[RJsonKey::name].toString();
            desc.description = pluginObject[RJsonKey::description].toString();
            desc.license = pluginObject[RJsonKey::license].toString();
            desc.type = key;

            ret.append(desc);
        }
    }

    return ret;
}

QList<RIOPluginDescription> IaitoCore::getRIOPluginDescriptions()
{
    QList<RIOPluginDescription> ret;
    QJsonArray plugins = (cmdj("oLj").isArray()) ? cmdj("oLj").array()
                                                 : cmdj("oLj").object()["io_plugins"].toArray();

    if (plugins.size() == 0) {
        R_LOG_ERROR("Cannot find io plugins from r2");
    }
    for (const QJsonValue pluginValue : plugins) {
        QJsonObject pluginObject = pluginValue.toObject();

        RIOPluginDescription plugin;

        plugin.name = pluginObject["name"].toString();

        plugin.description = pluginObject["description"].toString();
        plugin.license = pluginObject["license"].toString();
        plugin.permissions = pluginObject["permissions"].toString();
        for (const auto uri : pluginObject["uris"].toArray()) {
            plugin.uris << uri.toString();
        }

        ret << plugin;
    }

    return ret;
}

QList<RCorePluginDescription> IaitoCore::getRCorePluginDescriptions()
{
    QList<RCorePluginDescription> ret;

    QJsonArray plugins = cmdj("Lcj").array();
    for (const QJsonValue pluginValue : plugins) {
        QJsonObject pluginObject = pluginValue.toObject();

        RCorePluginDescription plugin;

        plugin.name = pluginObject["name"].toString();
        plugin.description = pluginObject["desc"].toString();
        plugin.author = pluginObject["author"].toString();
        plugin.license = pluginObject["license"].toString();

        ret << plugin;
    }

    return ret;
}

QList<RAsmPluginDescription> IaitoCore::getRAsmPluginDescriptions()
{
    CORE_LOCK();
    RListIter *it;
    QList<RAsmPluginDescription> ret;

#if R2_VERSION_NUMBER >= 50809
    RArchPlugin *ap;
    if (core->anal->arch != nullptr) {
        IaitoRListForeach(core->anal->arch->plugins, it, RArchPlugin, ap)
        {
            RAsmPluginDescription plugin;

            plugin.name = ap->meta.name;
            plugin.author = ap->meta.author;
            plugin.version = ap->meta.version;
            plugin.description = ap->meta.desc;
            plugin.license = ap->meta.license;
            plugin.architecture = ap->arch;
            plugin.cpus = ap->cpus;

            ret << plugin;
        }
    }
#elif R2_VERSION_NUMBER >= 50709
    RArchPlugin *ap;
    IaitoRListForeach(core->anal->arch->plugins, it, RArchPlugin, ap)
    {
        RAsmPluginDescription plugin;

        plugin.name = ap->name;
        plugin.architecture = ap->arch;
        plugin.author = ap->author;
        plugin.version = ap->version;
        plugin.cpus = ap->cpus;
        plugin.description = ap->desc;
        plugin.license = ap->license;

        ret << plugin;
    }
#else
    RAsmPlugin *ap;
    IaitoRListForeach(core->rasm->plugins, it, RAsmPlugin, ap)
    {
        RAsmPluginDescription plugin;

        plugin.name = ap->name;
        plugin.architecture = ap->arch;
        plugin.author = ap->author;
        plugin.version = ap->version;
        plugin.cpus = ap->cpus;
        plugin.description = ap->desc;
        plugin.license = ap->license;

        ret << plugin;
    }
#endif

    return ret;
}

QList<RAsmPluginDescription> IaitoCore::getRAnalPluginDescriptions()
{
    CORE_LOCK();
    RListIter *it;
    QList<RAsmPluginDescription> ret;

    RAnalPlugin *ap;
    IaitoRListForeach(core->anal->plugins, it, RAnalPlugin, ap)
    {
        RAsmPluginDescription plugin;
#if R2_VERSION_NUMBER >= 50809
        plugin.name = ap->meta.name;
        plugin.author = ap->meta.author;
        plugin.version = ap->meta.version;
        plugin.description = ap->meta.desc;
        plugin.license = ap->meta.license;
#else
        plugin.name = ap->name;
        plugin.author = ap->author;
        plugin.version = ap->version;
        plugin.description = ap->desc;
        plugin.license = ap->license;
#endif
        // plugin.architecture = ap->arch;
        // plugin.cpus = ap->cpus;

        ret << plugin;
    }

    return ret;
}

QList<FunctionDescription> IaitoCore::getAllFunctions()
{
    CORE_LOCK();

    QList<FunctionDescription> funcList;
    funcList.reserve(r_list_length(core->anal->fcns));

    RListIter *iter;
    RAnalFunction *fcn;
    IaitoRListForeach(core->anal->fcns, iter, RAnalFunction, fcn)
    {
        FunctionDescription function;
        function.offset = fcn->addr;
        function.linearSize = r_anal_function_linear_size(fcn);
        function.realSize = r_anal_function_realsize(fcn);
        function.nargs = r_anal_var_count(core->anal, fcn, 'b', 1)
                         + r_anal_var_count(core->anal, fcn, 'r', 1)
                         + r_anal_var_count(core->anal, fcn, 's', 1);
        function.nlocals = r_anal_var_count(core->anal, fcn, 'b', 0)
                           + r_anal_var_count(core->anal, fcn, 'r', 0)
                           + r_anal_var_count(core->anal, fcn, 's', 0);
        function.nbbs = r_list_length(fcn->bbs);
        function.calltype = fcn->cc ? QString::fromUtf8(fcn->cc) : QString();
        function.name = fcn->name ? QString::fromUtf8(fcn->name) : QString();
        function.edges = r_anal_function_count_edges(fcn, nullptr);
        function.stackframe = fcn->maxstack;
        funcList.append(function);
    }

    return funcList;
}

QList<ImportDescription> IaitoCore::getAllImports()
{
    CORE_LOCK();
    QList<ImportDescription> ret;

#if 0
    QJsonArray importsArray = cmdj("iij").array();

    for (const QJsonValue value : importsArray) {
        QJsonObject importObject = value.toObject();

        ImportDescription import;

        import.plt = importObject[RJsonKey::plt].toVariant().toULongLong();
        import.ordinal = importObject[RJsonKey::ordinal].toInt();
        import.bind = importObject[RJsonKey::bind].toString();
        import.type = importObject[RJsonKey::type].toString();
        import.libname = importObject[RJsonKey::libname].toString();
        import.name = importObject[RJsonKey::name].toString();

        ret << import;
    }
#else
    RBinImport *bi;
    RListIter *it;
    const RList *imports = r_bin_get_imports(core->bin);
    // IaitoRListForeach(core->bin->cur->BO->imports, it, RBinImport, bi)
    IaitoRListForeach(imports, it, RBinImport, bi)
    {
        QString type = QString(bi->bind) + " " + QString(bi->type);
        ImportDescription imp;
        const char *name = r_bin_name_tostring(bi->name);
        char *fname = r_str_newf("sym.imp.%s", name);
        RFlagItem *fi = r_flag_get(core->flags, fname);
        if (!fi) {
            free(fname);
            fname = r_str_newf("reloc.%s", name);
            fi = r_flag_get(core->flags, fname);
        }
        free(fname);
        ut64 addr = fi ? fi->offset : 0;
        imp.plt = addr;
        imp.name = QString(name);
        imp.bind = QString(bi->bind);
        imp.type = QString(bi->type);
        ret << imp;
    }
#endif

    return ret;
}

QList<ExportDescription> IaitoCore::getAllExports()
{
    CORE_LOCK();
    QList<ExportDescription> ret;

    QJsonArray exportsArray = cmdj("iEj").array();

    for (const QJsonValue value : exportsArray) {
        QJsonObject exportObject = value.toObject();

        ExportDescription exp;

        exp.vaddr = exportObject[RJsonKey::vaddr].toVariant().toULongLong();
        exp.paddr = exportObject[RJsonKey::paddr].toVariant().toULongLong();
        exp.size = exportObject[RJsonKey::size].toVariant().toULongLong();
        exp.type = exportObject[RJsonKey::type].toString();
        exp.name = exportObject[RJsonKey::name].toString();
        exp.flag_name = exportObject[RJsonKey::flagname].toString();

        ret << exp;
    }

    return ret;
}

QList<SymbolDescription> IaitoCore::getAllSymbols()
{
    CORE_LOCK();
    RListIter *it;

    QList<SymbolDescription> ret;

    RBinSymbol *bs;
    if (core && core->bin && core->bin->cur && core->bin->cur->BO) {
        IaitoRListForeach(core->bin->cur->BO->symbols, it, RBinSymbol, bs)
        {
            QString type = QString(bs->bind) + " " + QString(bs->type);
            SymbolDescription symbol;
            symbol.vaddr = bs->vaddr;
            symbol.name = QString(r_bin_name_tostring(bs->name));
            symbol.bind = QString(bs->bind);
            symbol.type = QString(bs->type);
            ret << symbol;
        }

        /* list entrypoints as symbols too */
        int n = 0;
        RBinAddr *entry;
        IaitoRListForeach(core->bin->cur->BO->entries, it, RBinAddr, entry)
        {
            SymbolDescription symbol;
            symbol.vaddr = entry->vaddr;
            symbol.name = QStringLiteral("entry") + QString::number(n++);
            symbol.bind.clear();
            symbol.type = "entry";
            ret << symbol;
        }
    }

    return ret;
}

QList<HeaderDescription> IaitoCore::getAllHeaders()
{
    CORE_LOCK();
    QList<HeaderDescription> ret;

    QJsonArray headersArray = cmdj("ihj").array();

    for (const QJsonValue value : headersArray) {
        QJsonObject headerObject = value.toObject();

        HeaderDescription header;

        header.vaddr = headerObject[RJsonKey::vaddr].toVariant().toULongLong();
        header.paddr = headerObject[RJsonKey::paddr].toVariant().toULongLong();
        header.value = headerObject[RJsonKey::comment].toString();
        header.name = headerObject[RJsonKey::name].toString();

        ret << header;
    }

    return ret;
}

QList<ZignatureDescription> IaitoCore::getAllZignatures()
{
    CORE_LOCK();
    QList<ZignatureDescription> zignatures;

    QJsonArray zignaturesArray = cmdj("zj").array();

    for (const QJsonValue value : zignaturesArray) {
        QJsonObject zignatureObject = value.toObject();

        ZignatureDescription zignature;

        zignature.name = zignatureObject[RJsonKey::name].toString();
        zignature.bytes = zignatureObject[RJsonKey::bytes].toString();
        zignature.offset = zignatureObject[RJsonKey::offset].toVariant().toULongLong();
        for (const QJsonValue ref : zignatureObject[RJsonKey::refs].toArray()) {
            zignature.refs << ref.toString();
        }

        QJsonObject graphObject = zignatureObject[RJsonKey::graph].toObject();
        zignature.cc = graphObject[RJsonKey::cc].toVariant().toULongLong();
        zignature.nbbs = graphObject[RJsonKey::nbbs].toVariant().toULongLong();
        zignature.edges = graphObject[RJsonKey::edges].toVariant().toULongLong();
        zignature.ebbs = graphObject[RJsonKey::ebbs].toVariant().toULongLong();

        zignatures << zignature;
    }

    return zignatures;
}

QList<CommentDescription> IaitoCore::getAllComments(const QString &filterType)
{
    CORE_LOCK();
    QList<CommentDescription> ret;

    QJsonArray commentsArray = cmdj("CCj").array();
    for (const QJsonValue value : commentsArray) {
        QJsonObject commentObject = value.toObject();

        QString type = commentObject[RJsonKey::type].toString();
        if (type != filterType)
            continue;

        CommentDescription comment;
        comment.offset = commentObject[RJsonKey::offset].toVariant().toULongLong();
        comment.name = commentObject[RJsonKey::name].toString();

        ret << comment;
    }
    return ret;
}

QList<RelocDescription> IaitoCore::getAllRelocs()
{
    CORE_LOCK();
    QList<RelocDescription> ret;

    if (core && core->bin && core->bin->cur && core->bin->cur->BO) {
        RBinReloc *br;
#if R2_VERSION_NUMBER >= 50609
        auto relocs = core->bin->cur->BO->relocs;
        ////  RBIter iter;
        RRBNode *iter;
        r_crbtree_foreach(relocs, iter, RBinReloc, br)
        {
            RelocDescription reloc;

            reloc.vaddr = br->vaddr;
            reloc.paddr = br->paddr;
            reloc.type = (br->additive ? "ADD_" : "SET_") + QString::number(br->type);

            if (br->import)
                reloc.name = r_bin_name_tostring(br->import->name);
            else
                reloc.name = QStringLiteral("reloc_%1").arg(QString::number(br->vaddr, 16));

            ret << reloc;
        }
#elif R2_VERSION_NUMBER > 50500
        RListIter *iter;
        RList *list = r_bin_get_relocs_list(core->bin);
        void *_br;
        r_list_foreach(list, iter, _br)
        {
            br = (RBinReloc *) _br;
            RelocDescription reloc;

            reloc.vaddr = br->vaddr;
            reloc.paddr = br->paddr;
            reloc.type = (br->additive ? "ADD_" : "SET_") + QString::number(br->type);

            if (br->import)
                reloc.name = br->import->name;
            else
                reloc.name = QStringLiteral("reloc_%1").arg(QString::number(br->vaddr, 16));

            ret << reloc;
        }
#else
        auto relocs = core->bin->cur->BO->relocs;
        RBIter iter;
        r_rbtree_foreach(relocs, iter, br, RBinReloc, vrb)
        {
            RelocDescription reloc;

            reloc.vaddr = br->vaddr;
            reloc.paddr = br->paddr;
            reloc.type = (br->additive ? "ADD_" : "SET_") + QString::number(br->type);

            if (br->import)
                reloc.name = br->import->name;
            else
                reloc.name = QStringLiteral("reloc_%1").arg(QString::number(br->vaddr, 16));

            ret << reloc;
        }
#endif
    }

    return ret;
}

QList<StringDescription> IaitoCore::getAllStrings()
{
    return parseStringsJson(cmdjTask("izzj"));
}

QList<StringDescription> IaitoCore::parseStringsJson(const QJsonDocument &doc)
{
    QList<StringDescription> ret;

    QJsonArray stringsArray = doc.array();
    for (const QJsonValue value : stringsArray) {
        QJsonObject stringObject = value.toObject();

        StringDescription string;

        string.string = stringObject[RJsonKey::string].toString();
        string.vaddr = stringObject[RJsonKey::vaddr].toVariant().toULongLong();
        string.type = stringObject[RJsonKey::type].toString();
        string.size = stringObject[RJsonKey::size].toVariant().toUInt();
        string.length = stringObject[RJsonKey::length].toVariant().toUInt();
        string.section = stringObject[RJsonKey::section].toString();

        ret << string;
    }

    return ret;
}

QList<FlagspaceDescription> IaitoCore::getAllFlagspaces()
{
    CORE_LOCK();
    QList<FlagspaceDescription> ret;

    QJsonArray flagspacesArray = cmdj("fsj").array();
    for (const QJsonValue value : flagspacesArray) {
        QJsonObject flagspaceObject = value.toObject();

        FlagspaceDescription flagspace;

        flagspace.name = flagspaceObject[RJsonKey::name].toString();

        ret << flagspace;
    }
    return ret;
}

QList<FlagDescription> IaitoCore::getAllFlags(QString flagspace)
{
    CORE_LOCK();
    QList<FlagDescription> ret;

    if (!flagspace.isEmpty())
        cmdRaw("fs " + flagspace);
    else
        cmdRaw("fs *");

    QJsonArray flagsArray = cmdj("fj").array();
    for (const QJsonValue value : flagsArray) {
        QJsonObject flagObject = value.toObject();

        FlagDescription flag;

        flag.offset = flagObject[RJsonKey::offset].toVariant().toULongLong();
        flag.size = flagObject[RJsonKey::size].toVariant().toULongLong();
        flag.name = flagObject[RJsonKey::name].toString();
        flag.realname = flagObject[RJsonKey::realname].toString();

        ret << flag;
    }
    return ret;
}

QList<SectionDescription> IaitoCore::getAllSections()
{
    CORE_LOCK();
    QList<SectionDescription> sections;

    QJsonDocument sectionsDoc = cmdj("iSj entropy");
    QJsonObject sectionsObj = sectionsDoc.object();
    QJsonArray sectionsArray = sectionsObj[RJsonKey::sections].toArray();

    for (const QJsonValue value : sectionsArray) {
        QJsonObject sectionObject = value.toObject();

        QString name = sectionObject[RJsonKey::name].toString();
        if (name.isEmpty())
            continue;

        SectionDescription section;
        section.name = name;
        section.vaddr = sectionObject[RJsonKey::vaddr].toVariant().toULongLong();
        section.vsize = sectionObject[RJsonKey::vsize].toVariant().toULongLong();
        section.paddr = sectionObject[RJsonKey::paddr].toVariant().toULongLong();
        section.size = sectionObject[RJsonKey::size].toVariant().toULongLong();
        section.perm = sectionObject[RJsonKey::perm].toString();
        section.entropy = sectionObject[RJsonKey::entropy].toString();

        sections << section;
    }
    return sections;
}

QStringList IaitoCore::getSectionList()
{
    CORE_LOCK();
    QStringList ret;

    QJsonArray sectionsArray = cmdj("iSj").array();
    for (const QJsonValue value : sectionsArray) {
        ret << value.toObject()[RJsonKey::name].toString();
    }
    return ret;
}

QList<SegmentDescription> IaitoCore::getAllSegments()
{
    CORE_LOCK();
    QList<SegmentDescription> ret;

    QJsonArray segments = cmdj("iSSj").array();

    for (const QJsonValue value : segments) {
        QJsonObject segmentObject = value.toObject();

        QString name = segmentObject[RJsonKey::name].toString();
        if (name.isEmpty())
            continue;

        SegmentDescription segment;
        segment.name = name;
        segment.vaddr = segmentObject[RJsonKey::vaddr].toVariant().toULongLong();
        segment.paddr = segmentObject[RJsonKey::paddr].toVariant().toULongLong();
        segment.size = segmentObject[RJsonKey::size].toVariant().toULongLong();
        segment.vsize = segmentObject[RJsonKey::vsize].toVariant().toULongLong();
        segment.perm = segmentObject[RJsonKey::perm].toString();

        ret << segment;
    }
    return ret;
}

QList<EntrypointDescription> IaitoCore::getAllEntrypoint()
{
    CORE_LOCK();
    QList<EntrypointDescription> ret;

    QJsonArray entrypointsArray = cmdj("iej").array();
    for (const QJsonValue value : entrypointsArray) {
        QJsonObject entrypointObject = value.toObject();

        EntrypointDescription entrypoint;

        entrypoint.vaddr = entrypointObject[RJsonKey::vaddr].toVariant().toULongLong();
        entrypoint.paddr = entrypointObject[RJsonKey::paddr].toVariant().toULongLong();
        entrypoint.baddr = entrypointObject[RJsonKey::baddr].toVariant().toULongLong();
        entrypoint.laddr = entrypointObject[RJsonKey::laddr].toVariant().toULongLong();
        entrypoint.haddr = entrypointObject[RJsonKey::haddr].toVariant().toULongLong();
        entrypoint.type = entrypointObject[RJsonKey::type].toString();

        ret << entrypoint;
    }
    return ret;
}

QList<BinClassDescription> IaitoCore::getAllClassesFromBin()
{
    CORE_LOCK();
    QList<BinClassDescription> ret;

    QJsonArray classesArray = cmdj("icj").array();
    for (const QJsonValue value : classesArray) {
        QJsonObject classObject = value.toObject();

        BinClassDescription cls;

        cls.name = classObject[RJsonKey::classname].toString();
        cls.addr = classObject[RJsonKey::addr].toVariant().toULongLong();
        cls.index = classObject[RJsonKey::index].toVariant().toULongLong();

        for (const QJsonValue value2 : classObject[RJsonKey::methods].toArray()) {
            QJsonObject methObject = value2.toObject();

            BinClassMethodDescription meth;

            meth.name = methObject[RJsonKey::name].toString();
            meth.addr = methObject[RJsonKey::addr].toVariant().toULongLong();

            cls.methods << meth;
        }

        for (const QJsonValue value2 : classObject[RJsonKey::fields].toArray()) {
            QJsonObject fieldObject = value2.toObject();

            BinClassFieldDescription field;

            field.name = fieldObject[RJsonKey::name].toString();
            field.addr = fieldObject[RJsonKey::addr].toVariant().toULongLong();

            cls.fields << field;
        }

        ret << cls;
    }
    return ret;
}

QList<BinClassDescription> IaitoCore::getAllClassesFromFlags()
{
    static const QRegularExpression classFlagRegExp("^class\\.(.*)$");
    static const QRegularExpression methodFlagRegExp("^method\\.([^\\.]*)\\.(.*)$");

    CORE_LOCK();
    QList<BinClassDescription> ret;
    QMap<QString, BinClassDescription *> classesCache;

    QJsonArray flagsArray = cmdj("fj@F:classes").array();
    for (const QJsonValue value : flagsArray) {
        QJsonObject flagObject = value.toObject();

        QString flagName = flagObject[RJsonKey::name].toString();

        QRegularExpressionMatch match = classFlagRegExp.match(flagName);
        if (match.hasMatch()) {
            QString className = match.captured(1);
            BinClassDescription *desc = nullptr;
            auto it = classesCache.find(className);
            if (it == classesCache.end()) {
                BinClassDescription cls = {};
                ret << cls;
                desc = &ret.last();
                classesCache[className] = desc;
            } else {
                desc = it.value();
            }
            desc->name = match.captured(1);
            desc->addr = flagObject[RJsonKey::offset].toVariant().toULongLong();
            desc->index = RVA_INVALID;
            continue;
        }

        match = methodFlagRegExp.match(flagName);
        if (match.hasMatch()) {
            QString className = match.captured(1);
            BinClassDescription *classDesc = nullptr;
            auto it = classesCache.find(className);
            if (it == classesCache.end()) {
                // add a new stub class, will be replaced if class flag comes
                // after it
                BinClassDescription cls;
                cls.name = tr("Unknown (%1)").arg(className);
                cls.addr = RVA_INVALID;
                cls.index = 0;
                ret << cls;
                classDesc = &ret.last();
                classesCache[className] = classDesc;
            } else {
                classDesc = it.value();
            }

            BinClassMethodDescription meth;
            meth.name = match.captured(2);
            meth.addr = flagObject[RJsonKey::offset].toVariant().toULongLong();
            classDesc->methods << meth;
            continue;
        }
    }
    return ret;
}

QList<QString> IaitoCore::getAllAnalClasses(bool sorted)
{
    CORE_LOCK();
    QList<QString> ret;

    SdbListPtr l = makeSdbListPtr(r_anal_class_get_all(core->anal, sorted));
    if (!l) {
        return ret;
    }
    ret.reserve(static_cast<int>(l->length));

    SdbListIter *it;
    void *entry;
    ls_foreach(l, it, entry)
    {
        auto kv = reinterpret_cast<SdbKv *>(entry);
        ret.append(QString::fromUtf8(reinterpret_cast<const char *>(kv->base.key)));
    }

    return ret;
}

QList<AnalMethodDescription> IaitoCore::getAnalClassMethods(const QString &cls)
{
    CORE_LOCK();
    QList<AnalMethodDescription> ret;

    RVector *meths = r_anal_class_method_get_all(core->anal, cls.toUtf8().constData());
    if (!meths) {
        return ret;
    }

    ret.reserve(static_cast<int>(meths->len));
    RAnalMethod *meth;
    IaitoRVectorForeach(meths, meth, RAnalMethod)
    {
        AnalMethodDescription desc;
        desc.name = QString::fromUtf8(meth->name);
        desc.addr = meth->addr;
        desc.vtableOffset = meth->vtable_offset;
        ret.append(desc);
    }
    r_vector_free(meths);

    return ret;
}

QList<AnalBaseClassDescription> IaitoCore::getAnalClassBaseClasses(const QString &cls)
{
    CORE_LOCK();
    QList<AnalBaseClassDescription> ret;

    RVector *bases = r_anal_class_base_get_all(core->anal, cls.toUtf8().constData());
    if (!bases) {
        return ret;
    }

    ret.reserve(static_cast<int>(bases->len));
    RAnalBaseClass *base;
    IaitoRVectorForeach(bases, base, RAnalBaseClass)
    {
        AnalBaseClassDescription desc;
        desc.id = QString::fromUtf8(base->id);
        desc.offset = base->offset;
        desc.className = QString::fromUtf8(base->class_name);
        ret.append(desc);
    }
    r_vector_free(bases);

    return ret;
}

QList<AnalVTableDescription> IaitoCore::getAnalClassVTables(const QString &cls)
{
    CORE_LOCK();
    QList<AnalVTableDescription> acVtables;

    RVector *vtables = r_anal_class_vtable_get_all(core->anal, cls.toUtf8().constData());
    if (!vtables) {
        return acVtables;
    }

    acVtables.reserve(static_cast<int>(vtables->len));
    RAnalVTable *vtable;
    IaitoRVectorForeach(vtables, vtable, RAnalVTable)
    {
        AnalVTableDescription desc;
        desc.id = QString::fromUtf8(vtable->id);
        desc.offset = vtable->offset;
        desc.addr = vtable->addr;
        acVtables.append(desc);
    }
    r_vector_free(vtables);

    return acVtables;
}

void IaitoCore::createNewClass(const QString &cls)
{
    CORE_LOCK();
    r_anal_class_create(core->anal, cls.toUtf8().constData());
}

void IaitoCore::renameClass(const QString &oldName, const QString &newName)
{
    CORE_LOCK();
    r_anal_class_rename(core->anal, oldName.toUtf8().constData(), newName.toUtf8().constData());
}

void IaitoCore::deleteClass(const QString &cls)
{
    CORE_LOCK();
    r_anal_class_delete(core->anal, cls.toUtf8().constData());
}

bool IaitoCore::getAnalMethod(const QString &cls, const QString &meth, AnalMethodDescription *desc)
{
    CORE_LOCK();
    RAnalMethod analMeth;
    if (r_anal_class_method_get(
            core->anal, cls.toUtf8().constData(), meth.toUtf8().constData(), &analMeth)
        != R_ANAL_CLASS_ERR_SUCCESS) {
        return false;
    }
    desc->name = QString::fromUtf8(analMeth.name);
    desc->addr = analMeth.addr;
    desc->vtableOffset = analMeth.vtable_offset;
    r_anal_class_method_fini(&analMeth);
    return true;
}

void IaitoCore::setAnalMethod(const QString &className, const AnalMethodDescription &meth)
{
    CORE_LOCK();
    RAnalMethod analMeth;
    analMeth.name = strdup(meth.name.toUtf8().constData());
    analMeth.addr = meth.addr;
    analMeth.vtable_offset = meth.vtableOffset;
    r_anal_class_method_set(core->anal, className.toUtf8().constData(), &analMeth);
    r_anal_class_method_fini(&analMeth);
}

void IaitoCore::renameAnalMethod(
    const QString &className, const QString &oldMethodName, const QString &newMethodName)
{
    CORE_LOCK();
    r_anal_class_method_rename(
        core->anal,
        className.toUtf8().constData(),
        oldMethodName.toUtf8().constData(),
        newMethodName.toUtf8().constData());
}

QList<ResourcesDescription> IaitoCore::getAllResources()
{
    CORE_LOCK();
    QList<ResourcesDescription> resources;

    QJsonArray resourcesArray = cmdj("iRj").array();
    for (const QJsonValue value : resourcesArray) {
        QJsonObject resourceObject = value.toObject();

        ResourcesDescription res;

        res.name = resourceObject[RJsonKey::name].toString();
        res.vaddr = resourceObject[RJsonKey::vaddr].toVariant().toULongLong();
        res.index = resourceObject[RJsonKey::index].toVariant().toULongLong();
        res.type = resourceObject[RJsonKey::type].toString();
        res.size = resourceObject[RJsonKey::size].toVariant().toULongLong();
        res.lang = resourceObject[RJsonKey::lang].toString();

        resources << res;
    }
    return resources;
}

QList<VTableDescription> IaitoCore::getAllVTables()
{
    CORE_LOCK();
    QList<VTableDescription> vtables;

    QJsonArray vTablesArray = cmdj("avj").array();
    for (const QJsonValue vTableValue : vTablesArray) {
        QJsonObject vTableObject = vTableValue.toObject();

        VTableDescription res;

        res.addr = vTableObject[RJsonKey::offset].toVariant().toULongLong();
        QJsonArray methodArray = vTableObject[RJsonKey::methods].toArray();

        for (const QJsonValue methodValue : methodArray) {
            QJsonObject methodObject = methodValue.toObject();

            BinClassMethodDescription method;

            method.addr = methodObject[RJsonKey::offset].toVariant().toULongLong();
            method.name = methodObject[RJsonKey::name].toString();

            res.methods << method;
        }

        vtables << res;
    }
    return vtables;
}

QList<TypeDescription> IaitoCore::getAllTypes()
{
    QList<TypeDescription> types;

    types.append(getAllPrimitiveTypes());
    types.append(getAllUnions());
    types.append(getAllStructs());
    types.append(getAllEnums());
    types.append(getAllTypedefs());

    return types;
}

QList<TypeDescription> IaitoCore::getAllPrimitiveTypes()
{
    CORE_LOCK();
    QList<TypeDescription> primitiveTypes;

    QJsonArray typesArray = cmdj("tj").array();
    for (const QJsonValue value : typesArray) {
        QJsonObject typeObject = value.toObject();

        TypeDescription exp;

        exp.type = typeObject[RJsonKey::type].toString();
        exp.size = typeObject[RJsonKey::size].toVariant().toULongLong();
        exp.format = typeObject[RJsonKey::format].toString();
        exp.category = tr("Primitive");
        primitiveTypes << exp;
    }

    return primitiveTypes;
}

QList<TypeDescription> IaitoCore::getAllUnions()
{
    CORE_LOCK();
    QList<TypeDescription> unions;

    QJsonArray typesArray = cmdj("tuj").array();
    for (const QJsonValue value : typesArray) {
        QJsonObject typeObject = value.toObject();

        TypeDescription exp;

        exp.type = typeObject[RJsonKey::type].toString();
        exp.size = typeObject[RJsonKey::size].toVariant().toULongLong();
        exp.category = "Union";
        unions << exp;
    }

    return unions;
}

QList<TypeDescription> IaitoCore::getAllStructs()
{
    CORE_LOCK();
    QList<TypeDescription> structs;

    QJsonArray typesArray = cmdj("tsj").array();
    for (const QJsonValue value : typesArray) {
        QJsonObject typeObject = value.toObject();

        TypeDescription exp;

        exp.type = typeObject[RJsonKey::type].toString();
        exp.size = typeObject[RJsonKey::size].toVariant().toULongLong();
        exp.category = "Struct";
        structs << exp;
    }

    return structs;
}

QList<TypeDescription> IaitoCore::getAllEnums()
{
    CORE_LOCK();
    QList<TypeDescription> enums;

    QJsonObject typesObject = cmdj("tej").object();
    for (QString key : typesObject.keys()) {
        TypeDescription exp;
        exp.type = key;
        exp.size = 0;
        exp.category = "Enum";
        enums << exp;
    }

    return enums;
}

QList<TypeDescription> IaitoCore::getAllTypedefs()
{
    CORE_LOCK();
    QList<TypeDescription> typeDefs;

    QJsonObject typesObject = cmdj("ttj").object();
    for (QString key : typesObject.keys()) {
        TypeDescription exp;
        exp.type = key;
        exp.size = 0;
        exp.category = "Typedef";
        typeDefs << exp;
    }

    return typeDefs;
}

QString IaitoCore::addTypes(const char *str)
{
    CORE_LOCK();
    char *error_msg = nullptr;
#if R2_VERSION_NUMBER >= 50709
    char *parsed = r_anal_cparse(core->anal, str, &error_msg);
#else
    char *parsed = r_parse_c_string(core->anal, str, &error_msg);
#endif
    QString error;

    if (!parsed) {
        if (error_msg) {
            error = error_msg;
            r_mem_free(error_msg);
        }
        return error;
    }

    r_anal_save_parsed_type(core->anal, parsed);
    r_mem_free(parsed);

    if (error_msg) {
        error = error_msg;
        r_mem_free(error_msg);
    }

    return error;
}

QString IaitoCore::getTypeAsC(QString name, QString category)
{
    CORE_LOCK();
    QString output = "Failed to fetch the output.";
    if (name.isEmpty() || category.isEmpty()) {
        return output;
    }
    QString typeName = sanitizeStringForCommand(name);
    if (category == "Struct") {
        output = cmdRaw(QStringLiteral("tsc %1").arg(typeName));
    } else if (category == "Union") {
        output = cmdRaw(QStringLiteral("tuc %1").arg(typeName));
    } else if (category == "Enum") {
        output = cmdRaw(QStringLiteral("tec %1").arg(typeName));
    } else if (category == "Typedef") {
        output = cmdRaw(QStringLiteral("ttc %1").arg(typeName));
    }
    return output;
}

bool IaitoCore::isAddressMapped(RVA addr)
{
    // If value returned by "om. @ addr" is empty means that address is not
    // mapped
    return !Core()->cmdRawAt(QStringLiteral("om."), addr).isEmpty();
}

QList<SearchDescription> IaitoCore::getAllSearch(QString search_for, QString space)
{
    CORE_LOCK();
    QList<SearchDescription> searchRef;

    QJsonArray searchArray = cmdj(space + QStringLiteral(" ") + search_for).array();

    if (space == "/Rj") {
        for (const QJsonValue value : searchArray) {
            QJsonObject searchObject = value.toObject();

            SearchDescription exp;

            exp.code.clear();
            for (const QJsonValue value2 : searchObject[RJsonKey::opcodes].toArray()) {
                QJsonObject gadget = value2.toObject();
                exp.code += gadget[RJsonKey::opcode].toString() + ";  ";
            }

            exp.offset = searchObject[RJsonKey::opcodes]
                             .toArray()
                             .first()
                             .toObject()[RJsonKey::offset]
                             .toVariant()
                             .toULongLong();
            exp.size = searchObject[RJsonKey::size].toVariant().toULongLong();

            searchRef << exp;
        }
    } else {
        for (const QJsonValue value : searchArray) {
            QJsonObject searchObject = value.toObject();

            SearchDescription exp;

            exp.offset = searchObject[RJsonKey::offset].toVariant().toULongLong();
            exp.size = searchObject[RJsonKey::len].toVariant().toULongLong();
            exp.code = searchObject[RJsonKey::code].toString();
            exp.data = searchObject[RJsonKey::data].toString();

            searchRef << exp;
        }
    }
    return searchRef;
}

BlockStatistics IaitoCore::getBlockStatistics(unsigned int blocksCount)
{
    BlockStatistics blockStats;
    if (blocksCount == 0) {
        blockStats.from = blockStats.to = blockStats.blocksize = 0;
        return blockStats;
    }

    QJsonObject statsObj;

    // User TempConfig here to set the search boundaries to all sections. This
    // makes sure that the Visual Navbar will show all the relevant addresses.
    {
        TempConfig tempConfig;
        tempConfig.set("search.in", "bin.sections");
        statsObj = cmdj("p-j " + QString::number(blocksCount)).object();
    }

    blockStats.from = statsObj[RJsonKey::from].toVariant().toULongLong();
    blockStats.to = statsObj[RJsonKey::to].toVariant().toULongLong();
    blockStats.blocksize = statsObj[RJsonKey::blocksize].toVariant().toULongLong();

    QJsonArray blocksArray = statsObj[RJsonKey::blocks].toArray();

    for (const QJsonValue value : blocksArray) {
        QJsonObject blockObj = value.toObject();

        BlockDescription block;

        block.addr = blockObj[RJsonKey::offset].toVariant().toULongLong();
        block.size = blockObj[RJsonKey::size].toVariant().toULongLong();
        block.flags = blockObj[RJsonKey::flags].toInt(0);
        block.functions = blockObj[RJsonKey::functions].toInt(0);
        block.inFunctions = blockObj[RJsonKey::in_functions].toInt(0);
        block.comments = blockObj[RJsonKey::comments].toInt(0);
        block.symbols = blockObj[RJsonKey::symbols].toInt(0);
        block.strings = blockObj[RJsonKey::strings].toInt(0);

        block.rwx = 0;
        QString rwxStr = blockObj[RJsonKey::rwx].toString();
        if (rwxStr.length() == 3) {
            if (rwxStr[0] == 'r') {
                block.rwx |= (1 << 0);
            }
            if (rwxStr[1] == 'w') {
                block.rwx |= (1 << 1);
            }
            if (rwxStr[2] == 'x') {
                block.rwx |= (1 << 2);
            }
        }

        blockStats.blocks << block;
    }

    return blockStats;
}

QList<XrefDescription> IaitoCore::getXRefsForVariable(
    QString variableName, bool findWrites, RVA offset)
{
    QList<XrefDescription> xrefList = QList<XrefDescription>();
    QJsonArray xrefsArray;
    if (findWrites) {
        xrefsArray = cmdjAt("afvWj", offset).array();
    } else {
        xrefsArray = cmdjAt("afvRj", offset).array();
    }
    for (const QJsonValue value : xrefsArray) {
        QJsonObject xrefObject = value.toObject();
        QString name = xrefObject[RJsonKey::name].toString();
        if (name == variableName) {
            QJsonArray addressArray = xrefObject[RJsonKey::addrs].toArray();
            for (const QJsonValue address : addressArray) {
                XrefDescription xref;
                RVA addr = address.toVariant().toULongLong();
                xref.from = addr;
                xref.to = addr;
                if (findWrites) {
                    xref.from_str = RAddressString(addr);
                } else {
                    xref.to_str = RAddressString(addr);
                }
                xrefList << xref;
            }
        }
    }
    return xrefList;
}

QList<XrefDescription> IaitoCore::getXRefs(
    RVA addr, bool to, bool whole_function, const QString &filterType)
{
    QList<XrefDescription> xrefList = QList<XrefDescription>();

    QJsonArray xrefsArray;

    if (to) {
        xrefsArray = cmdj("axtj@" + QString::number(addr)).array();
    } else {
        xrefsArray = cmdj("axfj@" + QString::number(addr)).array();
    }

    for (const QJsonValue value : xrefsArray) {
        QJsonObject xrefObject = value.toObject();

        XrefDescription xref;

        xref.type = xrefObject[RJsonKey::type].toString();

        if (!filterType.isNull() && filterType != xref.type)
            continue;

        xref.from = xrefObject[RJsonKey::from].toVariant().toULongLong();
        if (!to) {
            xref.from_str = RAddressString(xref.from);
        } else {
            QString fcn = xrefObject[RJsonKey::fcn_name].toString();
            if (!fcn.isEmpty()) {
                RVA fcnAddr = xrefObject[RJsonKey::fcn_addr].toVariant().toULongLong();
                xref.from_str = fcn + " + 0x" + QString::number(xref.from - fcnAddr, 16);
            } else {
                xref.from_str = RAddressString(xref.from);
            }
        }

        if (!whole_function && !to && xref.from != addr) {
            continue;
        }

        if (to && !xrefObject.contains(RJsonKey::to)) {
            xref.to = addr;
        } else {
            xref.to = xrefObject[RJsonKey::to].toVariant().toULongLong();
        }
        xref.to_str = Core()->cmdRaw(QStringLiteral("fd %1").arg(xref.to)).trimmed();

        xrefList << xref;
    }

    return xrefList;
}

void IaitoCore::addFlag(RVA offset, QString name, RVA size, QString color, QString comment)
{
    CORE_LOCK();
    name = sanitizeStringForCommand(name);
    auto fi = r_flag_set(this->core()->flags, name.toStdString().c_str(), offset, size);
    if (fi) {
        if (color != "") {
            r_flag_item_set_color(fi, color.toStdString().c_str());
        }
        if (comment != "") {
            r_flag_item_set_comment(fi, comment.toStdString().c_str());
        }
    }
    emit flagsChanged();
}

/**
 * @brief Gets all the flags present at a specific address
 * @param addr The address to be checked
 * @return String containing all the flags which are comma-separated
 */
QString IaitoCore::listFlagsAsStringAt(RVA addr)
{
    CORE_LOCK();
    char *flagList = r_flag_get_liststr(core->flags, addr);
    QString result = fromOwnedCharPtr(flagList);
    return result;
}

QString IaitoCore::nearestFlag(RVA offset, RVA *flagOffsetOut)
{
    auto r = cmdj(QStringLiteral("fdj @") + QString::number(offset)).object();
    QString name = r.value("name").toString();
    if (flagOffsetOut) {
        int queryOffset = r.value("offset").toInt(0);
        *flagOffsetOut = offset + static_cast<RVA>(-queryOffset);
    }
    return name;
}

void IaitoCore::handleREvent(int type, void *data)
{
    switch (type) {
    case R_EVENT_CLASS_NEW: {
        auto ev = reinterpret_cast<REventClass *>(data);
        emit classNew(QString::fromUtf8(ev->name));
        break;
    }
    case R_EVENT_CLASS_DEL: {
        auto ev = reinterpret_cast<REventClass *>(data);
        emit classDeleted(QString::fromUtf8(ev->name));
        break;
    }
    case R_EVENT_CLASS_RENAME: {
        auto ev = reinterpret_cast<REventClassRename *>(data);
        emit classRenamed(QString::fromUtf8(ev->name_old), QString::fromUtf8(ev->name_new));
        break;
    }
    case R_EVENT_CLASS_ATTR_SET: {
        auto ev = reinterpret_cast<REventClassAttrSet *>(data);
        emit classAttrsChanged(QString::fromUtf8(ev->attr.class_name));
        break;
    }
    case R_EVENT_CLASS_ATTR_DEL: {
        auto ev = reinterpret_cast<REventClassAttr *>(data);
        emit classAttrsChanged(QString::fromUtf8(ev->class_name));
        break;
    }
    case R_EVENT_CLASS_ATTR_RENAME: {
        auto ev = reinterpret_cast<REventClassAttrRename *>(data);
        emit classAttrsChanged(QString::fromUtf8(ev->attr.class_name));
        break;
    }
    case R_EVENT_DEBUG_PROCESS_FINISHED: {
        auto ev = reinterpret_cast<REventDebugProcessFinished *>(data);
        emit debugProcessFinished(ev->pid);
        break;
    }
    default:
        break;
    }
}

void IaitoCore::triggerFlagsChanged()
{
    emit flagsChanged();
}

void IaitoCore::triggerVarsChanged()
{
    emit varsChanged();
}

void IaitoCore::triggerFunctionRenamed(const RVA offset, const QString &newName)
{
    emit functionRenamed(offset, newName);
}

void IaitoCore::loadPDB(const QString &file)
{
    cmdRaw0(QStringLiteral("idp ") + sanitizeStringForCommand(file));
}

void IaitoCore::openProject(const QString &name)
{
    bool ok = cmdRaw0(QStringLiteral("'P ") + name); //  + "@e:scr.interactive=false");
    if (ok) {
        notes = QString::fromUtf8(QByteArray::fromBase64(cmdRaw("Pnj").toUtf8()));
    } else {
        QMessageBox::critical(
            nullptr, tr("Error"), tr("Cannot open project. See console for details"));
    }
    // QString notes =
    // QString::fromUtf8(QByteArray::fromBase64(cmdRaw("Pnj").toUtf8()));
    // TODO: do something with the notes
}

void IaitoCore::saveProject(const QString &name)
{
    Core()->setConfig("scr.interactive", false);
    const bool ok = cmdRaw0(QStringLiteral("'Ps ") + name.trimmed());
    if (!ok) {
        QMessageBox::critical(
            nullptr,
            tr("Error"),
            tr("Cannot save project. Ensure the project name doesnt have any "
               "special or uppercase character"));
    }
#if 0
    cmdRaw(QStringLiteral("Pnj %1").arg(QString(notes.toUtf8().toBase64())));
#endif
    emit projectSaved(ok, name);
}

void IaitoCore::deleteProject(const QString &name)
{
    cmdRaw0("'P-" + name);
}

bool IaitoCore::isProjectNameValid(const QString &name)
{
    // see is_valid_project_name() in libr/core/project.
    QString pattern(R"(^[a-zA-Z0-9\\\._:-]{1,}$)");
    // The below construct mimics the behaviour of QRegexP::exactMatch(), which
    // was here before
    static const QRegularExpression regexp("\\A(?:" + pattern + ")\\z");
    return regexp.match(name).hasMatch() && !name.endsWith(".zrp");
}

QList<DisassemblyLine> IaitoCore::disassembleLines(RVA offset, int lines)
{
    QJsonArray array
        = cmdj(QStringLiteral("pdJ ") + QString::number(lines) + QStringLiteral(" @ ") + QString::number(offset))
              .array();
    QList<DisassemblyLine> r;

    for (const QJsonValueRef value : array) {
        QJsonObject object = value.toObject();
        DisassemblyLine line;
        line.offset = object[RJsonKey::offset].toVariant().toULongLong();
        line.text = ansiEscapeToHtml(object[RJsonKey::text].toString());

        const auto &arrow = object[RJsonKey::arrow];
        line.arrow = arrow.isNull() ? RVA_INVALID : arrow.toVariant().toULongLong();
        r << line;
    }

    return r;
}

/**
 * @brief return hexdump of <size> from an <offset> by a given formats
 * @param address - the address from which to print the hexdump
 * @param size - number of bytes to print
 * @param format - the type of hexdump (qwords, words. decimal, etc)
 */
QString IaitoCore::hexdump(RVA address, int size, HexdumpFormats format)
{
    QString command = "px";
    switch (format) {
    case HexdumpFormats::Normal:
        break;
    case HexdumpFormats::Half:
        command += "h";
        break;
    case HexdumpFormats::Word:
        command += "w";
        break;
    case HexdumpFormats::Quad:
        command += "q";
        break;
    case HexdumpFormats::Signed:
        command += "d";
        break;
    case HexdumpFormats::Octal:
        command += "o";
        break;
    }

    return cmdRawAt(QStringLiteral("%1 %2").arg(command).arg(size), address);
}

QByteArray IaitoCore::hexStringToBytes(const QString &hex)
{
    QByteArray hexChars = hex.toUtf8();
    QByteArray bytes;
    bytes.reserve(hexChars.length() / 2);
    int size = r_hex_str2bin(hexChars.constData(), reinterpret_cast<ut8 *>(bytes.data()));
    bytes.resize(size);
    return bytes;
}

QString IaitoCore::bytesToHexString(const QByteArray &bytes)
{
    QByteArray hex;
    hex.resize(bytes.length() * 2);
    r_hex_bin2str(reinterpret_cast<const ut8 *>(bytes.constData()), bytes.size(), hex.data());
    return QString::fromUtf8(hex);
}

void IaitoCore::loadScript(const QString &scriptname)
{
    CORE_LOCK();
    r_core_cmd_file(core, scriptname.toUtf8().constData());
    triggerRefreshAll();
}

QString IaitoCore::getVersionInformation()
{
    int i;
    QString versionInfo;
    struct vcs_t
    {
        const char *name;
        const char *(*callback)();
    } vcs[]
        = {{"r_anal", &r_anal_version},
           {"r_lib", &r_lib_version},
           {"r_egg", &r_egg_version},
           {"r_asm", &r_asm_version},
           {"r_bin", &r_bin_version},
           {"r_cons", &r_cons_version},
           {"r_flag", &r_flag_version},
           {"r_core", &r_core_version},
           {"r_crypto", &r_crypto_version},
           {"r_bp", &r_bp_version},
           {"r_debug", &r_debug_version},
           {"r_hash", &r_hash_version},
           {"r_fs", &r_fs_version},
           {"r_io", &r_io_version},
#if !USE_LIB_MAGIC
           {"r_magic", &r_magic_version},
#endif
           {"r_reg", &r_reg_version},
           {"r_sign", &r_sign_version},
           {"r_search", &r_search_version},
           {"r_syscall", &r_syscall_version},
           {"r_util", &r_util_version},
           /* ... */
           {NULL, NULL}};
    versionInfo.append(QStringLiteral("%1 r2\n").arg(R2_GITTAP));
    for (i = 0; vcs[i].name; i++) {
        struct vcs_t *v = &vcs[i];
        const char *name = v->callback();
        versionInfo.append(QStringLiteral("%1 %2\n").arg(name, v->name));
    }
    return versionInfo;
}

QJsonArray IaitoCore::getOpenedFiles()
{
    QJsonDocument files = cmdj("oj");
    return files.array();
}

QList<QString> IaitoCore::getColorThemes()
{
    QList<QString> r;
    QJsonDocument themes = cmdj("ecoj");
    for (const QJsonValue s : themes.array()) {
        r << s.toString();
    }
    return r;
}

QString IaitoCore::ansiEscapeToHtml(const QString &text)
{
    int len;
    char *html = r_cons_html_filter(text.toUtf8().constData(), &len);
    if (!html) {
        return QString();
    }
    QString r = QString::fromUtf8(html, len);
    r_mem_free(html);
    return r;
}

BasicBlockHighlighter *IaitoCore::getBBHighlighter()
{
    return bbHighlighter;
}

BasicInstructionHighlighter *IaitoCore::getBIHighlighter()
{
    return &biHighlighter;
}

void IaitoCore::setIOCache(bool enabled)
{
    if (enabled) {
        // disable write mode when cache is enabled
        setWriteMode(false);
    }
    setConfig("io.cache", enabled);
    this->iocache = enabled;

    emit ioCacheChanged(enabled);
    emit ioModeChanged();
}

bool IaitoCore::isIOCacheEnabled() const
{
    return iocache;
}

void IaitoCore::commitWriteCache()
{
    // Temporarily disable cache mode
    TempConfig tempConfig;
    tempConfig.set("io.cache", false);
    if (!isWriteModeEnabled()) {
        cmdRaw("oo+");
        cmdRaw("wci");
        cmdRaw("oo");
    } else {
        cmdRaw("wci");
    }
}

// Enable or disable write-mode. Avoid unecessary changes if not need.
void IaitoCore::setWriteMode(bool enabled)
{
    bool writeModeState = isWriteModeEnabled();

    if (writeModeState == enabled && !this->iocache) {
        // New mode is the same as current and IO Cache is disabled. Do nothing.
        return;
    }

    // Change from read-only to write-mode
    if (enabled && !writeModeState) {
        cmdRaw("oo+");
        // Change from write-mode to read-only
    } else {
        cmdRaw("oo");
    }
    // Disable cache mode because we specifically set write or
    // read-only modes.
    setIOCache(false);
    writeModeChanged(enabled);
    emit ioModeChanged();
}

bool IaitoCore::isWriteModeEnabled()
{
    using namespace std;
    QJsonArray ans = cmdj("oj").array();
    return find_if(
               begin(ans),
               end(ans),
               [](const QJsonValue &v) { return v.toObject().value("raised").toBool(); })
        ->toObject()
        .value("writable")
        .toBool();
}

/**
 * @brief get a compact disassembly preview for tooltips
 * @param address - the address from which to print the disassembly
 * @param num_of_lines - number of instructions to print
 */
QStringList IaitoCore::getDisassemblyPreview(RVA address, int num_of_lines)
{
    QList<DisassemblyLine> disassemblyLines;
    {
        // temporarily simplify the disasm output to get it colorful and simple
        // to read
        TempConfig tempConfig;
        tempConfig.set("scr.color", COLOR_MODE_16M)
            .set("asm.lines", false)
            .set("asm.var", false)
            .set("asm.comments", false)
            .set("asm.bytes", false)
            .set("asm.lines.fcn", false)
            .set("asm.lines.out", false)
            .set("asm.lines.bb", false);

        disassemblyLines = disassembleLines(address, num_of_lines + 1);
    }
    QStringList disasmPreview;
    for (const DisassemblyLine &line : disassemblyLines) {
        disasmPreview << line.text;
        if (disasmPreview.length() >= num_of_lines) {
            disasmPreview << "...";
            break;
        }
    }
    if (!disasmPreview.isEmpty()) {
        return disasmPreview;
    } else {
        return QStringList();
    }
}

/**
 * @brief get a compact hexdump preview for tooltips
 * @param address - the address from which to print the hexdump
 * @param size - number of bytes to print
 */
QString IaitoCore::getHexdumpPreview(RVA address, int size)
{
    // temporarily simplify the disasm output to get it colorful and simple to
    // read
    TempConfig tempConfig;
    tempConfig.set("scr.color", COLOR_MODE_16M)
        .set("asm.offset", true)
        .set("hex.header", false)
        .set("hex.cols", 16);
    return ansiEscapeToHtml(hexdump(address, size, HexdumpFormats::Normal))
        .replace(QLatin1Char('\n'), "<br>");
}

QByteArray IaitoCore::ioRead(RVA addr, int len)
{
    CORE_LOCK();

    QByteArray array;

    if (len <= 0)
        return array;

    /* Zero-copy */
    array.resize(len);
    if (!r_io_read_at(core->io, addr, (uint8_t *) array.data(), len)) {
        qWarning() << "Can't read data" << addr << len;
        array.fill(0xff);
    }

    return array;
}
