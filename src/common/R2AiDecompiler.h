#ifndef R2AI_DECOMPILER_H
#define R2AI_DECOMPILER_H

#include "Decompiler.h"
#include "IaitoCommon.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>

class R2AiDecompiler : public Decompiler
{
    Q_OBJECT

private:
    QTimer *pollTimer;
    QHash<QString, RCodeMeta *> cache;
    int taskId = 0;
    QString taskCacheKey;
    QString taskLastOutput;
    bool previousAsyncPurge = false;
    bool asyncPurgeSaved = false;
    mutable bool offsetCommandChecked = false;
    mutable bool offsetCommandAvailable = false;

    QString cacheKeyForAddr(RVA addr) const;
    QString decompileCommand(RVA addr) const;
    void ensureAsyncConfig();
    void restoreAsyncPurgeConfig();
    void finishWithCode(RCodeMeta *code, bool storeInCache);
    void finishWithWarning(const QString &message);
    void clearCache();

private slots:
    void pollAsyncTask();

public:
    explicit R2AiDecompiler(QObject *parent = nullptr);
    ~R2AiDecompiler() override;
    void decompileAt(RVA addr) override;
    RCodeMeta *decompileSync(RVA addr) override;
    void cancel() override;
    void setOption(const QString &key, const QString &value) override;

    bool isRunning() override { return taskId > 0; }
    bool isCancelable() override { return taskId > 0; }
    bool prefersBackground() const override { return true; }
    bool requiresManualTrigger() const override { return true; }
    bool hasCachedResult(RVA addr) const override;
    QString getConfigPrefix() const override { return QStringLiteral("r2ai"); }

    static bool isAvailable();
};

#endif
