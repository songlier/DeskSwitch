#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600 // Windows Vista+
#endif

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <vector>
#include <string>
#include <fstream>
#include <map>
#include <tlhelp32.h>

#ifdef _MSC_VER
#pragma comment(lib, "USER32")
#pragma comment(lib, "winmm.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#endif

// Function pointer definitions
typedef int (*GetDesktopCountProc)();
typedef int (*GetCurrentDesktopNumberProc)();
typedef void (*GoToDesktopNumberProc)(int number);
typedef void (*MoveWindowToDesktopNumberProc)(HWND window, int number);

// Function pointers loaded from DLL
static GetDesktopCountProc g_pGetDesktopCount = NULL;
static GetCurrentDesktopNumberProc g_pGetCurrentDesktopNumber = NULL;
static GoToDesktopNumberProc g_pGoToDesktopNumber = NULL;
static MoveWindowToDesktopNumberProc g_pMoveWindowToDesktopNumber = NULL;
static HMODULE g_hVdaDll = NULL;
#define KEYDOWN(k) ((k) & 0x80)

#define HOT_AREA_VALUE_ABS 0
#define HOT_AREA_VALUE_PERCENT 1

typedef struct HotAreaValue
{
    int type;
    double percent;
    long offsetPx;
    long absValue;
} HotAreaValue;

typedef struct HotAreaSpec
{
    HotAreaValue left;
    HotAreaValue top;
    HotAreaValue right;
    HotAreaValue bottom;
} HotAreaSpec;

static HotAreaSpec kHotCorner_in_topleft;
static HotAreaSpec kHotCorner_in_topright;
static HotAreaSpec kHotCorner_out_topleft;
static HotAreaSpec kHotCorner_out_topright;
static const DWORD kHotKeyModifiers = MOD_CONTROL | MOD_ALT;
static const DWORD kHotKey = VK_ESCAPE;
static const DWORD kExitHotKeyModifiers = MOD_CONTROL | MOD_SHIFT | MOD_ALT;
static const DWORD kExitHotKey = VK_ESCAPE;

// ==================
// --- [ conf ]
// ==================

// 单双角模式 [0:双角(默认), 1:单角]
static DWORD kCornerMode = 0;
// 单角方向记忆超时 [默认:1000]
static DWORD kSwitchTimeout = 1000;
// 连续切换 [0:关闭(默认), 1:开启]
static DWORD kKeepSwitch = 0;
// 连续切换时间间隔 [默认:560]
static DWORD kRepeatInterval = 560;
// 自动反向切换 [0:关闭(默认), 1:开启]
static DWORD kAutoSwitch = 0;
// 离开时触发 [0:关闭, 1:左上角, 2:右上角, 3:全部(默认)]
static DWORD kCloseProtect = 3;
// 窗口快捷移动 [0:关闭(默认), 1:左键, 2:右键, 3:开启]
static DWORD kAltShiftClickMode = 0;
// 自动屏蔽右上角 [0:不屏蔽(默认), 1:屏蔽]
static DWORD kDisableTopRightWhenDesktopLE2 = 0;
// 动画切换的按键等待时间 [默认:10]
static DWORD kKeystrokeSleep = 10;
// 新建桌面等待时间 [默认:50]
static DWORD kNewDesktopWait = 50;
// 重置状态计时 [默认:50]
static DWORD kLeaveCornerPollInterval = 50;
// FancyZones 等待响应时间 [默认:50]
static DWORD Fancyzonessleep = 50;
// 异常重试等待时间 [默认:100]
static DWORD kWorkerRetrySleep = 100;
// 提示音间隔 [默认:200]
static DWORD kBeepInterval = 200;
// 退出程序延迟 [默认:1000]
static DWORD kExitDelay = 1000;
// ==================

// Global variables
static CRITICAL_SECTION g_cs;
static HANDLE g_hCornerWorkerThread = INVALID_HANDLE_VALUE;
static HANDLE g_hHotCornerEvent = NULL;
static volatile BOOL g_bAppIsExiting = FALSE;
static BOOL g_bJustCreatedDesktop = FALSE;
static volatile LONG g_lRightClickCountDuringDrag = 0;
static volatile LONG g_lFancyZonesShouldRun = 0;
static volatile BOOL g_bIsPaused = FALSE;
static BOOL g_bSingleCornerMode = FALSE;
static BOOL g_bAutoSwitch = TRUE;
static BOOL g_bKeepSwitch = TRUE;
static DWORD g_dwCloseProtectMode = 0;
static DWORD g_dwLastSwitchTime = 0;
static int g_nLastDesktopNumber = -1;
static LONG g_lastdesktop = 0;
static int g_numChange = 0;
static BOOL g_firstchange = TRUE;
static volatile BOOL g_bCloseProtectArmed = FALSE;
static volatile DWORD g_dwCloseProtectInitialButtons = 0;
static volatile BOOL g_bCloseProtectButtonStateChanged = FALSE;
static volatile LONG g_lForcedTriggeredCorner = 0;
static volatile LONG g_lCloseProtectCorner = 0;
static std::vector<std::wstring> g_vSpecialProcesses;
static BOOL g_bSuppressRightClickUp = FALSE;
static BOOL g_bSuppressLeftClickUp = FALSE;
static BOOL g_bDisableTopRightWhenDesktopLE2 = TRUE;
static volatile int g_iTriggeredCorner = 0;
static volatile LONG g_lAreaScreenWidth = 0;
static volatile LONG g_lAreaScreenHeight = 0;

std::wstring GetExePath()
{
    TCHAR buffer[MAX_PATH];
    GetModuleFileName(NULL, buffer, MAX_PATH);
    std::wstring::size_type pos = std::wstring(buffer).find_last_of(L"\\/");
    return std::wstring(buffer).substr(0, pos);
}

std::wstring GetPauseFlagPath()
{
    return GetExePath() + L"\\pause";
}

void LoadPauseStateByFile()
{
    g_bIsPaused = (GetFileAttributesW(GetPauseFlagPath().c_str()) != INVALID_FILE_ATTRIBUTES);
}

