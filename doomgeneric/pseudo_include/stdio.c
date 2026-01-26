#include <efi.h>
#include <efilib.h>
#include "stdio.h"
#include "malloc.h"
#include "malloc_extension.h"
#include "strings_extension.h"

int errno = 0;

int printf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    vfprintf(stdout, format, ap);
    va_end(ap);
    return 0;
}

int sprintf(char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, 0x7FFFFFFF, format, ap);
    va_end(ap);
    return ret;
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, size, format, ap);
    va_end(ap);
    return ret;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    if (size == 0) return 0;
    
    // Check if format has %s and if so, how many, to ensure we don't overflow u_format
    // Actually, u_format is already big enough since %s -> %a is same length.
    
    // Convert format to Unicode for VSPrint
    size_t fmt_len = strlen(format);
    CHAR16* u_format = malloc((fmt_len + 1) * sizeof(CHAR16));
    if (!u_format) return 0;
    
    size_t u_idx = 0;
    for (size_t i = 0; i <= fmt_len; i++) {
        if (format[i] == '%') {
            // Handle %%
            if (format[i+1] == '%') {
                u_format[u_idx++] = '%';
                u_format[u_idx++] = '%';
                i++;
                continue;
            }
            // Simple check for %s and translate to %a for EFI's VSPrint (which expects ASCII for %a)
            // We should also handle %d, %i, %x, %u, %p etc. but those are same in EFI.
            if (format[i+1] == 's') {
                u_format[u_idx++] = '%';
                u_format[u_idx++] = 'a';
                i++;
                continue;
            }
            // Handle %.3d and similar used in STCFN%.3d
            if (format[i+1] == '.' && format[i+2] >= '0' && format[i+2] <= '9' && format[i+3] == 'd') {
                u_format[u_idx++] = '%';
                u_format[u_idx++] = '0';
                u_format[u_idx++] = (CHAR16)format[i+2];
                u_format[u_idx++] = 'd';
                i += 3;
                continue;
            }
            // Handle %03d which is also common
            if (format[i+1] == '0' && format[i+2] >= '0' && format[i+2] <= '9' && format[i+3] == 'd') {
                u_format[u_idx++] = '%';
                u_format[u_idx++] = '0';
                u_format[u_idx++] = (CHAR16)format[i+2];
                u_format[u_idx++] = 'd';
                i += 3;
                continue;
            }
            // Handle %d, %i, %u, %x, %p etc. specifically to avoid issues if we add more complex logic
            if (format[i+1] == 'd' || format[i+1] == 'i' || format[i+1] == 'u' || 
                format[i+1] == 'x' || format[i+1] == 'X' || format[i+1] == 'p') {
                u_format[u_idx++] = '%';
                u_format[u_idx++] = (CHAR16)format[i+1];
                i++;
                continue;
            }
        }
        u_format[u_idx++] = (CHAR16)format[i];
    }
    
    // VSPrint can be dangerous if we don't have enough space in u_buf.
    // Let's use a reasonably large temporary buffer if size is very large (like from sprintf)
    size_t actual_size = (size > 1024) ? 1024 : size;
    CHAR16* u_buf = malloc((actual_size + 1) * sizeof(CHAR16));
    if (!u_buf) {
        free(u_format);
        return 0;
    }
    
    // Initialize u_buf to zeros to be safe
    for (size_t j = 0; j <= actual_size; j++) u_buf[j] = 0;
    
    VSPrint(u_buf, (actual_size + 1) * sizeof(CHAR16), u_format, ap);
    
    size_t i_out;
    for (i_out = 0; u_buf[i_out] && i_out < size - 1 && i_out < actual_size; i_out++) {
        str[i_out] = (char)u_buf[i_out];
    }
    str[i_out] = '\0';
    
    free(u_format);
    free(u_buf);
    return (int)i_out;
}

int sscanf(const char *str, const char *format, ...) {
    if (strcmp(format, "%i") == 0 || strcmp(format, "%d") == 0) {
        va_list ap;
        va_start(ap, format);
        int *v = va_arg(ap, int *);
        *v = atoi(str);
        va_end(ap);
        return 1;
    }
    if (strcmp(format, "%x") == 0) {
        va_list ap;
        va_start(ap, format);
        unsigned int *v = va_arg(ap, unsigned int *);
        unsigned int res = 0;
        const char *s = str;
        while (*s) {
            char c = *s++;
            res <<= 4;
            if (c >= '0' && c <= '9') res += c - '0';
            else if (c >= 'a' && c <= 'f') res += c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') res += c - 'A' + 10;
            else { res >>= 4; break; }
        }
        *v = res;
        va_end(ap);
        return 1;
    }
    return 0;
}

int fscanf(FILE *stream, const char *format, ...) {
    return 0; // Stub for now
}

int feof(FILE *stream) {
    return 1; // Always EOF for now
}

int fprintf(FILE *stream, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    vfprintf(stream, format, ap);
    va_end(ap);
    return 0;
}

int vfprintf(FILE *stream, const char *format, va_list ap) {
    char buf[1024];
    vsnprintf(buf, sizeof(buf), format, ap);
    
    // Convert to Unicode for EFI Print
    CHAR16 u_buf[1024];
    int i;
    int j = 0;
    for (i = 0; buf[i] && j < 1022; i++) {
        if (buf[i] == '\n') {
            u_buf[j++] = L'\r';
        }
        u_buf[j++] = (CHAR16)buf[i];
    }
    u_buf[j] = 0;
    Print(L"%s", u_buf);
    
    return 0;
}

int fflush(FILE *stream) {
    return 0;
}

void *fopen(const char *filename, const char *mode) {
    if (strstr(filename, ".wad") != NULL || strstr(filename, ".WAD") != NULL) {
        return (void *)1; // Fake handle
    }
    if (strstr(filename, ".cfg") != NULL || strstr(filename, ".CFG") != NULL) {
        return NULL; // Explicitly say no config files for now to avoid hangs
    }
    return NULL;
}

int fclose(void *stream) {
    return 0;
}

int fseek(void *stream, long offset, int whence) {
    return 0;
}

long ftell(void *stream) {
    return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, void *stream) {
    return 0;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, void *stream) {
    return 0;
}

int remove(const char *filename) {
    return 0;
}

int rename(const char *oldname, const char *newname) {
    return 0;
}

int mkdir(const char *pathname, ...) {
    return 0;
}
