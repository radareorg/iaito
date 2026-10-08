#pragma once

#include "FridaTypes.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <QObject>
#include <QSet>
#include <QSharedPointer>
#include <functional>

class R2Task;

// Talks to io_frida through the already-open radare2 core.
// The static file stays the current descriptor. Frida is opened with `of`
// (no maps) and selected only for the duration of a `:` command.
class FridaBackend : public QObject
{
    Q_OBJECT

public:
    explicit FridaBackend(QObject *parent = nullptr);

    bool pluginAvailable();
    bool busy() const { return busy_; }
    int currentFd() const;
    int hostFd() const { return hostFd_; }
    int fridaFd() const { return fridaFd_; }
    void setHostFd(int fd) { hostFd_ = fd; }
    void setFridaFd(int fd) { fridaFd_ = fd; }

    QSet<int> openFds() const;
    int findNewFridaFd(const QSet<int> &before) const;
    void closeFd(int fd);

    void runBatch(const QString &batch, const std::function<void(QString)> &done);
    QString fridaCommand(const QString &command);

    static QList<FridaDeviceInfo> parseDevices(const QString &output);
    static QList<FridaTargetInfo> parseTargets(const QString &output, FridaListKind kind);
    static QJsonDocument parseJsonLoose(const QString &text);
    static quint64 parseAddress(const QJsonValue &value);

private:
    int restoreFd() const;

    int hostFd_ = -1;
    int fridaFd_ = -1;
    bool busy_ = false;
    bool availabilityChecked_ = false;
    bool available_ = false;
    QSharedPointer<R2Task> task_;
};
