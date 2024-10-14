
#ifndef ASYNCTASK_H
#define ASYNCTASK_H

#include "core/IaitoCommon.h"

#include <QElapsedTimer>
#include <QList>
#include <QMutex>
#include <QRunnable>
#include <QSharedPointer>
#include <QThreadPool>

class AsyncTaskManager;

// 8 MB should be enough for deep analysis.. default is 512KB
#define R2THREAD_STACK_SIZE 1024 * 1024 * 8

class IAITO_EXPORT AsyncTask : public QObject, public QRunnable
{
    Q_OBJECT

    friend class AsyncTaskManager;

public:
    using Ptr = QSharedPointer<AsyncTask>;

    AsyncTask();
    ~AsyncTask();

    void run() override final;

    void wait();
    bool wait(int timeout);
    virtual void interrupt();
    bool isInterrupted() { return interrupted; }
    bool isRunning() { return running; }

    const QString &getLog() { return logBuffer; }
    const QElapsedTimer &getTimer() { return timer; }
    qint64 getElapsedTime() { return timer.isValid() ? timer.elapsed() : 0; }

    virtual QString getTitle() { return QString(); }

protected:
    virtual void runTask() = 0;

    void log(QString s);

signals:
    void finished();
    void logChanged(const QString &log);

private:
    bool running;
    bool interrupted;
    QMutex runningMutex;

    QElapsedTimer timer;
    QString logBuffer;

    void prepareRun();
};

class AsyncTaskManager : public QObject
{
    Q_OBJECT

private:
    QThreadPool *threadPool;
    QList<AsyncTask::Ptr> tasks;

public:
    explicit AsyncTaskManager(QObject *parent = nullptr);
    ~AsyncTaskManager();

    void start(AsyncTask::Ptr task);
    bool getTasksRunning();

signals:
    void tasksChanged();
};

#endif // ASYNCTASK_H
