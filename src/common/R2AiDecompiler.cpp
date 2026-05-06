#include "R2AiDecompiler.h"
#include "CodeMetaRange.h"
#include "Iaito.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>

#include <utility>

namespace {

constexpr int R2AI_POLL_INTERVAL_MS = 1000;

QString stripAnsi(QString text)
{
    static const QRegularExpression ansiPattern(QStringLiteral("\x1b\\[[0-?]*[ -/]*[@-~]"));
    text.remove(ansiPattern);
    return text;
}

QString normalizeNewlines(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text;
}

QJsonDocument parseJsonObjectResult(const QString &result)
{
    QByteArray json = result.toUtf8().trimmed();
    const int begin = json.indexOf('{');
    const int end = json.lastIndexOf('}');
    if (begin > 0 && end > begin) {
        json = json.mid(begin, end - begin + 1);
    }

    QJsonParseError error;
    QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return QJsonDocument();
    }
    return document;
}

int queuedTaskId(const QString &result)
{
    static const QRegularExpression queuedPattern(
        QStringLiteral("\\[async\\]\\s+task\\s+(\\d+)\\s+queued"));
    const QRegularExpressionMatch match = queuedPattern.match(stripAnsi(result));
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

bool isFinalState(const QString &state)
{
    return state == QLatin1String("complete") || state == QLatin1String("error")
           || state == QLatin1String("cancelled") || state == QLatin1String("stopped");
}

void setCodeString(RCodeMeta *code, const QString &text)
{
    QByteArray bytes = text.toUtf8();
    free(code->code);
    code->code = strdup(bytes.constData());
}

RCodeMeta *copyCodeMeta(RCodeMeta *source)
{
    RCodeMeta *copy = r_codemeta_new("");
    free(copy->code);
    copy->code = strdup(source && source->code ? source->code : "");
    if (!source) {
        return copy;
    }

#if R2_ABIVERSION >= 40
    RCodeMetaItem *oldItem;
    R_VEC_FOREACH(&source->annotations, oldItem)
    {
#else
    void *iter;
    r_vector_foreach(&source->annotations, iter)
    {
        RCodeMetaItem *oldItem = reinterpret_cast<RCodeMetaItem *>(iter);
#endif
        RCodeMetaItem *newItem = r_codemeta_item_new();
        r_codemeta_item_copy(newItem, oldItem);
        r_codemeta_add_item(copy, newItem);
    }
    return copy;
}

void addOffsetAnnotation(RCodeMeta *code, size_t start, size_t end, RVA offset)
{
    if (!code || offset == RVA_INVALID || end <= start) {
        return;
    }
    RCodeMetaItem *mi = r_codemeta_item_new();
    mi->start = start;
    mi->end = end;
    mi->type = R_CODEMETA_TYPE_OFFSET;
    mi->offset.offset = offset;
    r_codemeta_add_item(code, mi);
}

RCodeMeta *buildCodeMetaFromJson(const QJsonDocument &document)
{
    if (!document.isObject() && !document.isArray()) {
        return nullptr;
    }

    RCodeMeta *code = r_codemeta_new("");
    QString codeString;

    auto appendLine = [&code, &codeString](const QJsonObject &obj) {
        QString text = obj.value(QStringLiteral("str")).toString();
        if (text.isEmpty()) {
            text = obj.value(QStringLiteral("code")).toString();
        }
        if (text.isEmpty()) {
            text = obj.value(QStringLiteral("text")).toString();
        }
        const size_t start = static_cast<size_t>(codeString.size());
        codeString.append(text);
        codeString.append(QLatin1Char('\n'));
        const size_t end = static_cast<size_t>(codeString.size());
        auto offset = codeMetaAddressFromJson(obj.value(QStringLiteral("offset")));
        if (!offset) {
            offset = codeMetaAddressFromJson(obj.value(QStringLiteral("addr")));
        }
        if (offset) {
            addOffsetAnnotation(code, start, end, *offset);
        }
    };

    if (document.isArray()) {
        for (const QJsonValue &value : document.array()) {
            if (value.isObject()) {
                appendLine(value.toObject());
            }
        }
        setCodeString(code, codeString);
        return code;
    }

    const QJsonObject root = document.object();
    if (root.contains(QStringLiteral("code"))) {
        codeString = root.value(QStringLiteral("code")).toString();
        for (const QJsonValue &value : root.value(QStringLiteral("annotations")).toArray()) {
            const QJsonObject obj = value.toObject();
            if (obj.value(QStringLiteral("type")).toString() != QLatin1String("offset")) {
                continue;
            }
            auto range = codeMetaOffsetRangeFromJson(obj, codeString);
            auto offset = codeMetaAddressFromJson(obj.value(QStringLiteral("offset")));
            if (range && offset) {
                addOffsetAnnotation(code, range->start, range->end, *offset);
            }
        }
        setCodeString(code, codeString);
        return code;
    }

    const QJsonArray lines = root.value(QStringLiteral("lines")).toArray();
    if (!lines.isEmpty()) {
        for (const QJsonValue &value : lines) {
            if (value.isObject()) {
                appendLine(value.toObject());
            }
        }
        setCodeString(code, codeString);
        return code;
    }

    r_codemeta_free(code);
    return nullptr;
}

bool parseLineAddress(const QString &line, RVA *address, QString *withoutAddress)
{
    static const QRegularExpression addressPattern(QStringLiteral(
        "^\\s*(/\\*|//|#|\\[)?\\s*(0x[0-9a-fA-F]+)\\s*(\\*/|\\])?\\s*([:\\|\\-])?\\s*(.*)$"));
    const QRegularExpressionMatch match = addressPattern.match(line);
    if (!match.hasMatch()) {
        return false;
    }

    const QString openMarker = match.captured(1);
    const QString closeMarker = match.captured(3);
    const QString separator = match.captured(4);
    const QString addressText = match.captured(2);
    if (openMarker.isEmpty() && closeMarker.isEmpty() && separator.isEmpty()
        && addressText.size() < 8) {
        return false;
    }

    bool ok = false;
    qulonglong parsed = addressText.toULongLong(&ok, 0);
    if (!ok) {
        return false;
    }

    *address = parsed;
    *withoutAddress = match.captured(5);
    return true;
}

RCodeMeta *buildCodeMetaFromText(QString output)
{
    output = normalizeNewlines(stripAnsi(output)).trimmed();

    const QJsonDocument document = QJsonDocument::fromJson(output.toUtf8());
    if (RCodeMeta *jsonCode = buildCodeMetaFromJson(document)) {
        return jsonCode;
    }

    RCodeMeta *code = r_codemeta_new("");
    QString codeString;
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        RVA address = RVA_INVALID;
        QString line = rawLine;
        const bool hasAddress = parseLineAddress(rawLine, &address, &line);

        const size_t start = static_cast<size_t>(codeString.size());
        codeString.append(line);
        codeString.append(QLatin1Char('\n'));
        const size_t end = static_cast<size_t>(codeString.size());
        if (hasAddress) {
            addOffsetAnnotation(code, start, end, address);
        }
    }

    setCodeString(code, codeString);
    return code;
}

} // namespace

