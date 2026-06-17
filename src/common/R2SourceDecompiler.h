#ifndef R2SOURCEDECOMPILER_H
#define R2SOURCEDECOMPILER_H

#include "Decompiler.h"

class R2SourceDecompiler : public R2JsonDecompiler
{
    Q_OBJECT

public:
    explicit R2SourceDecompiler(QObject *parent = nullptr);

    static bool isAvailable();
};

#endif // R2SOURCEDECOMPILER_H
