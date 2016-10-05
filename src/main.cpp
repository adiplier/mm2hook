#include "main.h"

LPFNDIRECTINPUTCREATE lpDICreate;

// Export as 'DirectInputCreateA/W' so we can hook into MM2
// (Apparently DirectInputCreateW gets called sometimes...)
#pragma comment(linker, "/EXPORT:DirectInputCreateA=_DirectInputCreateA_Impl")
#pragma comment(linker, "/EXPORT:DirectInputCreateW=_DirectInputCreateA_Impl")
HRESULT NAKED DirectInputCreateA_Impl(HINSTANCE hinst, DWORD dwVersion, LPVOID *ppDI, LPUNKNOWN punkOuter)
{
    _asm jmp DWORD PTR ds:lpDICreate
}

CMidtownMadness2 *pMM2;

MM2Version gameVersion;

WNDPROC hProcOld;
LRESULT APIENTRY WndProcNew(HWND, UINT, WPARAM, LPARAM);

bool isWindowSubclassed = false;
bool isConsoleOpen = false;

// ==========================
// Game-related properties
// ==========================

/* AGE Debugging */

FILE *ageLog;

LPCSTR ageLogFile = "AGE.log";
bool ageDebugEnabled = false;
char ageDebug_buffer[1024] = { NULL };

/* Heap fix */

int g_heapSize = 128;

/* PSDL shading fix */

const double cosNum = 1.570796;

UINT32 vglColor;
UINT32 vglCalculatedColor = 0xFFFFFFFF;

MM2::Vector3 vglAmbient;
MM2::Vector3 vglKeyColor;
MM2::Vector3 vglFill1Color;
MM2::Vector3 vglFill2Color;
MM2::Vector3 vglShadedColor;

/* ARGB color */
struct {
    char b;
    char g;
    char r;
    char a;
} vglResultColor = {
    (char)0,
    (char)0,
    (char)0,
    (char)255,
};

/* Dashboard experiment */

static MM2::Matrix34 sm_DashOffset;

// ==========================
// Function hooks
// ==========================

MM2FnHook<void> $CreateGameMutex            ( NULL, NULL, 0x402180 );

MM2FnHook<void> $dgBangerInstance_Draw      ( NULL, NULL, 0x4415E0 );

MM2FnHook<UINT32> $sdlPage16_GetShadedColor ( NULL, NULL, 0x450880 );

MM2FnHook<void> $asLinearCS_Update          ( NULL, NULL, 0x4A3370 );

MM2FnHook<bool> $gfxAutoDetect              ( NULL, NULL, 0x4ABE00 );

MM2FnHook<void> $setRes                     ( NULL, NULL, 0x4A8CE0 );

/*
    TODO: Move VGL stuff to a separate file?
*/

MM2FnHook<void> $vglBegin                   ( NULL, NULL, 0x4A5500 );
MM2FnHook<void> $vglEnd                     ( NULL, NULL, 0x4A5A90 );

MM2FnHook<void> $memSafeHeap_Init           ( NULL, NULL, 0x577210 );

MM2FnHook<void> $DefaultPrintString         ( NULL, NULL, 0x4C9510 );
MM2FnHook<void> $DefaultPrinter             ( NULL, NULL, 0x4C95F0 );

// ==========================
// Pointer hooks
// ==========================

MM2PtrHook<MM2::cityTimeWeatherLighting> 
                $TIMEWEATHER                ( NULL, NULL, 0x6299A8 );

MM2PtrHook<int> $timeOfDay                  ( NULL, NULL, 0x62B068 );

MM2PtrHook<UINT32> $vglCurrentColor         ( NULL, NULL, 0x661974 );

MM2PtrHook<MM2::asNode> $ROOT               ( NULL, NULL, 0x661738 );

MM2PtrHook<void (*)(LPCSTR)>
                    $PrintString            ( NULL, NULL, 0x5CECF0 );
MM2PtrHook<void (*)(int, LPCSTR, char *)>
                    $Printer                ( NULL, NULL, 0x5CED24);

/*
    !! THESE ARE ABSOLUTELY CRITICAL TO THE HOOK WORKING PROPERLY !!
*/

MM2PtrHook<DWORD> $__VtResumeSampling       ( 0x5C86B8, 0x5DF710, 0x5E0CC4 );
MM2PtrHook<DWORD> $__VtPauseSampling        ( 0x5C86C8, 0x5DF724, 0x5E0CD8 );
MM2PtrHook<BOOL> $gameClosing               ( 0x667DEC, 0x6B0150, 0x6B1708 );

/*
    ===========================================================================
*/

