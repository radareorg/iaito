#include "Android.h"

#include <r_io.h>
#include <vector>
#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>

namespace {
int openContent(void *, const char *uri, int permissions)
{
    const QJniObject context(QNativeInterface::QAndroidApplication::context());
    const auto resolver
        = context.callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;");
    const auto androidUri = QJniObject::callStaticObjectMethod(
        "android/net/Uri",
        "parse",
        "(Ljava/lang/String;)Landroid/net/Uri;",
        QJniObject::fromString(QString::fromUtf8(uri)).object());
    if (!resolver.isValid() || !androidUri.isValid()) {
        return -1;
    }
    // "w" can truncate a provider's file. Use "rw" even for write-only r2 opens.
    const auto mode = QJniObject::fromString(permissions & R_PERM_W ? "rw" : "r");
    const auto parcel = resolver.callObjectMethod(
        "openFileDescriptor",
        "(Landroid/net/Uri;Ljava/lang/String;)Landroid/os/ParcelFileDescriptor;",
        androidUri.object(),
        mode.object());
    if (!parcel.isValid()) {
        return -1;
    }
    // Use JNI directly here: an exception must return -1, not JNI's default int (0).
    QJniEnvironment env;
    const auto detach = env->GetMethodID(parcel.objectClass(), "detachFd", "()I");
    int fd = -1;
    if (!env.checkAndClearExceptions() && detach) {
        fd = env->CallIntMethod(parcel.object(), detach);
        if (env.checkAndClearExceptions()) {
            fd = -1;
        }
    }
    parcel.callMethod<void>("close");
    return fd; // Ownership transfers to r2, which also handles non-seekable providers.
}
} // namespace

bool Iaito::initializeAndroidContent(r_io_t *io)
{
    auto *builtin = static_cast<RIOPlugin *>(r_libstore_find_name(io->libstore, "content"));
    if (!builtin || !io->libstore->static_plugins) {
        return false;
    }
    // These outlive iaito's singleton core and every descriptor it opens.
    static RIOContent resolver = {openContent, nullptr};
    static RIOPlugin content = *builtin;
    content.data = &resolver;
    static std::vector<const RIOPlugin *> plugins;
    if (plugins.empty()) {
        auto entries = static_cast<const RIOPlugin *const *>(io->libstore->static_plugins);
        for (; *entries; ++entries) {
            plugins.push_back(*entries == builtin ? &content : *entries);
        }
        plugins.push_back(nullptr);
    }
    if (!r_libstore_remove(io->libstore, builtin)) {
        return false;
    }
    if (!r_libstore_add(io->libstore, &content)) {
        r_libstore_add(io->libstore, builtin);
        return false;
    }
    // Closing all files resets IO plugins; keep the resolver after that reset too.
    io->libstore->static_plugins = plugins.data();
    return true;
}
