#ifndef R2DECAI_DECOMPILER_H
#define R2DECAI_DECOMPILER_H

#include "Decompiler.h"
#include "IaitoCommon.h"
#include "R2Task.h"

#include <QObject>
#include <QString>

class R2DecaiDecompiler : public Decompiler
{
    Q_OBJECT

private:
    R2Task *task;

public:
    explicit R2DecaiDecompiler(QObject *parent = nullptr);
    void decompileAt(RVA addr) override;
    RCodeMeta *decompileSync(RVA addr) override;

    bool isRunning() override { return task != nullptr; }

    static bool isAvailable();
};

#endif // DECOMPILER_H
