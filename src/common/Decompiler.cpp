
#include "Decompiler.h"
#include "CodeMetaRange.h"
#include "Configuration.h"
#include "Iaito.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

QString R2CommandDecompiler::codeFromOutput(const QByteArray &output) const
{
    return QString::fromUtf8(output);
}

QString R2CommandDecompiler::takeCodeText(RCodeMeta *code)
{
    if (!code) {
        return {};
    }
    const QString text = QString::fromUtf8(code->code);
    r_codemeta_free(code);
    return text;
}

Decompiler::Decompiler(const QString &id, const QString &name, QObject *parent)
    : QObject(parent)
    , id(id)
    , name(name)
{}

RCodeMeta *Decompiler::makeWarning(QString warningMessage)
{
    std::string temporary = warningMessage.toStdString();
    return r_codemeta_new(temporary.c_str());
}

QStringList Decompiler::listOptions()
{
    QString prefix = getConfigPrefix();
    if (prefix.isEmpty()) {
        return {};
    }
    QStringList result;
    QStringList lines = Core()->cmdList(QString("e %1.").arg(prefix).toUtf8().constData());
    QString keyPrefix = prefix + QLatin1Char('.');
    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || !trimmed.startsWith(keyPrefix)) {
            continue;
        }
        result.append(trimmed.mid(keyPrefix.length()));
    }
    return result;
}

QString Decompiler::getOption(const QString &key)
{
    QString prefix = getConfigPrefix();
    if (prefix.isEmpty() || key.isEmpty()) {
        return QString();
    }
    return Config()->getConfigString(prefix + QLatin1Char('.') + key);
}

void Decompiler::setOption(const QString &key, const QString &value)
{
    QString prefix = getConfigPrefix();
    if (prefix.isEmpty() || key.isEmpty()) {
        return;
    }
    Config()->setConfig(prefix + QLatin1Char('.') + key, value);
}

static RSyntaxHighlightType syntaxHighlightTypeFromString(const QString &str)
{
    struct SyntaxHighlightType
    {
        const char *name;
        RSyntaxHighlightType type;
    };
    static const SyntaxHighlightType types[] = {
        {"keyword", R_SYNTAX_HIGHLIGHT_TYPE_KEYWORD},
        {"comment", R_SYNTAX_HIGHLIGHT_TYPE_COMMENT},
        {"datatype", R_SYNTAX_HIGHLIGHT_TYPE_DATATYPE},
        {"function_name", R_SYNTAX_HIGHLIGHT_TYPE_FUNCTION_NAME},
        {"function_parameter", R_SYNTAX_HIGHLIGHT_TYPE_FUNCTION_PARAMETER},
        {"local_variable", R_SYNTAX_HIGHLIGHT_TYPE_LOCAL_VARIABLE},
        {"constant_variable", R_SYNTAX_HIGHLIGHT_TYPE_CONSTANT_VARIABLE},
        {"global_variable", R_SYNTAX_HIGHLIGHT_TYPE_GLOBAL_VARIABLE},
    };

    for (const SyntaxHighlightType &type : types) {
        if (str == QLatin1String(type.name)) {
            return type.type;
        }
    }
    return R_SYNTAX_HIGHLIGHT_TYPE_KEYWORD;
}

static char *jsonStringDup(const QJsonObject &obj, const QString &key)
{
    const QByteArray bytes = obj.value(key).toString().toUtf8();
    return strdup(bytes.constData());
}

static bool setCodeMetaAddress(RCodeMetaItem *mi, const QJsonObject &obj, RCodeMetaItemType type)
{
    auto offset = codeMetaAddressFromJson(obj.value(QStringLiteral("offset")));
    if (!offset) {
        return false;
    }
    mi->type = type;
    mi->reference.offset = *offset;
    return true;
}

