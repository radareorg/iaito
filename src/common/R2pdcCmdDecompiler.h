#ifndef R2PDC_DECOMPILER_H
#define R2PDC_DECOMPILER_H

#include "IaitoCommon.h"
#include "R2Task.h"
#include "Decompiler.h"

#include <QString>
#include <QObject>

class R2pdcCmdDecompiler: public Decompiler
{
    Q_OBJECT

private:
    R2Task *task;

public:
    explicit R2pdcCmdDecompiler(QObject *parent = nullptr);
    void decompileAt(RVA addr) override;
    RCodeMeta *decompileSync(RVA addr) override;

    bool isRunning() override    { return task != nullptr; }

    static bool isAvailable();
};

#endif //DECOMPILER_H