void SavePauseStateByFile(BOOL isPaused)
{
    std::wstring path = GetPauseFlagPath();
    if (isPaused)
    {
        HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE)
            CloseHandle(hFile);
    }
    else
    {
        DeleteFileW(path.c_str());
    }
}

static DWORD GetMouseButtonMask()
{
    DWORD mask = 0;
    if (GetKeyState(VK_LBUTTON) < 0)
        mask |= 0x1;
    if (GetKeyState(VK_RBUTTON) < 0)
        mask |= 0x2;
    return mask;
}

static void UpdateCloseProtectButtonStateChanged()
{
    if (g_bCloseProtectArmed)
    {
        DWORD currentMask = GetMouseButtonMask();
        if (currentMask != g_dwCloseProtectInitialButtons)
        {
            g_bCloseProtectButtonStateChanged = TRUE;
        }
    }
}

static HotAreaValue MakeAreaAbs(long v)
{
    HotAreaValue value;
    value.type = HOT_AREA_VALUE_ABS;
    value.percent = 0.0;
    value.offsetPx = 0;
    value.absValue = v;
    return value;
}

static HotAreaValue MakeAreaPercent(double percent, long offsetPx)
{
    HotAreaValue value;
    value.type = HOT_AREA_VALUE_PERCENT;
    value.percent = percent;
    value.offsetPx = offsetPx;
    value.absValue = 0;
    return value;
}

static long ResolveAreaValue(const HotAreaValue &value, int total)
{
    if (value.type == HOT_AREA_VALUE_PERCENT)
    {
        return (long)(total * (value.percent / 100.0) + value.offsetPx);
    }
    return value.absValue;
}

static void RefreshAreaScreenSize()
{
    MONITORINFO mi = {sizeof(mi)};
    if (GetMonitorInfo(MonitorFromWindow(NULL, MONITOR_DEFAULTTOPRIMARY), &mi))
    {
        InterlockedExchange(&g_lAreaScreenWidth, mi.rcMonitor.right - mi.rcMonitor.left);
        InterlockedExchange(&g_lAreaScreenHeight, mi.rcMonitor.bottom - mi.rcMonitor.top);
    }
}

static void GetCachedAreaScreenSize(int *screenWidth, int *screenHeight)
{
    LONG width = InterlockedCompareExchange(&g_lAreaScreenWidth, 0, 0);
    LONG height = InterlockedCompareExchange(&g_lAreaScreenHeight, 0, 0);
    if (width <= 0 || height <= 0)
    {
        RefreshAreaScreenSize();
        width = InterlockedCompareExchange(&g_lAreaScreenWidth, 0, 0);
        height = InterlockedCompareExchange(&g_lAreaScreenHeight, 0, 0);
    }
    *screenWidth = (int)width;
    *screenHeight = (int)height;
}

static RECT ResolveHotAreaRect(const HotAreaSpec &spec)
{
    int screenWidth = 0;
    int screenHeight = 0;
    GetCachedAreaScreenSize(&screenWidth, &screenHeight);

    RECT rc;
    rc.left = ResolveAreaValue(spec.left, screenWidth);
    rc.top = ResolveAreaValue(spec.top, screenHeight);
    rc.right = ResolveAreaValue(spec.right, screenWidth);
    rc.bottom = ResolveAreaValue(spec.bottom, screenHeight);
    return rc;
}

static BOOL PtInHotArea(const HotAreaSpec &spec, POINT pt)
{
    RECT rc = ResolveHotAreaRect(spec);
    return PtInRect(&rc, pt);
}

static std::string TrimAreaString(std::string s)
{
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

static HotAreaValue ParseAreaValue(std::string s)
{
    s = TrimAreaString(s);

    std::string compact;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] != ' ' && s[i] != '\t' && s[i] != '\r' && s[i] != '\n')
        {
            compact.push_back(s[i]);
        }
    }

    size_t percentPos = compact.find('%');
    if (percentPos != std::string::npos)
    {
        double percent = atof(compact.substr(0, percentPos).c_str());
        long offsetPx = 0;

        std::string suffix = compact.substr(percentPos + 1);
        if (!suffix.empty() && (suffix[0] == '+' || suffix[0] == '-'))
        {
            offsetPx = strtol(suffix.c_str(), NULL, 10);
        }

        return MakeAreaPercent(percent, offsetPx);
    }

    return MakeAreaAbs((long)atoi(compact.c_str()));
}

static BOOL ParseHotAreaSpec(const std::string &vals, HotAreaSpec *outSpec)
{
    std::vector<std::string> parts;
    size_t last = 0, next = 0;
    while ((next = vals.find(',', last)) != std::string::npos)
    {
        parts.push_back(vals.substr(last, next - last));
        last = next + 1;
    }
    parts.push_back(vals.substr(last));

    if (parts.size() != 4)
    {
        return FALSE;
    }

    outSpec->left = ParseAreaValue(parts[0]);
    outSpec->top = ParseAreaValue(parts[1]);
    outSpec->right = ParseAreaValue(parts[2]);
    outSpec->bottom = ParseAreaValue(parts[3]);
    return TRUE;
}

static void SetDefaultAreaConfig()
{
    kHotCorner_in_topleft.left = MakeAreaAbs(0);
    kHotCorner_in_topleft.top = MakeAreaAbs(0);
    kHotCorner_in_topleft.right = MakeAreaAbs(4);
    kHotCorner_in_topleft.bottom = MakeAreaAbs(4);

    kHotCorner_in_topright.left = MakeAreaPercent(100.0, -4);
    kHotCorner_in_topright.top = MakeAreaAbs(0);
    kHotCorner_in_topright.right = MakeAreaPercent(100.0, 0);
    kHotCorner_in_topright.bottom = MakeAreaAbs(4);

    kHotCorner_out_topleft = kHotCorner_in_topleft;
    kHotCorner_out_topright = kHotCorner_in_topright;
}

