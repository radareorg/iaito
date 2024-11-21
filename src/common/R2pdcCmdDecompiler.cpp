
#include "R2pdcCmdDecompiler.h"
#include "Iaito.h"

#include <QJsonArray>
#include <QJsonObject>

/*
RCodeMeta *Decompiler::makeWarning(QString warningMessage){
    std::string temporary = warningMessage.toStdString();
    return r_codemeta_new(temporary.c_str());
}
*/

R2pdcCmdDecompiler::R2pdcCmdDecompiler(QObject *parent)
    : Decompiler("pdc", "pdc", parent)
{
    task = nullptr;
}

bool R2pdcCmdDecompiler::isAvailable()
{
    return Core()->cmdList("e cmd.pdc=?").contains(QStringLiteral("pdc"));
}

RCodeMeta *R2pdcCmdDecompiler::decompileSync(RVA addr)
{
    auto document = Core()->cmdj("pdcj @ " + QString::number(addr));
    QJsonObject json = document.object();
    if (json.isEmpty()) {
        //    emit finished(Decompiler::makeWarning(tr("Failed to parse JSON
        //    from pdc")));
        return NULL;
    }
    QString codeString = json["code"].toString();
    RCodeMeta *code = r_codemeta_new("");
    QJsonArray linesArray = json["annotations"].toArray();
    for (const QJsonValueRef line : linesArray) {
        QJsonObject lineObject = line.toObject();
        if (lineObject.isEmpty()) {
            continue;
        }
        if (lineObject["type"].toString() != "offset") {
            continue;
        }
        RCodeMetaItem *mi = r_codemeta_item_new();
        mi->start = lineObject["start"].toInt();
        mi->end = lineObject["end"].toInt();
        bool ok;
        mi->type = R_CODEMETA_TYPE_OFFSET;
        mi->offset.offset = lineObject["offset"].toVariant().toULongLong(&ok);
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

// thready, crashing, unsafe
void R2pdcCmdDecompiler::decompileAt(RVA addr)
{
    if (task) {
        return;
    }
    task = new R2Task("pdcj @ " + QString::number(addr));
    connect(task, &R2Task::finished, this, [this]() {
        QJsonObject json = task->getResultJson().object();
        delete task;
        task = nullptr;
        if (json.isEmpty()) {
            emit finished(Decompiler::makeWarning(tr("Failed to parse JSON from pdc")));
            return;
        }
        QString codeString = json["code"].toString();
        RCodeMeta *code = r_codemeta_new("");
        QJsonArray linesArray = json["annotations"].toArray();
        for (const QJsonValueRef line : linesArray) {
            QJsonObject lineObject = line.toObject();
            if (lineObject.isEmpty()) {
                continue;
            }
            if (lineObject["type"].toString() != "offset") {
                continue;
            }
            RCodeMetaItem *mi = r_codemeta_item_new();
            mi->start = lineObject["start"].toInt();
            mi->end = lineObject["end"].toInt();
            bool ok;
            mi->type = R_CODEMETA_TYPE_OFFSET;
            mi->offset.offset = lineObject["offset"].toVariant().toULongLong(&ok);
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
        emit finished(code);
    });
    task->startTask();
}
