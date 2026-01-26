#include <efi.h>
#include <efilib.h>
#include "strings_extension.h"
#include "malloc.h"
#include "ctype.h"

#ifndef __GNUC__
void *memcpy(void *dest, const void *src, size_t n) {
    uefi_call_wrapper(ST->BootServices->CopyMem, 3, dest, (void*)src, n);
    return dest;
}

void *memset(void *s, int c, size_t n) {
    uefi_call_wrapper(ST->BootServices->SetMem, 3, s, n, (UINT8)c);
    return s;
}
#endif

void *memmove(void *dest, const void *src, size_t n) {
    uefi_call_wrapper(ST->BootServices->CopyMem, 3, dest, (void*)src, n);
    return dest;
}

int strcasecmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        char c1 = *s1++;
        char c2 = *s2++;
        if ('A' <= c1 && c1 <= 'Z')
            c1 += 'a' - 'A';
        if ('A' <= c2 && c2 <= 'Z')
            c2 += 'a' - 'A';
        if (c1 != c2)
            return (unsigned char)c1 - (unsigned char)c2;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

int strncasecmp(const char *s1, const char *s2, unsigned long n) {
    while (n > 0) {
        char c1 = (unsigned char)*s1++;
        char c2 = (unsigned char)*s2++;
        
        if ('A' <= c1 && c1 <= 'Z') c1 += 'a' - 'A';
        if ('A' <= c2 && c2 <= 'Z') c2 += 'a' - 'A';

        if (c1 != c2)
            return (int)c1 - (int)c2;

        if (c1 == '\0')
            return 0;

        n--;
    }
    return 0;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i = 0;
    while (i < n && src[i] != '\0') {
        dest[i] = src[i];
        i+=1;
    }
    while (i < n) {
        dest[i] = '\0';
        i+=1;
    }
    return dest;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    size_t i = 0;
    while (i < n) {
        if ((unsigned char)s1[i] < (unsigned char)s2[i]) return -1;
        if ((unsigned char)s1[i] > (unsigned char)s2[i]) return 1;
        if (s1[i] == '\0') break;
        i++;
    }
    return 0;
}

char *strdup(const char *s) {
    if (s == NULL)
        return NULL;
    size_t size = strlen(s);

    char *duped = (char *)malloc(size + 1);
    if (duped == NULL)
        return NULL;

    memcpy(duped, s, size + 1);
    return duped;
}

char *strchr(const char *str, int c) {
    char target_char = (char)c;

    while (*str != '\0') {
        if (*str == target_char) {
            return (char *)str;
        }
        str++;
    }

    if (target_char == '\0')
        return (char *)str;
    return NULL;
}

char *strrchr(const char *str, int c) {
    const char *ret = NULL;
    char target_char = (char)c;

    while (*str != '\0') {
        if (*str == target_char)
            ret = str;
        str++;
    }

    if (target_char == '\0')
        ret = str;
    return (char *)ret;
}

char *strstr(const char *s1, const char *s2) {
    size_t n = strlen(s2);
    if (n == 0) return (char *)s1;
    while (*s1) {
        if (!strncmp(s1, s2, n))
            return (char *)s1;
        s1++;
    }
    return NULL;
}

int puts(const char *s) {
    // Basic puts using EFI Print
    Print(L"%a\r\n", s);
    return 0;
}

int putchar(int ch) {
    Print(L"%c", (CHAR16)ch);
    return ch;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}