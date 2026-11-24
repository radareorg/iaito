#include "common/BinaryPatchTask.h"

#if QT_CONFIG(process)
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>

BinaryPatchTask::BinaryPatchTask(QString tool, QString patch, QString source, QString target)
    : tool(std::move(tool))
    , patch(std::move(patch))
    , source(std::move(source))
    , target(std::move(target))
{}

QString BinaryPatchTask::getTitle()
{
    return tr("Apply Binary Patch");
}

void BinaryPatchTask::interrupt()
{
    cancelled = true;
}

bool BinaryPatchTask::transfer(QIODevice &input, QIODevice &output)
{
    while (!cancelled && !input.atEnd()) {
        const QByteArray bytes = input.read(64 * 1024);
        if (bytes.isEmpty()) {
            error = input.errorString();
            return false;
        }
        if (output.write(bytes) != bytes.size()) {
            error = output.errorString();
            return false;
        }
    }
    return !cancelled;
}

void BinaryPatchTask::runTask()
{
    QTemporaryDir stage(QFileInfo(target).dir().filePath(QStringLiteral(".iaito-patch-XXXXXX")));
    if (!stage.isValid()) {
        error = stage.errorString();
        return;
    }
    log(tr("Copying %1").arg(source));
    QFile input(source);
    QFile staged(stage.filePath(QStringLiteral("binary")));
    if (!input.open(QIODevice::ReadOnly)) {
        error = input.errorString();
        return;
    }
    const auto permissions = input.permissions();
    if (!staged.open(QIODevice::WriteOnly)) {
        error = staged.errorString();
        return;
    }
    if (!transfer(input, staged) || !staged.flush()) {
        if (error.isEmpty() && !cancelled) {
            error = staged.errorString();
        }
        return;
    }
    staged.close();
    input.close();

    log(tr("Applying %1").arg(patch));
    QProcess process;
    process.start(tool, {"-B", patch, staged.fileName()});
    while (process.state() != QProcess::NotRunning && !process.waitForFinished(100)) {
        if (cancelled) {
            process.kill();
            process.waitForFinished();
            return;
        }
    }
    if (cancelled) {
        return;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode()
        || process.error() == QProcess::FailedToStart) {
        error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (error.isEmpty()) {
            error = (process.error() == QProcess::FailedToStart)
                        ? process.errorString()
                        : tr("rapatch2 exited with code %1.").arg(process.exitCode());
        }
        return;
    }

    QSaveFile output(target);
    if (!staged.open(QIODevice::ReadOnly)) {
        error = staged.errorString();
        return;
    }
    if (!output.open(QIODevice::WriteOnly) || !output.setPermissions(permissions)) {
        error = output.errorString();
        return;
    }
    if (transfer(staged, output) && !cancelled && !output.commit()) {
        error = output.errorString();
    }
}
#endif
