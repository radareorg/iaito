
#include "R2GhidraCmdDecompiler.h"
#include "Iaito.h"

#include <QJsonArray>
#include <QJsonObject>

static RSyntaxHighlightType syntaxHighlightTypeFromString(const QString &str)
{
    if (str == "keyword") {
        return R_SYNTAX_HIGHLIGHT_TYPE_KEYWORD;
    } else if (str == "comment") {
        return R_SYNTAX_HIGHLIGHT_TYPE_COMMENT;
    } else if (str == "datatype") {
        return R_SYNTAX_HIGHLIGHT_TYPE_DATATYPE;
    } else if (str == "function_name") {
        return R_SYNTAX_HIGHLIGHT_TYPE_FUNCTION_NAME;
    } else if (str == "function_parameter") {
        return R_SYNTAX_HIGHLIGHT_TYPE_FUNCTION_PARAMETER;
    } else if (str == "local_variable") {
        return R_SYNTAX_HIGHLIGHT_TYPE_LOCAL_VARIABLE;
    } else if (str == "constant_variable") {
        return R_SYNTAX_HIGHLIGHT_TYPE_CONSTANT_VARIABLE;
    } else if (str == "global_variable") {
        return R_SYNTAX_HIGHLIGHT_TYPE_GLOBAL_VARIABLE;
    }
    return R_SYNTAX_HIGHLIGHT_TYPE_KEYWORD;
}

static void parseCodeMetaJson(RCodeMeta *code, const QJsonArray &annotations)
{
    for (const auto &line : annotations) {
        QJsonObject obj = line.toObject();
        if (obj.isEmpty()) {
            continue;
        }
        QString type = obj["type"].toString();
        RCodeMetaItem *mi = r_codemeta_item_new();
        mi->start = obj["start"].toInt();
        mi->end = obj["end"].toInt();

        if (type == "offset") {
            mi->type = R_CODEMETA_TYPE_OFFSET;
            bool ok;
            mi->offset.offset = obj["offset"].toVariant().toULongLong(&ok);
        } else if (type == "syntax_highlight") {
            mi->type = R_CODEMETA_TYPE_SYNTAX_HIGHLIGHT;
            mi->syntax_highlight.type = syntaxHighlightTypeFromString(
                obj["syntax_highlight"].toString());
        } else if (type == "function_name") {
            mi->type = R_CODEMETA_TYPE_FUNCTION_NAME;
            mi->reference.name = strdup(obj["name"].toString().toUtf8().constData());
            bool ok;
            mi->reference.offset = obj["offset"].toVariant().toULongLong(&ok);
        } else if (type == "global_variable") {
            mi->type = R_CODEMETA_TYPE_GLOBAL_VARIABLE;
            bool ok;
            mi->reference.offset = obj["offset"].toVariant().toULongLong(&ok);
        } else if (type == "constant_variable") {
            mi->type = R_CODEMETA_TYPE_CONSTANT_VARIABLE;
            bool ok;
            mi->reference.offset = obj["offset"].toVariant().toULongLong(&ok);
        } else if (type == "local_variable") {
            mi->type = R_CODEMETA_TYPE_LOCAL_VARIABLE;
            mi->variable.name = strdup(obj["name"].toString().toUtf8().constData());
        } else if (type == "function_parameter") {
            mi->type = R_CODEMETA_TYPE_FUNCTION_PARAMETER;
            mi->variable.name = strdup(obj["name"].toString().toUtf8().constData());
        } else {
            r_codemeta_item_free(mi);
            continue;
        }
        r_codemeta_add_item(code, mi);
    }
}

R2GhidraCmdDecompiler::R2GhidraCmdDecompiler(QObject *parent)
    : Decompiler("pdg", "pdg", parent)
{
    task = nullptr;
}

bool R2GhidraCmdDecompiler::isAvailable()
{
    return Core()->cmdList("e cmd.pdc=?").contains(QStringLiteral("pdg"));
}

RCodeMeta *R2GhidraCmdDecompiler::decompileSync(RVA addr)
{
    auto document = Core()->cmdj("pdgj @ " + QString::number(addr));
    QJsonObject json = document.object();
    if (json.isEmpty()) {
        return NULL;
    }
    QString codeString = json["code"].toString();
    RCodeMeta *code = r_codemeta_new("");
    parseCodeMetaJson(code, json["annotations"].toArray());

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
        QString codeString = json["code"].toString();
        RCodeMeta *code = r_codemeta_new("");
        parseCodeMetaJson(code, json["annotations"].toArray());

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
