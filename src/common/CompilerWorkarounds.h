#ifndef IAITO_COMPILER_WORKAROUNDS_H
#define IAITO_COMPILER_WORKAROUNDS_H

#if defined(__APPLE__) && defined(__aarch64__) && defined(__clang__)
#if __has_include(<arm_acle.h>)
#include <arm_acle.h>
#endif
#endif

#endif // IAITO_COMPILER_WORKAROUNDS_H
