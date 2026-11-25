#ifndef R2JADX_DECOMPILER_H
#define R2JADX_DECOMPILER_H

#include "Decompiler.h"
#include "IaitoCommon.h"
#include "R2Task.h"

#include <QObject>
#include <QString>

class R2JadxDecompiler : public Decompiler, public R2CommandDecompiler
{
    Q_OBJECT

private:
    R2Task *task;

public:
    explicit R2JadxDecompiler(QObject *parent = nullptr);
    RCodeMeta *decompileSync(RVA addr) override;
    void decompileAt(RVA addr) override;
    QString commandAt(RVA addr) const override;
    QString codeFromOutput(const QByteArray &output) const override;

    bool isRunning() override { return task != nullptr; }

    static bool isAvailable();
};

#endif // R2JADX_DECOMPILER_H
