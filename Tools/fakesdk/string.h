#ifndef _FAKE_STRING_H
#define _FAKE_STRING_H
#include <stddef.h>
void  *memcpy(void *, const void *, size_t);
void  *memset(void *, int, size_t);
void  *memmove(void *, const void *, size_t);
int    memcmp(const void *, const void *, size_t);
size_t strlen(const char *);
int    strcmp(const char *, const char *);
int    strncmp(const char *, const char *, size_t);
size_t strlcat(char *, const char *, size_t);
size_t strlcpy(char *, const char *, size_t);
char  *strcpy(char *, const char *);
char  *strncpy(char *, const char *, size_t);
char  *strchr(const char *, int);
#endif