bool SubclassGameWindow(HWND gameWnd, WNDPROC pWndProcNew, WNDPROC *ppWndProcOld)
{
    LogFile::Write("Subclassing window...");
    if (gameWnd != NULL)
    {
        WNDPROC hProcOld = (WNDPROC)GetWindowLong(gameWnd, GWL_WNDPROC);

        *ppWndProcOld = hProcOld;

        if (hProcOld != NULL && SetWindowLong(gameWnd, GWL_WNDPROC, (LONG)pWndProcNew))
        {
            LogFile::WriteLine("Done!");
            return true;
        }
    }
    LogFile::WriteLine("FAIL!");
    return false;
}

bool HandleKeyPress(DWORD vKey)
{
    // Inform Lua of any changes beforehand
    MM2Lua::OnKeyPress(vKey);

    switch (vKey) {
        // '~'
        case VK_OEM_2:
        case VK_OEM_3:
        {
            // tell the game to open a chat box,
            // and then use a local variable to check if it's open

            MM2::mmGameManager *mgr = MM2::mmGameManager::Instance();
            auto gamePtr = mgr->getGame();

            if (gamePtr != NULL)
            {
                auto popup = gamePtr->getPopup();

                if (popup != NULL) {
                    // don't try opening it again if it's already open
                    if (popup->IsEnabled() && isConsoleOpen)
                        return true;

                    popup->ProcessChat();
                    isConsoleOpen = true;
                }
            }
        } return true;
    }
    return false;
}

LRESULT APIENTRY WndProcNew(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_KEYUP:
        case WM_SYSKEYUP:
        {
            if (HandleKeyPress((DWORD)wParam))
                return 0;
        } break;
    }
    return CallWindowProc(hProcOld, hWnd, uMsg, wParam, lParam);
}

// ==========================
// Generic functions
// ==========================

MM2::Vector3 addPitch(MM2::Vector3 *vec, float pitch) {
    float p = (float)fmod(pitch, 3.14159);
    bool pitchIsZero = (pitch >= 0.0f);

    return{
        (float)((!pitchIsZero) ? vec->X * cos(pitch + cosNum) : 0.0f),
        (float)((!pitchIsZero) ? vec->Y * cos(pitch + cosNum) : 0.0f),
        (float)((!pitchIsZero) ? vec->Z * cos(pitch + cosNum) : 0.0f),
    };
}

float normalize(float value) {
    if (value >= 2.0f)
        value = 1.0f;

    return (value > 1.0f) ? (value - (value - 1.0f)) : value;
};

MM2::Vector3 intToColor(int value) {
    return{
        (float)((char)((value & 0xFF0000) >> 16) / 256.0),
        (float)((char)((value & 0xFF00) >> 8) / 256.0),
        (float)((char)((value & 0xFF)) / 256.0),
    };
}

// ==========================
// Callback handlers
// ==========================

#define FOREGROUND_PINK (FOREGROUND_BLUE | FOREGROUND_RED)
#define FOREGROUND_TEAL (FOREGROUND_BLUE | FOREGROUND_GREEN)
#define FOREGROUND_YELLOW (FOREGROUND_RED | FOREGROUND_GREEN)
#define FOREGROUND_WHITE (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)

#define BACKGROUND_PINK (BACKGROUND_BLUE | BACKGROUND_RED)
#define BACKGROUND_TEAL (BACKGROUND_BLUE | BACKGROUND_GREEN)
#define BACKGROUND_YELLOW (BACKGROUND_RED | BACKGROUND_GREEN)
#define BACKGROUND_WHITE (BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE)

short printer_types[] = {
    FOREGROUND_INTENSITY, // print
    FOREGROUND_INTENSITY, // message
    FOREGROUND_INTENSITY, // display
    FOREGROUND_YELLOW | FOREGROUND_INTENSITY, // warning
    FOREGROUND_RED | FOREGROUND_INTENSITY, // error
    FOREGROUND_RED | FOREGROUND_INTENSITY, // quit/abort
};

class PrinterHandler {
public:
    static void PrintString(LPCSTR message) {
        // TODO: redirect to a log file?
        $DefaultPrintString(message);
    };

    static void Print(int level, LPCSTR message, char *va_args) {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

        SetConsoleTextAttribute(hConsole, printer_types[level]);
        $DefaultPrinter(level, message, va_args);
        SetConsoleTextAttribute(hConsole, FOREGROUND_WHITE);
    };
};

class HookSystemHandler {
private:
    static void InitializeLua() {
        // Guaranteed to be loaded before anything vital is called (e.g. AngelReadString)
        if (gameVersion == MM2_RETAIL)
        {
            MM2Lua::Initialize();
        } else {
            // no lua support for betas yet
            MessageBox(NULL, "NOTE: This game version does not currently support Lua scripting.", "MM2Hook", MB_OK | MB_ICONINFORMATION);
            return;
        }
    }
public:
    static void Initialize(int argv, char **argc) {
        // initialize the Lua engine
        InitializeLua();

        if (gameVersion == MM2_RETAIL)
        {
            // hook into the printer
            $Printer.set(&PrinterHandler::Print);
            $PrintString.set(&PrinterHandler::PrintString);

            /* Won't write to the log file for some reason :(
            LogFile::Write("Redirecting MM2 output...");

            if (MM2::datOutput::OpenLog("mm2.log"))
                LogFile::Write("Done!\n");
            else
                LogFile::Write("FAIL!\n");
            */
        }
    }

