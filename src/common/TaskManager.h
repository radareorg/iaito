#ifndef TASKMANAGER_H
#define TASKMANAGER_H

#include "core/IaitoCommon.h"
#include "AsyncTask.h"
#include "R2Task.h"

#include <QObject>
#include <QMap>
#include <QQueue>
#include <QMutex>
#include <QElapsedTimer>
#include <QThreadPool>
#include <QSharedPointer>

enum class TaskPriority {
    Critical = 0,   // Immediate UI response tasks
    High = 1,       // User-initiated commands
    Normal = 2,     // Standard operations
    Low = 3         // Background analysis, etc.
};

enum class TaskState {
    Queued,
    Running,
    Completed,
    Cancelled,
    Failed
};

class IAITO_EXPORT Task : public QObject
{
    Q_OBJECT

public:
    using Ptr = QSharedPointer<Task>;

    Task(QString title, TaskPriority priority = TaskPriority::Normal);
    virtual ~Task();

    TaskPriority priority() const { return m_priority; }
    TaskState state() const { return m_state; }
    bool isCancellable() const { return m_cancellable; }
    const QString &title() const { return m_title; }
    const QString &description() const { return m_description; }
    qint64 elapsedTime() const;
    int progress() const { return m_progress; }

    virtual void cancel();

protected:
    virtual void run() = 0;
    void setState(TaskState state);
    void setDescription(const QString &description);
    void setProgress(int progress);
    void setCancellable(bool cancellable);
    bool shouldCancel() const { return m_shouldCancel; }

private:
    friend class TaskManager;
    QString m_title;
    QString m_description;
    TaskPriority m_priority;
    TaskState m_state;
    bool m_cancellable;
    bool m_shouldCancel;
    int m_progress;
    QElapsedTimer m_timer;

signals:
    void stateChanged(TaskState state);
    void progressChanged(int progress);
    void descriptionChanged(const QString &description);
    void finished();
};

class IAITO_EXPORT AsyncCommandTask : public Task 
{
    Q_OBJECT

public:
    AsyncCommandTask(const QString &title, const QString &cmd, 
                    TaskPriority priority = TaskPriority::Normal);
    ~AsyncCommandTask();

    QString getOutput() const { return m_output; }

protected:
    void run() override;
    void cancel() override;

private:
    QString m_cmd;
    QString m_output;
    QSharedPointer<R2Task> r2Task;
};

class IAITO_EXPORT TaskManager : public QObject
{
    Q_OBJECT

public:
    static TaskManager *getInstance();

    TaskManager(QObject *parent = nullptr);
    ~TaskManager();

    void startTask(Task::Ptr task);
    QList<Task::Ptr> getAllTasks() const;
    QList<Task::Ptr> getRunningTasks() const;
    void cancelTask(quint64 taskId);
    void cancelAllTasks();
    
    // Convenience methods for common tasks
    Task::Ptr createAsyncCommand(const QString &title, const QString &cmd, 
                              TaskPriority priority = TaskPriority::Normal);

private:
    static TaskManager *instance;
    
    QThreadPool m_threadPool;
    QMap<quint64, Task::Ptr> m_tasks;
    // Mutex guarding m_tasks; mutable to allow locking in const methods
    mutable QMutex m_tasksMutex;
    quint64 m_nextTaskId;
    
    void processNext();
    quint64 registerTask(Task::Ptr task);
    void cleanupCompletedTasks();

signals:
    void taskAdded(quint64 taskId);
    void taskRemoved(quint64 taskId);
    void taskStateChanged(quint64 taskId, TaskState state);
};

#endif // TASKMANAGER_H