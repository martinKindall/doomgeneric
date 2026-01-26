#include <efi.h>
#include <efilib.h>

#include "doomgeneric.h"
#include "doomkeys.h"

// GOP related
static EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
static UINT32* video_buffer = NULL;
static UINT32 screen_width = 0;
static UINT32 screen_height = 0;
static UINT32 pixels_per_scanline = 0;

// Timing related
static UINT64 tsc_freq = 0;
static UINT64 start_tsc = 0;

static UINT64 read_tsc() {
    UINT32 hi, lo;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((UINT64)hi << 32) | lo;
}

static void calibrate_tsc() {
    UINT64 start = read_tsc();
    // Stall for 100ms
    uefi_call_wrapper(ST->BootServices->Stall, 1, 100000);
    UINT64 end = read_tsc();
    tsc_freq = (end - start) * 10; // Ticks per second
    if (tsc_freq == 0) tsc_freq = 2000000000; // Fallback to 2GHz
}

void DG_Init() {
    // Already initialized in efi_main
}

void DG_DrawFrame() {
    if (!video_buffer || !DG_ScreenBuffer) return;

    // Center the 640x400 Doom frame in the EFI framebuffer
    UINT32 start_x = (screen_width > DOOMGENERIC_RESX) ? (screen_width - DOOMGENERIC_RESX) / 2 : 0;
    UINT32 start_y = (screen_height > DOOMGENERIC_RESY) ? (screen_height - DOOMGENERIC_RESY) / 2 : 0;

    for (UINT32 y = 0; y < DOOMGENERIC_RESY; y++) {
        if (y + start_y >= screen_height) break;
        
        UINT32* dest = video_buffer + (y + start_y) * pixels_per_scanline + start_x;
        pixel_t* src = DG_ScreenBuffer + y * DOOMGENERIC_RESX;
        
        UINT32 width_to_copy = DOOMGENERIC_RESX;
        if (start_x + width_to_copy > screen_width) width_to_copy = screen_width - start_x;
        
        uefi_call_wrapper(ST->BootServices->CopyMem, 3, dest, src, width_to_copy * sizeof(pixel_t));
    }
}

void DG_SleepMs(uint32_t ms) {
    uefi_call_wrapper(ST->BootServices->Stall, 1, (UINTN)ms * 1000);
}

uint32_t DG_GetTicksMs() {
    if (tsc_freq == 0) {
        calibrate_tsc();
        start_tsc = read_tsc();
    }
    UINT64 current_tsc = read_tsc();
    if (current_tsc < start_tsc) {
        // Handle TSC overflow/reset (unlikely but good practice)
        start_tsc = current_tsc;
        return 0;
    }
    return (uint32_t)((current_tsc - start_tsc) * 1000 / tsc_freq);
}

static unsigned char map_efi_key(EFI_INPUT_KEY key) {
    if (key.ScanCode == 0) {
        // Unicode character
        if (key.UnicodeChar == L'\r' || key.UnicodeChar == L'\n') return KEY_ENTER;
        if (key.UnicodeChar == 27) return KEY_ESCAPE;
        if (key.UnicodeChar == L'\b') return KEY_BACKSPACE;
        if (key.UnicodeChar == L'\t') return KEY_TAB;
        
        // Map common ASCII
        if (key.UnicodeChar >= 32 && key.UnicodeChar <= 126) {
            // Convert to lowercase as Doom expects it for some keys or just use as is
            if (key.UnicodeChar >= L'A' && key.UnicodeChar <= L'Z')
                return (unsigned char)(key.UnicodeChar - L'A' + L'a');
            return (unsigned char)key.UnicodeChar;
        }
    } else {
        // Scan codes
        switch (key.ScanCode) {
            case 0x01: return KEY_UPARROW;
            case 0x02: return KEY_DOWNARROW;
            case 0x03: return KEY_RIGHTARROW;
            case 0x04: return KEY_LEFTARROW;
            case 0x05: return KEY_HOME;
            case 0x06: return KEY_END;
            case 0x07: return KEY_INS;
            case 0x08: return KEY_DEL;
            case 0x09: return KEY_PGUP;
            case 0x0A: return KEY_PGDN;
            case 0x0B: return KEY_F1;
            case 0x0C: return KEY_F2;
            case 0x0D: return KEY_F3;
            case 0x0E: return KEY_F4;
            case 0x0F: return KEY_F5;
            case 0x10: return KEY_F6;
            case 0x11: return KEY_F7;
            case 0x12: return KEY_F8;
            case 0x13: return KEY_F9;
            case 0x14: return KEY_F10;
            case 0x15: return KEY_F11;
            case 0x16: return KEY_F12;
            case 0x17: return KEY_ESCAPE;
        }
    }
    return 0;
}

int DG_GetKey(int* pressed, unsigned char* key) {
    EFI_INPUT_KEY efi_key;
    EFI_STATUS status = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &efi_key);
    
    if (status == EFI_SUCCESS) {
        *pressed = 1;
        *key = map_efi_key(efi_key);
        if (*key != 0) return 1;
    }
    
    return 0;
}

void DG_SetWindowTitle(const char * title) {
    // Not supported in EFI
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);
    
    EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_STATUS status = uefi_call_wrapper(SystemTable->BootServices->LocateProtocol, 3, &gopGuid, NULL, (void**)&gop);
    
    if (EFI_ERROR(status)) {
        Print(L"Unable to locate GOP. Please ensure you have a graphics console.\n");
        return status;
    }
    
    video_buffer = (UINT32*) gop->Mode->FrameBufferBase;
    screen_width = gop->Mode->Info->HorizontalResolution;
    screen_height = gop->Mode->Info->VerticalResolution;
    pixels_per_scanline = gop->Mode->Info->PixelsPerScanLine;
    
    // Clear screen to black
    for (UINT32 i = 0; i < pixels_per_scanline * screen_height; i++) {
        video_buffer[i] = 0;
    }
    
    Print(L"Starting DOOM...\n");

    // Initialize timing
    calibrate_tsc();
    start_tsc = read_tsc();
    
    // Call doomgeneric_Create with dummy argc/argv
    char* argv[] = {"doomgeneric", "-iwad", "doom1.wad", NULL};
    doomgeneric_Create(3, argv);
    
    while(1) {
        doomgeneric_Tick();
    }
    
    return EFI_SUCCESS;
}
