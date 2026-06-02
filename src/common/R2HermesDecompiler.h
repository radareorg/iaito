#ifndef R2HERMES_DECOMPILER_H
#define R2HERMES_DECOMPILER_H

#include "Decompiler.h"

class R2HermesDecompiler : public R2JsonDecompiler
{
    Q_OBJECT

public:
    explicit R2HermesDecompiler(QObject *parent = nullptr);

    QString getConfigPrefix() const override { return QStringLiteral("hbc"); }

    static bool isAvailable();
};

#endif // R2HERMES_DECOMPILER_H