    static void Reset(bool restarting) {
        LogFile::Write("Hook reset request received: ");
        LogFile::WriteLine((restarting) ? "leaving GameLoop" : "entering GameLoop");

        if (restarting)
            MM2Lua::OnRestart();

        MM2Lua::Reset();
    }

    static void Start() {
        if (!$gameClosing) {
            if (!isWindowSubclassed)
            {
                SubclassGameWindow(pMM2->GetMainWindowHwnd(), WndProcNew, &hProcOld);
                isWindowSubclassed = true;
            }

            // GameLoop was restarted
            Reset(false);
        } else {
            LogFile::WriteLine("WTF: Hook startup request received, but the game is closing!");
        }
    }

    static void Stop() {
        if ($gameClosing) {
            LogFile::WriteLine("Hook shutdown request received.");

            LogFile::Close();
            L.close(); // release Lua
        } else {
            // GameLoop is restarting
            Reset(true);
        }
    }
};

class TickHandler {
public:
    static void Reset(void) {
        // TODO: reset tick stuff
    }

    static void Update(void) {
        MM2Lua::OnTick();

        // pass control back to MM2
        MM2::datTimeManager::Update();
    }
};

class ChatHandler {
public:
    void Process(char *message) {
        if (isConsoleOpen) {
            MM2Lua::SendCommand(message);

            // return normal chatbox behavior
            isConsoleOpen = false;
        } else {
            LogFile::Format("Got chat message: %s\n", message);
        }
    }
};

struct mmGraphicsInterface
{
    GUID GUID;
    char Name[64];

    unsigned int DeviceCaps;

    enum mmRenderer : unsigned int
    {
        Software = 0,    // Software (No 3D Video Card)
        Hardware = 1,    // Hardware (3D Video Card)
        HardwareWithTnL = 2     // Hardware (3D Video Card With T&L)
    };

    mmRenderer Renderer;

    unsigned int ResolutionCount;   // Max of 64 mmResolutions
    unsigned int ResolutionChoice;

    enum mmColorDepthFlag : unsigned int
    {
        Depth16 = 0x400,
        Depth24 = 0x200,
        Depth32 = 0x100
    };

    mmColorDepthFlag AcceptableDepths;  // Used to check if mmResolution::Depth is allowed

    unsigned int AvailableMemory;
    unsigned int VendorID;
    unsigned int DeviceID;

    struct mmResolution
    {
        unsigned short ScreenWidth;
        unsigned short ScreenHeight;
        unsigned short ColorDepth;      // Always 16
        unsigned short Is16BitColor;    // = (ColorDepth == 16) + 6 // Always 7
    };

    mmResolution Resolutions[64];
};

#define DIRECT3D_VERSION        0x0700
#define DIRECTINPUT_VERSION     0x0700

#include <d3d.h>

BOOL __stdcall AutoDetectCallback (GUID*    lpGUID,
                                   LPSTR    lpDriverDescription,
                                   LPSTR    lpDriverName,
                                   LPVOID   lpContext)
{
    LogFile::Format ("GFXAutoDetect: Description=%s, Name=%s\n", lpDriverDescription, lpDriverName);

    MM2PtrHook<HRESULT(WINAPI*)(GUID*,
                                LPVOID*,
                                REFIID,
                                IUnknown*)> DirectDrawCreateEx  (NULL, NULL, 0x684518);

    MM2PtrHook<IDirectDraw7*>               lpDD                (NULL, NULL, 0x6830A8);
    MM2PtrHook<IDirect3D7*>                 lpD3D               (NULL, NULL, 0x6830AC);

    MM2PtrHook<mmGraphicsInterface>         gfxInterfaces       (NULL, NULL, 0x683130);
    MM2PtrHook<unsigned int>                gfxInterfaceCount   (NULL, NULL, 0x6844C0);

    MM2PtrHook<HRESULT(CALLBACK)(LPSTR,
                                 LPSTR,
                                 LPD3DDEVICEDESC7,
                                 LPVOID)>
                                            lpDeviceCallback    (NULL, NULL, 0x4AC3D0);
    MM2PtrHook<HRESULT(PASCAL)(LPDDSURFACEDESC2,
                               LPVOID)>
                                            lpResCallback       (NULL, NULL, 0x4AC6F0);

    MM2PtrHook<unsigned int>                gfxMaxScreenWidth   (NULL, NULL, 0x6844FC);
    MM2PtrHook<unsigned int>                gfxMaxScreenHeight  (NULL, NULL, 0x6844D8);

    if (DirectDrawCreateEx (lpGUID, (LPVOID*) &lpDD, IID_IDirectDraw7, nullptr) == DD_OK)
    {
        mmGraphicsInterface *gfxInterface = &gfxInterfaces[gfxInterfaceCount];

        strcpy (gfxInterface->Name, lpDriverDescription);

        gfxInterface->DeviceCaps = 1;
        gfxInterface->AcceptableDepths = mmGraphicsInterface::Depth32;

        DDDEVICEIDENTIFIER2 ddDeviceIdentifier = { NULL };

        if (lpDD->GetDeviceIdentifier(&ddDeviceIdentifier, 0) == DD_OK)
        {
            gfxInterface->VendorID  = ddDeviceIdentifier.dwVendorId;
            gfxInterface->DeviceID  = ddDeviceIdentifier.dwDeviceId;
            gfxInterface->GUID      = ddDeviceIdentifier.guidDeviceIdentifier;
        }

        if (lpDD->QueryInterface(IID_IDirect3D7, (LPVOID*) &lpD3D) == DD_OK)
        {
            lpD3D->EnumDevices(lpDeviceCallback, gfxInterface);
            lpD3D->Release();
            *lpD3D = nullptr;
        }

        gfxInterface->Renderer          = mmGraphicsInterface::HardwareWithTnL;
        gfxInterface->ResolutionCount   = 0;
        gfxInterface->ResolutionChoice  = 0;
        gfxInterface->AvailableMemory   = 0x40000000; // 1GB = 1024 * 1024 * 1024

        *gfxMaxScreenWidth = 0;
        *gfxMaxScreenHeight = 0;

        lpDD->EnumDisplayModes(0, 0, gfxInterface, lpResCallback);
        lpDD->Release();
        *lpDD = nullptr;

        ++*gfxInterfaceCount;
    }

    return TRUE;
}

