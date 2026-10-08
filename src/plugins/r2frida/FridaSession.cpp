#include "FridaSession.h"

#include "FridaUri.h"
#include "core/Iaito.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryFile>

FridaSession::FridaSession(QObject *parent)
    : QObject(parent)
{}

bool FridaSession::checkAvailable()
{
    available_ = backend_.pluginAvailable();
    return available_;
}

void FridaSession::setAddressMode(FridaAddressMode mode)
{
    if (addressMode_ == mode) {
        return;
    }
    addressMode_ = mode;
    emit changed();
}

void FridaSession::setState(FridaSessionState state)
{
    state_ = state;
    emit changed();
}

void FridaSession::clearTarget()
{
    suspended_ = false;
    process_ = FridaProcessInfo();
    modules_.clear();
    maps_.clear();
    staticBase_ = 0;
    runtimeBase_ = 0;
    matchedSize_ = 0;
    slide_ = 0;
    uri_.clear();
    deviceLabel_.clear();
    transportLabel_.clear();
}

bool FridaSession::containsStatic(quint64 addr) const
{
    return matchedSize_ > 0 && addr >= staticBase_ && addr - staticBase_ < matchedSize_;
}

bool FridaSession::containsRuntime(quint64 addr) const
{
    return matchedSize_ > 0 && addr >= runtimeBase_ && addr - runtimeBase_ < matchedSize_;
}

quint64 FridaSession::toRuntime(quint64 staticVa) const
{
    if (!containsStatic(staticVa)) {
        return RVA_INVALID;
    }
    return runtimeBase_ + (staticVa - staticBase_);
}

quint64 FridaSession::toStatic(quint64 runtimeVa) const
{
    if (!containsRuntime(runtimeVa)) {
        return RVA_INVALID;
    }
    return staticBase_ + (runtimeVa - runtimeBase_);
}

QString FridaSession::formatRuntime(quint64 runtimeVa) const
{
    if (addressMode_ == FridaAddressMode::ModuleOffset) {
        for (const FridaModuleInfo &module : modules_) {
            if (module.size > 0 && runtimeVa >= module.base
                && runtimeVa - module.base < module.size) {
                return module.name + QStringLiteral(" + ")
                       + RAddressString(runtimeVa - module.base);
            }
        }
    }
    if (addressMode_ == FridaAddressMode::Static) {
        const quint64 staticVa = toStatic(runtimeVa);
        if (staticVa != RVA_INVALID) {
            return RAddressString(staticVa);
        }
    }
    return RAddressString(runtimeVa);
}

QString FridaSession::describe(quint64 addr) const
{
    quint64 staticVa = addr;
    quint64 runtimeVa = addr;
    if (containsStatic(addr)) {
        runtimeVa = toRuntime(addr);
    } else if (containsRuntime(addr)) {
        staticVa = toStatic(addr);
    } else {
        return tr("No static mapping for %1").arg(RAddressString(addr));
    }
    QString moduleText = RAddressString(runtimeVa);
    for (const FridaModuleInfo &module : modules_) {
        if (module.size > 0 && runtimeVa >= module.base && runtimeVa - module.base < module.size) {
            moduleText = module.name + QStringLiteral(" + ")
                         + RAddressString(runtimeVa - module.base);
            break;
        }
    }
    return tr("static %1\nruntime %2\n%3\nslide %4")
        .arg(
            RAddressString(staticVa),
            RAddressString(runtimeVa),
            moduleText,
            RAddressString(slide_));
}

void FridaSession::probe(const QString &uri, const std::function<void(QString)> &done)
{
    if (!checkAvailable()) {
        if (done) {
            done(tr("r2frida is not loaded. Install it with r2pm -ci r2frida and restart iaito."));
        }
        return;
    }
    if (backend_.busy()) {
        if (done) {
            done(tr("r2frida is busy."));
        }
        return;
    }
    const QSet<int> before = backend_.openFds();
    // r2frida prints the table with r_cons_gprintf, which follows the
    // thread-local console. A background task drains a child console, so the
    // table never comes back. Run here, on the console cmd() captures.
    // `of` still performs the plugin open, and stays quiet when it returns
    // no descriptor. `o` would log "Cannot open file" for that.
    const QString savedColor = Core()->getConfig(QStringLiteral("scr.color"));
    const QString batch = QStringLiteral("e scr.color=0\nof ") + FridaUri::quote(uri)
                          + QStringLiteral("\ne scr.color=") + savedColor + QLatin1Char('\n');
    RCons *previous = r_cons_singleton();
    RCons *cons = r_core_get_cons(Core()->core_);
    if (cons) {
        r_cons_global(cons);
    }
    const QString output = Core()->cmd(batch);
    if (previous && previous != cons) {
        r_cons_global(previous);
    }
    const int leaked = backend_.findNewFridaFd(before);
    backend_.closeFd(leaked);
    QStringList kept;
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        if (line.contains(QStringLiteral("Cannot open"))) {
            continue;
        }
        kept.append(line);
    }
    if (done) {
        done(kept.join(QLatin1Char('\n')));
    }
}

