#include <efi.h>
#include <efilib.h>

#include "doomgeneric.h"
#include "doomkeys.h"

// Define resolution defaults if Makefile doesn't
#ifndef DOOMGENERIC_RESX
#define DOOMGENERIC_RESX 640
#endif
#ifndef DOOMGENERIC_RESY
#define DOOMGENERIC_RESY 400
#endif

#define KEY_TTL 4
static uint8_t key_countdown[256] = {0};
static uint8_t key_current_state[256] = {0};

static EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;

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
    uefi_call_wrapper(ST->BootServices->Stall, 1, 100000); // 100ms
    UINT64 end = read_tsc();
    tsc_freq = (end - start) * 10;
    if (tsc_freq == 0) tsc_freq = 2000000000;
}

void DG_Init() {
    // 1. Allocate the 32-bit screen buffer
    // Width * Height * 4 bytes-per-pixel
    DG_ScreenBuffer = malloc(DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4);

    // 2. Clear it to a debug color (e.g., Purple) to prove allocation worked
    if (DG_ScreenBuffer) {
        memset(DG_ScreenBuffer, 0xFF00FF, DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4);
    }
}

void DG_DrawFrame() {
    // 1. Input Maintenance
    for (int i = 0; i < 256; i++) {
        if (key_countdown[i] > 0) key_countdown[i]--;
    }

    if (!gop || !DG_ScreenBuffer) return;

    // 2. HEARTBEAT (Debug): 
    // Flashing Green (Active) vs Red (Stalled).
    static int frame_count = 0;
    frame_count++;
    // Write directly to buffer
    DG_ScreenBuffer[0] = (frame_count % 10 < 5) ? 0x00FF00 : 0x000000;

    // 3. THE FIX: FLUSH CACHE
    // This assembly instruction forces the CPU to write all cache lines to RAM.
    // Without this, the GOP hardware reads empty zeros from RAM while 
    // your game pixels are stuck in the CPU L1/L2 cache.
    __asm__ __volatile__ ("wbinvd" ::: "memory");

    // 4. Calculate Centering
    UINT32 screen_width = gop->Mode->Info->HorizontalResolution;
    UINT32 screen_height = gop->Mode->Info->VerticalResolution;
    
    // Safety check to prevent huge Blts from crashing
    if (DOOMGENERIC_RESX > screen_width || DOOMGENERIC_RESY > screen_height) {
        return; 
    }

    UINT32 start_x = (screen_width - DOOMGENERIC_RESX) / 2;
    UINT32 start_y = (screen_height - DOOMGENERIC_RESY) / 2;

    // 5. Blt to Screen
    uefi_call_wrapper(gop->Blt, 10, 
        gop, 
        (EFI_GRAPHICS_OUTPUT_BLT_PIXEL*)DG_ScreenBuffer, 
        EfiBltBufferToVideo, 
        0, 0,           // Source X, Y
        start_x, start_y, // Dest X, Y
        DOOMGENERIC_RESX, DOOMGENERIC_RESY, // Width, Height
        0 // Delta
    );
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
    return (uint32_t)((current_tsc - start_tsc) * 1000 / tsc_freq);
}

static unsigned char map_efi_key(EFI_INPUT_KEY k) {
    switch(k.ScanCode) {
        case 0x01: return KEY_UPARROW;
        case 0x02: return KEY_DOWNARROW;
        case 0x03: return KEY_RIGHTARROW;
        case 0x04: return KEY_LEFTARROW;
        case 0x17: return KEY_ESCAPE;
    }
    char c = k.UnicodeChar;
    if (c == 'z' || c == 'Z') return KEY_FIRE;
    if (c == 'e' || c == 'E' || c == ' ') return KEY_USE;
    if (c == 13) return KEY_ENTER;
    if (c == 'x' || c == 'X' || c == ',') return KEY_RALT;
    if (c == 'c' || c == 'C' || c == '.') return KEY_RSHIFT;
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

int DG_GetKey(int* pressed, unsigned char* key) {
    EFI_INPUT_KEY efi_key;
    EFI_STATUS status;

    while (1) {
        status = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &efi_key);
        if (status != EFI_SUCCESS) break;
        unsigned char k = map_efi_key(efi_key);
        if (k != 0) key_countdown[k] = KEY_TTL;
    }

    for (int i = 0; i < 256; i++) {
        if (key_countdown[i] > 0 && key_current_state[i] == 0) {
            *pressed = 1; *key = (unsigned char)i; key_current_state[i] = 1; return 1;
        }
        if (key_countdown[i] == 0 && key_current_state[i] == 1) {
            *pressed = 0; *key = (unsigned char)i; key_current_state[i] = 0; return 1;
        }
    }
    return 0;
}

void DG_SetWindowTitle(const char * title) {}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);

    // 1. Disable Watchdog Timer (Prevents system reset during long load/play)
    uefi_call_wrapper(SystemTable->BootServices->SetWatchdogTimer, 4, 0, 0, 0, NULL);

    EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_STATUS status = uefi_call_wrapper(SystemTable->BootServices->LocateProtocol, 3, &gopGuid, NULL, (void**)&gop);

    if (EFI_ERROR(status)) {
        Print(L"GOP Not Found.\n");
        return status;
    }

    // 2. Hide Console Cursor
    uefi_call_wrapper(SystemTable->ConOut->EnableCursor, 2, SystemTable->ConOut, FALSE);

    Print(L"Starting DOOM...\n");

    calibrate_tsc();
    start_tsc = read_tsc();

    char* argv[] = {"doomgeneric", "-iwad", "\\doom1.wad", NULL};
    doomgeneric_Create(3, argv);

    while(1) {
        doomgeneric_Tick();
    }

    return EFI_SUCCESS;
}
