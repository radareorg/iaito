
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
    this->cache = new QHash<QString, RCodeMeta*>();
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
    QString k = QString::number(addr);
    bool is_cached = this->cache->contains (k);
    task = new R2Task(is_cached? "?e": "pdgj @ " + k);
    connect(task, &R2Task::finished, this, [this, addr, k, is_cached]() {
        if (is_cached) {
            RCodeMeta *code = r_codemeta_clone (this->cache->value(k));
            delete task;
            emit finished(code);
            return;
        }
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
            r_codemeta_add_item (code, mi);
        }

        for (const auto line : json["errors"].toArray()) {
            if (!line.isString()) {
                continue;
            }
            codeString.append(line.toString() + "\n");
        }
        std::string tmp = codeString.toStdString();
        code->code = strdup(tmp.c_str());
	if (!is_cached) {
            this->cache->insert(k, r_codemeta_clone (code));
	}
        emit finished(code);
    });
    task->startTask();
}
