#include "common/RunScriptTask.h"
#include "core/Iaito.h"
#include "core/MainWindow.h"

RunScriptTask::RunScriptTask()
    : AsyncTask()
{}

RunScriptTask::~RunScriptTask() {}

void RunScriptTask::interrupt()
{
    AsyncTask::interrupt();
#if R2_VERSION_NUMBER >= 50909
    RCore *core = Core()->core_;
    core->cons->context->breaked = true;
#else
    r_cons_singleton()->context->breaked = true;
#endif
}

void RunScriptTask::runTask()
{
    if (!this->fileName.isNull()) {
        log(tr("Executing script..."));
        // Execute the script and log its output
        QString result = Core()->cmdTask(". " + this->fileName);
        if (!result.isEmpty()) {
            log(result);
        }
        if (isInterrupted()) {
            return;
        }
    }
}
