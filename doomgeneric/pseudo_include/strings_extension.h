#ifndef STRINGS_EXTENSION_H
#define STRINGS_EXTENSION_H

#include <stddef.h>

int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, unsigned long n);
char *strncpy(char *dest, const char *src, size_t n);
int strncmp(const char *s1, const char *s2, size_t n);
int strcmp(const char *s1, const char *s2);
char *strdup(const char *s);
char *strchr(const char *str, int c);
char *strrchr(const char *str, int c);
char *strstr(const char *s1, const char *s2);
size_t strlen(const char *s);

int puts(const char *s);
int putchar(int ch);

void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
void *memmove(void *dest, const void *src, size_t n);

#endif