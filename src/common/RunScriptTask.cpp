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
    r_cons_singleton()->context->breaked = true;
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
