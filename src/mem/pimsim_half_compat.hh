#ifndef __MEM_PIMSIM_HALF_COMPAT_HH__
#define __MEM_PIMSIM_HALF_COMPAT_HH__

/*
 * Compatibility definitions for PIMSimulator/lib/half.h.
 *
 * gem5 enables -Wundef. PIMSimulator's half.h checks several macros
 * that may be undefined in gem5's build environment. Define them
 * explicitly to preserve the default behavior and silence -Wundef.
 */

#ifndef HALF_ERRHANDLING_FLAGS
#define HALF_ERRHANDLING_FLAGS 0
#endif

#ifndef HALF_ERRHANDLING_ERRNO
#define HALF_ERRHANDLING_ERRNO 0
#endif

#ifndef HALF_ERRHANDLING_FENV
#define HALF_ERRHANDLING_FENV 0
#endif

#ifndef HALF_ERRHANDLING_THROWS
#define HALF_ERRHANDLING_THROWS 0
#endif

/*
 * half.h may use:
 *
 *   #define HALF_ENABLE_F16C_INTRINSICS __F16C__
 *
 * When gem5 is not compiled with -mf16c, __F16C__ is undefined and
 * -Wundef emits warnings. Disable F16C explicitly unless you really
 * want to compile PIMSimulator with host F16C intrinsics.
 */
#ifndef HALF_ENABLE_F16C_INTRINSICS
#define HALF_ENABLE_F16C_INTRINSICS 0
#endif

#endif // __MEM_PIMSIM_HALF_COMPAT_HH__