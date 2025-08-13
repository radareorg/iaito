#ifndef BACKGROUNDANALTASK_H
#define BACKGROUNDANALTASK_H

#include "common/TaskManager.h"
#include "common/InitialOptions.h"
#include "core/Iaito.h"

class IaitoCore;
class MainWindow;

class BackgroundAnalTask : public Task
{
    Q_OBJECT

public:
    explicit BackgroundAnalTask(const InitialOptions &options);
    ~BackgroundAnalTask();

protected:
    void run() override;
    void cancel() override;

signals:
    void openFileFailed();

private:
    InitialOptions options;
    bool openFailed = false;
};

#endif // BACKGROUNDANALTASK_H