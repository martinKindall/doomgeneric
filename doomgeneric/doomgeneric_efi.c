#include <efi.h>
#include <efilib.h>

#include "doomgeneric.h"
#include "doomkeys.h"
#include <string.h>

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

// --- DIRECT FRAMEBUFFER GLOBALS (The fix from main.c) ---
static UINT32 *FrameBufferBase = NULL;
static UINT32 ScreenWidth = 0;
static UINT32 ScreenHeight = 0;
static UINT32 PixelsPerScanLine = 0;
// --------------------------------------------------------

// Timing related
static UINT64 tsc_freq = 0;
static UINT64 start_tsc = 0;

static UINT64 read_tsc() {
    UINT32 hi, lo;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((UINT64)hi << 32) | lo;
}

static void calibrate_tsc() {
    UINT64 t1, t2;
    
    // 1. Snapshot TSC
    t1 = read_tsc();
    
    // 2. Wait exactly 100,000 microseconds (0.1 seconds)
    // The UEFI firmware provides this high-precision delay
    uefi_call_wrapper(ST->BootServices->Stall, 1, 100000);
    
    // 3. Snapshot TSC again
    t2 = read_tsc();
    
    // 4. Calculate ticks per second (delta * 10 because we waited 0.1s)
    tsc_freq = (t2 - t1) * 10;
    
    // Avoid division by zero paranoia
    if (tsc_freq == 0) tsc_freq = 1; 
    
    // Print it just so we know it worked (debug info)
    Print(L"TSC Calibrated: %lu Hz\r\n", tsc_freq);
}

void DG_Init() {
    // OLD: Allocates only for 640x400
    // DG_ScreenBuffer = malloc(DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4);

    // NEW: Allocate enough for the full physical screen (1920x1080 * 4 bytes approx 8MB)
    // We use a safe upper limit to handle scaling.
    DG_ScreenBuffer = malloc(1920 * 1080 * 4);

    if (DG_ScreenBuffer) {
        memset(DG_ScreenBuffer, 0, 1920 * 1080 * 4);
    }
}

typedef struct { unsigned char r, g, b; } dg_palette_entry_t;
extern dg_palette_entry_t DG_Palette[256];

void DG_DrawFrame() {
    // 1. Get UEFI Video Details
    UINT32 *VideoMem = (UINT32*)gop->Mode->FrameBufferBase;
    UINT32 PixelsPerScanLine = gop->Mode->Info->PixelsPerScanLine;
    
    // 2. Loop through Doom's 640x400 buffer
    // defined in your Makefile
    int w = DOOMGENERIC_RESX; 
    int h = DOOMGENERIC_RESY;

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            // Get the 8-bit color index from Doom
            unsigned char idx = DG_ScreenBuffer[y * w + x];

            // Convert to UEFI 32-bit Color (BGR reserved)
            // Note: If colors look blue/red swapped, swap the R and B shifts below
            UINT32 pixel = (0xFF000000) |               // Reserved/Alpha
                           (DG_Palette[idx].r << 16) |  // Red
                           (DG_Palette[idx].g << 8) |   // Green
                           (DG_Palette[idx].b);         // Blue

            // Write to Video Memory
            // We use PixelsPerScanLine (Stride) to jump rows correctly
            VideoMem[y * PixelsPerScanLine + x] = pixel;
        }
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
    UINT64 delta = current_tsc - start_tsc;
    
    // Calculate milliseconds: (delta * 1000) / freq
    // We use standard 64-bit math.
    // Note: This wraps around if you play for >500,000 years.
    return (uint32_t)((delta * 1000) / tsc_freq);
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

    // 1. Disable Watchdog Timer
    uefi_call_wrapper(SystemTable->BootServices->SetWatchdogTimer, 4, 0, 0, 0, NULL);

    EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_STATUS status = uefi_call_wrapper(SystemTable->BootServices->LocateProtocol, 3, &gopGuid, NULL, (void**)&gop);

    if (EFI_ERROR(status)) {
        Print(L"GOP Not Found.\n");
        return status;
    }

    // --- SETUP DIRECT FRAMEBUFFER INFO ---
    FrameBufferBase = (UINT32*) gop->Mode->FrameBufferBase;
    ScreenWidth = gop->Mode->Info->HorizontalResolution;
    ScreenHeight = gop->Mode->Info->VerticalResolution;
    PixelsPerScanLine = gop->Mode->Info->PixelsPerScanLine;

    // FIX: If UEFI returns 0 (common bug), use ScreenWidth instead.
    if (PixelsPerScanLine == 0) {
        Print(L"Fixing PixelsPerScanLine (was 0)\n");
        PixelsPerScanLine = ScreenWidth;
    }

    Print(L"Resolution: %d x %d\n", ScreenWidth, ScreenHeight);
    Print(L"ScanLine: %d\n", PixelsPerScanLine); // Print this to verify!
    Print(L"FB Base: 0x%lx\n", (UINT64)FrameBufferBase);
    // -------------------------------------

    // 2. Hide Console Cursor
    uefi_call_wrapper(SystemTable->ConOut->EnableCursor, 2, SystemTable->ConOut, FALSE);

    Print(L"Starting DOOM...\n");

    calibrate_tsc();
    start_tsc = read_tsc();

    char* argv[] = {"doomgeneric", "-iwad", "doom1.wad", NULL};
    doomgeneric_Create(3, argv);

    while(1) {
        doomgeneric_Tick();
    }

    return EFI_SUCCESS;
}
