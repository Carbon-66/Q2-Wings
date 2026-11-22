#ifndef _STDIO_H
#define _STDIO_H
#include "_q2.h"
#include <stdarg.h>
/* only the formatting q2-wings uses: files go through open/read/write */
int snprintf(char *, size_t, const char *, ...) __attribute__((format(printf, 3, 4)));
int vsnprintf(char *, size_t, const char *, va_list);
int rename(const char *, const char *);
#endif
