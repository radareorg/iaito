#ifndef DECOMPILETASK_H
#define DECOMPILETASK_H

#include "common/AsyncTask.h"
#include "common/Decompiler.h"
#include "core/IaitoCommon.h"

#include <signal.h>

class DecompileTask : public AsyncTask
{
    Q_OBJECT

public:
    explicit DecompileTask(Decompiler *decompiler, RVA addr, QObject *parent = nullptr);
    ~DecompileTask() override;

    void interrupt() override;

    RCodeMeta *getCode() { return code; }

protected:
    void runTask() override;

private:
    Decompiler *decompiler;
    RVA addr;
    RCodeMeta *code;
};

#endif // DECOMPILETASK_H