void FridaSession::openTarget(
    const QString &uri, bool suspended, const QString &deviceLabel, const QString &transportLabel)
{
    if (!checkAvailable()) {
        lastError_ = tr(
            "r2frida is not loaded. Install it with r2pm -ci r2frida and restart iaito.");
        setState(FridaSessionState::Error);
        emit errorMessage(lastError_);
        return;
    }
    if (backend_.busy()) {
        emit errorMessage(tr("r2frida is busy."));
        return;
    }
    if (backend_.fridaFd() >= 0) {
        detach();
    }

    const int generation = ++generation_;
    const QSet<int> before = backend_.openFds();
    backend_.setHostFd(backend_.currentFd());
    deviceLabel_ = deviceLabel;
    transportLabel_ = transportLabel;
    uri_ = uri;
    suspended_ = suspended;
    setState(FridaSessionState::Connecting);
    emit consoleMessage(tr("opening %1").arg(uri));

    const QString batch = QStringLiteral("of ") + FridaUri::quote(uri);
    backend_.runBatch(batch, [this, before, generation, suspended](const QString &output) {
        const int fd = backend_.findNewFridaFd(before);
        if (generation != generation_) {
            backend_.closeFd(fd);
            return;
        }
        if (fd < 0) {
            lastError_ = output.trimmed().isEmpty()
                             ? tr("Could not open the r2frida target.")
                             : output.trimmed();
            clearTarget();
            setState(FridaSessionState::Error);
            emit errorMessage(lastError_);
            emit consoleMessage(lastError_);
            return;
        }
        backend_.setFridaFd(fd);
        const int host = backend_.hostFd();
        if (host > 0 && backend_.currentFd() != host) {
            Core()->cmd(QStringLiteral("o=") + QString::number(host));
        }
        suspended_ = suspended;
        readSnapshot();
        setState(FridaSessionState::Attached);
        emit consoleMessage(
            tr("attached pid %1 (%2)").arg(process_.pid).arg(process_.name));
    });
}

void FridaSession::detach()
{
    ++generation_;
    const int fd = backend_.fridaFd();
    const int host = backend_.hostFd();
    if (host > 0 && backend_.currentFd() == fd) {
        Core()->cmd(QStringLiteral("o=") + QString::number(host));
    }
    backend_.closeFd(fd);
    clearTarget();
    lastError_.clear();
    setState(FridaSessionState::Disconnected);
    emit consoleMessage(tr("detached"));
}

void FridaSession::resume()
{
    if (!isAttached()) {
        return;
    }
    command(QStringLiteral("dc"), [this](const QString &output) {
        suspended_ = false;
        emit consoleMessage(output.trimmed().isEmpty() ? tr("resumed") : output.trimmed());
        emit changed();
    });
}

void FridaSession::command(const QString &fridaCmd, const std::function<void(QString)> &done)
{
    if (!isAttached()) {
        if (done) {
            done(tr("Not attached."));
        }
        return;
    }
    if (backend_.busy()) {
        if (done) {
            done(tr("r2frida is busy."));
        }
        return;
    }
    const int generation = generation_;
    const int fd = backend_.fridaFd();
    const int back = backend_.currentFd() == fd ? backend_.hostFd() : backend_.currentFd();
    QString batch = QStringLiteral("o=") + QString::number(fd) + QStringLiteral("\n:") + fridaCmd
                    + QLatin1Char('\n');
    if (back > 0 && back != fd) {
        batch += QStringLiteral("o=") + QString::number(back) + QLatin1Char('\n');
    }
    backend_.runBatch(batch, [this, generation, done](const QString &output) {
        if (generation != generation_) {
            return;
        }
        if (done) {
            done(output);
        }
    });
}

void FridaSession::note(const QString &text)
{
    emit consoleMessage(text);
}

QString FridaSession::writeScript(const QString &source)
{
    QString dir = QDir::tempPath();
    if (dir.contains(QLatin1Char(' '))) {
        dir = QStringLiteral("/tmp");
    }
    auto *file = new QTemporaryFile(dir + QStringLiteral("/iaito-frida-XXXXXX.js"), this);
    file->setAutoRemove(true);
    if (!file->open()) {
        delete file;
        return QString();
    }
    file->write(source.toUtf8());
    file->flush();
    return file->fileName();
}

