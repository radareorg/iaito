
#include "R2GhidraCmdDecompiler.h"
#include "Iaito.h"

#include <QJsonObject>
#include <QJsonArray>

/*
RCodeMeta *Decompiler::makeWarning(QString warningMessage){
    std::string temporary = warningMessage.toStdString();
    return r_codemeta_new(temporary.c_str());
}
*/

R2GhidraCmdDecompiler::R2GhidraCmdDecompiler(QObject *parent)
    : Decompiler("pdg", "pdg", parent)
{
    task = nullptr;
}

bool R2GhidraCmdDecompiler::isAvailable()
{
    return Core()->cmdList("e cmd.pdc=?").contains(QStringLiteral("pdg"));
}

void R2GhidraCmdDecompiler::decompileAt(RVA addr)
{
    if (task) {
        return;
    }
    task = new R2Task("pdgj @ " + QString::number(addr));
    connect(task, &R2Task::finished, this, [this]() {
        QJsonObject json = task->getResultJson().object();
        delete task;
        task = nullptr;
        if (json.isEmpty()) {
            emit finished(Decompiler::makeWarning(tr("Failed to parse JSON from r2ghidra")));
            return;
        }
        RCodeMeta *code = r_codemeta_new (nullptr);
        QString codeString = json["code"].toString();
        QJsonArray linesArray = json["annotations"].toArray();
        for (const QJsonValueRef line : linesArray) {
            QJsonObject lineObject = line.toObject();
            if (lineObject.isEmpty()) {
                continue;
            }
            if (lineObject["type"].toString() != "offset") {
                continue;
            }
            RCodeMetaItem *mi = r_codemeta_item_new ();
            mi->start = lineObject["start"].toInt();
            mi->end = lineObject["end"].toInt();
            bool ok;
            mi->type = R_CODEMETA_TYPE_OFFSET;
            mi->offset.offset = lineObject["offset"].toVariant().toULongLong(&ok);
            r_codemeta_add_annotation(code, mi);
            r_codemeta_item_free (mi, NULL);
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
