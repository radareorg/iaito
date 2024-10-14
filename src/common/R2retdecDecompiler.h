#ifndef R2RETDEC_DECOMPILER_H
#define R2RETDEC_DECOMPILER_H

#include "Decompiler.h"
#include "IaitoCommon.h"
#include "R2Task.h"

#include <QObject>
#include <QString>

class R2retdecDecompiler : public Decompiler
{
    Q_OBJECT

private:
    R2Task *task;

public:
    explicit R2retdecDecompiler(QObject *parent = nullptr);

    RCodeMeta *decompileSync(RVA addr) override;
    void decompileAt(RVA addr) override;

    bool isRunning() override { return task != nullptr; }

    static bool isAvailable();
};

#endif // R2RETDEC_DECOMPILER_H
