
#include "CommandTask.h"
#include "TempConfig.h"

CommandTask::CommandTask(
    const QString &cmd, ColorMode colorMode, bool outFormatHtml, ExecutionMode executionMode)
    : cmd(cmd)
    , colorMode(colorMode)
    , outFormatHtml(outFormatHtml)
    , executionMode(executionMode)
{}

void CommandTask::runTask()
{
    TempConfig tempConfig;
    tempConfig.set("scr.color", colorMode);
    auto res = executionMode == ExecutionMode::DIRECT_CMD ? Core()->cmd(cmd) : Core()->cmdTask(cmd);
    if (outFormatHtml) {
        res = IaitoCore::ansiEscapeToHtml(res);
    }
    emit finished(res);
}