class gfxHandler
{
private:
    static void InstallAutoDetectPatch ()
    {
        InstallGameCallback("AutoDetectCallback", gameVersion, &AutoDetectCallback, HOOK_JMP,
        {
            { NULL, NULL, 0x4AC030 },
        });
    };
public:
    static bool gfxAutoDetect(bool *success)
    {
        if (MM2::datArgParser::Get("noautodetect"))
        {
            LogFile::WriteLine("Hooking AutoDetect");
            InstallAutoDetectPatch();
        }

        return $gfxAutoDetect(success);
    };

    static void setRes(int width, int height, int cdepth, int zdepth, bool detectArgs)
    {
        LogFile::WriteLine("Additional graphics params enabled");

        $setRes(width, height, cdepth, zdepth, true);
    }
};

class CallbackHandler {
public:
    static void CreateGameMutex(LPCSTR lpName) {
        if (MM2::datArgParser::Get("nomutex")) {
            LogFile::WriteLine("Game mutex disabled.");
            return;
        }

        $CreateGameMutex(lpName);
    };

    bool LoadAmbientSFX(LPCSTR name) {
        LPCSTR szAmbientSFX = NULL;

        auto city = (LPCSTR)MM2::szCityName;

        if ((_strcmpi(city, "sf") == 0) && (_strcmpi(city, "london") == 0))
        {
            char ambientSFX[80] = { NULL };

            sprintf(ambientSFX, "%sambience", city);

            bool exists = !(MM2::datAssetManager::Exists("aud\\dmusic\\csv_files", ambientSFX, "csv"));

            // default to 'sfambience' instead of 'londonambience'
            szAmbientSFX = (exists) ? ambientSFX : "sfambience";
        }
        else
        {
            szAmbientSFX = name;
        }

        LogFile::Format("AmbientSFX: %s\n", szAmbientSFX);

        // pass to MM2
        return ((MM2::mmGameMusicData*)this)->LoadAmbientSFX(szAmbientSFX);
    };

    static void SetSirenCSVName(LPCSTR name) {
        char siren_name[80] = { NULL };

        sprintf(siren_name, "%spolicesiren", (LPCSTR)MM2::szCityName);

        bool useDefault = !(MM2::datAssetManager::Exists("aud\\cardata\\player", siren_name, "csv"));

        LPCSTR szSirenName = (useDefault) ? name : siren_name;

        LogFile::Format("SirenCSVName: %s\n", szSirenName);

        // pass to MM2
        MM2::vehCarAudioContainer::SetSirenCSVName(szSirenName);
    };

    static void ageDebug(int enabled, LPCSTR format, ...) {
        // this makes the game load up reeeeeally slow if enabled!
        if (ageDebugEnabled || (ageDebugEnabled = MM2::datArgParser::Get("age_debug")))
        {
            va_list va;
            va_start(va, format);
            vsprintf(ageDebug_buffer, format, va);
            va_end(va);

            // 'LogFile::WriteLine' seems to be causing crashes upon exiting?
            if (ageLog == NULL)
            {
                FILE *logFile = fopen(ageLogFile, "w");
                
                fputs("--<< A.G.E. Log file >>--\n\n", logFile);
                fclose(logFile);
            }

            ageLog = fopen(ageLogFile, "a+");

            fputs(ageDebug_buffer, ageLog);
            fputs("\n", ageLog);

            fclose(ageLog);
        }
    };

