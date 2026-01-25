#include <efi.h>
#include <efilib.h>

// Simple efi hello world app which writes a color to the Framebuffer

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);

    EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    EFI_STATUS status;

    status = uefi_call_wrapper(SystemTable->BootServices->LocateProtocol, 3, &gopGuid, NULL, (void**)&gop);

    if (EFI_ERROR(status)) {
        Print(L"Unable to locate GOP. Are you using a text-only terminal?\n");
        return status;
    }

    UINT32* video_buffer = (UINT32*) gop->Mode->FrameBufferBase;
    UINT32 width = gop->Mode->Info->HorizontalResolution;
    UINT32 height = gop->Mode->Info->VerticalResolution;
    UINT32 pixels_per_scanline = gop->Mode->Info->PixelsPerScanLine;

    Print(L"GOP Found!\n");
    Print(L"Resolution: %d x %d\n", width, height);
    Print(L"Framebuffer Base: 0x%lx\n", gop->Mode->FrameBufferBase);

    // We treat the screen as a giant 1D array of integers.

    // Color 1: Fill the whole screen with Red
    for (UINT32 y = 0; y < height; y++) {
        for (UINT32 x = 0; x < width; x++) {
            // Calculate the index for this pixel
            UINT32 index = (y * pixels_per_scanline) + x;

            // Write the color directly to memory
            video_buffer[index] = 0x000000FF; // Red
        }
    }

    // Color 2: Draw a White Square in the middle
    UINT32 square_size = 100;
    UINT32 start_x = (width / 2) - (square_size / 2);
    UINT32 start_y = (height / 2) - (square_size / 2);

    for (UINT32 y = start_y; y < start_y + square_size; y++) {
        for (UINT32 x = start_x; x < start_x + square_size; x++) {
            UINT32 index = (y * pixels_per_scanline) + x;
            video_buffer[index] = 0x00FFFFFF; // White
        }
    }

    EFI_INPUT_KEY key;

    while(1) {
        status = uefi_call_wrapper(SystemTable->ConIn->ReadKeyStroke, 2, SystemTable->ConIn, &key);

        if (status == EFI_SUCCESS) {
            if (key.UnicodeChar == L'q') break;
        }
    }

    return EFI_SUCCESS;
}
