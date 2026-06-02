
#include "R2pdcCmdDecompiler.h"
#include "Iaito.h"

R2pdcCmdDecompiler::R2pdcCmdDecompiler(QObject *parent)
    : R2JsonDecompiler(
          "pdc", "pdc", QStringLiteral("pdcj"), QStringLiteral("pdc"), AnnotationMode::Full, parent)
{}

bool R2pdcCmdDecompiler::isAvailable()
{
    return Core()->cmdList("e cmd.pdc=?").contains(QStringLiteral("pdc"));
}