    static LPCSTR AngelReadString(UINT stringId) {
        // currently this overrides the old AngelReadString function
        // and provides no backwards compat. for MMLANG.DLL
        auto str = "";
        L.getGlobal("GetLocaleString");
        L.push(stringId);
        if (L.pcall(1, 1, 0) == LUA_OK)
        {
            str = L.toString(-1);
        }
        else
        {
            LogFile::Format("[LUA] ERROR: %s\n", stringId, L.toString(-1));
            str = "???";
        }   
        L.pop(1);
        return str;
    }
};

class GraphicsCallbackHandler
{
private:
    static UINT32 CalculateShadedColor(int color) {
        auto timeWeather = &$TIMEWEATHER[$timeOfDay];

        vglKeyColor = addPitch(&timeWeather->KeyColor, timeWeather->KeyPitch);
        vglFill1Color = addPitch(&timeWeather->Fill1Color, timeWeather->Fill1Pitch);
        vglFill2Color = addPitch(&timeWeather->Fill2Color, timeWeather->Fill2Pitch);

        // convert the ambient to a vector3 for better accuracy
        vglAmbient = intToColor(timeWeather->Ambient);

        // compute le values
        vglShadedColor = {
            normalize((vglKeyColor.X + vglFill1Color.X + vglFill2Color.X) + vglAmbient.X),
            normalize((vglKeyColor.Y + vglFill1Color.Y + vglFill2Color.Y) + vglAmbient.Y),
            normalize((vglKeyColor.Z + vglFill1Color.Z + vglFill2Color.Z) + vglAmbient.Z),
        };

        vglResultColor.r = (char)(vglShadedColor.X * 255.999);
        vglResultColor.g = (char)(vglShadedColor.Y * 255.999);
        vglResultColor.b = (char)(vglShadedColor.Z * 255.999);

        return $sdlPage16_GetShadedColor(color, *(int*)&vglResultColor);
    }
public:
    static void vglBegin(int gfxMode, int p1) {
        vglColor = $vglCurrentColor;

        // calculate a nice shaded color ;)
        vglCalculatedColor = CalculateShadedColor(vglColor);
        $vglCurrentColor.set(vglCalculatedColor);

        $vglBegin(gfxMode, p1);
    }

    static void vglEnd(void) {
        // restore color
        $vglCurrentColor.set(vglColor);
        $vglEnd();
    }
};

class BridgeFerryCallbackHandler
{
public:
    void Cull(int lod) {
        // wtf
        //setPtr(this, 0x1B, (char)++lod);
    }

    void Draw(int lod) {
        $dgBangerInstance_Draw(this, lod);
    }
};

class mmDashViewCallbackHandler
{
public:
    void UpdateCS() {
        auto dashCam = getPtr<MM2::Matrix34>(this, 0x18);

        // apply an offset (mainly doing this from CheatEngine, etc.)
        dashCam->m11 += sm_DashOffset.m11;
        dashCam->m12 += sm_DashOffset.m12;
        dashCam->m13 += sm_DashOffset.m13;
        dashCam->m14 += sm_DashOffset.m14;

        dashCam->m21 += sm_DashOffset.m21;
        dashCam->m22 += sm_DashOffset.m22;
        dashCam->m23 += sm_DashOffset.m23;
        dashCam->m24 += sm_DashOffset.m24;

        dashCam->m31 += sm_DashOffset.m31;
        dashCam->m32 += sm_DashOffset.m32;
        dashCam->m33 += sm_DashOffset.m33;
        dashCam->m34 += sm_DashOffset.m34;

        $asLinearCS_Update(this);
    };
};

class memSafeHeapCallbackHandler
{
public:
    void Init(void *memAllocator, unsigned int heapSize, bool p3, bool p4, bool checkAlloc) {
        MM2::datArgParser::Get("heapsize", 0, &g_heapSize);

        // fast way of expanding to the proper size
        // same as ((g_heapSize * 1024) * 1024)
        heapSize = (g_heapSize << 20);

        LogFile::Format("memSafeHeap::Init -- Allocating %dMB heap (%d bytes)\n", g_heapSize, heapSize);
        return $memSafeHeap_Init(this, memAllocator, heapSize, p3, p4, checkAlloc);
    };
};

/*
    ===========================================================================
*/

// ==========================
// Patch definitions
// ==========================

const PATCH_INSTALL_INFO<1, 3> chatSize_patch = {
    { 60 }, {
        { NULL, NULL, 0x4E68B5 },
        { NULL, NULL, 0x4E68B9 },
        { NULL, NULL, 0x50BBCF }
    }
};

