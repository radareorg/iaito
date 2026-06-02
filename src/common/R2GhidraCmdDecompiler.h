#ifndef R2G_DECOMPILER_H
#define R2G_DECOMPILER_H

#include "Decompiler.h"

class R2GhidraCmdDecompiler : public R2JsonDecompiler
{
    Q_OBJECT

public:
    explicit R2GhidraCmdDecompiler(QObject *parent = nullptr);

    QString getConfigPrefix() const override { return QStringLiteral("r2ghidra"); }

    static bool isAvailable();
};

#endif // DECOMPILER_H
