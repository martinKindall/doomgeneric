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
    UINT64 start = read_tsc();
    // Stall for 100ms
    uefi_call_wrapper(ST->BootServices->Stall, 1, 100000);
    UINT64 end = read_tsc();
    tsc_freq = (end - start) * 10; // Ticks per second
    if (tsc_freq == 0) tsc_freq = 2000000000; // Fallback to 2GHz
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

    // 2. Setup Source Buffer
    // Cast DG_ScreenBuffer to UINT32 because i_video.c already put 32-bit pixels there.
    UINT32 *SrcBuffer = (UINT32*)DG_ScreenBuffer;

    // Use the resolution defined in your build
    int w = DOOMGENERIC_RESX; 
    int h = DOOMGENERIC_RESY;

    // 3. Copy Loop
    for (int y = 0; y < h; y++)
    {
        // Calculate offsets
        // Source is tightly packed (w pixels wide)
        // Destination has a stride (PixelsPerScanLine)
        UINT32 *SrcRow = &SrcBuffer[y * w];
        UINT32 *DestRow = &VideoMem[y * PixelsPerScanLine];

        // Copy the entire row at once for performance
        // (Note: You can use CopyMem/memcpy here if available in your EFI lib)
        for (int x = 0; x < w; x++) {
            DestRow[x] = SrcRow[x];
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

static void InitFPU() {
    // Reset FPU state (x87)
    __asm__ volatile ("fninit");

    // Set MXCSR to default (Mask all exceptions: 0x1F80)
    // Bits 7-12 are exception masks. 0x1F80 masks them all.
    // (Precision, Underflow, Overflow, Zero Divide, Denormal, Invalid Op)
    uint32_t mxcsr = 0x1F80; 
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr));
}

// 2. Add this strictly to satisfy some linkers that see 'float' usage
int _fltused = 1;

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);

    // CALL IT HERE, immediately after library init
    InitFPU();

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

    if (gop->Mode->Info->PixelsPerScanLine > gop->Mode->Info->HorizontalResolution) {
        PixelsPerScanLine = gop->Mode->Info->PixelsPerScanLine;
    } else if (gop->Mode->Info->PixelsPerScanLine == 0) {
        // Only fix if it's actually missing
        PixelsPerScanLine = gop->Mode->Info->HorizontalResolution;
    }

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