const PATCH_INSTALL_INFO<4, 1> windowStyle_patch =
{
    { 0x00, 0x00, 0xCA, 0x80 },
    {
        { NULL, NULL, 0x4A8BD1 }
    }
};

const PATCH_INSTALL_INFO<1, 1> copLimit_patch =
{
    { 0x40 },
    {
        { NULL, NULL, 0x55100B }
    }
};

// ==========================
// Callback hook definitions
// ==========================

// Replaces a call to ArchInit (a null function) just before ExceptMain
// This is PERFECT for initializing everything before the game even starts!

// ==========================
// VTable hook definitions
// ==========================

const VT_INSTALL_INFO<2> bridgeFerryDraw_VT = {
    &BridgeFerryCallbackHandler::Draw, {
        { NULL, NULL, 0x5B5FB8 }, // gizBridge::Draw
        { NULL, NULL, 0x5B61AC } // gizFerry::Draw
    }
};

/*
    ===========================================================================
*/
bool InitializeFramework(MM2Version gameVersion) {
    LogFile::Write("Hooking into the framework...");

    if (gameVersion == MM2_INVALID || gameVersion > MM2_NUM_VERSIONS)
    {
        LogFile::WriteLine("FAIL!");
        return false;
    }

    $__VtResumeSampling.set((DWORD)&HookSystemHandler::Start);
    $__VtPauseSampling.set((DWORD)&HookSystemHandler::Stop);

    LogFile::WriteLine("Done!");
    return true;
};

void InstallPatches(MM2Version gameVersion) {
    LogFile::WriteLine("Installing patches...");

    InstallGamePatch("Increase chat buffer size", gameVersion, chatSize_patch);

    InstallGamePatch ("Change window style", gameVersion, windowStyle_patch);
    InstallGamePatch ("Increase cop limit", gameVersion, copLimit_patch);
};

