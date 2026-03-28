#ifndef IAITO_RADARE2_COMPAT_H
#define IAITO_RADARE2_COMPAT_H

#if defined(slots)
#pragma push_macro("slots")
#undef slots
#define IAITO_RESTORE_QT_SLOTS
#endif

#if defined(signals)
#pragma push_macro("signals")
#undef signals
#define IAITO_RESTORE_QT_SIGNALS
#endif

#if defined(emit)
#pragma push_macro("emit")
#undef emit
#define IAITO_RESTORE_QT_EMIT
#endif

#include <r_core.h>

#ifdef IAITO_RESTORE_QT_EMIT
#pragma pop_macro("emit")
#undef IAITO_RESTORE_QT_EMIT
#endif

#ifdef IAITO_RESTORE_QT_SIGNALS
#pragma pop_macro("signals")
#undef IAITO_RESTORE_QT_SIGNALS
#endif

#ifdef IAITO_RESTORE_QT_SLOTS
#pragma pop_macro("slots")
#undef IAITO_RESTORE_QT_SLOTS
#endif

#endif // IAITO_RADARE2_COMPAT_H
