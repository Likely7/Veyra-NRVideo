// lmxxf 78f548749e74824327b8458c57be31a1df78376a: the only GNU attribute
// used by this runtime is __attribute__((noinline)); map it for MSVC.
#ifdef _MSC_VER
#define __attribute__(x) __declspec(noinline)
#endif
