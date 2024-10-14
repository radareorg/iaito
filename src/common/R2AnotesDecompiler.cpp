
#include "R2AnotesDecompiler.h"
#include "Iaito.h"

#include <QJsonArray>
#include <QJsonObject>

R2AnotesDecompiler::R2AnotesDecompiler(QObject *parent)
    : Decompiler("anos", "anos", parent)
{
    task = nullptr;
}

bool R2AnotesDecompiler::isAvailable()
{
    // Check if ?Vq without dots is bigger than 595
    return true;
}

RCodeMeta *R2AnotesDecompiler::decompileSync(RVA addr)
{
    auto document = Core()->cmd("anos @ " + QString::number(addr));
    RCodeMeta *code = r_codemeta_new(nullptr);
    // TODO: decai have no json output or source-line information
    code->code = strdup(document.toStdString().c_str());
    return code;
}

void R2AnotesDecompiler::decompileAt(RVA addr)
{
    if (task) {
        return;
    }
    task = new R2Task("anos @ " + QString::number(addr));
    connect(task, &R2Task::finished, this, [this]() {
        QString text = task->getResult();
        delete task;
        task = nullptr;
        RCodeMeta *code = r_codemeta_new(nullptr);
        code->code = strdup(text.toStdString().c_str());
        emit finished(code);
    });
    task->startTask();
}
