#pragma once

#include "FridaTypes.h"

class FridaUri
{
public:
    static QString listDevices();
    static QString listTargets(
        FridaTransport transport, FridaListKind kind, const QString &deviceId);
    static QString target(
        FridaTransport transport,
        FridaAction action,
        const QString &deviceId,
        const QString &targetId);
    static QString quote(const QString &text);
    static bool targetIsSafe(const QString &targetId);
};
