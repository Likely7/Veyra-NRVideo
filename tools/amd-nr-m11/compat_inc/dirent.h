#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef struct nr_compat_DIR { int unused; } DIR;
struct dirent { char d_name[260]; };
static inline DIR* opendir(const char* p) { (void)p; return (DIR*)0; }
static inline struct dirent* readdir(DIR* d) { (void)d; return (struct dirent*)0; }
static inline int closedir(DIR* d) { (void)d; return 0; }
#ifdef __cplusplus
}
#endif
