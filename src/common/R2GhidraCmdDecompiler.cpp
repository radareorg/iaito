
#include "R2GhidraCmdDecompiler.h"
#include "Iaito.h"

R2GhidraCmdDecompiler::R2GhidraCmdDecompiler(QObject *parent)
    : R2JsonDecompiler(
          "pdg",
          "pdg",
          QStringLiteral("pdgj"),
          QStringLiteral("r2ghidra"),
          AnnotationMode::Full,
          parent)
{}

bool R2GhidraCmdDecompiler::isAvailable()
{
    return Core()->cmdList("e cmd.pdc=?").contains(QStringLiteral("pdg"));
}
