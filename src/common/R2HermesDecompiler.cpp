
#include "R2HermesDecompiler.h"
#include "Iaito.h"

R2HermesDecompiler::R2HermesDecompiler(QObject *parent)
    : R2JsonDecompiler(
          "r2hermes",
          "r2hermes",
          QStringLiteral("pd:hj"),
          QStringLiteral("r2hermes (pd:hj)"),
          AnnotationMode::OffsetsOnly,
          parent)
{}

bool R2HermesDecompiler::isAvailable()
{
    return Core()->cmd("Lc").contains(QStringLiteral("core_hbc"));
}
