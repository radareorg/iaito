#include "IaitoLayout.h"

using namespace Iaito;

bool Iaito::isBuiltinLayoutName(const QString &name)
{
    return name == LAYOUT_DEFAULT || name == LAYOUT_DEBUG;
}