void FridaSession::importExports(const QString &moduleName)
{
    if (!isAttached() || moduleName.isEmpty()) {
        return;
    }
    const QString commandText = QStringLiteral("iEj ") + moduleName;
    command(commandText, [this, moduleName](const QString &output) {
        const QJsonDocument doc = FridaBackend::parseJsonLoose(output);
        const QJsonArray exports = doc.array();
        QString script = QStringLiteral("fs frida\n");
        int imported = 0;
        const QString prefix = QStringLiteral("frida.") + moduleName + QLatin1Char('.');
        for (const QJsonValue value : exports) {
            const QJsonObject exp = value.toObject();
            const quint64 runtime
                = FridaBackend::parseAddress(exp.value(QStringLiteral("address")));
            const quint64 staticVa = toStatic(runtime);
            if (staticVa == RVA_INVALID) {
                continue;
            }
            QString name = exp.value(QStringLiteral("name")).toString();
            name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._]")), QStringLiteral("_"));
            if (name.isEmpty()) {
                continue;
            }
            script += QStringLiteral("f ");
            script += prefix;
            script += name;
            script += QStringLiteral(" = ");
            script += RAddressString(staticVa);
            script += QLatin1Char('\n');
            ++imported;
            if (imported >= 20000) {
                break;
            }
        }
        script += QStringLiteral("fs *\n");
        if (imported == 0) {
            emit errorMessage(tr("No exports from %1 map into the static file.").arg(moduleName));
            return;
        }
        Core()->cmd(script);
        Core()->triggerRefreshAll();
        emit consoleMessage(tr("imported %1 symbols from %2").arg(imported).arg(moduleName));
    });
}

void FridaSession::readSnapshot()
{
    const QString blob = backend_.fridaCommand(
        QStringLiteral("ij\n?e ---IAITO-FRIDA---\n:ilj\n?e ---IAITO-FRIDA---\n:dmj"));
    QStringList parts = blob.split(QStringLiteral("---IAITO-FRIDA---"));
    if (parts.size() < 3) {
        parts = QStringList{blob, QString(), QString()};
    }
    applyInfo(FridaBackend::parseJsonLoose(parts.value(0)).object());
    applyModules(FridaBackend::parseJsonLoose(parts.value(1)).array());
    applyMaps(FridaBackend::parseJsonLoose(parts.value(2)).array());
}

void FridaSession::applyInfo(const QJsonObject &info)
{
    process_ = FridaProcessInfo();
    process_.pid = info.value(QStringLiteral("pid")).toInt(-1);
    process_.arch = info.value(QStringLiteral("arch")).toString();
    process_.bits = info.value(QStringLiteral("bits")).toInt();
    process_.os = info.value(QStringLiteral("os")).toString();
    process_.moduleName = info.value(QStringLiteral("modulename")).toString();
    process_.moduleBase = FridaBackend::parseAddress(info.value(QStringLiteral("modulebase")));
    const QString app = info.value(QStringLiteral("appname")).toString();
    const QString packageName = info.value(QStringLiteral("packageName")).toString();
    const QString bundle = info.value(QStringLiteral("bundle")).toString();
    if (!app.isEmpty()) {
        process_.name = app;
    } else if (!packageName.isEmpty()) {
        process_.name = packageName;
    } else if (!bundle.isEmpty()) {
        process_.name = bundle;
    } else {
        process_.name = process_.moduleName;
    }
}

void FridaSession::applyModules(const QJsonArray &modules)
{
    modules_.clear();
    const QString opened = QFileInfo(Core()->getFilePath()).fileName();
    int match = -1;
    for (const QJsonValue value : modules) {
        const QJsonObject object = value.toObject();
        FridaModuleInfo module;
        module.name = object.value(QStringLiteral("name")).toString();
        module.path = object.value(QStringLiteral("path")).toString();
        module.base = FridaBackend::parseAddress(object.value(QStringLiteral("base")));
        module.size = static_cast<quint64>(object.value(QStringLiteral("size")).toDouble());
        const QString fileName = QFileInfo(module.path).fileName();
        const bool nameMatch = !opened.isEmpty() && module.name == opened;
        const bool pathMatch = !opened.isEmpty() && fileName == opened;
        modules_.append(module);
        if (nameMatch || pathMatch) {
            if (match < 0 || nameMatch) {
                match = modules_.size() - 1;
            }
        }
    }
    staticBase_ = 0;
    runtimeBase_ = 0;
    matchedSize_ = 0;
    slide_ = 0;
    if (match < 0) {
        return;
    }
    const QJsonObject bin
        = Core()->cmdj(QStringLiteral("ij")).object().value(QStringLiteral("bin")).toObject();
    staticBase_ = bin.value(QStringLiteral("baddr")).toVariant().toULongLong();
    FridaModuleInfo &module = modules_[match];
    module.matched = true;
    runtimeBase_ = module.base;
    matchedSize_ = module.size;
    slide_ = runtimeBase_ - staticBase_;
}

void FridaSession::applyMaps(const QJsonArray &maps)
{
    maps_.clear();
    for (const QJsonValue value : maps) {
        const QJsonObject object = value.toObject();
        FridaMapInfo map;
        map.start = FridaBackend::parseAddress(object.value(QStringLiteral("base")));
        map.end = map.start + static_cast<quint64>(object.value(QStringLiteral("size")).toDouble());
        map.permission = object.value(QStringLiteral("protection")).toString();
        const QJsonValue file = object.value(QStringLiteral("file"));
        if (file.isObject()) {
            map.path = file.toObject().value(QStringLiteral("path")).toString();
        } else if (file.isString()) {
            map.path = file.toString();
        }
        maps_.append(map);
    }
}
