
#include "R2Task.h"

R2Task::R2Task(const QString &cmd, bool transient)
{
    task = r_core_task_new(
        Core()->core(),
        true,
        cmd.toLocal8Bit().constData(),
        static_cast<RCoreTaskCallback>(&R2Task::taskFinishedCallback),
        this);
    if (task) {
        task->transient = transient;
        r_core_task_incref(task);
    }
}

R2Task::~R2Task()
{
    r_core_task_decref(task);
}

void R2Task::taskFinishedCallback(void *user, char *res)
{
    reinterpret_cast<R2Task *>(user)->taskFinished(res);
}

void R2Task::taskFinished(char *res)
{
    // Copy the result into resultRaw so it remains valid after r2 frees its
    // internal buffer. Doing this here (in the r2 task callback) ensures we
    // capture the output before it can be destroyed.
    if (res) {
        resultRaw = QByteArray(res);
    } else {
        resultRaw.clear();
    }
    emit finished();
}

void R2Task::startTask()
{
    r_core_task_enqueue(&Core()->core_->tasks, task);
}

void R2Task::breakTask()
{
    if (task) {
        r_core_task_break(&Core()->core_->tasks, task->id);
    }
}

void R2Task::joinTask()
{
    if (task) {
        r_core_task_join(&Core()->core_->tasks, nullptr, task->id);
    }
}

QString R2Task::getResult()
{
    if (!resultRaw.isEmpty()) {
        return QString::fromUtf8(resultRaw.constData());
    }
    if (task == nullptr) {
        return QString("");
    }
    return QString::fromUtf8(task->res ? task->res : "");
}

QJsonDocument R2Task::getResultJson()
{
    if (!resultRaw.isEmpty()) {
        return Core()->parseJson(resultRaw.constData(), task ? task->cmd : nullptr);
    }
    if (task == nullptr) {
        return QJsonDocument();
    }
    return Core()->parseJson(task->res, task->cmd);
}

const char *R2Task::getResultRaw()
{
    if (!resultRaw.isEmpty()) {
        return resultRaw.constData();
    }
    return task != nullptr ? task->res : nullptr;
}
