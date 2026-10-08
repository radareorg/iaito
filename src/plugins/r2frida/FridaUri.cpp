#include "FridaUri.h"

static QString actionName(FridaAction action)
{
    switch (action) {
    case FridaAction::Spawn:
        return QStringLiteral("spawn");
    case FridaAction::Launch:
        return QStringLiteral("launch");
    case FridaAction::Attach:
        break;
    }
    return QStringLiteral("attach");
}

static QString linkName(FridaTransport transport)
{
    switch (transport) {
    case FridaTransport::Usb:
        return QStringLiteral("usb");
    case FridaTransport::Remote:
        return QStringLiteral("remote");
    case FridaTransport::Local:
        break;
    }
    return QStringLiteral("local");
}

QString FridaUri::quote(const QString &text)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QStringLiteral("'") + escaped + QStringLiteral("'");
}

bool FridaUri::targetIsSafe(const QString &targetId)
{
    if (targetId.isEmpty()) {
        return false;
    }
    const QString forbidden = QStringLiteral("/'\"`;|@\n\r");
    for (const QChar ch : targetId) {
        if (forbidden.contains(ch)) {
            return false;
        }
    }
    return true;
}

QString FridaUri::listDevices()
{
    // Three components with an empty device id: r2frida dumps every device
    // (local, usb, and remote), then fails the open on purpose.
    return QStringLiteral("frida://list/usb/");
}

QString FridaUri::listTargets(
    FridaTransport transport, FridaListKind kind, const QString &deviceId)
{
    const QString action = kind == FridaListKind::Applications ? QStringLiteral("apps")
                                                               : QStringLiteral("list");
    QString id = deviceId;
    if (transport == FridaTransport::Usb && (id.isEmpty() || id == QStringLiteral("usb"))) {
        id.clear();
    }
    if (transport == FridaTransport::Local) {
        id.clear();
    }
    return QStringLiteral("frida://") + action + QStringLiteral("/") + linkName(transport)
           + QStringLiteral("/") + id + QStringLiteral("/");
}

QString FridaUri::target(
    FridaTransport transport,
    FridaAction action,
    const QString &deviceId,
    const QString &targetId)
{
    const QString verb = actionName(action);
    if (transport == FridaTransport::Local) {
        return QStringLiteral("frida://") + verb + QStringLiteral("/") + targetId;
    }
    QString id = deviceId;
    if (transport == FridaTransport::Usb && (id.isEmpty() || id == QStringLiteral("usb"))) {
        id.clear();
    }
    return QStringLiteral("frida://") + verb + QStringLiteral("/") + linkName(transport)
           + QStringLiteral("/") + id + QStringLiteral("/") + targetId;
}
