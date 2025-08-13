
#include "AsyncTask.h"

#include <cstdio>
#include <cstdlib>
#include <thread>

static void signalChildProcessesAsync(int sig)
{
    // Run in detached thread to keep interrupt non-blocking.
    std::thread([sig]() {
        pid_t mypid = getpid();
        // Use ps to enumerate child processes (portable and simple).
        FILE *fp = popen("ps -axo pid,ppid", "r");
        if (!fp) {
            return;
        }
        char *line = nullptr;
        size_t len = 0;
        // Skip header line
        ssize_t read = getline(&line, &len, fp);
        (void) read;
        while ((read = getline(&line, &len, fp)) != -1) {
            // parse two integers
            long pid = 0, ppid = 0;
            if (sscanf(line, "%ld %ld", &pid, &ppid) != 2) {
                continue;
            }
            if ((pid_t) ppid == mypid && (pid_t) pid != mypid) {
                // Send the requested signal to the direct child.
                kill((pid_t) pid, sig);
            }
        }
        free(line);
        pclose(fp);
    }).detach();
}

AsyncTask::AsyncTask()
    : QObject(nullptr)
    , QRunnable()
{
    setAutoDelete(false);
    running = false;
}

AsyncTask::~AsyncTask()
{
    wait();
}

void AsyncTask::wait()
{
    runningMutex.lock();
    runningMutex.unlock();
}

bool AsyncTask::wait(int timeout)
{
    bool r = runningMutex.tryLock(timeout);
    if (r) {
        runningMutex.unlock();
    }
    return r;
}

void AsyncTask::interrupt()
{
    interrupted = true;
    // Non-blocking best-effort: signal child processes (e.g., curl spawned by r2js)
    // with SIGINT so they stop promptly. This is done asynchronously so
    // interrupt() returns immediately.
    signalChildProcessesAsync(SIGINT);
}

void AsyncTask::prepareRun()
{
    interrupted = false;
    wait();
    timer.start();
}

void AsyncTask::run()
{
    runningMutex.lock();

    running = true;

    logBuffer.clear();
    emit logChanged(logBuffer);
    runTask();

    running = false;

    emit finished();

    runningMutex.unlock();
}

void AsyncTask::log(QString s)
{
    logBuffer += s.append(QLatin1Char('\n'));
    emit logChanged(logBuffer);
}

AsyncTaskManager::AsyncTaskManager(QObject *parent)
    : QObject(parent)
{
    threadPool = new QThreadPool(this);
}

AsyncTaskManager::~AsyncTaskManager() {}

void AsyncTaskManager::start(AsyncTask::Ptr task)
{
    tasks.append(task);
    task->prepareRun();

    QWeakPointer<AsyncTask> weakPtr = task;
    connect(task.data(), &AsyncTask::finished, this, [this, weakPtr]() {
        tasks.removeOne(weakPtr);
        emit tasksChanged();
    });
#if QT_VERSION >= 0x050a00
    threadPool->setStackSize(R2THREAD_STACK_SIZE);
#endif
    threadPool->start(task.data());
    emit tasksChanged();
}

bool AsyncTaskManager::getTasksRunning()
{
    return !tasks.isEmpty();
}
