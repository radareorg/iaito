
#include "R2DecaiDecompiler.h"
#include "Iaito.h"

#include <QJsonArray>
#include <QJsonObject>

R2DecaiDecompiler::R2DecaiDecompiler(QObject *parent)
    : Decompiler("decai", "decai", parent)
{
    task = nullptr;
}

bool R2DecaiDecompiler::isAvailable()
{
    return Core()->cmdList("e cmd.pdc=?").contains(QStringLiteral("decai"));
}

RCodeMeta *R2DecaiDecompiler::decompileSync(RVA addr)
{
    auto document = Core()->cmd("decai -d @ " + QString::number(addr));
    RCodeMeta *code = r_codemeta_new("");
    // TODO: decai have no json output or source-line information
    code->code = strdup(document.toStdString().c_str());
    return code;
}

void R2DecaiDecompiler::decompileAt(RVA addr)
{
    if (task) {
        return;
    }
    task = new R2Task("decai -d @ " + QString::number(addr));
    connect(task, &R2Task::finished, this, [this]() {
        QString text = task->getResult();
        delete task;
        task = nullptr;
        RCodeMeta *code = r_codemeta_new("");
        code->code = strdup(text.toStdString().c_str());
        emit finished(code);
    });
    task->startTask();
}
