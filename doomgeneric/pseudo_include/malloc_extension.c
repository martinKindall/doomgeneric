#include <efi.h>
#include <efilib.h>
#include "malloc.h"

typedef struct {
    size_t size;
} alloc_header_t;

void* malloc(size_t size) {
    void* ptr = NULL;
    size_t total_size = size + sizeof(alloc_header_t);
    EFI_STATUS status = uefi_call_wrapper(ST->BootServices->AllocatePool, 3, EfiLoaderData, total_size, &ptr);
    if (EFI_ERROR(status)) {
        Print(L"malloc failed for %u bytes\r\n", (UINT32)size);
        return NULL;
    }
    
    alloc_header_t* header = (alloc_header_t*)ptr;
    header->size = size;
    return (void*)(header + 1);
}

void free(void* ptr) {
    if (ptr) {
        alloc_header_t* header = (alloc_header_t*)ptr - 1;
        uefi_call_wrapper(ST->BootServices->FreePool, 1, header);
    }
}

void* calloc(size_t num_elements, size_t element_size) {
    size_t bytes = num_elements * element_size;
    void* ret = malloc(bytes);
    if (ret != NULL) {
        uefi_call_wrapper(ST->BootServices->SetMem, 3, ret, bytes, 0);
    } else {
        Print(L"calloc failed for %u bytes\r\n", (UINT32)bytes);
    }
    return ret;
}

void* realloc(void* ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    alloc_header_t* header = (alloc_header_t*)ptr - 1;
    if (header->size >= size) {
        // Optimization: if existing block is large enough, just return it.
        // Though EFI AllocatePool doesn't easily allow shrinking/growing.
        return ptr;
    }

    void* new_ptr = malloc(size);
    if (new_ptr) {
        uefi_call_wrapper(ST->BootServices->CopyMem, 3, new_ptr, ptr, header->size);
        free(ptr);
    }
    return new_ptr;
}

int atoi(const char *str) {
    int res = 0;
    int sign = 1;
    if (*str == '-') {
        sign = -1;
        str++;
    }
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }
    return res * sign;
}

int abs(int x) {
    return x < 0 ? -x : x;
}

void exit(int status) {
    if (status != 0) {
        Print(L"Exit with status %d\r\n", status);
    }
    // Instead of busy-looping, let's try to return to EFI firmware if possible,
    // but efi_main doesn't have a clean way to be called back.
    // For now, at least we won't spin at 100% CPU if we use Stall.
    while(1) {
        uefi_call_wrapper(ST->BootServices->Stall, 1, 1000000);
    }
}

int system(const char *command) {
    return 0;
}

double atof(const char *str) {
    return 0.0;
}
