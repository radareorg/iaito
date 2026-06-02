#ifndef R2PDC_DECOMPILER_H
#define R2PDC_DECOMPILER_H

#include "Decompiler.h"

class R2pdcCmdDecompiler : public R2JsonDecompiler
{
    Q_OBJECT

public:
    explicit R2pdcCmdDecompiler(QObject *parent = nullptr);

    static bool isAvailable();
};

#endif // DECOMPILER_H
