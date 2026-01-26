#include <efi.h>
#include <efilib.h>

#include "doomgeneric.h"
#include "doomkeys.h"

#define KEY_TTL 4  // Keep key 'held' for 6 frames after last UEFI event
static uint8_t key_countdown[256] = {0};      // How many frames to keep holding the key
static uint8_t key_current_state[256] = {0};  // What we currently told Doom (1=Down, 0=Up)

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
    // 1. INPUT MAINTENANCE: Decrease key timers every frame
    for (int i = 0; i < 256; i++) {
        if (key_countdown[i] > 0) {
            key_countdown[i]--;
        }
    }

    // 2. DRAWING LOGIC (Your original code preserved)
    if (!video_buffer || !DG_ScreenBuffer) return;

    UINT32 start_x = (screen_width > DOOMGENERIC_RESX) ? (screen_width - DOOMGENERIC_RESX) / 2 : 0;
    UINT32 start_y = (screen_height > DOOMGENERIC_RESY) ? (screen_height - DOOMGENERIC_RESY) / 2 : 0;

    for (UINT32 y = 0; y < DOOMGENERIC_RESY; y++) {
        if (y + start_y >= screen_height) break;

        UINT32* dest = video_buffer + (y + start_y) * pixels_per_scanline + start_x;
        pixel_t* src = DG_ScreenBuffer + y * DOOMGENERIC_RESX;

        UINT32 width_to_copy = DOOMGENERIC_RESX;
        if (start_x + width_to_copy > screen_width) width_to_copy = screen_width - start_x;

        for (UINT32 x = 0; x < width_to_copy; x++) {
            dest[x] = src[x];
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
    if (current_tsc < start_tsc) {
        // Handle TSC overflow/reset (unlikely but good practice)
        start_tsc = current_tsc;
        return 0;
    }
    return (uint32_t)((current_tsc - start_tsc) * 1000 / tsc_freq);
}

static unsigned char map_efi_key(EFI_INPUT_KEY k) {
    // 1. Handle Scan Codes (Non-ASCII keys)
    switch(k.ScanCode) {
        case 0x01: return KEY_UPARROW;
        case 0x02: return KEY_DOWNARROW;
        case 0x03: return KEY_RIGHTARROW;
        case 0x04: return KEY_LEFTARROW;
        case 0x17: return KEY_ESCAPE;
        // Map F1-F12 if needed here
    }

    // 2. Handle ASCII / Unicode
    char c = k.UnicodeChar;

    // --- MAPPING CORRECTIONS ---

    // FIRE: Map 'z' to KEY_FIRE (0xa3) instead of RCTRL
    // The game engine expects this specific internal value
    if (c == 'z' || c == 'Z') return KEY_FIRE;

    // USE/OPEN: Map 'e' or Space to KEY_USE (0xa2)
    // Sending raw ' ' (0x20) failed because the game expects 0xa2
    if (c == 'e' || c == 'E' || c == ' ') return KEY_USE;

    // ENTER: Select in menus
    if (c == 13) return KEY_ENTER;

    // STRAFE: Map 'x' to Alt
    if (c == 'x' || c == 'X' || c == ',') return KEY_RALT;

    // RUN: Map 'c' to Shift
    if (c == 'c' || c == 'C' || c == '.') return KEY_RSHIFT;

    // ---------------------

    // Standard ASCII: lowercase to uppercase for Doom engine
    if (c >= 'a' && c <= 'z') return c - 32;

    return c;
}

static int key_pending_release = 0;

int DG_GetKey(int* pressed, unsigned char* key) {
    // 1. Poll UEFI for ALL available keys in the buffer
    EFI_INPUT_KEY efi_key;
    EFI_STATUS status;

    // Loop until buffer is empty to catch all inputs
    while (1) {
        status = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &efi_key);
        if (status != EFI_SUCCESS) break; // Buffer empty

        unsigned char k = map_efi_key(efi_key);
        if (k != 0) {
            // Reset the "Time To Live" for this key
            key_countdown[k] = KEY_TTL;
        }
    }

    // 2. Compare internal state (TTL) vs Doom state (key_current_state)
    // We iterate through all keys to see if status changed.
    // DoomGeneric usually calls DG_GetKey continuously until we return 0.
    for (int i = 0; i < 256; i++) {
        // CASE A: Key is active in UEFI (countdown > 0), but Doom thinks it's Up.
        // -> Send KEY DOWN
        if (key_countdown[i] > 0 && key_current_state[i] == 0) {
            *pressed = 1;
            *key = (unsigned char)i;
            key_current_state[i] = 1; // Update our state
            return 1; // Event generated
        }

        // CASE B: Key has expired (countdown == 0), but Doom thinks it's Down.
        // -> Send KEY UP
        if (key_countdown[i] == 0 && key_current_state[i] == 1) {
            *pressed = 0;
            *key = (unsigned char)i;
            key_current_state[i] = 0; // Update our state
            return 1; // Event generated
        }
    }

    return 0; // No events generated this pass
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
