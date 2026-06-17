#include "R2SourceDecompiler.h"
#include "Iaito.h"

R2SourceDecompiler::R2SourceDecompiler(QObject *parent)
    : R2JsonDecompiler(
          QStringLiteral("source"),
          QStringLiteral("source"),
          QStringLiteral("CLdj"),
          QStringLiteral("CLd"),
          AnnotationMode::OffsetsOnly,
          parent)
{}

bool R2SourceDecompiler::isAvailable()
{
    return Core()->cmd(QStringLiteral("CLd?")).contains(QStringLiteral("CLdj"));
}
