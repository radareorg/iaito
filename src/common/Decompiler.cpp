
#include "Decompiler.h"
#include "Iaito.h"

#include <QJsonArray>
#include <QJsonObject>

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

R2DecDecompiler::R2DecDecompiler(QObject *parent)
    : Decompiler("r2dec", "r2dec", parent)
{
    task = nullptr;
}

bool R2DecDecompiler::isAvailable()
{
    return Core()->cmdList("e cmd.pdc=?").contains(QStringLiteral("pdd"));
}

RCodeMeta *R2DecDecompiler::decompileSync(RVA addr)
{
    // auto document = Core()->cmdj("pddj @ " + QString::number(addr));
    auto document = Core()->cmdjAt("pddj", addr);
    QJsonObject json = document.object();
    if (json.isEmpty()) {
        //    emit finished(Decompiler::makeWarning(tr("Failed to parse JSON
        //    from pdc")));
        return NULL;
    }
    // note that r2dec doesnt have the "text" object like pdc does. so we cant
    // reuse code
    RCodeMeta *code = r_codemeta_new(nullptr);
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
        RCodeMetaItem *mi = r_codemeta_item_new();
        mi->start = codeString.length();
        codeString.append(lineObject["str"].toString() + "\n");
        mi->end = codeString.length();
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

void R2DecDecompiler::decompileAt(RVA addr)
{
    if (task) {
        return;
    }

    task = new R2Task("pddj @ " + QString::number(addr));
    connect(task, &R2Task::finished, this, [this]() {
        QJsonObject json = task->getResultJson().object();
        delete task;
        task = nullptr;
        if (json.isEmpty()) {
            emit finished(Decompiler::makeWarning(tr("Failed to parse JSON from r2dec")));
            return;
        }
        RCodeMeta *code = r_codemeta_new(nullptr);
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
            RCodeMetaItem *mi = r_codemeta_item_new();
            mi->start = codeString.length();
            codeString.append(lineObject["str"].toString() + "\n");
            mi->end = codeString.length();
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