void InstallCallbacks(MM2Version gameVersion) {
    LogFile::WriteLine("Installing callbacks / virtual tables...");

    switch (gameVersion)
    {
        case MM2_BETA_1:
        case MM2_BETA_2:
        {
            // Disables time check on betas
            InstallGameCallback("TrialTimeExpired", gameVersion, &ReturnNullOrZero, HOOK_CALL,
            {
                { 0x4011B0, 0x4012AC, NULL }
            });
        } break;
        case MM2_RETAIL:
        {
            // mutex was introduced in retail
            InstallGameCallback("CreateGameMutex", gameVersion, &CallbackHandler::CreateGameMutex, HOOK_CALL,
            {
                { NULL, NULL, 0x40128D },
            });
            
            // revert bridges/ferries to how they were in the betas
            InstallGameCallback("Bridge/Ferry: Cull", gameVersion, &BridgeFerryCallbackHandler::Cull, HOOK_CALL,
            {
                { NULL, NULL, 0x5780BC },   // gizBridgeMgr::Cull
                { NULL, NULL, 0x5798F0 }    // gizFerryMgr::Cull
            });

            InstallVTableHook("Bridge/Ferry: Draw", gameVersion, bridgeFerryDraw_VT);
        } break;
    }

    InstallGameCallback("ArchInit", gameVersion, &HookSystemHandler::Initialize, HOOK_CALL,
    {         
        { NULL, NULL, 0x4023DB }
    });

    InstallGameCallback("ageDebug", gameVersion, &CallbackHandler::ageDebug, HOOK_JMP,
    {
        { NULL, NULL, 0x402630 }
    });

    InstallGameCallback("gfxAutoDetect", gameVersion, &gfxHandler::gfxAutoDetect, HOOK_CALL,
    {
        { NULL, NULL, 0x401440 }
    });

    InstallGameCallback("setRes", gameVersion, &gfxHandler::setRes, HOOK_CALL,
    {
        { NULL, NULL, 0x401482 }
    });
    
    InstallGameCallback("memSafeHeap::Init [Heap fix]", gameVersion, &memSafeHeapCallbackHandler::Init, HOOK_CALL,
    {
        { NULL, NULL, 0x4015DD }
    });

    //// not supported for betas yet
    //if (gameVersion == MM2_RETAIL)
    //{
    //    // NOTE: Completely overrides AngelReadString!
    //    InstallGameCallback("AngelReadString", gameVersion, &CallbackHandler::AngelReadString, HOOK_JMP,
    //    {
    //        { NULL, NULL, 0x534790 },
    //    });
    //}

    InstallGameCallback("vglBegin", gameVersion, &GraphicsCallbackHandler::vglBegin, HOOK_CALL,
    {
        { NULL, NULL, 0x448424 }, { NULL, NULL, 0x448697 }, { NULL, NULL, 0x448903 }, { NULL, NULL, 0x448BFD },
        { NULL, NULL, 0x448DE4 }, { NULL, NULL, 0x44902A }, { NULL, NULL, 0x4492A4 }, { NULL, NULL, 0x4494C3 },
        { NULL, NULL, 0x4496A5 }, { NULL, NULL, 0x44986B }, { NULL, NULL, 0x449A13 }, { NULL, NULL, 0x449BD9 },
        { NULL, NULL, 0x449D82 }, { NULL, NULL, 0x449F67 }, { NULL, NULL, 0x44A21C }, { NULL, NULL, 0x44A444 },
        { NULL, NULL, 0x44A629 }, { NULL, NULL, 0x44A7EF }, { NULL, NULL, 0x44A997 }, { NULL, NULL, 0x44AB5D },
        { NULL, NULL, 0x44AD06 }, { NULL, NULL, 0x44AECA }, { NULL, NULL, 0x44B0EC }, { NULL, NULL, 0x44B24B },
        { NULL, NULL, 0x44B3B6 }, { NULL, NULL, 0x44B557 }, { NULL, NULL, 0x44B6F3 }, { NULL, NULL, 0x44B8F1 },
        { NULL, NULL, 0x44BA8A }, { NULL, NULL, 0x44BC29 }, { NULL, NULL, 0x44BE9C }, { NULL, NULL, 0x44C136 },
        { NULL, NULL, 0x44C40C }, { NULL, NULL, 0x44C64A }, { NULL, NULL, 0x44C7C0 }, { NULL, NULL, 0x44CAD6 },
        { NULL, NULL, 0x44CCF5 }, { NULL, NULL, 0x44CF6D }, { NULL, NULL, 0x44D0D4 }, { NULL, NULL, 0x44D5F7 },
        { NULL, NULL, 0x44D789 }, { NULL, NULL, 0x44DC55 }, { NULL, NULL, 0x44E050 }, { NULL, NULL, 0x44E14B },
        { NULL, NULL, 0x44E2A3 }, { NULL, NULL, 0x44E69D }, { NULL, NULL, 0x44E79E }, { NULL, NULL, 0x44EAA0 },
        { NULL, NULL, 0x44EBA5 }, { NULL, NULL, 0x44EFD0 }, { NULL, NULL, 0x44F0DC }, { NULL, NULL, 0x44F588 },
        { NULL, NULL, 0x44F7E2 }, { NULL, NULL, 0x44FC1E }, { NULL, NULL, 0x44FDD4 }, { NULL, NULL, 0x44FF10 },
        { NULL, NULL, 0x450085 }, { NULL, NULL, 0x450269 },
        // ------------------------------------
        { NULL, NULL, 0x443B9D }, // dgRoadDecalInstance
        { NULL, NULL, 0x57AC4A }, // ped LODs
    });

    InstallGameCallback("vglEnd", gameVersion, &GraphicsCallbackHandler::vglEnd, HOOK_CALL,
    {
        { NULL, NULL, 0x4485D3 }, { NULL, NULL, 0x448B82 }, { NULL, NULL, 0x448D8C }, { NULL, NULL, 0x448FB7 },
        { NULL, NULL, 0x449219 }, { NULL, NULL, 0x449480 }, { NULL, NULL, 0x44963E }, { NULL, NULL, 0x44983C },
        { NULL, NULL, 0x4499D4 }, { NULL, NULL, 0x449BAA }, { NULL, NULL, 0x449D42 }, { NULL, NULL, 0x449F5A },
        { NULL, NULL, 0x44A146 }, { NULL, NULL, 0x44A3F8 }, { NULL, NULL, 0x44A5BF }, { NULL, NULL, 0x44A7C0 },
        { NULL, NULL, 0x44A958 }, { NULL, NULL, 0x44AB2E }, { NULL, NULL, 0x44ACC6 }, { NULL, NULL, 0x44AEBC },
        { NULL, NULL, 0x44B083 }, { NULL, NULL, 0x44B23D }, { NULL, NULL, 0x44B394 }, { NULL, NULL, 0x44B531 },
        { NULL, NULL, 0x44B6E1 }, { NULL, NULL, 0x44B895 }, { NULL, NULL, 0x44BA7C }, { NULL, NULL, 0x44BC03 },
        { NULL, NULL, 0x44BE8E }, { NULL, NULL, 0x44C118 }, { NULL, NULL, 0x44C3EA }, { NULL, NULL, 0x44C638 },
        { NULL, NULL, 0x44C77A }, { NULL, NULL, 0x44C989 }, { NULL, NULL, 0x44CC44 }, { NULL, NULL, 0x44CE63 },
        { NULL, NULL, 0x44D04E }, { NULL, NULL, 0x44D403 }, { NULL, NULL, 0x44D780 }, { NULL, NULL, 0x44D8E9 },
        { NULL, NULL, 0x44E014 }, { NULL, NULL, 0x44E131 }, { NULL, NULL, 0x44E22C }, { NULL, NULL, 0x44E661 },
        { NULL, NULL, 0x44E785 }, { NULL, NULL, 0x44E886 }, { NULL, NULL, 0x44EB82 }, { NULL, NULL, 0x44EDC3 },
        { NULL, NULL, 0x44F0B9 }, { NULL, NULL, 0x44F316 }, { NULL, NULL, 0x44F64C }, { NULL, NULL, 0x44FB9D },
        { NULL, NULL, 0x44FD30 }, { NULL, NULL, 0x44FE4E }, { NULL, NULL, 0x44FFB3 }, { NULL, NULL, 0x450162 },
        { NULL, NULL, 0x450390 }, { NULL, NULL, 0x45078C },
        // ------------------------------------
        { NULL, NULL, 0x443DCC }, // road decals
        { NULL, NULL, 0x57AD41 }, // ped LODs
    });

    InstallGameCallback("datTimeManager::Update", gameVersion, &TickHandler::Update, HOOK_CALL,
    {
        { NULL, NULL, 0x401A2F },
    });

    InstallGameCallback("mmGame::SendChatMessage", gameVersion, &ChatHandler::Process, HOOK_CALL,
    {
        { NULL, NULL, 0x414EB6 },
    });

    InstallGameCallback("mmGameMusicData::LoadAmbientSFX", gameVersion, &CallbackHandler::LoadAmbientSFX, HOOK_CALL,
    {
        { NULL, NULL, 0x433F93 },
    });

    InstallGameCallback("vehCarAudioContainer::SetSirenCSVName", gameVersion, &CallbackHandler::SetSirenCSVName, HOOK_CALL,
    {
        { NULL, NULL, 0x412783 },
        { NULL, NULL, 0x412772 },
    });

    // dashboard testing
    InstallGameCallback("mmDashView::Update [EXPERIMENTAL]", gameVersion, &mmDashViewCallbackHandler::UpdateCS, HOOK_CALL,
    {
        { NULL, NULL, 0x430F87 }, // replaces call to asLinearCS::Update
    });
};

