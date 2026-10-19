// lmxxf4764e4f1: source-compatible MSVC build adaptations only.
#ifdef _MSC_VER
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d12.h>
// rpcndr.h defines `small` as a type macro; upstream uses it as an identifier.
#ifdef small
#undef small
#endif
#define __attribute__(x) __declspec(noinline)
#endif
