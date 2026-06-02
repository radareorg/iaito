#ifndef R2RETDEC_DECOMPILER_H
#define R2RETDEC_DECOMPILER_H

#include "Decompiler.h"

class R2retdecDecompiler : public R2JsonDecompiler
{
    Q_OBJECT

public:
    explicit R2retdecDecompiler(QObject *parent = nullptr);

    static bool isAvailable();
};

#endif // R2RETDEC_DECOMPILER_H
