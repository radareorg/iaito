#ifndef PYTHONMANAGER_H
#define PYTHONMANAGER_H

#ifdef IAITO_ENABLE_PYTHON_BINDINGS

#include <QObject>

typedef struct _ts PyThreadState;
class QString;

class PythonManager : public QObject
{
    Q_OBJECT

public:
    static PythonManager *getInstance();

    PythonManager();
    ~PythonManager();

    void initialize();
    void shutdown();

    void addPythonPath(const QString &path);

    void restoreThread();
    void saveThread();

    /**
     * @brief RAII Helper class to call restoreThread() and saveThread()
     * automatically
     *
     * As long as an object of this class is in scope, the Python thread will
     * remain restored.
     */
    class ThreadHolder
    {
    public:
        ThreadHolder() { getInstance()->restoreThread(); }
        ~ThreadHolder() { getInstance()->saveThread(); }
    };

signals:
    void willShutDown();

private:
    PyThreadState *pyThreadState = nullptr;
    int pyThreadStateCounter = 0;
};

#define Python() (PythonManager::getInstance())

#endif // IAITO_ENABLE_PYTHON_BINDINGS

#endif // PYTHONMANAGER_H
