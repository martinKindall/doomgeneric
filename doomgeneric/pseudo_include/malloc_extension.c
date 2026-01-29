#include <efi.h>
#include <efilib.h>
#include "malloc.h"

// Extended header to track if we used Pool or Pages
typedef struct {
    size_t size;
    UINTN pages; // 0 if allocated via Pool, >0 if allocated via Pages
} alloc_header_t;

void* malloc(size_t size) {
    void* ptr = NULL;
    size_t total_size = size + sizeof(alloc_header_t);
    EFI_STATUS status;

    // Threshold: 4KB. Anything larger uses AllocatePages to avoid pool fragmentation.
    if (total_size > 4096) {
        Print(L"Allocating more than 4096\n");

        // Calculate number of 4k pages needed
        UINTN pages = (total_size + 4095) / 4096;
        EFI_PHYSICAL_ADDRESS phys_addr = 0;

        status = uefi_call_wrapper(ST->BootServices->AllocatePages, 4, 
                           AllocateAnyPages, EfiLoaderData, pages, &phys_addr);

        if (EFI_ERROR(status)) {
            Print(L"malloc (pages) failed for %u bytes (status: %r)\r\n", (UINT32)size, status);
            return NULL;
        }
        ptr = (void*)(UINTN)phys_addr;

        alloc_header_t* header = (alloc_header_t*)ptr;
        header->size = size;
        header->pages = pages;
        return (void*)(header + 1);

    } else {
        // Use standard pool for small allocations
        status = uefi_call_wrapper(ST->BootServices->AllocatePool, 3,
                                   EfiLoaderData, total_size, &ptr);

        if (EFI_ERROR(status)) {
            Print(L"malloc (pool) failed for %u bytes\r\n", (UINT32)size);
            return NULL;
        }

        alloc_header_t* header = (alloc_header_t*)ptr;
        header->size = size;
        header->pages = 0; // Mark as pool
        return (void*)(header + 1);
    }
}

void free(void* ptr) {
    if (ptr) {
        alloc_header_t* header = (alloc_header_t*)ptr - 1;

        if (header->pages > 0) {
            // It was a page allocation
            uefi_call_wrapper(ST->BootServices->FreePages, 2,
                              (EFI_PHYSICAL_ADDRESS)(UINTN)header, header->pages);
        } else {
            // It was a pool allocation
            uefi_call_wrapper(ST->BootServices->FreePool, 1, header);
        }
    }
}

// Keep calloc/realloc mostly the same, they relay to malloc/free
void* calloc(size_t num_elements, size_t element_size) {
    size_t bytes = num_elements * element_size;
    void* ret = malloc(bytes);
    if (ret != NULL) {
        uefi_call_wrapper(ST->BootServices->SetMem, 3, ret, bytes, 0);
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
    // Attempt to set a background color to indicate death
    if (ST->ConOut) {
        uefi_call_wrapper(ST->ConOut->SetAttribute, 2, ST->ConOut, EFI_BACKGROUND_RED | EFI_WHITE);
        uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut);
        Print(L"DOOM CRASHED: Status %d\r\n", status);
    }

    // Spin forever
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
