
#ifndef R2TASK_H
#define R2TASK_H

#include "core/Iaito.h"

class R2Task : public QObject
{
    Q_OBJECT

private:
    RCoreTask *task;

    static void taskFinishedCallback(void *user, char *res);
    void taskFinished();
    void taskFinished(char *res);

    QByteArray resultRaw; // copy of the task result, populated in the callback

public:
    using Ptr = QSharedPointer<R2Task>;

    explicit R2Task(const QString &cmd, bool transient = true);
    ~R2Task();

    void startTask();
    void breakTask();
    void joinTask();

    QString getResult();
    QJsonDocument getResultJson();
    const char *getResultRaw();

signals:
    void finished();
};

#endif // R2TASK_H
