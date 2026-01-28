#include <efi.h>
#include <efilib.h>
#include "stdio.h"
#include "malloc.h"
#include "strings_extension.h"

// Global protocol pointers
static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Volume = NULL;
static EFI_FILE_PROTOCOL *Root = NULL;

// Helper to initialize the file system on the boot device
void InitFileSystem() {
    if (Root) return; // Already initialized

    EFI_STATUS Status;

    // 1. Locate the file system protocol
    Status = uefi_call_wrapper(ST->BootServices->LocateProtocol, 3,
                               &gEfiSimpleFileSystemProtocolGuid, NULL, (void**)&Volume);

    if (EFI_ERROR(Status)) {
        Print(L"Error: Could not locate FileSystemProtocol!\n");
        return;
    }

    // 2. Open the root directory volume
    Status = uefi_call_wrapper(Volume->OpenVolume, 2, Volume, &Root);
}

void *fopen(const char *filename, const char *mode) {
    InitFileSystem();
    if (!Root) return NULL;

    EFI_FILE_PROTOCOL *FileHandle = NULL;

    // 1. Convert char* filename to CHAR16* (Unicode)
    CHAR16 UFileName[256];
    size_t i;
    for (i = 0; i < 255 && filename[i]; i++) {
        // Handle backslashes for UEFI paths
        if (filename[i] == '/') UFileName[i] = '\\';
        else UFileName[i] = (CHAR16)filename[i];
    }
    UFileName[i] = 0;

    // 2. Open the file (Read Only)
    EFI_STATUS Status = uefi_call_wrapper(Root->Open, 5, Root, &FileHandle, UFileName, EFI_FILE_MODE_READ, 0);

    if (EFI_ERROR(Status)) {
        // Print(L"Failed to open file: %s\n", UFileName); // Debug
        return NULL;
    }

    return (void*)FileHandle;
}

int fclose(void *stream) {
    if (!stream) return -1;
    EFI_FILE_PROTOCOL *File = (EFI_FILE_PROTOCOL *)stream;
    uefi_call_wrapper(File->Close, 1, File);
    return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, void *stream) {
    if (!stream || !ptr || size == 0 || nmemb == 0) return 0;

    EFI_FILE_PROTOCOL *File = (EFI_FILE_PROTOCOL *)stream;
    UINTN BytesToRead = size * nmemb;

    EFI_STATUS Status = uefi_call_wrapper(File->Read, 3, File, &BytesToRead, ptr);

    if (EFI_ERROR(Status)) return 0;

    return BytesToRead / size;
}

int fseek(void *stream, long offset, int whence) {
    if (!stream) return -1;
    EFI_FILE_PROTOCOL *File = (EFI_FILE_PROTOCOL *)stream;
    EFI_STATUS Status;
    UINT64 Pos;

    // UEFI uses absolute positioning
    if (whence == SEEK_SET) {
        Pos = (UINT64)offset;
    } else if (whence == SEEK_CUR) {
        uefi_call_wrapper(File->GetPosition, 2, File, &Pos);
        Pos += offset;
    } else if (whence == SEEK_END) {
        // Trick to find end: Get info
        // Simpler for Doom WADs: We usually only SEEK_SET
        return -1;
    }

    Status = uefi_call_wrapper(File->SetPosition, 2, File, Pos);
    return EFI_ERROR(Status) ? -1 : 0;
}

long ftell(void *stream) {
    if (!stream) return -1L;
    EFI_FILE_PROTOCOL *File = (EFI_FILE_PROTOCOL *)stream;
    UINT64 Pos;
    uefi_call_wrapper(File->GetPosition, 2, File, &Pos);
    return (long)Pos;
}
