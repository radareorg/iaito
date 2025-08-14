#include "DecompileTask.h"
#include "common/Configuration.h"
#include "core/Iaito.h"

#include <QDebug>
#include <QMetaObject>

static bool loopInterrupted = false;
DecompileTask::DecompileTask(Decompiler *decompiler, RVA addr, QObject *parent)
    : AsyncTask()
    , decompiler(decompiler)
    , addr(addr)
    , code(nullptr)
{}

DecompileTask::~DecompileTask() {}

void DecompileTask::interrupt()
{
    loopInterrupted = true;
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
    // Previously we raised SIGINT in-process here which can interrupt
    // Qt/libc allocations and lead to crashes. Instead rely on radare2's
    // break flag plus signaling child processes from AsyncTask::interrupt.
}

#include <QEventLoop>
#include <QObject>

void DecompileTask::runTask()
{
    // async
    if (!decompiler) {
        code = Decompiler::makeWarning(QObject::tr("No decompiler available"));
        return;
    }
    // Respect user setting: when background mode is disabled (default), run synchronously.
    const bool runSync = !Config()->getDecompilerRunInBackground();
    if (runSync) {
        // Some decompilers and core commands expect to run on the main (GUI) thread
        // to properly collect output. Execute the synchronous decompilation on the
        // decompiler's thread (main thread) using a blocking queued invocation so
        // r2 internals behave correctly.
        RCodeMeta *result = nullptr;
        bool invoked = QMetaObject::invokeMethod(
            decompiler,
            [&result, this]() { result = decompiler->decompileSync(addr); },
            Qt::BlockingQueuedConnection);
        if (!invoked) {
            code = Decompiler::makeWarning(QObject::tr("Failed to invoke decompiler synchronously"));
        } else {
            code = result;
        }
        return;
    }

    // Prefer using the decompiler's asynchronous API so radare2 runs the
    // decompilation as a core task and does not hold the global core mutex in
    // this thread. We create a local event loop and a notifier QObject that
    // lives in this worker thread to receive the finished() signal with a
    // queued connection.

    QEventLoop loop;
    QObject notifier;
    // Ensure notifier lives in this thread (it is created here so it's fine).

    RCodeMeta *resultCode = nullptr;

    // Connect using a queued connection so the slot runs in this thread's
    // event loop when the decompiler emits finished() from the r2 task thread.
    connect(
        decompiler,
        &Decompiler::finished,
        &notifier,
        [&](RCodeMeta *c) {
            resultCode = c;
            loop.quit();
        },
        Qt::QueuedConnection);

    // Start asynchronous decompilation which will enqueue an r2 core task.
    // Ensure the call runs on the decompiler's thread (main thread) so radare2
    // internals behave correctly. Invoke the decompilation using a queued
    // invocation on the decompiler object.
    bool invoked = QMetaObject::invokeMethod(
        decompiler, [this]() { decompiler->decompileAt(addr); }, Qt::QueuedConnection);
    if (!invoked) {
        code = Decompiler::makeWarning(QObject::tr("Failed to invoke decompiler asynchronously"));
        return;
    }

    // Run the local event loop and wait for the finished signal to be queued
    // and processed here. This avoids holding the IaitoCore mutex in this
    // thread while radare2 executes the heavy work.
    loop.exec();

    if (loopInterrupted) {
        eprintf("DecompilerTask was interrupted\n");
        loopInterrupted = false;
        return;
    }
    // Store resulting code (or a warning if null). To avoid potential
    // use-after-free or allocator/lifetime issues caused by R2 internals
    // allocating objects in different threads, deep-copy the RCodeMeta we
    // received and free the original.
    if (resultCode) {
        RCodeMeta *copy = r_codemeta_new("");
        if (resultCode->code) {
            copy->code = strdup(resultCode->code);
        } else {
            copy->code = strdup("");
        }
        // Copy annotations safely (only essential fields)
        void *iter;
        r_vector_foreach(&resultCode->annotations, iter)
        {
            RCodeMetaItem *oldItem = (RCodeMetaItem *) iter;
            RCodeMetaItem *newItem = r_codemeta_item_new();
            newItem->start = oldItem->start;
            newItem->end = oldItem->end;
            newItem->type = oldItem->type;
            if (newItem->type == R_CODEMETA_TYPE_OFFSET) {
                newItem->offset.offset = oldItem->offset.offset;
            }
            r_codemeta_add_item(copy, newItem);
        }
        // Free original and keep our copy
        // XXX may fail is result fails
        r_codemeta_free(resultCode);
        code = copy;
    } else {
        code = Decompiler::makeWarning(QObject::tr("Failed to decompile (no result)"));
    }

    // Disconnect to be safe (not strictly necessary for local notifier)
    disconnect(decompiler, &Decompiler::finished, &notifier, nullptr);
}
