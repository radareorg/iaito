#ifndef R2AI_DECOMPILER_H
#define R2AI_DECOMPILER_H

#include "Decompiler.h"
#include "IaitoCommon.h"
#include "R2Task.h"

#include <QObject>
#include <QString>

class R2AiDecompiler : public Decompiler
{
    Q_OBJECT

private:
    R2Task *task;

public:
    explicit R2AiDecompiler(QObject *parent = nullptr);
    void decompileAt(RVA addr) override;
    RCodeMeta *decompileSync(RVA addr) override;

    bool isRunning() override { return task != nullptr; }
    QString getConfigPrefix() const override { return QStringLiteral("r2ai"); }

    static bool isAvailable();
};

#endif
