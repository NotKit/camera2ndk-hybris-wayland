// Compatibility shims to compile Android NDK headers with a standard Linux toolchain.
// Injected as -include prefix header for all TUs.
#pragma once

// AOSP availability macros — no-ops outside Android
#ifndef __INTRODUCED_IN
#  define __INTRODUCED_IN(api_level)
#endif
#ifndef __DEPRECATED_IN
#  define __DEPRECATED_IN(api_level)
#endif
#ifndef __REMOVED_IN
#  define __REMOVED_IN(api_level)
#endif

// Clang nullability annotations — no-ops on non-Clang / without feature flag
#ifndef _Nullable
#  define _Nullable
#endif
#ifndef _Nonnull
#  define _Nonnull
#endif
#ifndef _Null_unspecified
#  define _Null_unspecified
#endif

// Ensure size_t is available
#include <stddef.h>
#include <stdint.h>

// __BEGIN_DECLS / __END_DECLS come from sys/cdefs.h
#include <sys/cdefs.h>
