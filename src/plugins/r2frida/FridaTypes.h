#pragma once

#include <QString>
#include <QtGlobal>

enum class FridaTransport { Local, Usb, Remote };

enum class FridaAction { Attach, Spawn, Launch };

enum class FridaListKind { Devices, Processes, Applications };

enum class FridaAddressMode { Static, Runtime, ModuleOffset };

enum class FridaSessionState { Disconnected, Connecting, Attached, Error };

struct FridaDeviceInfo
{
    QString id;
    QString type;
    QString name;
};

struct FridaTargetInfo
{
    QString pid;
    QString name;
    QString identifier;
};

struct FridaModuleInfo
{
    QString name;
    QString path;
    quint64 base = 0;
    quint64 size = 0;
    bool matched = false;
};

struct FridaMapInfo
{
    quint64 start = 0;
    quint64 end = 0;
    QString permission;
    QString path;
};

struct FridaProcessInfo
{
    int pid = -1;
    QString name;
    QString identifier;
    QString arch;
    int bits = 0;
    QString os;
    QString moduleName;
    quint64 moduleBase = 0;
};

struct FridaHookInfo
{
    quint64 address = 0;
    QString summary;
};
