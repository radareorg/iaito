#include "DecompileTask.h"
#include "core/Iaito.h"

#include <QDebug>

DecompileTask::DecompileTask(Decompiler *decompiler, RVA addr, QObject *parent)
    : AsyncTask()
    , decompiler(decompiler)
    , addr(addr)
    , code(nullptr)
{}

DecompileTask::~DecompileTask() {}

void DecompileTask::interrupt()
{
    AsyncTask::interrupt();
#if R2_VERSION_NUMBER >= 50909
    RCore *core = Core()->core_;
    if (core && core->cons && core->cons->context) {
        core->cons->context->breaked = true;
    }
#else
    if (r_cons_singleton() && r_cons_singleton()->context) {
        r_cons_singleton()->context->breaked = true;
    }
#endif
    // Send SIGINT to ourselves to make r2 interrupt long-running commands.
    raise(SIGINT);
}

void DecompileTask::runTask()
{
    if (!decompiler) {
        code = Decompiler::makeWarning(QObject::tr("No decompiler available"));
        return;
    }

    // Run the decompiler synchronously in this background thread.
    code = decompiler->decompileSync(addr);
}
