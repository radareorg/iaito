
#include "R2AiDecompiler.h"
#include "Iaito.h"

R2AiDecompiler::R2AiDecompiler(QObject *parent)
    : Decompiler("r2ai", "r2ai", parent)
{
    task = nullptr;
}

bool R2AiDecompiler::isAvailable()
{
    return Core()->cmdList("e cmd.pdc=?").contains(QStringLiteral("r2ai"));
}

RCodeMeta *R2AiDecompiler::decompileSync(RVA addr)
{
    auto document = Core()->cmd("r2ai -d @ " + QString::number(addr));
    RCodeMeta *code = r_codemeta_new("");
    code->code = strdup(document.toStdString().c_str());
    return code;
}

void R2AiDecompiler::decompileAt(RVA addr)
{
    if (task) {
        return;
    }
    task = new R2Task("r2ai -d @ " + QString::number(addr));
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
