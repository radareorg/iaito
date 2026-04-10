#ifdef IAITO_ENABLE_PYTHON_BINDINGS

#include "PythonManager.h"
#include "Iaito.h"

#include <QCoreApplication>
#include <QDebug>

#include <pyside.h>
#include <shiboken.h>
#include <signalmanager.h>

#include "QtResImporter.h"

static PythonManager *uniqueInstance = nullptr;

PythonManager *PythonManager::getInstance()
{
    if (!uniqueInstance) {
        uniqueInstance = new PythonManager();
    }
    return uniqueInstance;
}

PythonManager::PythonManager() {}

PythonManager::~PythonManager() {}
extern "C" PyObject *PyInit_IaitoBindings();

void PythonManager::initialize()
{
    PyImport_AppendInittab("_qtres", &PyInit_qtres);
    PyImport_AppendInittab("IaitoBindings", &PyInit_IaitoBindings);
    Py_Initialize();
    PyEval_InitThreads();
    pyThreadStateCounter = 1; // we have the thread now => 1

    RegQtResImporter();

    saveThread();
}

static void pySideDestructionVisitor(SbkObject *pyObj, void *data)
{
    void **realData = reinterpret_cast<void **>(data);
    auto pyQApp = reinterpret_cast<SbkObject *>(realData[0]);
    auto pyQObjectType = reinterpret_cast<PyTypeObject *>(realData[1]);

    if (pyObj == pyQApp || !PyObject_TypeCheck(pyObj, pyQObjectType)) {
        return;
    }
    if (!Shiboken::Object::hasOwnership(pyObj) || !Shiboken::Object::isValid(pyObj, false)) {
        return;
    }

    const char *reprStr = "";
    PyObject *repr = PyObject_Repr(reinterpret_cast<PyObject *>(pyObj));
    PyObject *reprBytes;
    if (repr) {
        reprBytes = PyUnicode_AsUTF8String(repr);
        reprStr = PyBytes_AsString(reprBytes);
    }
    qWarning() << "Warning: QObject from Python remaining (leaked from plugin?):" << reprStr;
    if (repr) {
        Py_DecRef(reprBytes);
        Py_DecRef(repr);
    }

    Shiboken::Object::setValidCpp(pyObj, false);
    Py_BEGIN_ALLOW_THREADS Shiboken::callCppDestructor<QObject>(
        Shiboken::Object::cppPointer(pyObj, pyQObjectType));
    Py_END_ALLOW_THREADS
};

void PythonManager::shutdown()
{
    emit willShutDown();

    restoreThread();

    // This is necessary to prevent a segfault when the IaitoCore instance is
    // deleted after the Shiboken::BindingManager
    Core()->setProperty("_PySideInvalidatePtr", QVariant());

    // see PySide::destroyQCoreApplication()
    PySide::SignalManager::instance().clear();
    Shiboken::BindingManager &bm = Shiboken::BindingManager::instance();
    SbkObject *pyQApp = bm.retrieveWrapper(QCoreApplication::instance());
    PyTypeObject *pyQObjectType = Shiboken::Conversions::getPythonTypeObject("QObject*");
    void *data[2] = {pyQApp, pyQObjectType};
    bm.visitAllPyObjects(&pySideDestructionVisitor, &data);

    PySide::runCleanupFunctions();

    Py_Finalize();
}

void PythonManager::addPythonPath(const QString &path)
{
    restoreThread();

    QByteArray pathBytes = path.toLocal8Bit();
    PyObject *sysModule = PyImport_ImportModule("sys");
    if (!sysModule) {
        return;
    }
    PyObject *pythonPath = PyObject_GetAttrString(sysModule, "path");
    if (!pythonPath) {
        return;
    }
    PyObject *append = PyObject_GetAttrString(pythonPath, "append");
    if (!append) {
        return;
    }
    PyEval_CallFunction(append, "(s)", pathBytes.constData());

    saveThread();
}

void PythonManager::restoreThread()
{
    pyThreadStateCounter++;
    if (pyThreadStateCounter == 1 && pyThreadState) {
        PyEval_RestoreThread(pyThreadState);
    }
}

void PythonManager::saveThread()
{
    pyThreadStateCounter--;
    if (pyThreadStateCounter == 0) {
        pyThreadState = PyEval_SaveThread();
    }
}

#endif
