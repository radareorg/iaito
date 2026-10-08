#pragma once

#include "FridaBackend.h"
#include "FridaTypes.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <functional>

class FridaSession : public QObject
{
    Q_OBJECT

public:
    explicit FridaSession(QObject *parent = nullptr);

    bool checkAvailable();
    bool available() const { return available_; }
    FridaSessionState state() const { return state_; }
    bool isAttached() const { return state_ == FridaSessionState::Attached && fridaFd() >= 0; }
    bool isSuspended() const { return suspended_; }
    bool busy() const { return backend_.busy(); }
    QString lastError() const { return lastError_; }
    QString deviceLabel() const { return deviceLabel_; }
    QString transportLabel() const { return transportLabel_; }
    QString uri() const { return uri_; }

    const FridaProcessInfo &process() const { return process_; }
    const QList<FridaModuleInfo> &modules() const { return modules_; }
    const QList<FridaMapInfo> &maps() const { return maps_; }
    const QList<FridaHookInfo> &hooks() const { return hooks_; }
    quint64 staticBase() const { return staticBase_; }
    quint64 slide() const { return slide_; }
    bool hasStaticMapping() const { return matchedSize_ > 0; }

    FridaAddressMode addressMode() const { return addressMode_; }
    void setAddressMode(FridaAddressMode mode);

    quint64 toRuntime(quint64 staticVa) const;
    quint64 toStatic(quint64 runtimeVa) const;
    bool containsStatic(quint64 addr) const;
    bool containsRuntime(quint64 addr) const;
    QString formatRuntime(quint64 runtimeVa) const;
    QString describe(quint64 addr) const;

    void probe(const QString &uri, const std::function<void(QString)> &done);
    void openTarget(
        const QString &uri,
        bool suspended,
        const QString &deviceLabel,
        const QString &transportLabel);
    void detach();
    void resume();
    void command(const QString &fridaCmd, const std::function<void(QString)> &done);
    QByteArray readBytes(quint64 runtime, int length);
    void importExports(const QString &moduleName);
    QString writeScript(const QString &source);
    void note(const QString &text);
    void addHook(quint64 address, const QString &summary);

signals:
    void changed();
    void errorMessage(const QString &message);
    void consoleMessage(const QString &message);

private:
    int fridaFd() const { return backend_.fridaFd(); }
    void setState(FridaSessionState state);
    void clearTarget();
    void readSnapshot();
    void applyInfo(const QJsonObject &info);
    void applyModules(const QJsonArray &modules);
    void applyMaps(const QJsonArray &maps);

    FridaBackend backend_;
    FridaSessionState state_ = FridaSessionState::Disconnected;
    bool available_ = false;
    bool suspended_ = false;
    int generation_ = 0;
    QString lastError_;
    QString deviceLabel_;
    QString transportLabel_;
    QString uri_;
    FridaProcessInfo process_;
    QList<FridaModuleInfo> modules_;
    QList<FridaMapInfo> maps_;
    QList<FridaHookInfo> hooks_;
    quint64 staticBase_ = 0;
    quint64 runtimeBase_ = 0;
    quint64 matchedSize_ = 0;
    quint64 slide_ = 0;
    FridaAddressMode addressMode_ = FridaAddressMode::Runtime;
};
