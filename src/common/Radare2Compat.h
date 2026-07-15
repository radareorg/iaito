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

#if R2_VERSION_NUMBER < 60000
#error "iaito requires radare2 6.0.0 or newer"
#endif

// The generic RBin resource API is available starting with ABI 110.
#if defined(R2_ABIVERSION) && R2_ABIVERSION >= 110
#define IAITO_HAS_R_BIN_RESOURCES 1
#else
#define IAITO_HAS_R_BIN_RESOURCES 0
#endif

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