R2AiDecompiler::R2AiDecompiler(QObject *parent)
    : Decompiler("r2ai", "r2ai", parent)
    , pollTimer(new QTimer(this))
{
    pollTimer->setInterval(R2AI_POLL_INTERVAL_MS);
    connect(pollTimer, &QTimer::timeout, this, &R2AiDecompiler::pollAsyncTask);
}

R2AiDecompiler::~R2AiDecompiler()
{
    pollTimer->stop();
    if (taskId > 0) {
        Core()->cmd(QStringLiteral("r2ai -sk %1").arg(taskId));
        restoreAsyncPurgeConfig();
    }
    clearCache();
}

bool R2AiDecompiler::isAvailable()
{
    return Core()->cmdList("e cmd.pdc=?").contains(QStringLiteral("r2ai"));
}

QString R2AiDecompiler::cacheKeyForAddr(RVA addr) const
{
    RVA functionAddr = Core()->getFunctionStart(addr);
    if (functionAddr == RVA_INVALID) {
        functionAddr = addr;
    }
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
        .arg(RAddressString(functionAddr))
        .arg(Core()->getConfig(QStringLiteral("r2ai.api")))
        .arg(Core()->getConfig(QStringLiteral("r2ai.model")))
        .arg(Core()->getConfig(QStringLiteral("r2ai.prompt")))
        .arg(Core()->getConfig(QStringLiteral("r2ai.cmds")))
        .arg(Core()->getConfig(QStringLiteral("r2ai.lang")))
        .arg(Core()->getConfig(QStringLiteral("r2ai.system")));
}

QString R2AiDecompiler::decompileCommand(RVA addr) const
{
    if (!offsetCommandChecked) {
        const QString help = Core()->cmd(QStringLiteral("r2ai -h"));
        offsetCommandAvailable = help.contains(QStringLiteral(" -do"));
        offsetCommandChecked = true;
    }
    const QString mode = offsetCommandAvailable ? QStringLiteral("-do") : QStringLiteral("-d");
    return QStringLiteral("r2ai %1 @ %2").arg(mode, QString::number(addr));
}

void R2AiDecompiler::ensureAsyncConfig()
{
    if (!asyncPurgeSaved) {
        previousAsyncPurge = Core()->getConfigb(QStringLiteral("r2ai.async.purge"));
        asyncPurgeSaved = true;
    }
    Core()->setConfig(QStringLiteral("r2ai.async"), true);
    Core()->setConfig(QStringLiteral("r2ai.async.purge"), false);
}

