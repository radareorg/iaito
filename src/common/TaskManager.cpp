#include "TaskManager.h"
#include "core/Iaito.h"
#include <QDebug>

TaskManager *TaskManager::instance = nullptr;

TaskManager *TaskManager::getInstance()
{
    if (!instance) {
        instance = new TaskManager();
    }
    return instance;
}

TaskManager::TaskManager(QObject *parent) 
    : QObject(parent)
    , m_nextTaskId(1)
{
#if QT_VERSION >= 0x050a00
    m_threadPool.setStackSize(R2THREAD_STACK_SIZE);
#endif
    m_threadPool.setMaxThreadCount(QThread::idealThreadCount());
}

TaskManager::~TaskManager() 
{
    cancelAllTasks();
    m_threadPool.waitForDone();
}

void TaskManager::startTask(Task::Ptr task)
{
    quint64 id = registerTask(task);
    
    connect(task.data(), &Task::finished, this, [this, id]() {
        emit taskStateChanged(id, m_tasks[id]->state());
    });

    task->setState(TaskState::Queued);
    emit taskAdded(id);
    
    // Run the task in the thread pool using a lambda wrapper
    m_threadPool.start([task]() {
        if (task->state() != TaskState::Cancelled) {
            task->setState(TaskState::Running);
            task->m_timer.start();
            task->run();
            if (task->state() == TaskState::Running) {
                task->setState(TaskState::Completed);
            }
            emit task->finished();
        }
    });
}

QList<Task::Ptr> TaskManager::getAllTasks() const
{
    QMutexLocker locker(&m_tasksMutex);
    return m_tasks.values();
}

QList<Task::Ptr> TaskManager::getRunningTasks() const
{
    QMutexLocker locker(&m_tasksMutex);
    QList<Task::Ptr> runningTasks;
    for (const auto &task : m_tasks) {
        if (task->state() == TaskState::Running) {
            runningTasks.append(task);
        }
    }
    return runningTasks;
}

void TaskManager::cancelTask(quint64 taskId)
{
    QMutexLocker locker(&m_tasksMutex);
    if (m_tasks.contains(taskId)) {
        Task::Ptr task = m_tasks[taskId];
        if (task->isCancellable() && 
            (task->state() == TaskState::Queued || task->state() == TaskState::Running)) {
            task->cancel();
            task->setState(TaskState::Cancelled);
            emit taskStateChanged(taskId, TaskState::Cancelled);
        }
    }
}

void TaskManager::cancelAllTasks()
{
    QMutexLocker locker(&m_tasksMutex);
    for (const auto &task : m_tasks) {
        if (task->isCancellable() && 
            (task->state() == TaskState::Queued || task->state() == TaskState::Running)) {
            task->cancel();
            task->setState(TaskState::Cancelled);
            emit taskStateChanged(task->m_title.toULongLong(), TaskState::Cancelled);
        }
    }
}

quint64 TaskManager::registerTask(Task::Ptr task)
{
    QMutexLocker locker(&m_tasksMutex);
    quint64 id = m_nextTaskId++;
    m_tasks[id] = task;
    
    connect(task.data(), &Task::stateChanged, this, [this, id](TaskState state) {
        emit taskStateChanged(id, state);
        if (state == TaskState::Completed || state == TaskState::Failed || state == TaskState::Cancelled) {
            // Schedule cleanup after task is done
            QTimer::singleShot(30000, this, &TaskManager::cleanupCompletedTasks);
        }
    });
    
    return id;
}

void TaskManager::cleanupCompletedTasks()
{
    QMutexLocker locker(&m_tasksMutex);
    QList<quint64> keysToRemove;
    
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        TaskState state = it.value()->state();
        if (state == TaskState::Completed || state == TaskState::Failed || state == TaskState::Cancelled) {
            keysToRemove.append(it.key());
        }
    }
    
    for (quint64 key : keysToRemove) {
        m_tasks.remove(key);
        emit taskRemoved(key);
    }
}

Task::Ptr TaskManager::createAsyncCommand(const QString &title, const QString &cmd, TaskPriority priority)
{
    return Task::Ptr(new AsyncCommandTask(title, cmd, priority));
}

// Task implementation

Task::Task(QString title, TaskPriority priority)
    : m_title(title)
    , m_description("")
    , m_priority(priority)
    , m_state(TaskState::Queued)
    , m_cancellable(false)
    , m_shouldCancel(false)
    , m_progress(0)
{
}

Task::~Task()
{
}

qint64 Task::elapsedTime() const
{
    return m_timer.isValid() ? m_timer.elapsed() : 0;
}

void Task::cancel()
{
    m_shouldCancel = true;
}

void Task::setState(TaskState state)
{
    if (m_state != state) {
        m_state = state;
        emit stateChanged(state);
    }
}

void Task::setDescription(const QString &description)
{
    if (m_description != description) {
        m_description = description;
        emit descriptionChanged(description);
    }
}

void Task::setProgress(int progress)
{
    if (m_progress != progress) {
        m_progress = progress;
        emit progressChanged(progress);
    }
}

void Task::setCancellable(bool cancellable)
{
    m_cancellable = cancellable;
}

// AsyncCommandTask implementation

AsyncCommandTask::AsyncCommandTask(const QString &title, const QString &cmd, TaskPriority priority)
    : Task(title, priority)
    , m_cmd(cmd)
{
    setCancellable(true);
}

AsyncCommandTask::~AsyncCommandTask()
{
}

void AsyncCommandTask::run()
{
    setDescription("Running: " + m_cmd);
    
    // Create and start a radare2 task
    r2Task = QSharedPointer<R2Task>(new R2Task(m_cmd));
    
    connect(r2Task.data(), &R2Task::finished, this, [this]() {
        m_output = r2Task->getResult();
        setProgress(100);
    });
    
    r2Task->startTask();
    
    // Wait for the task to complete or be cancelled
    while (!shouldCancel()) {
        if (Core()->getAsyncTaskManager()->getTasksRunning()) {
            QThread::msleep(100);
        } else {
            break;
        }
    }
    
    if (shouldCancel()) {
        r2Task->breakTask();
    }
}

void AsyncCommandTask::cancel()
{
    Task::cancel();
    
    if (r2Task) {
        r2Task->breakTask();
    }
}