void LoadAreaConfig()
{
    std::wstring path = GetExePath() + L"\\area.txt";
    std::ifstream file(path.c_str());
    SetDefaultAreaConfig();
    RefreshAreaScreenSize();
    if (!file.is_open())
        return;

    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line.find('{') == std::string::npos)
            continue;
        size_t start = line.find('{');
        size_t end = line.find('}');
        if (end == std::string::npos || end <= start)
            continue;
        size_t eq = line.find('=');
        std::string key;
        if (eq != std::string::npos && eq < start)
            key = TrimAreaString(line.substr(0, eq));
        else
            key = TrimAreaString(line.substr(0, start));
        std::string vals = line.substr(start + 1, end - start - 1);
        HotAreaSpec temp;
        if (ParseHotAreaSpec(vals, &temp))
        {
            if (key == "in_topleft")
                kHotCorner_in_topleft = temp;
            else if (key == "in_topright")
                kHotCorner_in_topright = temp;
            else if (key == "out_topleft")
                kHotCorner_out_topleft = temp;
            else if (key == "out_topright")
                kHotCorner_out_topright = temp;
            else if (key == "topleft")
            {
                kHotCorner_in_topleft = temp;
                kHotCorner_out_topleft = temp;
            }
            else if (key == "topright")
            {
                kHotCorner_in_topright = temp;
                kHotCorner_out_topright = temp;
            }
        }
    }
    file.close();
}

void LoadSpeedConfig()
{
    std::wstring path = GetExePath() + L"\\conf.txt";
    std::ifstream file(path.c_str());
    if (!file.is_open())
    {
        return;
    }
    std::map<std::string, DWORD *> configMap;
    configMap["// 单双角模式 [0:双角(默认), 1:单角]"] = &kCornerMode;
    configMap["// 单角方向记忆超时 [默认:1000]"] = &kSwitchTimeout;
    configMap["// 连续切换 [0:关闭(默认), 1:开启]"] = &kKeepSwitch;
    configMap["// 连续切换时间间隔 [默认:560]"] = &kRepeatInterval;
    configMap["// 自动反向切换 [0:关闭(默认), 1:开启]"] = &kAutoSwitch;
    configMap["// 离开时触发 [0:关闭, 1:左上角, 2:右上角, 3:全部(默认)]"] = &kCloseProtect;
    configMap["// 窗口快捷移动 [0:关闭(默认), 1:左键, 2:右键, 3:开启]"] = &kAltShiftClickMode;
    configMap["// 自动屏蔽右上角 [0:不屏蔽(默认), 1:屏蔽]"] = &kDisableTopRightWhenDesktopLE2;
    configMap["// 动画切换的按键等待时间 [默认:10]"] = &kKeystrokeSleep;
    configMap["// 新建桌面等待时间 [默认:50]"] = &kNewDesktopWait;
    configMap["// 重置状态计时 [默认:50]"] = &kLeaveCornerPollInterval;
    configMap["// FancyZones 等待响应时间 [默认:50]"] = &Fancyzonessleep;
    configMap["// 异常重试等待时间 [默认:100]"] = &kWorkerRetrySleep;
    configMap["// 提示音间隔 [默认:200]"] = &kBeepInterval;
    configMap["// 退出程序延迟 [默认:1000]"] = &kExitDelay;
    std::string line;
    while (std::getline(file, line))
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
        {
            line.pop_back();
        }
        auto it = configMap.find(line);
        if (it != configMap.end())
        {
            std::string valueLine;
            if (std::getline(file, valueLine))
            {
                try
                {
                    *(it->second) = std::stoul(valueLine);
                }
                catch(...)
                {
                }
            }
        }
    }
    file.close();
    if (kAltShiftClickMode > 3)
    {
        kAltShiftClickMode = 3;
    }
    g_bSingleCornerMode = (kCornerMode != 0);
    g_bAutoSwitch = (kAutoSwitch != 0);
    g_bKeepSwitch = (kKeepSwitch != 0);
    if (g_bKeepSwitch)
    {
        kCloseProtect = 0;
        g_dwCloseProtectMode = 0;
    }
    else
    {
        g_dwCloseProtectMode = kCloseProtect;
        if (g_dwCloseProtectMode > 3)
        {
            g_dwCloseProtectMode = 0;
        }
    }
    g_bDisableTopRightWhenDesktopLE2 = (kDisableTopRightWhenDesktopLE2 != 0);
}

void LoadSpecialProcesses(const TCHAR *filePath)
{
    g_vSpecialProcesses.clear();
    std::wifstream file(filePath);
    if (file.is_open())
    {
        std::wstring line;
        while (std::getline(file, line))
        {
            if (!line.empty() && line.back() == L'\r')
            {
                line.pop_back();
            }
            if (!line.empty())
            {
                g_vSpecialProcesses.push_back(line);
            }
        }
        file.close();
    }
}

BOOL IsForegroundWindowFromSpecialProcess()
{
    if (g_vSpecialProcesses.empty())
    {
        return FALSE;
    }
    HWND hForegroundWnd = GetForegroundWindow();
    if (hForegroundWnd == NULL)
    {
        return FALSE;
    }
    DWORD pid;
    GetWindowThreadProcessId(hForegroundWnd, &pid);
    if (pid == 0)
    {
        return FALSE;
    }
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProcess == NULL)
    {
        return FALSE;
    }
    WCHAR buffer[MAX_PATH];
    DWORD size = MAX_PATH;
    BOOL foundMatch = FALSE;
    if (QueryFullProcessImageNameW(hProcess, 0, buffer, &size))
    {
        std::wstring fullPath(buffer);
        std::wstring exeName;
        size_t lastSlash = fullPath.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos)
        {
            exeName = fullPath.substr(lastSlash + 1);
        }
        else
        {
            exeName = fullPath;
        }
        for (const auto &specialName : g_vSpecialProcesses)
        {
            if (_wcsicmp(exeName.c_str(), specialName.c_str()) == 0)
            {
                foundMatch = TRUE;
                break;
            }
        }
    }
    CloseHandle(hProcess);
    return foundMatch;
}

void PlayWavOrBeep(const TCHAR *soundPath, int play_times)
{
    if (!PlaySound(soundPath, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT))
    {
        MessageBeep(MB_OK);
        if (play_times == 2)
        {
            Sleep(kBeepInterval);
            MessageBeep(MB_OK);
        }
    }
}

void LaunchFancyZones()
{
    const TCHAR *fancyZonesPath = TEXT("C:\\Program Files\\PowerToys\\PowerToys.FancyZones.exe");
    if (GetFileAttributes(fancyZonesPath) == INVALID_FILE_ATTRIBUTES)
    {
        return;
    }
    ShellExecute(NULL, TEXT("open"), fancyZonesPath, NULL, NULL, SW_SHOWNORMAL);
}

