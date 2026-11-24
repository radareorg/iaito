#ifndef IAITO_BINARY_PATCH_TASK_H
#define IAITO_BINARY_PATCH_TASK_H

#include <QtGlobal>

#if QT_CONFIG(process)
#include "common/AsyncTask.h"
#include <atomic>

class QIODevice;

class BinaryPatchTask : public AsyncTask
{
public:
    BinaryPatchTask(QString tool, QString patch, QString source, QString target);
    QString getTitle() override;
    void interrupt() override;
    bool wasCancelled() const { return cancelled.load(); }
    QString errorString() const { return error; }

protected:
    void runTask() override;

private:
    bool transfer(QIODevice &source, QIODevice &target);
    QString tool;
    QString patch;
    QString source;
    QString target;
    QString error;
    std::atomic_bool cancelled{false};
};
#endif

#endif