static void addCodeMetaAnnotation(
    RCodeMeta *code,
    const QJsonObject &obj,
    const QString &codeString,
    R2JsonDecompiler::AnnotationMode annotationMode)
{
    const QString type = obj.value(QStringLiteral("type")).toString();
    const bool isOffset = type == QLatin1String("offset");
    if (!isOffset && annotationMode == R2JsonDecompiler::AnnotationMode::OffsetsOnly) {
        return;
    }

    auto range = isOffset ? codeMetaOffsetRangeFromJson(obj, codeString)
                          : codeMetaRangeFromJson(obj, static_cast<size_t>(codeString.size()));
    if (!range) {
        return;
    }

    RCodeMetaItem *mi = r_codemeta_item_new();
    if (!mi) {
        return;
    }
    mi->start = range->start;
    mi->end = range->end;

    if (isOffset) {
        auto offset = codeMetaAddressFromJson(obj.value(QStringLiteral("offset")));
        if (!offset) {
            r_codemeta_item_free(mi);
            return;
        }
        mi->type = R_CODEMETA_TYPE_OFFSET;
        mi->offset.offset = *offset;
    } else if (type == QLatin1String("syntax_highlight")) {
        mi->type = R_CODEMETA_TYPE_SYNTAX_HIGHLIGHT;
        mi->syntax_highlight.type = syntaxHighlightTypeFromString(
            obj.value(QStringLiteral("syntax_highlight")).toString());
    } else if (type == QLatin1String("function_name")) {
        if (!setCodeMetaAddress(mi, obj, R_CODEMETA_TYPE_FUNCTION_NAME)) {
            r_codemeta_item_free(mi);
            return;
        }
        mi->reference.name = jsonStringDup(obj, QStringLiteral("name"));
    } else if (type == QLatin1String("global_variable")) {
        if (!setCodeMetaAddress(mi, obj, R_CODEMETA_TYPE_GLOBAL_VARIABLE)) {
            r_codemeta_item_free(mi);
            return;
        }
    } else if (type == QLatin1String("constant_variable")) {
        if (!setCodeMetaAddress(mi, obj, R_CODEMETA_TYPE_CONSTANT_VARIABLE)) {
            r_codemeta_item_free(mi);
            return;
        }
    } else if (type == QLatin1String("local_variable")) {
        mi->type = R_CODEMETA_TYPE_LOCAL_VARIABLE;
        mi->variable.name = jsonStringDup(obj, QStringLiteral("name"));
    } else if (type == QLatin1String("function_parameter")) {
        mi->type = R_CODEMETA_TYPE_FUNCTION_PARAMETER;
        mi->variable.name = jsonStringDup(obj, QStringLiteral("name"));
    } else {
        r_codemeta_item_free(mi);
        return;
    }

    r_codemeta_add_item(code, mi);
}

static void addCodeMetaAnnotations(
    RCodeMeta *code,
    const QJsonArray &annotations,
    const QString &codeString,
    R2JsonDecompiler::AnnotationMode annotationMode)
{
    for (const QJsonValue &line : annotations) {
        const QJsonObject obj = line.toObject();
        if (!obj.isEmpty()) {
            addCodeMetaAnnotation(code, obj, codeString, annotationMode);
        }
    }
}

static void appendJsonErrors(QString &codeString, const QJsonArray &errors)
{
    for (const QJsonValue &line : errors) {
        if (line.isString()) {
            codeString.append(line.toString());
            codeString.append(QLatin1Char('\n'));
        }
    }
}

R2JsonDecompiler::R2JsonDecompiler(
    const QString &id,
    const QString &name,
    const QString &command,
    const QString &jsonName,
    AnnotationMode annotationMode,
    QObject *parent)
    : Decompiler(id, name, parent)
    , command(command)
    , jsonName(jsonName)
    , annotationMode(annotationMode)
{}

QString R2JsonDecompiler::commandAt(RVA addr) const
{
    return decompileCommand(addr);
}

QString R2JsonDecompiler::decompileCommand(RVA addr) const
{
    return command + QStringLiteral(" @ ") + QString::number(addr);
}

RCodeMeta *R2JsonDecompiler::buildCodeMeta(const QJsonObject &json) const
{
    const QString codeString = json.value(QStringLiteral("code")).toString();
    QString output = codeString;
    appendJsonErrors(output, json.value(QStringLiteral("errors")).toArray());

    const QByteArray bytes = output.toUtf8();
    RCodeMeta *code = r_codemeta_new(bytes.constData());
    if (!code) {
        return nullptr;
    }
    addCodeMetaAnnotations(
        code, json.value(QStringLiteral("annotations")).toArray(), codeString, annotationMode);
    return code;
}