void AnimateDesktopSwitch(int currentDesktop, int targetDesktop)
{
    if (currentDesktop == targetDesktop)
        return;
    WORD directionKey = (targetDesktop > currentDesktop) ? VK_RIGHT : VK_LEFT;
    INPUT seq[6] = {0};
    seq[0].type = INPUT_KEYBOARD;
    seq[0].ki.wVk = VK_LCONTROL;
    seq[1].type = INPUT_KEYBOARD;
    seq[1].ki.wVk = VK_LWIN;
    seq[2].type = INPUT_KEYBOARD;
    seq[2].ki.wVk = directionKey;
    seq[3].type = INPUT_KEYBOARD;
    seq[3].ki.wVk = directionKey;
    seq[3].ki.dwFlags = KEYEVENTF_KEYUP;
    seq[4].type = INPUT_KEYBOARD;
    seq[4].ki.wVk = VK_LWIN;
    seq[4].ki.dwFlags = KEYEVENTF_KEYUP;
    seq[5].type = INPUT_KEYBOARD;
    seq[5].ki.wVk = VK_LCONTROL;
    seq[5].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput((sizeof(seq) / sizeof(seq[0])), seq, sizeof(INPUT));
    Sleep(kKeystrokeSleep);
}

void CreateNewDesktop()
{
    INPUT seq[6] = {0};
    seq[0].type = INPUT_KEYBOARD;
    seq[0].ki.wVk = VK_LCONTROL;
    seq[1].type = INPUT_KEYBOARD;
    seq[1].ki.wVk = VK_LWIN;
    seq[2].type = INPUT_KEYBOARD;
    seq[2].ki.wVk = 'D';
    seq[3].type = INPUT_KEYBOARD;
    seq[3].ki.wVk = 'D';
    seq[3].ki.dwFlags = KEYEVENTF_KEYUP;
    seq[4].type = INPUT_KEYBOARD;
    seq[4].ki.wVk = VK_LWIN;
    seq[4].ki.dwFlags = KEYEVENTF_KEYUP;
    seq[5].type = INPUT_KEYBOARD;
    seq[5].ki.wVk = VK_LCONTROL;
    seq[5].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput((sizeof(seq) / sizeof(seq[0])), seq, sizeof(INPUT));
}
typedef LONG(WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
DWORD GetWindowsBuildNumber()
{
    HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
    if (hMod)
    {
        RtlGetVersionPtr fxPtr = (RtlGetVersionPtr)GetProcAddress(hMod, "RtlGetVersion");
        if (fxPtr)
        {
            RTL_OSVERSIONINFOW rovi = {0};
            rovi.dwOSVersionInfoSize = sizeof(rovi);
            if (fxPtr(&rovi) == 0)
                return rovi.dwBuildNumber;
        }
    }
    return 0;
}
HWND GetRealWindowAtPoint(POINT pt)
{
    HWND hWnd = GetTopWindow(NULL);
    while (hWnd)
    {
        if (IsWindowVisible(hWnd))
        {
            RECT rc;
            GetWindowRect(hWnd, &rc);
            if (PtInRect(&rc, pt))
            {
                HWND hShell = GetShellWindow();
                if (hWnd != hShell)
                {
                    return hWnd;
                }
            }
        }
        hWnd = GetNextWindow(hWnd, GW_HWNDNEXT);
    }
    return NULL;
}

static BOOL IsCornerProtected(int corner)
{
    switch (g_dwCloseProtectMode)
    {
    case 1:
        return (corner == 1);
    case 2:
        return (corner == 2);
    case 3:
        return (corner == 1 || corner == 2);
    default:
        return FALSE;
    }
}

static BOOL IsAltShiftClickAllowed(int button)
{
    switch (kAltShiftClickMode)
    {
    case 1:
        return (button == 1);
    case 2:
        return (button == 2);
    case 3:
        return (button == 1 || button == 2);
    default:
        return FALSE;
    }
}

static BOOL IsCornerEnabled(int corner)
{
    if (corner == 1)
    {
        return TRUE;
    }
    if (corner == 2)
    {
        if (g_bSingleCornerMode)
        {
            return FALSE;
        }
        if (!g_pGetDesktopCount)
        {
            return FALSE;
        }
        int desktopCount = g_pGetDesktopCount();
        if (desktopCount >= 3)
        {
            return TRUE;
        }
        return !g_bDisableTopRightWhenDesktopLE2;
    }
    return FALSE;
}

static void ResetSingleCornerState()
{
    g_bJustCreatedDesktop = FALSE;
    g_firstchange = TRUE;
    g_numChange = 0;
}

static int GetSingleCornerTargetDesktop(int currentDesktop, int desktopCount)
{
    DWORD currentTime = GetTickCount();
    BOOL isFirstTrigger = (g_firstchange || g_numChange == 0);
    if (currentDesktop == 0)
    {
        if (!isFirstTrigger && g_bKeepSwitch && !g_bAutoSwitch)
        {
            return -1;
        }
        return currentDesktop + 1;
    }
    else if (currentDesktop == desktopCount - 1)
    {
        if (!isFirstTrigger && g_bKeepSwitch && !g_bAutoSwitch)
        {
            return -1;
        }
        return currentDesktop - 1;
    }
    else
    {
        DWORD timeSinceLastSwitch = (g_dwLastSwitchTime == 0) ? (kSwitchTimeout + 1) : (currentTime - g_dwLastSwitchTime);
        if (timeSinceLastSwitch > kSwitchTimeout)
        {
            if (g_lastdesktop > currentDesktop)
            {
                return currentDesktop + 1;
            }
            else
            {
                return currentDesktop - 1;
            }
        }
        else
        {
            if (g_nLastDesktopNumber != -1 && currentDesktop > g_nLastDesktopNumber)
            {
                return currentDesktop + 1;
            }
            else
            {
                return currentDesktop - 1;
            }
        }
    }
}

static DWORD WINAPI CornerWorkerThread(LPVOID lpParameter)
{
    while (!g_bAppIsExiting)
    {
        WaitForSingleObject(g_hHotCornerEvent, INFINITE);
        if (g_bAppIsExiting)
            break;
        RefreshAreaScreenSize();
        int bounceCount = 0;
        int heldCorner = g_iTriggeredCorner;
        if (heldCorner != 1 && heldCorner != 2)
            heldCorner = 0;
        while (!g_bAppIsExiting)
        {
            POINT Point;
            int activeCorner = 0;
            LONG forcedCorner = InterlockedExchange(&g_lForcedTriggeredCorner, 0);
            if ((forcedCorner == 1 || forcedCorner == 2) && IsCornerEnabled((int)forcedCorner))
            {
                activeCorner = (int)forcedCorner;
                heldCorner = activeCorner;
            }
            else if (GetCursorPos(&Point))
            {
                if (heldCorner == 1 && PtInHotArea(kHotCorner_out_topleft, Point) && IsCornerEnabled(1))
                    activeCorner = 1;
                else if (heldCorner == 2 && PtInHotArea(kHotCorner_out_topright, Point) && IsCornerEnabled(2))
                    activeCorner = 2;
                else if (PtInHotArea(kHotCorner_in_topleft, Point) && IsCornerEnabled(1))
                {
                    activeCorner = 1;
                    heldCorner = 1;
                }
                else if (PtInHotArea(kHotCorner_in_topright, Point) && IsCornerEnabled(2))
                {
                    activeCorner = 2;
                    heldCorner = 2;
                }
            }
            if (activeCorner == 0)
            {
                break;
            }
            EnterCriticalSection(&g_cs);
            if (g_bSingleCornerMode && g_bJustCreatedDesktop)
            {
                LeaveCriticalSection(&g_cs);
                Sleep(kRepeatInterval);
                continue;
            }
            BOOL wasDragging = (GetKeyState(VK_LBUTTON) < 0);
            BYTE KeyState[256];
            if (GetKeyboardState(KeyState))
            {
                if (!wasDragging && (KEYDOWN(KeyState[VK_SHIFT]) || KEYDOWN(KeyState[VK_CONTROL]) ||
                                     KEYDOWN(KeyState[VK_MENU]) || KEYDOWN(KeyState[VK_LWIN]) ||
                                     KEYDOWN(KeyState[VK_RWIN])))
                {
                    LeaveCriticalSection(&g_cs);
                    Sleep(kRepeatInterval);
                    continue;
                }
            }
            int desktopCount = g_pGetDesktopCount();
            if (desktopCount < 2)
            {
                if (wasDragging)
                {
                    HWND hRootWnd = GetForegroundWindow();
                    if (hRootWnd != NULL)
                    {
                        LeaveCriticalSection(&g_cs);
                        CreateNewDesktop();
                        Sleep(kNewDesktopWait);
                        g_pMoveWindowToDesktopNumber(hRootWnd, 1);
                        EnterCriticalSection(&g_cs);
                        g_bJustCreatedDesktop = TRUE;
                        if (g_bSingleCornerMode)
                        {
                            g_dwLastSwitchTime = GetTickCount();
                            g_nLastDesktopNumber = 0;
                        }
                    }
                }
                else
                {
                    LeaveCriticalSection(&g_cs);
                    CreateNewDesktop();
                    Sleep(kNewDesktopWait);
                    EnterCriticalSection(&g_cs);
                    if (g_bSingleCornerMode)
                    {
                        g_dwLastSwitchTime = GetTickCount();
                        g_nLastDesktopNumber = 0;
                    }
                }
                LeaveCriticalSection(&g_cs);
                if (g_bSingleCornerMode)
                {
                    if (kRepeatInterval > kNewDesktopWait)
                    {
                        Sleep(kRepeatInterval - kNewDesktopWait);
                    }
                    else
                    {
                        Sleep(0);
                    }
                    continue;
                }
                goto WaitForLeave;
            }
            int currentDesktop = g_pGetCurrentDesktopNumber();
            int targetDesktop = -1;
            if (g_bSingleCornerMode)
            {
                DWORD currentTime = GetTickCount();
                targetDesktop = GetSingleCornerTargetDesktop(currentDesktop, desktopCount);
                if (targetDesktop != -1 && targetDesktop != currentDesktop)
                {
                    g_dwLastSwitchTime = currentTime;
                    g_nLastDesktopNumber = currentDesktop;
                }
            }
            else if (desktopCount == 2)
            {
                targetDesktop = (currentDesktop == 0) ? 1 : 0;
                bounceCount = 1;
            }
            else if (activeCorner == 1)
            {
                if (currentDesktop > 0)
                {
                    targetDesktop = currentDesktop - 1;
                }
                else
                {
                    if (g_bAutoSwitch)
                    {
                        if (bounceCount < 2)
                        {
                            targetDesktop = 1;
                            bounceCount++;
                        }
                        else
                        {
                            LeaveCriticalSection(&g_cs);
                            goto WaitForLeave;
                        }
                    }
                    else
                    {
                        LeaveCriticalSection(&g_cs);
                        goto WaitForLeave;
                    }
                }
            }
            else if (activeCorner == 2)
            {
                if (currentDesktop < desktopCount - 1)
                {
                    targetDesktop = currentDesktop + 1;
                }
                else
                {
                    if (g_bAutoSwitch)
                    {
                        if (bounceCount < 2)
                        {
                            targetDesktop = currentDesktop - 1;
                            bounceCount++;
                        }
                        else
                        {
                            LeaveCriticalSection(&g_cs);
                            goto WaitForLeave;
                        }
                    }
                    else
                    {
                        LeaveCriticalSection(&g_cs);
                        goto WaitForLeave;
                    }
                }
            }
            if (targetDesktop != -1)
            {
                LeaveCriticalSection(&g_cs);
                if (wasDragging)
                {
                    if (GetKeyState(VK_RBUTTON) < 0)
                    {
                        INPUT inputs[2] = {0};
                        inputs[0].type = INPUT_MOUSE;
                        inputs[0].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
                        inputs[1].type = INPUT_MOUSE;
                        inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
                        SendInput(2, inputs, sizeof(INPUT));
                        Sleep(Fancyzonessleep);
                        EnterCriticalSection(&g_cs);
                        g_bJustCreatedDesktop = TRUE;
                        ResetEvent(g_hHotCornerEvent);
                        LeaveCriticalSection(&g_cs);
                        continue;
                    }
                    else if (GetKeyState(VK_SHIFT) < 0)
                    {
                        INPUT inputs[2] = {0};
                        inputs[0].type = INPUT_KEYBOARD;
                        inputs[0].ki.wVk = VK_SHIFT;
                        inputs[0].ki.dwFlags = KEYEVENTF_KEYUP;
                        inputs[1].type = INPUT_MOUSE;
                        inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
                        SendInput(2, inputs, sizeof(INPUT));
                        Sleep(Fancyzonessleep);
                        EnterCriticalSection(&g_cs);
                        g_bJustCreatedDesktop = TRUE;
                        ResetEvent(g_hHotCornerEvent);
                        LeaveCriticalSection(&g_cs);
                        continue;
                    }
                    LONG rightClickCount = InterlockedCompareExchange(&g_lRightClickCountDuringDrag, 0, 0);
                    if (rightClickCount % 2 != 0)
                    {
                        INPUT inputs[2] = {0};
                        inputs[0].type = INPUT_MOUSE;
                        inputs[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
                        inputs[1].type = INPUT_MOUSE;
                        inputs[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
                        SendInput(2, inputs, sizeof(INPUT));
                        Sleep(Fancyzonessleep);
                        InterlockedExchange(&g_lFancyZonesShouldRun, 1);
                    }
                    HWND hRootWnd = GetForegroundWindow();
                    if (hRootWnd != NULL)
                    {
                        POINT currentPoint;
                        GetCursorPos(&currentPoint);
                        LRESULT hitTestResult = SendMessage(hRootWnd, WM_NCHITTEST, 0, MAKELPARAM(currentPoint.x, currentPoint.y));
                        if (hitTestResult == HTCAPTION)
                        {
                            g_pMoveWindowToDesktopNumber(hRootWnd, targetDesktop);
                        }
                    }
                    AnimateDesktopSwitch(currentDesktop, targetDesktop);
                }
                else
                {
                    BOOL useInstantSwitch = IsForegroundWindowFromSpecialProcess();
                    if (useInstantSwitch)
                        g_pGoToDesktopNumber(targetDesktop);
                    else
                        AnimateDesktopSwitch(currentDesktop, targetDesktop);
                }
                if (InterlockedExchange(&g_lFancyZonesShouldRun, 0) == 1)
                {
                    LaunchFancyZones();
                }
                if (g_bSingleCornerMode)
                {
                    EnterCriticalSection(&g_cs);
                    g_lastdesktop = currentDesktop;
                    g_numChange = g_numChange + 1;
                    if (g_numChange > 4 * (desktopCount - 1))
                    {
                        g_bJustCreatedDesktop = TRUE;
                        ResetEvent(g_hHotCornerEvent);
                        LeaveCriticalSection(&g_cs);
                        goto WaitForLeave;
                    }
                    BOOL isFirstChange = g_firstchange;
                    if (g_firstchange)
                    {
                        g_firstchange = FALSE;
                    }
                    LeaveCriticalSection(&g_cs);
                    if (!g_bKeepSwitch)
                    {
                        goto WaitForLeave;
                    }
                    if (isFirstChange)
                    {
                        Sleep(kRepeatInterval);
                    }
                    else
                    {
                        Sleep(kRepeatInterval);
                    }
                    continue;
                }
                if (desktopCount == 2)
                {
                    goto WaitForLeave;
                }
                if (!g_bKeepSwitch)
                {
                    goto WaitForLeave;
                }
                Sleep(kRepeatInterval);
            }
            else
            {
                LeaveCriticalSection(&g_cs);
                goto WaitForLeave;
            }
        }
    WaitForLeave:
        while (!g_bAppIsExiting)
        {
            POINT ptWait;
            if (GetCursorPos(&ptWait))
            {
                BOOL inTopLeft = IsCornerEnabled(1) && PtInHotArea(kHotCorner_out_topleft, ptWait);
                BOOL inTopRight = IsCornerEnabled(2) && PtInHotArea(kHotCorner_out_topright, ptWait);
                if (!inTopLeft && !inTopRight)
                    break;
            }
            Sleep(kLeaveCornerPollInterval);
        }
        ResetSingleCornerState();
        ResetEvent(g_hHotCornerEvent);
    }
    return 0;
}

static LRESULT CALLBACK MouseHookCallback(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (g_bIsPaused)
    {
        return CallNextHookEx(NULL, nCode, wParam, lParam);
    }
    if (nCode >= 0)
    {
        MSLLHOOKSTRUCT *evt = (MSLLHOOKSTRUCT *)lParam;
        switch (wParam)
        {
        case WM_LBUTTONDOWN:
        {
            UpdateCloseProtectButtonStateChanged();
            if (IsAltShiftClickAllowed(1) && GetKeyState(VK_MENU) < 0 && GetKeyState(VK_SHIFT) < 0)
            {
                int desktopCount = g_pGetDesktopCount();
                if (desktopCount == 2)
                {
                    POINT pt = evt->pt;
                    HWND hWndPoint = GetRealWindowAtPoint(pt);
                    if (hWndPoint != NULL)
                    {
                        HWND hRootWnd = GetAncestor(hWndPoint, GA_ROOT);
                        HWND hForeground = GetForegroundWindow();
                        if (hRootWnd == hForeground && hRootWnd != NULL && hRootWnd != GetDesktopWindow() && hRootWnd != GetShellWindow())
                        {
                            int currentDesktop = g_pGetCurrentDesktopNumber();
                            int targetDesktop = (currentDesktop == 0) ? 1 : 0;
                            g_pMoveWindowToDesktopNumber(hRootWnd, targetDesktop);
                            g_pGoToDesktopNumber(targetDesktop);
                            EnterCriticalSection(&g_cs);
                            LeaveCriticalSection(&g_cs);
                            g_bSuppressLeftClickUp = TRUE;
                            return 1;
                        }
                    }
                }
            }
            break;
        }
        case WM_LBUTTONUP:
        {
            UpdateCloseProtectButtonStateChanged();
            if (g_bSuppressLeftClickUp)
            {
                g_bSuppressLeftClickUp = FALSE;
                InterlockedExchange(&g_lRightClickCountDuringDrag, 0);
                return 1;
            }
            InterlockedExchange(&g_lRightClickCountDuringDrag, 0);
            break;
        }
        case WM_RBUTTONDOWN:
        {
            UpdateCloseProtectButtonStateChanged();
            if (IsAltShiftClickAllowed(2) && GetKeyState(VK_MENU) < 0 && GetKeyState(VK_SHIFT) < 0)
            {
                int desktopCount = g_pGetDesktopCount();
                if (desktopCount == 2)
                {
                    POINT pt = evt->pt;
                    HWND hWndPoint = GetRealWindowAtPoint(pt);
                    if (hWndPoint != NULL)
                    {
                        HWND hRootWnd = GetAncestor(hWndPoint, GA_ROOT);
                        HWND hForeground = GetForegroundWindow();
                        if (hRootWnd == hForeground && hRootWnd != NULL && hRootWnd != GetDesktopWindow() && hRootWnd != GetShellWindow())
                        {
                            int currentDesktop = g_pGetCurrentDesktopNumber();
                            int targetDesktop = (currentDesktop == 0) ? 1 : 0;
                            g_pMoveWindowToDesktopNumber(hRootWnd, targetDesktop);
                            g_bSuppressRightClickUp = TRUE;
                            return 1;
                        }
                    }
                }
            }
            if (GetKeyState(VK_LBUTTON) < 0)
            {
                InterlockedIncrement(&g_lRightClickCountDuringDrag);
            }
            break;
        }
        case WM_RBUTTONUP:
        {
            UpdateCloseProtectButtonStateChanged();
            if (g_bSuppressRightClickUp)
            {
                g_bSuppressRightClickUp = FALSE;
                return 1;
            }
            break;
        }
        case WM_MOUSEMOVE:
        {
            POINT pt;
            int currentCorner = 0;
            BOOL outTopLeft = FALSE;
            BOOL outTopRight = FALSE;
            if (GetCursorPos(&pt))
            {
                BOOL inTopLeft = IsCornerEnabled(1) && PtInHotArea(kHotCorner_in_topleft, pt);
                BOOL inTopRight = IsCornerEnabled(2) && PtInHotArea(kHotCorner_in_topright, pt);
                outTopLeft = IsCornerEnabled(1) && PtInHotArea(kHotCorner_out_topleft, pt);
                outTopRight = IsCornerEnabled(2) && PtInHotArea(kHotCorner_out_topright, pt);
                if (inTopLeft)
                    currentCorner = 1;
                else if (inTopRight)
                    currentCorner = 2;

                if (currentCorner != 0 && g_iTriggeredCorner != currentCorner)
                {
                    RefreshAreaScreenSize();
                    inTopLeft = IsCornerEnabled(1) && PtInHotArea(kHotCorner_in_topleft, pt);
                    inTopRight = IsCornerEnabled(2) && PtInHotArea(kHotCorner_in_topright, pt);
                    outTopLeft = IsCornerEnabled(1) && PtInHotArea(kHotCorner_out_topleft, pt);
                    outTopRight = IsCornerEnabled(2) && PtInHotArea(kHotCorner_out_topright, pt);
                    currentCorner = 0;
                    if (inTopLeft)
                        currentCorner = 1;
                    else if (inTopRight)
                        currentCorner = 2;
                }
            }
            LONG armedCorner = InterlockedCompareExchange(&g_lCloseProtectCorner, 0, 0);
            if (g_bCloseProtectArmed)
            {
                UpdateCloseProtectButtonStateChanged();
            }
            if (g_bCloseProtectArmed)
            {
                BOOL stillInArmedOutArea = FALSE;
                if (armedCorner == 1)
                    stillInArmedOutArea = outTopLeft;
                else if (armedCorner == 2)
                    stillInArmedOutArea = outTopRight;
                if (stillInArmedOutArea)
                {
                    g_iTriggeredCorner = (int)armedCorner;
                    break;
                }
                else
                {
                    BOOL shouldTrigger = !g_bCloseProtectButtonStateChanged;
                    g_bCloseProtectArmed = FALSE;
                    g_bCloseProtectButtonStateChanged = FALSE;
                    g_dwCloseProtectInitialButtons = 0;
                    InterlockedExchange(&g_lCloseProtectCorner, 0);
                    if ((armedCorner == 1 || armedCorner == 2) && shouldTrigger && g_hHotCornerEvent)
                    {
                        g_iTriggeredCorner = (int)armedCorner;
                        InterlockedExchange(&g_lForcedTriggeredCorner, armedCorner);
                        SetEvent(g_hHotCornerEvent);
                        break;
                    }
                }
            }
            if (IsCornerProtected(currentCorner))
            {
                g_bCloseProtectArmed = TRUE;
                g_dwCloseProtectInitialButtons = GetMouseButtonMask();
                g_bCloseProtectButtonStateChanged = FALSE;
                InterlockedExchange(&g_lCloseProtectCorner, currentCorner);
                g_iTriggeredCorner = currentCorner;
                break;
            }
            if (currentCorner == 1)
            {
                g_iTriggeredCorner = 1;
                if (g_hHotCornerEvent)
                    SetEvent(g_hHotCornerEvent);
            }
            else if (currentCorner == 2)
            {
                g_iTriggeredCorner = 2;
                if (g_hHotCornerEvent)
                    SetEvent(g_hHotCornerEvent);
            }
            else
            {
                if (!outTopLeft && !outTopRight)
                {
                    g_iTriggeredCorner = 0;
                    ResetSingleCornerState();
                }
            }
            break;
        }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}
int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    HMODULE hUser32 = GetModuleHandle(TEXT("user32.dll"));
    if (hUser32)
    {
        typedef BOOL(WINAPI * SetProcessDpiAwarenessContextPtr)(HANDLE);
        SetProcessDpiAwarenessContextPtr setDpiAware =
            (SetProcessDpiAwarenessContextPtr)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (setDpiAware)
        {
            setDpiAware((HANDLE)-4);
        }
    }
    SetProcessDPIAware();
    HANDLE hMutex = CreateMutex(NULL, TRUE, TEXT("MyVirtualDesktopHotCornerAppMutex"));
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBox(NULL, TEXT("Another instance of the application is already running."), TEXT("Information"), MB_ICONINFORMATION | MB_OK);
        return 0;
    }
    InitializeCriticalSection(&g_cs);
    std::wstring exeDir = GetExePath();
    std::wstring configPath = exeDir + L"\\except.txt";
    DWORD buildVer = GetWindowsBuildNumber();
    const wchar_t *verFolder = L"win10";
    if (buildVer >= 26100)
        verFolder = L"win11_26100";
    else if (buildVer >= 22631)
        verFolder = L"win11_22631";
    else if (buildVer >= 22621)
        verFolder = L"win11_22621_2215";
    else if (buildVer >= 22000)
        verFolder = L"win11_old";
    std::wstring dllPath = exeDir + L"\\VirtualDesktopAccessor\\" + verFolder + L"\\VirtualDesktopAccessor.dll";
    LoadSpecialProcesses(configPath.c_str());
    LoadSpeedConfig();
    LoadAreaConfig();
    LoadPauseStateByFile();
    g_hVdaDll = LoadLibrary(dllPath.c_str());
    if (g_hVdaDll == NULL)
    {
        MessageBox(NULL, TEXT("Could not load VirtualDesktopAccessor.dll.\n\nPlease make sure the DLL is in the same directory as this executable."), TEXT("Error"), MB_ICONERROR | MB_OK);
        DeleteCriticalSection(&g_cs);
        CloseHandle(hMutex);
        return 1;
    }
    g_pGetDesktopCount = (GetDesktopCountProc)GetProcAddress(g_hVdaDll, "GetDesktopCount");
    g_pGetCurrentDesktopNumber = (GetCurrentDesktopNumberProc)GetProcAddress(g_hVdaDll, "GetCurrentDesktopNumber");
    g_pGoToDesktopNumber = (GoToDesktopNumberProc)GetProcAddress(g_hVdaDll, "GoToDesktopNumber");
    g_pMoveWindowToDesktopNumber = (MoveWindowToDesktopNumberProc)GetProcAddress(g_hVdaDll, "MoveWindowToDesktopNumber");
    if (!g_pGetDesktopCount || !g_pGetCurrentDesktopNumber || !g_pGoToDesktopNumber || !g_pMoveWindowToDesktopNumber)
    {
        MessageBox(NULL, TEXT("Could not find required functions in VirtualDesktopAccessor.dll.\n\nThe DLL might be outdated or corrupted."), TEXT("Error"), MB_ICONERROR | MB_OK);
        FreeLibrary(g_hVdaDll);
        DeleteCriticalSection(&g_cs);
        CloseHandle(hMutex);
        return 1;
    }
    g_hHotCornerEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (g_hHotCornerEvent == NULL)
    {
        MessageBox(NULL, TEXT("Failed to create event!"), TEXT("Error"), MB_ICONERROR);
        FreeLibrary(g_hVdaDll);
        DeleteCriticalSection(&g_cs);
        CloseHandle(hMutex);
        return 1;
    }
    g_hCornerWorkerThread = CreateThread(NULL, 0, CornerWorkerThread, NULL, 0, NULL);
    if (g_hCornerWorkerThread == INVALID_HANDLE_VALUE)
    {
        MessageBox(NULL, TEXT("Failed to create worker thread!"), TEXT("Error"), MB_ICONERROR);
        CloseHandle(g_hHotCornerEvent);
        FreeLibrary(g_hVdaDll);
        DeleteCriticalSection(&g_cs);
        CloseHandle(hMutex);
        return 1;
    }
    HHOOK MouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookCallback, NULL, 0);
    if (!MouseHook)
    {
        MessageBox(NULL, TEXT("Failed to install mouse hook!"), TEXT("Error"), MB_ICONERROR);
        g_bAppIsExiting = TRUE;
        SetEvent(g_hHotCornerEvent);
        WaitForSingleObject(g_hCornerWorkerThread, 2000);
        CloseHandle(g_hCornerWorkerThread);
        CloseHandle(g_hHotCornerEvent);
        FreeLibrary(g_hVdaDll);
        DeleteCriticalSection(&g_cs);
        CloseHandle(hMutex);
        return 1;
    }
    RegisterHotKey(NULL, 1, kHotKeyModifiers, kHotKey);
    RegisterHotKey(NULL, 2, kExitHotKeyModifiers, kExitHotKey);
    MSG Msg;
    while (GetMessage(&Msg, NULL, 0, 0) > 0)
    {
        if (Msg.message == WM_HOTKEY)
        {
            if (Msg.wParam == 1)
            {
                g_bIsPaused = !g_bIsPaused;
                SavePauseStateByFile(g_bIsPaused);
                if (g_bIsPaused)
                {
                    PlayWavOrBeep(TEXT("C:\\Windows\\Media\\Speech Sleep.wav"), 1);
                }
                else
                {
                    PlayWavOrBeep(TEXT("C:\\Windows\\Media\\Speech On.wav"), 2);
                }
            }
            else if (Msg.wParam == 2)
            {
                PlayWavOrBeep(TEXT("C:\\Windows\\Media\\Speech Off.wav"), 3);
                Sleep(kExitDelay);
                PostQuitMessage(0);
            }
        }
    }
    g_bAppIsExiting = TRUE;
    UnhookWindowsHookEx(MouseHook);
    UnregisterHotKey(NULL, 1);
    UnregisterHotKey(NULL, 2);
    if (g_hHotCornerEvent)
        SetEvent(g_hHotCornerEvent);
    if (g_hCornerWorkerThread != INVALID_HANDLE_VALUE)
    {
        WaitForSingleObject(g_hCornerWorkerThread, INFINITE);
        CloseHandle(g_hCornerWorkerThread);
    }
    if (g_hHotCornerEvent)
        CloseHandle(g_hHotCornerEvent);
    if (g_hVdaDll)
        FreeLibrary(g_hVdaDll);
    DeleteCriticalSection(&g_cs);
    CloseHandle(hMutex);
    return (int)Msg.wParam;
}