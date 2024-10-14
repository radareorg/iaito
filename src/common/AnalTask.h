#ifndef ANALTHREAD_H
#define ANALTHREAD_H

#include "common/AsyncTask.h"
#include "common/InitialOptions.h"
#include "core/Iaito.h"

class IaitoCore;
class MainWindow;
class InitialOptionsDialog;

class AnalTask : public AsyncTask
{
    Q_OBJECT

public:
    explicit AnalTask();
    ~AnalTask();

    QString getTitle() override;

    void setOptions(const InitialOptions &options) { this->options = options; }

    void interrupt() override;

    bool getOpenFileFailed() { return openFailed; }

protected:
    void runTask() override;

signals:
    void openFileFailed();

private:
    InitialOptions options;

    bool openFailed = false;
};

#endif // ANALTHREAD_H