QString R2JsonDecompiler::codeFromOutput(const QByteArray &output) const
{
    const QJsonObject json = QJsonDocument::fromJson(output).object();
    return json.isEmpty() ? QString() : takeCodeText(buildCodeMeta(json));
}

RCodeMeta *R2JsonDecompiler::decompileSync(RVA addr)
{
    const QJsonObject json = Core()->cmdj(commandAt(addr)).object();
    if (json.isEmpty()) {
        return nullptr;
    }
    return buildCodeMeta(json);
}

void R2JsonDecompiler::decompileAt(RVA addr)
{
    if (task) {
        return;
    }

    task = new R2Task(commandAt(addr));
    connect(task, &R2Task::finished, this, [this]() {
        const QJsonObject json = task->getResultJson().object();
        delete task;
        task = nullptr;
        if (json.isEmpty()) {
            emit finished(Decompiler::makeWarning(tr("Failed to parse JSON from %1").arg(jsonName)));
            return;
        }
        RCodeMeta *code = buildCodeMeta(json);
        emit finished(
            code ? code : Decompiler::makeWarning(tr("Failed to build decompiler output")));
    });
    task->startTask();
}

R2DecDecompiler::R2DecDecompiler(QObject *parent)
    : Decompiler("r2dec", "r2dec", parent)
{
    task = nullptr;
}

bool R2DecDecompiler::isAvailable()
{
    return Core()->cmdList("e cmd.pdc=?").contains(QStringLiteral("pdd"));
}

static constexpr auto R2DecCommand = "pddj";

static RCodeMeta *buildR2DecCodeMeta(const QJsonObject &json)
{
    if (json.isEmpty()) {
        return nullptr;
    }
    // note that r2dec doesnt have the "text" object like pdc does. so we cant
    // reuse code
    RCodeMeta *code = r_codemeta_new("");
    QString codeString = "";
    for (const auto line : json["log"].toArray()) {
        if (!line.isString()) {
            continue;
        }
        codeString.append(line.toString() + "\n");
    }

    QJsonArray linesArray = json["lines"].toArray();
    for (const QJsonValueRef line : linesArray) {
        QJsonObject lineObject = line.toObject();
        if (lineObject.isEmpty()) {
            continue;
        }
        auto offset = codeMetaAddressFromJson(lineObject["offset"]);
        size_t start = codeString.length();
        codeString.append(lineObject["str"].toString() + "\n");
        if (!offset) {
            continue;
        }
        RCodeMetaItem *mi = r_codemeta_item_new();
        mi->start = start;
        mi->end = codeString.length();
        mi->type = R_CODEMETA_TYPE_OFFSET;
        mi->offset.offset = *offset;
        r_codemeta_add_item(code, mi);
    }

    for (const auto line : json["errors"].toArray()) {
        if (!line.isString()) {
            continue;
        }
        codeString.append(line.toString() + "\n");
    }
    std::string tmp = codeString.toStdString();
    free(code->code);
    code->code = strdup(tmp.c_str());
    return code;
}

QString R2DecDecompiler::commandAt(RVA addr) const
{
    return QLatin1String(R2DecCommand) + QStringLiteral(" @ ") + QString::number(addr);
}

QString R2DecDecompiler::codeFromOutput(const QByteArray &output) const
{
    return takeCodeText(buildR2DecCodeMeta(QJsonDocument::fromJson(output).object()));
}

RCodeMeta *R2DecDecompiler::decompileSync(RVA addr)
{
    return buildR2DecCodeMeta(Core()->cmdjAt(R2DecCommand, addr).object());
}

void R2DecDecompiler::decompileAt(RVA addr)
{
    if (task) {
        return;
    }

    task = new R2Task(commandAt(addr));
    connect(task, &R2Task::finished, this, [this]() {
        QJsonObject json = task->getResultJson().object();
        delete task;
        task = nullptr;
        if (json.isEmpty()) {
            emit finished(Decompiler::makeWarning(tr("Failed to parse JSON from r2dec")));
            return;
        }
        emit finished(buildR2DecCodeMeta(json));
    });
    task->startTask();
}
