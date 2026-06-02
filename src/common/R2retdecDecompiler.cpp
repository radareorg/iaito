
#include "R2retdecDecompiler.h"
#include "Iaito.h"

R2retdecDecompiler::R2retdecDecompiler(QObject *parent)
    : R2JsonDecompiler(
          "pdz",
          "pdz",
          QStringLiteral("pdzj"),
          QStringLiteral("retdec"),
          AnnotationMode::OffsetsOnly,
          parent)
{}

bool R2retdecDecompiler::isAvailable()
{
    return Core()->cmdList("e cmd.pdc=?").contains(QStringLiteral("pdz"));
}