void R2AiDecompiler::restoreAsyncPurgeConfig()
{
    if (!asyncPurgeSaved) {
        return;
    }
    Core()->setConfig(QStringLiteral("r2ai.async.purge"), previousAsyncPurge);
    asyncPurgeSaved = false;
}

void R2AiDecompiler::clearCache()
{
    for (RCodeMeta *code : std::as_const(cache)) {
        r_codemeta_free(code);
    }
    cache.clear();
}

bool R2AiDecompiler::hasCachedResult(RVA addr) const
{
    return cache.contains(cacheKeyForAddr(addr));
}

RCodeMeta *R2AiDecompiler::decompileSync(RVA addr)
{
    const QString key = cacheKeyForAddr(addr);
    if (RCodeMeta *cached = cache.value(key, nullptr)) {
        return copyCodeMeta(cached);
    }
    return Decompiler::makeWarning(tr("r2ai decompilation must be started explicitly"));
}

void R2AiDecompiler::decompileAt(RVA addr)
{
    RVA functionAddr = Core()->getFunctionStart(addr);
    if (functionAddr == RVA_INVALID) {
        functionAddr = addr;
    }

    const QString key = cacheKeyForAddr(functionAddr);
    if (RCodeMeta *cached = cache.value(key, nullptr)) {
        emit finished(copyCodeMeta(cached));
        return;
    }

    if (taskId > 0) {
        emit finished(Decompiler::makeWarning(tr("r2ai decompilation is already running")));
        return;
    }

    ensureAsyncConfig();
    taskCacheKey = key;
    taskLastOutput.clear();

    const QString result = Core()->cmd(decompileCommand(functionAddr));
    taskId = queuedTaskId(result);
    if (taskId <= 0) {
        restoreAsyncPurgeConfig();
        RCodeMeta *code = buildCodeMetaFromText(result);
        cache.insert(taskCacheKey, copyCodeMeta(code));
        taskCacheKey.clear();
        emit finished(code);
        return;
    }

    pollTimer->start();
}

void R2AiDecompiler::finishWithCode(RCodeMeta *code, bool storeInCache)
{
    pollTimer->stop();
    if (storeInCache && code) {
        RCodeMeta *oldCode = cache.take(taskCacheKey);
        if (oldCode) {
            r_codemeta_free(oldCode);
        }
        cache.insert(taskCacheKey, copyCodeMeta(code));
    }
    taskId = 0;
    taskCacheKey.clear();
    taskLastOutput.clear();
    restoreAsyncPurgeConfig();
    emit finished(code);
}

void R2AiDecompiler::finishWithWarning(const QString &message)
{
    finishWithCode(Decompiler::makeWarning(message), false);
}

void R2AiDecompiler::pollAsyncTask()
{
    if (taskId <= 0) {
        pollTimer->stop();
        return;
    }

    ensureAsyncConfig();
    const QJsonDocument document = parseJsonObjectResult(Core()->cmd(QStringLiteral("r2ai -sj")));
    if (!document.isObject()) {
        return;
    }

    const QJsonArray tasks = document.object().value(QStringLiteral("tasks")).toArray();
    QJsonObject currentTask;
    for (const QJsonValue &value : tasks) {
        const QJsonObject task = value.toObject();
        if (task.value(QStringLiteral("id")).toInt() == taskId) {
            currentTask = task;
            break;
        }
    }

    if (currentTask.isEmpty()) {
        if (!taskLastOutput.isEmpty()) {
            finishWithCode(buildCodeMetaFromText(taskLastOutput), true);
        } else {
            finishWithWarning(tr("r2ai async task disappeared before producing output"));
        }
        return;
    }

    const QString output = currentTask.value(QStringLiteral("output")).toString();
    if (!output.isEmpty()) {
        taskLastOutput = output;
    }

    const QString state = currentTask.value(QStringLiteral("state")).toString();
    if (!isFinalState(state)) {
        return;
    }

    if (state == QLatin1String("complete")) {
        if (taskLastOutput.isEmpty()) {
            finishWithWarning(tr("r2ai completed without output"));
        } else {
            finishWithCode(buildCodeMetaFromText(taskLastOutput), true);
        }
    } else if (state == QLatin1String("cancelled") || state == QLatin1String("stopped")) {
        finishWithWarning(tr("r2ai decompilation cancelled"));
    } else {
        const QString error = currentTask.value(QStringLiteral("error")).toString();
        finishWithWarning(error.isEmpty() ? tr("r2ai decompilation failed") : error);
    }
}

void R2AiDecompiler::cancel()
{
    if (taskId <= 0) {
        return;
    }
    Core()->cmd(QStringLiteral("r2ai -sk %1").arg(taskId));
    finishWithWarning(tr("r2ai decompilation cancelled"));
}

void R2AiDecompiler::setOption(const QString &key, const QString &value)
{
    Decompiler::setOption(key, value);
    clearCache();
}