//
// Initialize all the important stuff prior to MM2 starting up
//
void Initialize(ageInfoLookup &gameInfo) {
    // initialize game manager
    pMM2 = new CMidtownMadness2(gameInfo.info);
    pMM2->Initialize();

    // TODO: Remove dependency on 'gameVersion'?
    gameVersion = pMM2->GetVersion();

    if (InitializeFramework(gameVersion)) {
        InstallCallbacks(gameVersion);
        InstallPatches(gameVersion);
    } else {
        MessageBox(NULL, "MM2Hook was unable to initialize properly. The game will proceed normally.", "MM2Hook", MB_OK);
    }
}

bool IsGameSupported(ageInfoLookup &gameInfo) {
    LogFile::WriteLine("Checking for known MM2 versions...");

    if (CMidtownMadness2::GetGameInfo(gameInfo))
    {
        LogFile::Format(" - Detected game version %d\n", gameInfo.info.engineVersion);
        return gameInfo.isSupported;
    }
    else
    {
        LogFile::WriteLine("Unknown module detected! Terminating...");
        MessageBox(NULL, "Unknown module! MM2Hook will now terminate the process.", "MM2Hook", MB_OK | MB_ICONERROR);

        ExitProcess(EXIT_FAILURE);
    }

    return false;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
        case DLL_PROCESS_ATTACH:
        {
            debug("Initializing MM2Hook...");

            // Initialize the log file
            LogFile::Initialize("mm2hook.log", "--<< MM2Hook log file >>--\n");
            LogFile::WriteLine("Initializing...");

            HMODULE hDIModule = NULL;
            ageInfoLookup gameInfo;

            if (IsGameSupported(gameInfo))
            {
                if (LoadSystemLibrary("dinput.dll", &hDIModule) &&
                    GetHookProcAddress(hDIModule, "DirectInputCreateA", (FARPROC*)&lpDICreate))
                {
                    LogFile::WriteLine("Injected into the game process successfully.");

                    // initialize the hook
                    Initialize(gameInfo);
                }
                else
                {
                    LogFile::WriteLine("Failed to inject into the game process.");
                    MessageBox(NULL, "Could not inject into the game process. Unknown errors may occur.", "MM2Hook", MB_OK | MB_ICONWARNING);
                }
            }
            else
            {
                LogFile::WriteLine("Unsupported game version -- terminating...");

                if (gameInfo.info.gameVersion == MM2_BETA_2_PETITE)
                {
                    MessageBox(NULL, "Sorry, this version of Beta 2 was compressed with PeTite -- you'll need an unpacked version.\n\nOtherwise, please remove MM2Hook to launch the game.", "MM2Hook", MB_OK | MB_ICONERROR);
                } else
                {
                    MessageBox(NULL, "Sorry, this version of MM2 is unsupported. Please remove MM2Hook to launch the game.", "MM2Hook", MB_OK | MB_ICONERROR);
                }

                ExitProcess(EXIT_FAILURE);
            }
        } break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
        break;
	}
	return TRUE;
}

