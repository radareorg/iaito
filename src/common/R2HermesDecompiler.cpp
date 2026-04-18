
#include "R2HermesDecompiler.h"
#include "CodeMetaRange.h"
#include "Iaito.h"

#include <QJsonArray>
#include <QJsonObject>

static RCodeMeta *buildCodeMeta(const QJsonObject &json)
{
    QString codeString = json["code"].toString();
    RCodeMeta *code = r_codemeta_new("");

    for (const auto &line : json["annotations"].toArray()) {
        QJsonObject obj = line.toObject();
        if (obj.isEmpty()) {
            continue;
        }
        if (obj["type"].toString() != "offset") {
            continue;
        }
        auto range = codeMetaRangeFromJson(obj, static_cast<size_t>(codeString.size()));
        if (!range) {
            continue;
        }
        RCodeMetaItem *mi = r_codemeta_item_new();
        mi->start = range->start;
        mi->end = range->end;
        mi->type = R_CODEMETA_TYPE_OFFSET;
        bool ok;
        mi->offset.offset = obj["offset"].toVariant().toULongLong(&ok);
        r_codemeta_add_item(code, mi);
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

R2HermesDecompiler::R2HermesDecompiler(QObject *parent)
    : Decompiler("r2hermes", "r2hermes", parent)
{
    task = nullptr;
}

bool R2HermesDecompiler::isAvailable()
{
    return Core()->cmd("Lc").contains(QStringLiteral("core_hbc"));
}

RCodeMeta *R2HermesDecompiler::decompileSync(RVA addr)
{
    auto document = Core()->cmdj("pd:hj @ " + QString::number(addr));
    QJsonObject json = document.object();
    if (json.isEmpty()) {
        return nullptr;
    }
    return buildCodeMeta(json);
}

void R2HermesDecompiler::decompileAt(RVA addr)
{
    if (task) {
        return;
    }
    task = new R2Task("pd:hj @ " + QString::number(addr));
    connect(task, &R2Task::finished, this, [this]() {
        QJsonObject json = task->getResultJson().object();
        delete task;
        task = nullptr;
        if (json.isEmpty()) {
            emit finished(Decompiler::makeWarning(tr("Failed to parse JSON from r2hermes (pd:hj)")));
            return;
        }
        emit finished(buildCodeMeta(json));
    });
    task->startTask();
}
