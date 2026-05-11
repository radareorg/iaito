#include "R2JadxDecompiler.h"
#include "CodeMetaRange.h"
#include "Iaito.h"

#include <QJsonArray>
#include <QJsonObject>

static void appendLine(RCodeMeta *code, QString &codeString, const QJsonObject &lineObject)
{
    auto offset = codeMetaAddressFromJson(lineObject["offset"]);
    size_t start = codeString.length();
    codeString.append(lineObject["str"].toString() + "\n");
    if (!offset) {
        return;
    }

    RCodeMetaItem *mi = r_codemeta_item_new();
    mi->start = start;
    mi->end = codeString.length();
    mi->type = R_CODEMETA_TYPE_OFFSET;
    mi->offset.offset = *offset;
    r_codemeta_add_item(code, mi);
}

static RCodeMeta *buildCodeMeta(const QJsonObject &json)
{
    RCodeMeta *code = r_codemeta_new("");
    QString codeString;

    for (const auto line : json["lines"].toArray()) {
        QJsonObject lineObject = line.toObject();
        if (lineObject.isEmpty()) {
            continue;
        }
        appendLine(code, codeString, lineObject);
    }

    for (const auto line : json["errors"].toArray()) {
        if (!line.isString()) {
            continue;
        }
        codeString.append(line.toString() + "\n");
    }

    std::string tmp = codeString.toStdString();
    code->code = strdup(tmp.c_str());
    return code;
}

R2JadxDecompiler::R2JadxDecompiler(QObject *parent)
    : Decompiler("r2jadx", "r2jadx", parent)
{
    task = nullptr;
}

bool R2JadxDecompiler::isAvailable()
{
    const QStringList decompilers = Core()->cmdList("e cmd.pdc=?");
    return decompilers.contains(QStringLiteral("r2jadx"))
           || decompilers.contains(QStringLiteral("pd:j"));
}

RCodeMeta *R2JadxDecompiler::decompileSync(RVA addr)
{
    auto document = Core()->cmdj("pd:jj @ " + QString::number(addr));
    QJsonObject json = document.object();
    if (json.isEmpty()) {
        return nullptr;
    }
    return buildCodeMeta(json);
}

void R2JadxDecompiler::decompileAt(RVA addr)
{
    if (task) {
        return;
    }
    task = new R2Task("pd:jj @ " + QString::number(addr));
    connect(task, &R2Task::finished, this, [this]() {
        QJsonObject json = task->getResultJson().object();
        delete task;
        task = nullptr;
        if (json.isEmpty()) {
            emit finished(Decompiler::makeWarning(tr("Failed to parse JSON from r2jadx (pd:jj)")));
            return;
        }
        emit finished(buildCodeMeta(json));
    });
    task->startTask();
}
