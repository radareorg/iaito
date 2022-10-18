#ifndef R2G_DECOMPILER_H
#define R2G_DECOMPILER_H

#include "IaitoCommon.h"
#include "R2Task.h"
#include "Decompiler.h"

#include <QString>
#include <QObject>

class R2GhidraCmdDecompiler: public Decompiler
{
    Q_OBJECT

private:
    R2Task *task;

public:
    explicit R2GhidraCmdDecompiler(QObject *parent = nullptr);
    RCodeMeta *decompileSync(RVA addr) override;
    void decompileAt(RVA addr) override;

    bool isRunning() override    { return task != nullptr; }

    static bool isAvailable();
};

#endif //DECOMPILER_H
