// DesktopTip.cpp : 定义应用程序的入口点。
//

#include "framework.h"
#include "DesktopTip.h"
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <windows.h>
#include <shellapi.h>
#include <ctime>
#include <functional>
#include <commdlg.h>
#include <windowsx.h>
#include <regex>
#include <unordered_map>
#include <random>
#include <cwctype>
#include <algorithm>
#include <cmath>
#include <cstdint>

#include <shlobj.h>
#include <filesystem>
#include <objidl.h>
#define GDIPVER 0x0110   // 启用 GDI+ 1.1（Blur 等效果所需，须在 gdiplus.h 之前）
#include <gdiplus.h>
#include <gdipluseffects.h>
#include <initializer_list>
#include <commctrl.h>
#include <dwmapi.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
// 启用 v6 通用控件（让滑动条获得现代主题外观）
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using namespace std;
namespace fs = std::filesystem;
using namespace std::chrono;

#define WM_TASKBAR (WM_USER + 1)
#define ID_TRAY_EXIT 2
#define ID_TRAY_REFRESH 1
#define ID_TRAY_EDIT 3
#define ID_FONTSIZE_INCREASE 4
#define ID_FONTSIZE_DECREASE 5
#define ID_SPACING_INCREASE 6
#define ID_SPACING_DECREASE 7
#define ID_TEXT_COLOR 8
#define ID_TODO_CHANGE 9
#define ID_BG_COLOR 10
#define ID_OPACITY_INCREASE 11
#define ID_OPACITY_DECREASE 12
#define ID_SETTINGS 13
#define ID_MODE_DESKTOP 14
#define ID_MODE_TOPMOST 15
#define ID_COUNTDOWN_1MIN 16
#define ID_COUNTDOWN_5MIN 17
#define ID_COUNTDOWN_10MIN 18
#define ID_COUNTDOWN_CUSTOM 19
#define ID_ALARM_CUSTOM 20
#define ID_REMINDER_CANCEL 21
#define ID_FONT_SELECT 22
#define ID_COUNTDOWN_1HOUR 23
#define ID_COUNTDOWN_REPEAT 24

#define TIMER_REFRESH 1
#define TIMER_REMINDER_CHECK 2
#define TIMER_REMINDER_ANIMATE 3

#define WM_REMINDER_STOP (WM_APP + 1)

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

int lineHeight = 30; // 假设每行高度固定
int fontHeight = 10; // 行间距（额外的空白高度）

std::string TODO_PATH = "";
HWND hWnd;

static HFONT g_hFont = NULL;  // Global font handle
static COLORREF g_textColor = RGB(244,245,248);
static int g_bgAlpha = 128;                 // 卡片背景不透明度 0-255
static COLORREF g_bgColor = RGB(20, 25, 35); // 卡片背景色
static std::wstring g_fontFamily = L"Microsoft YaHei UI";

// 显示模式：
//   0 = 附着桌面（默认）—— 贴在壁纸层(WorkerW)上，Win+D /“显示桌面”不会使其最小化，永远附着。
//   1 = 始终置顶 —— 浮在所有窗口最上层，Win+D 也不影响，始终保持在最上层。
static int g_displayMode = 0;

// 手动拖动状态：子窗口(WS_CHILD)模式下无法用 HTCAPTION 拖动，需自行处理
static bool  g_dragging   = false;
static POINT g_dragOffset = { 0, 0 };
static POINT g_lastContextMenuPt = { 0, 0 };
static bool g_hasLastContextMenuPt = false;

struct TodoHitRect {
    RECT rc;
    int sourceIndex;
};

static std::vector<TodoHitRect> g_todoHitRects;

static bool g_reminderArmed = false;
static system_clock::time_point g_reminderDue;
static std::wstring g_reminderTitle;
static std::wstring g_reminderDetail;
static bool g_reminderIsCountdown = false;
static int g_reminderIntervalSeconds = 0;
static bool g_countdownRepeatEnabled = false;

static bool g_alertActive = false;
static bool g_restoringAlert = false;
static steady_clock::time_point g_alertStarted;
static std::wstring g_alertTitle;
static std::wstring g_alertDetail;
static RECT g_alertSavedRect = { 0, 0, 0, 0 };
static int g_alertSavedFontHeight = 10;
static int g_alertSavedLineHeight = 30;
static int g_alertSavedBgAlpha = 128;
static COLORREF g_alertSavedTextColor = RGB(244, 245, 248);
static COLORREF g_alertSavedBgColor = RGB(20, 25, 35);
static double g_alertX = 0.0;
static double g_alertY = 0.0;
static double g_alertVx = 0.0;
static double g_alertVy = 0.0;
static steady_clock::time_point g_alertLastFrame;
static steady_clock::time_point g_alertNextFontChange;
static std::wstring g_alertFontFamily;
static double g_alertCardW = 320.0;
static double g_alertCardH = 180.0;
static std::vector<std::wstring> g_systemFonts;
static HHOOK g_keyboardHook = NULL;
static std::mt19937 g_rng((unsigned)steady_clock::now().time_since_epoch().count());

// 黑洞提醒效果：倒计时/闹钟触发后，先捕获当前页面/桌面作为“采样源”，
// 但不再把原始页面整张画回输出层。最终输出层只画被黑洞偏折后的像素、
// 吸积盘、光子环和事件视界；影响范围之外保持透明，让下面的真实页面自然可见。
static std::vector<BYTE> g_blackholeBg; // captured desktop, BGRA, top-down, sampling source only
static int g_blackholeW = 0;
static int g_blackholeH = 0;
static int g_blackholeVX = 0;
static int g_blackholeVY = 0;
static bool g_blackholeBgReady = false;
static unsigned long long g_blackholeBgSerial = 0;

// Distortion-only mode knobs.
// SourceConstantAlpha stays at 255; visibility is controlled by per-pixel alpha.
// The original page is used only as a texture source for displaced samples, not as a base layer.
static const BYTE   BH_ALERT_LAYER_ALPHA = 255;          // keep 255; per-pixel alpha controls visibility
static const double BH_DISTORTION_ALPHA_GAIN = 1.18;     // opacity of the warped samples, 0.0..2.0
static const double BH_DISTORTION_MAX_ALPHA = 0.96;      // keep <1 near edge for a softer overlay
static const double BH_EVENT_HORIZON_OPACITY = 1.00;     // black circle darkness, 0.0..1.0
static const double BH_EVENT_HORIZON_RADIUS_SCALE = 1.18;
// The edge renders an aligned captured-base color that eases to the real desktop
// under the layered window. This keeps the outer transition visually registered
// with the bottom image while the inner field still shows lensing and darkness.
static const double BH_FIELD_FEATHER_START = 0.64;   // lower = wider aligned transition band
static const double BH_FIELD_ALPHA_POWER = 1.55;     // >1 makes the outer edge less visible
static const double BH_DARK_CORE_END = 0.42;         // dark carrier fully active only in the inner core
static const double BH_DARK_FEATHER_END = 0.68;      // dark carrier fades before the boundary alignment band
static const double BH_LOCAL_FIELD_TINT_R = 0.002;   // very dark carrier, not the page image
static const double BH_LOCAL_FIELD_TINT_G = 0.004;
static const double BH_LOCAL_FIELD_TINT_B = 0.010;
static const double BH_LOCAL_FIELD_VIGNETTE = 0.20;  // local gravitational well darkness
static const double BH_WARP_VIS_GAIN = 1.10;         // visual strength of warped samples

struct BlackHoleInstance {
    double cx = 0.5;
    double cy = 0.5;
    double moveVx = 0.0;
    double moveVy = 0.0;
    double moveAmpX = 0.0;
    double moveAmpY = 0.0;
    double movePeriodX = 7.0;
    double movePeriodY = 8.0;
    double movePhaseX = 0.0;
    double movePhaseY = 0.0;
    double minRh = 0.014;
    double maxRh = 0.024;
    double maxEffectR = 0.10;
    double period = 4.0;
    double phase = 0.0;
    double roll = 0.35;
    double incl = 1.5;
    double diskSpeed = 5.0;
    double seed = 0.0;
};

static std::vector<BlackHoleInstance> g_blackHoles;

// CPU/GDI fallback render cache for the full-screen black-hole alert.
// The window remains a layered topmost popup, but the expensive HDC/HBITMAP/DIB allocation
// is reused across animation frames. Each frame clears only the previous/current dirty
// rectangle to transparent and then redraws the current black-hole distortion region.
static HDC g_blackholeRenderDC = NULL;
static HBITMAP g_blackholeRenderBitmap = NULL;
static HBITMAP g_blackholeRenderOldBitmap = NULL;
static void* g_blackholeRenderBits = nullptr;
static int g_blackholeRenderW = 0;
static int g_blackholeRenderH = 0;
static int g_blackholeRenderVX = 0;
static int g_blackholeRenderVY = 0;
static bool g_blackholeFrameInitialized = false;
static unsigned long long g_blackholeFrameBgSerial = 0;
static RECT g_blackholePrevDirty = { 0, 0, 0, 0 };
static bool g_blackholePrevDirtyValid = false;

#define MAX_LOADSTRING 100

// 全局变量:
HINSTANCE hInst;                                // 当前实例
WCHAR szTitle[MAX_LOADSTRING];                  // 标题栏文本
WCHAR szWindowClass[MAX_LOADSTRING];            // 主窗口类名

// 此代码模块中包含的函数的前向声明:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
void RenderTip(HWND);
void ShowSettingsPanel(HWND);
void confWrite();

void FindWorkerW();
void ApplyDisplayMode(HWND hwnd, int mode);

HWND g_hWorkerW = NULL;
// 重新设置父窗口(SetParent)期间置 true：此时系统会先隐藏窗口再重挂父子关系，
// WM_WINDOWPOSCHANGING 里若仍剥掉 SWP_HIDEWINDOW 会打断重挂、导致 SetParent 静默失败。
static bool g_reparenting = false;

enum R_DAYS {DLW,DLM,DLY};

static std::wstring MbcToWide(const std::string& bytes, UINT cp, DWORD flags = 0)
{
    if (bytes.empty()) return L"";
    int wlen = MultiByteToWideChar(cp, flags, bytes.data(), (int)bytes.size(), nullptr, 0);
    if (wlen <= 0) return L"";
    std::wstring w(wlen, L'\0');
    MultiByteToWideChar(cp, flags, bytes.data(), (int)bytes.size(), w.data(), wlen);
    return w;
}

static std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), len, nullptr, nullptr);
    return out;
}

static bool LooksLikeUtf8(const std::string& bytes)
{
    if (bytes.empty()) return true; // 纯空/ASCII 都无所谓
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        bytes.data(), (int)bytes.size(), nullptr, 0);
    return wlen > 0;
}

static std::wstring DecodeAutoToUtf16(const std::string& bytes)
{
    // UTF-8 BOM
    if (bytes.size() >= 3 &&
        (unsigned char)bytes[0] == 0xEF &&
        (unsigned char)bytes[1] == 0xBB &&
        (unsigned char)bytes[2] == 0xBF)
    {
        return MbcToWide(bytes.substr(3), CP_UTF8, MB_ERR_INVALID_CHARS);
    }

    // UTF-16 LE BOM
    if (bytes.size() >= 2 &&
        (unsigned char)bytes[0] == 0xFF &&
        (unsigned char)bytes[1] == 0xFE)
    {
        std::wstring w((bytes.size() - 2) / 2, L'\0');
        memcpy(w.data(), bytes.data() + 2, w.size() * sizeof(wchar_t));
        return w;
    }

    // UTF-16 BE BOM（简单处理：交换字节）
    if (bytes.size() >= 2 &&
        (unsigned char)bytes[0] == 0xFE &&
        (unsigned char)bytes[1] == 0xFF)
    {
        size_t n = (bytes.size() - 2) / 2;
        std::wstring w(n, L'\0');
        for (size_t i = 0; i < n; ++i) {
            unsigned char hi = (unsigned char)bytes[2 + 2 * i];
            unsigned char lo = (unsigned char)bytes[2 + 2 * i + 1];
            w[i] = (wchar_t)((lo << 8) | hi);
        }
        return w;
    }

    // 无 BOM：先试 UTF-8（严格校验），不行就回退到系统 ANSI（GBK/GB2312）
    if (LooksLikeUtf8(bytes))
        return MbcToWide(bytes, CP_UTF8, MB_ERR_INVALID_CHARS);

    return MbcToWide(bytes, CP_ACP, 0);
}


std::string GetTheRemainingDays(R_DAYS dayType) {
    // Get today as a year_month_day
    auto today_sys = floor<days>(system_clock::now());
    year_month_day today{ today_sys };

    // 1) Remaining in week (Sunday=0…Saturday=6 by convention here)
    weekday wd = weekday{ today_sys };            // Monday=1…Sunday=0
    int today_idx = wd.c_encoding();            // Sunday=0
    int week_end_idx = 0;                       // choose Sunday as end
    int days_left_week = (week_end_idx + 7 - today_idx) % 7;

    // 2) Remaining in month
    year_month ym = today.year() / today.month();
    // last day of month:
    year_month_day_last ym_last{ ym / last };
    int last_day = unsigned(ym_last.day());
    int days_left_month = last_day - unsigned(today.day());

    // 3) Remaining in year
    year_month_day end_of_year{ today.year(), month{12}, day{31} };
    auto yod = sys_days{ end_of_year } - today_sys;
    int days_left_year = yod.count();

    switch (dayType)
    {
    case(DLW):
        return to_string(days_left_week);
    case(DLM):
        return to_string(days_left_month);
    case(DLY):
        return to_string(days_left_year);
    }
    return "";
}

// Helper to process one line of text and replace placeholders
std::string ProcessPlaceholders(const std::string& text) {
    // Map of placeholder → generator function
    static const std::unordered_map<std::string, std::function<std::string()>> generators = {
        { "DATE", []() {
            char buf[32];
            std::time_t t = std::time(nullptr);
            std::strftime(buf, sizeof(buf), "%Y-%m-%d", std::localtime(&t));
            return std::string(buf);
        }},
        { "TIME", []() {
            char buf[32];
            std::time_t t = std::time(nullptr);
            std::strftime(buf, sizeof(buf), "%H:%M", std::localtime(&t));
            return std::string(buf);
		}},
		{ "WEEK", []() {
			char buf[32];
			std::time_t t = std::time(nullptr);
			std::strftime(buf, sizeof(buf), "%A", std::localtime(&t));
			return std::string(buf);
		}},
		{ "MONTH", []() {
			char buf[32];
			std::time_t t = std::time(nullptr);
			std::strftime(buf, sizeof(buf), "%B", std::localtime(&t));
			return std::string(buf);
		}},
		{ "YEAR", []() {
			char buf[32];
			std::time_t t = std::time(nullptr);
			std::strftime(buf, sizeof(buf), "%Y", std::localtime(&t));
			return std::string(buf);
		}},
		{ "DAYS_LEFT_WEEK", []() { return GetTheRemainingDays(DLW); }},
		{ "DAYS_LEFT_MONTH", []() { return GetTheRemainingDays(DLM); }},
		{ "DAYS_LEFT_YEAR", []() { return GetTheRemainingDays(DLY); }},
        // Add more placeholders here...
    };

    std::string result = text;
    std::smatch match;
    // e.g. matches {{PLACEHOLDER}}
    static const std::regex re(R"(\{\{([A-Z_]+)\}\})");
    // Iterate over all matches
    while (std::regex_search(result, match, re)) {
        std::string key = match[1].str();
        auto it = generators.find(key);
        std::string replacement = (it != generators.end()
            ? it->second()
            : match[0].str()  // leave unchanged if unknown
            );
        result.replace(match.position(0), match.length(0), replacement);
    }
    return result;
}

static std::wstring ProcessPlaceholdersW(const std::wstring& text)
{
    return DecodeAutoToUtf16(ProcessPlaceholders(WideToUtf8(text)));
}

static void TrimLineRight(std::wstring& text)
{
    while (!text.empty()) {
        wchar_t ch = text.back();
        if (ch != L' ' && ch != L'\t' && ch != L'\r') break;
        text.pop_back();
    }
}

static bool StripOkMarker(std::wstring& text)
{
    size_t end = text.size();
    while (end > 0 && (text[end - 1] == L' ' || text[end - 1] == L'\t' || text[end - 1] == L'\r')) {
        end--;
    }
    if (end < 3 || text.compare(end - 3, 3, L"_OK") != 0) return false;

    size_t removeFrom = end - 3;
    while (removeFrom > 0 && (text[removeFrom - 1] == L' ' || text[removeFrom - 1] == L'\t')) {
        removeFrom--;
    }
    text.erase(removeFrom);
    return true;
}

static std::vector<std::wstring> SplitLinesW(const std::wstring& text)
{
    std::wstring normalized;
    normalized.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\r') {
            normalized.push_back(L'\n');
            if (i + 1 < text.size() && text[i + 1] == L'\n') i++;
        } else {
            normalized.push_back(text[i]);
        }
    }

    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start <= normalized.size()) {
        size_t pos = normalized.find(L'\n', start);
        if (pos == std::wstring::npos) pos = normalized.size();
        lines.push_back(normalized.substr(start, pos - start));
        start = pos + 1;
        if (pos == normalized.size()) break;
    }
    return lines;
}

static bool ReadTodoFileLinesW(std::vector<std::wstring>& lines)
{
    lines.clear();
    std::ifstream file(TODO_PATH, std::ios::binary);
    if (!file.is_open()) return false;

    std::string bytes((std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    lines = SplitLinesW(DecodeAutoToUtf16(bytes));
    return true;
}

static bool WriteTodoFileLinesW(const std::vector<std::wstring>& lines)
{
    std::ofstream file(TODO_PATH, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;

    for (size_t i = 0; i < lines.size(); ++i) {
        std::string bytes = WideToUtf8(lines[i]);
        file.write(bytes.data(), (std::streamsize)bytes.size());
        if (i + 1 < lines.size()) file.write("\n", 1);
    }
    return true;
}

static bool ParseTodoLineState(const std::wstring& line, std::wstring* todoText, bool* completed)
{
    std::wstring s = line;
    TrimLineRight(s);

    size_t b = 0;
    while (b < s.size() && (s[b] == L' ' || s[b] == L'\t')) b++;
    std::wstring t = s.substr(b);
    if (t.empty() || t[0] == L'#' || t[0] == L'>') return false;

    std::wstring text;
    if ((t[0] == L'-' || t[0] == L'*') && t.size() > 1 && t[1] == L' ') {
        text = t.substr(2);
    } else {
        text = t;
    }

    bool isCompleted = StripOkMarker(text);
    if (todoText) *todoText = text;
    if (completed) *completed = isCompleted;
    return true;
}

static bool IsTodoLine(const std::wstring& line)
{
    return ParseTodoLineState(line, nullptr, nullptr);
}

static void SortTodoLinesByCompletion(std::vector<std::wstring>& lines)
{
    std::vector<size_t> slots;
    std::vector<std::wstring> openLines;
    std::vector<std::wstring> doneLines;

    for (size_t i = 0; i < lines.size(); ++i) {
        bool completed = false;
        if (!ParseTodoLineState(lines[i], nullptr, &completed)) continue;
        slots.push_back(i);
        if (completed) doneLines.push_back(lines[i]);
        else openLines.push_back(lines[i]);
    }

    size_t n = 0;
    for (const auto& line : openLines) lines[slots[n++]] = line;
    for (const auto& line : doneLines) lines[slots[n++]] = line;
}

static bool ToggleTodoLineCompletion(int sourceIndex)
{
    if (sourceIndex < 0) return false;

    std::vector<std::wstring> lines;
    if (!ReadTodoFileLinesW(lines)) return false;
    if ((size_t)sourceIndex >= lines.size() || !IsTodoLine(lines[(size_t)sourceIndex])) return false;

    bool completed = false;
    ParseTodoLineState(lines[(size_t)sourceIndex], nullptr, &completed);
    if (completed) {
        StripOkMarker(lines[(size_t)sourceIndex]);
    } else {
        TrimLineRight(lines[(size_t)sourceIndex]);
        StripOkMarker(lines[(size_t)sourceIndex]);
        lines[(size_t)sourceIndex] += L"_OK";
    }

    SortTodoLinesByCompletion(lines);
    return WriteTodoFileLinesW(lines);
}

static int HitTestTodoCheckbox(int x, int y)
{
    for (const auto& hit : g_todoHitRects) {
        if (x >= hit.rc.left && x <= hit.rc.right && y >= hit.rc.top && y <= hit.rc.bottom) {
            return hit.sourceIndex;
        }
    }
    return -1;
}

static bool ToggleTodoAtPoint(HWND hwnd, int x, int y)
{
    int sourceIndex = HitTestTodoCheckbox(x, y);
    if (sourceIndex < 0) return false;
    if (ToggleTodoLineCompletion(sourceIndex)) {
        RenderTip(hwnd);
    }
    return true;
}

static int RandInt(int lo, int hi)
{
    if (hi < lo) hi = lo;
    std::uniform_int_distribution<int> dist(lo, hi);
    return dist(g_rng);
}

static double RandDouble(double lo, double hi)
{
    if (hi < lo) hi = lo;
    std::uniform_real_distribution<double> dist(lo, hi);
    return dist(g_rng);
}

static double ClampDouble(double value, double lo, double hi)
{
    if (hi < lo) hi = lo;
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static int ClampByte(double value)
{
    return (int)ClampDouble(value + 0.5, 0.0, 255.0);
}

static double PingPong01(double elapsedSeconds, double periodSeconds)
{
    if (periodSeconds <= 0.0) return 0.0;
    double phase = std::fmod(elapsedSeconds, periodSeconds) / periodSeconds;
    return phase < 0.5 ? phase * 2.0 : (1.0 - phase) * 2.0;
}

static COLORREF HsvToRgb(double hue, double saturation, double value)
{
    hue = std::fmod(hue, 360.0);
    if (hue < 0.0) hue += 360.0;
    saturation = ClampDouble(saturation, 0.0, 1.0);
    value = ClampDouble(value, 0.0, 1.0);

    double c = value * saturation;
    double x = c * (1.0 - std::fabs(std::fmod(hue / 60.0, 2.0) - 1.0));
    double m = value - c;
    double r = 0.0, g = 0.0, b = 0.0;

    if (hue < 60.0) { r = c; g = x; }
    else if (hue < 120.0) { r = x; g = c; }
    else if (hue < 180.0) { g = c; b = x; }
    else if (hue < 240.0) { g = x; b = c; }
    else if (hue < 300.0) { r = x; b = c; }
    else { r = c; b = x; }

    return RGB(ClampByte((r + m) * 255.0),
        ClampByte((g + m) * 255.0),
        ClampByte((b + m) * 255.0));
}

static int CALLBACK EnumSystemFontProc(const LOGFONTW* lf, const TEXTMETRICW*, DWORD, LPARAM lParam)
{
    auto fonts = reinterpret_cast<std::vector<std::wstring>*>(lParam);
    if (!lf || !fonts) return 1;
    std::wstring name = lf->lfFaceName;
    if (name.empty() || name[0] == L'@') return 1; // 跳过竖排字体别名
    fonts->push_back(name);
    return 1;
}

static void EnsureSystemFontsLoaded()
{
    if (!g_systemFonts.empty()) return;

    HDC dc = GetDC(NULL);
    if (dc) {
        LOGFONTW lf = { 0 };
        lf.lfCharSet = DEFAULT_CHARSET;
        EnumFontFamiliesExW(dc, &lf, EnumSystemFontProc, (LPARAM)&g_systemFonts, 0);
        ReleaseDC(NULL, dc);
    }

    std::sort(g_systemFonts.begin(), g_systemFonts.end());
    g_systemFonts.erase(std::unique(g_systemFonts.begin(), g_systemFonts.end()), g_systemFonts.end());
    if (g_systemFonts.empty()) {
        g_systemFonts.push_back(L"Microsoft YaHei UI");
        g_systemFonts.push_back(L"Segoe UI");
        g_systemFonts.push_back(L"Arial");
    }
}

static void PickRandomAlertFont()
{
    EnsureSystemFontsLoaded();
    if (!g_systemFonts.empty()) {
        g_alertFontFamily = g_systemFonts[(size_t)RandInt(0, (int)g_systemFonts.size() - 1)];
    }
    g_alertNextFontChange = steady_clock::now() + milliseconds(RandInt(55, 130));
}

static std::wstring TrimCopy(const std::wstring& text)
{
    size_t first = 0;
    while (first < text.size() && iswspace((wint_t)text[first])) first++;
    size_t last = text.size();
    while (last > first && iswspace((wint_t)text[last - 1])) last--;
    return text.substr(first, last - first);
}

static bool TryParseUnsigned(const std::wstring& text, int& value)
{
    std::wstring s = TrimCopy(text);
    if (s.empty()) return false;
    int n = 0;
    for (wchar_t ch : s) {
        if (!iswdigit((wint_t)ch)) return false;
        n = n * 10 + (ch - L'0');
        if (n > 24 * 60 * 60) return false;
    }
    value = n;
    return true;
}

static std::vector<std::wstring> SplitWide(const std::wstring& text, wchar_t sep)
{
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= text.size()) {
        size_t pos = text.find(sep, start);
        if (pos == std::wstring::npos) {
            parts.push_back(text.substr(start));
            break;
        }
        parts.push_back(text.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

static std::wstring FormatDuration(int totalSeconds)
{
    if (totalSeconds < 0) totalSeconds = 0;
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int seconds = totalSeconds % 60;
    wchar_t buf[64];
    if (hours > 0) {
        swprintf(buf, 64, L"%d小时%02d分%02d秒", hours, minutes, seconds);
    } else if (minutes > 0) {
        swprintf(buf, 64, L"%d分%02d秒", minutes, seconds);
    } else {
        swprintf(buf, 64, L"%d秒", seconds);
    }
    return buf;
}

static std::wstring FormatTimePoint(system_clock::time_point tp)
{
    std::time_t t = system_clock::to_time_t(tp);
    std::tm tmv{};
    localtime_s(&tmv, &t);
    wchar_t buf[64];
    swprintf(buf, 64, L"%04d-%02d-%02d %02d:%02d:%02d",
        tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
        tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return buf;
}

static bool TryParseCountdownSeconds(const std::wstring& input, int& seconds)
{
    std::wstring s = TrimCopy(input);
    for (wchar_t& ch : s) {
        if (ch == L'：') ch = L':';
    }
    if (s.empty()) return false;

    std::vector<std::wstring> parts = SplitWide(s, L':');
    int total = 0;
    if (parts.size() == 1) {
        int minutes = 0;
        if (!TryParseUnsigned(parts[0], minutes) || minutes <= 0) return false;
        total = minutes * 60;
    } else if (parts.size() == 2 || parts.size() == 3) {
        int vals[3] = { 0, 0, 0 };
        int offset = (parts.size() == 2) ? 1 : 0;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (!TryParseUnsigned(parts[i], vals[offset + i])) return false;
        }
        if (vals[1] > 59 || vals[2] > 59) return false;
        total = vals[0] * 3600 + vals[1] * 60 + vals[2];
        if (total <= 0) return false;
    } else {
        return false;
    }
    seconds = total;
    return true;
}

static bool TryParseAlarmTime(const std::wstring& input, system_clock::time_point& due)
{
    std::wstring s = TrimCopy(input);
    for (wchar_t& ch : s) {
        if (ch == L'：') ch = L':';
    }
    std::vector<std::wstring> parts = SplitWide(s, L':');
    if (parts.size() != 2 && parts.size() != 3) return false;

    int hour = 0, minute = 0, second = 0;
    if (!TryParseUnsigned(parts[0], hour) ||
        !TryParseUnsigned(parts[1], minute) ||
        (parts.size() == 3 && !TryParseUnsigned(parts[2], second))) {
        return false;
    }
    if (hour > 23 || minute > 59 || second > 59) return false;

    std::time_t nowTime = std::time(nullptr);
    std::tm tmv{};
    localtime_s(&tmv, &nowTime);
    tmv.tm_hour = hour;
    tmv.tm_min = minute;
    tmv.tm_sec = second;
    std::time_t dueTime = std::mktime(&tmv);
    if (dueTime <= nowTime) dueTime += 24 * 60 * 60;
    due = system_clock::from_time_t(dueTime);
    return true;
}

#define IDC_PROMPT_EDIT 3201
#define IDC_PROMPT_OK 3202
#define IDC_PROMPT_CANCEL 3203

struct TextPromptState {
    std::wstring label;
    std::wstring initial;
    std::wstring output;
    bool accepted = false;
};

static LRESULT CALLBACK TextPromptProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    TextPromptState* state = (TextPromptState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        state = (TextPromptState*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HWND label = CreateWindowExW(0, L"STATIC", state->label.c_str(),
            WS_CHILD | WS_VISIBLE,
            18, 16, 300, 24, hwnd, NULL, GetModuleHandle(NULL), NULL);
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->initial.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            18, 46, 300, 26, hwnd, (HMENU)(INT_PTR)IDC_PROMPT_EDIT, GetModuleHandle(NULL), NULL);
        HWND ok = CreateWindowExW(0, L"BUTTON", L"确定",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            150, 88, 78, 28, hwnd, (HMENU)(INT_PTR)IDC_PROMPT_OK, GetModuleHandle(NULL), NULL);
        HWND cancel = CreateWindowExW(0, L"BUTTON", L"取消",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            240, 88, 78, 28, hwnd, (HMENU)(INT_PTR)IDC_PROMPT_CANCEL, GetModuleHandle(NULL), NULL);
        SendMessageW(label, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(edit, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(ok, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(cancel, WM_SETFONT, (WPARAM)font, TRUE);
        SetFocus(edit);
        SendMessageW(edit, EM_SETSEL, 0, -1);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_PROMPT_OK || LOWORD(wParam) == IDOK) {
            wchar_t buf[256] = { 0 };
            GetDlgItemTextW(hwnd, IDC_PROMPT_EDIT, buf, 256);
            state->output = buf;
            state->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == IDC_PROMPT_CANCEL || LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool ShowTextPrompt(HWND owner, const wchar_t* title, const wchar_t* label,
    const std::wstring& initial, std::wstring& output)
{
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc = TextPromptProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"DesktopTipTextPrompt";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        registered = true;
    }

    TextPromptState state;
    state.label = label;
    state.initial = initial;

    RECT wa;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
    int w = 340, h = 160;
    int x = wa.left + ((wa.right - wa.left) - w) / 2;
    int y = wa.top + ((wa.bottom - wa.top) - h) / 2;

    HWND dlg = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"DesktopTipTextPrompt", title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h, owner, NULL, GetModuleHandle(NULL), &state);
    if (!dlg) return false;

    if (owner) EnableWindow(owner, FALSE);
    MSG msg;
    BOOL gm = TRUE;
    while (IsWindow(dlg) && (gm = GetMessageW(&msg, NULL, 0, 0)) > 0) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    if (gm == 0) PostQuitMessage((int)msg.wParam);

    if (state.accepted) {
        output = state.output;
        return true;
    }
    return false;
}

static void InstallReminderKeyboardHook();
static void StopReminderAlert(HWND hwnd);
static void CaptureBlackHoleBackground(HWND hwnd);
static void ResetBlackHoleAlertState();
static void InitBlackHoleInstances(int W, int H);
static void EnsureBlackHoleWindowBounds(HWND hwnd);
static void RenderBlackHoleAlert(HWND hwnd);

static std::vector<std::wstring> BuildReminderAlertLines()
{
    std::vector<std::wstring> lines;
    lines.push_back(L"# " + g_alertTitle);
    if (!g_alertDetail.empty()) {
        lines.push_back(L"> " + g_alertDetail);
    }
    auto elapsed = duration_cast<seconds>(steady_clock::now() - g_alertStarted).count();
    lines.push_back(elapsed >= 10 ? L"- 按任意键停止提醒" : L"- 10 秒后按任意键停止");
    return lines;
}

static LRESULT CALLBACK ReminderKeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && g_alertActive &&
        (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        auto elapsed = duration_cast<milliseconds>(steady_clock::now() - g_alertStarted).count();
        if (elapsed >= 10000 && hWnd) {
            PostMessageW(hWnd, WM_REMINDER_STOP, 0, 0);
        }
    }
    return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
}

static void InstallReminderKeyboardHook()
{
    if (!g_keyboardHook) {
        g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, ReminderKeyboardProc,
            GetModuleHandle(NULL), 0);
    }
}

static void UninstallReminderKeyboardHook()
{
    if (g_keyboardHook) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = NULL;
    }
}

static double RandomSignedSpeed(double lo, double hi)
{
    double speed = RandDouble(lo, hi);
    return RandInt(0, 1) == 0 ? -speed : speed;
}

static void StartReminderAlert(HWND hwnd, const std::wstring& title, const std::wstring& detail)
{
    if (g_alertActive) return;
    g_alertActive = true;
    g_alertStarted = steady_clock::now();
    g_alertLastFrame = g_alertStarted;
    g_alertTitle = title;
    g_alertDetail = detail;
    GetWindowRect(hwnd, &g_alertSavedRect);
    g_alertSavedFontHeight = fontHeight;
    g_alertSavedLineHeight = lineHeight;
    g_alertSavedBgAlpha = g_bgAlpha;
    g_alertSavedTextColor = g_textColor;
    g_alertSavedBgColor = g_bgColor;

    // 先把原便签临时隐藏并截取桌面，作为黑洞 shader 的 iChannel0。
    // 这样透镜区域采样的是倒计时结束那一刻的桌面，而不是上一帧黑洞自身。
    CaptureBlackHoleBackground(hwnd);
    InitBlackHoleInstances(GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));

    if (GetParent(hwnd) != NULL) {
        g_reparenting = true;
        SetParent(hwnd, NULL);
        g_reparenting = false;
    }
    SetWindowLongPtr(hwnd, GWLP_HWNDPARENT, 0);

    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    style &= ~WS_CHILD;
    style |= WS_POPUP;
    SetWindowLongPtr(hwnd, GWL_STYLE, style);

    LONG_PTR ex = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    ex |= WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex);
    EnsureBlackHoleWindowBounds(hwnd);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);

    InstallReminderKeyboardHook();
    SetTimer(hwnd, TIMER_REMINDER_ANIMATE, 16, nullptr);
    RenderTip(hwnd);
}

static void AnimateReminderAlert(HWND hwnd)
{
    if (!g_alertActive) return;

    auto now = steady_clock::now();
    double dt = duration<double>(now - g_alertLastFrame).count();
    if (dt <= 0.0) dt = 0.016;
    if (dt > 0.10) dt = 0.10;
    g_alertLastFrame = now;

    EnsureBlackHoleWindowBounds(hwnd);
    RenderTip(hwnd);
}

static void StopReminderAlert(HWND hwnd)
{
    if (!g_alertActive) return;

    g_restoringAlert = true;
    KillTimer(hwnd, TIMER_REMINDER_ANIMATE);
    UninstallReminderKeyboardHook();

    fontHeight = g_alertSavedFontHeight;
    lineHeight = g_alertSavedLineHeight;
    g_bgAlpha = g_alertSavedBgAlpha;
    g_textColor = g_alertSavedTextColor;
    g_bgColor = g_alertSavedBgColor;
    g_alertFontFamily.clear();
    g_alertActive = false;

    if (GetParent(hwnd) != NULL) {
        g_reparenting = true;
        SetParent(hwnd, NULL);
        g_reparenting = false;
    }
    SetWindowLongPtr(hwnd, GWLP_HWNDPARENT, 0);
    LONG_PTR ex = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    ex |= WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOPMOST;
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex);
    SetWindowPos(hwnd, HWND_TOPMOST, g_alertSavedRect.left, g_alertSavedRect.top, 0, 0,
        SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    RenderTip(hwnd);

    ApplyDisplayMode(hwnd, g_displayMode);

    ResetBlackHoleAlertState();
    g_restoringAlert = false;
    confWrite();
}

static void ArmCountdown(HWND hwnd, int seconds)
{
    g_reminderArmed = true;
    g_reminderDue = system_clock::now() + std::chrono::seconds(seconds);
    g_reminderTitle = L"倒计时结束";
    g_reminderDetail = L"倒计时 " + FormatDuration(seconds) + L" 已到";
    g_reminderIsCountdown = true;
    g_reminderIntervalSeconds = seconds;
    std::wstring msg = L"倒计时已开始：" + FormatDuration(seconds);
    if (g_countdownRepeatEnabled) {
        msg += L"（重复计时已开启）";
    }
    MessageBoxW(hwnd, msg.c_str(), L"DesktopTip", MB_OK | MB_ICONINFORMATION);
}

static void ShowCountdownPrompt(HWND hwnd)
{
    std::wstring value;
    if (!ShowTextPrompt(hwnd, L"设置倒计时", L"输入分钟数，或 MM:SS / HH:MM:SS", L"10", value)) {
        return;
    }
    int seconds = 0;
    if (!TryParseCountdownSeconds(value, seconds)) {
        MessageBoxW(hwnd, L"请输入有效的倒计时时长。", L"DesktopTip", MB_OK | MB_ICONWARNING);
        return;
    }
    ArmCountdown(hwnd, seconds);
}

static void ShowAlarmPrompt(HWND hwnd)
{
    auto defaultTime = system_clock::now() + minutes(5);
    std::time_t t = system_clock::to_time_t(defaultTime);
    std::tm tmv{};
    localtime_s(&tmv, &t);
    wchar_t initial[16];
    swprintf(initial, 16, L"%02d:%02d", tmv.tm_hour, tmv.tm_min);

    std::wstring value;
    if (!ShowTextPrompt(hwnd, L"设置闹钟", L"输入闹钟时间 HH:MM 或 HH:MM:SS", initial, value)) {
        return;
    }
    system_clock::time_point due;
    if (!TryParseAlarmTime(value, due)) {
        MessageBoxW(hwnd, L"请输入有效的闹钟时间。", L"DesktopTip", MB_OK | MB_ICONWARNING);
        return;
    }

    g_reminderArmed = true;
    g_reminderDue = due;
    g_reminderTitle = L"闹钟时间到";
    g_reminderDetail = L"设定时间：" + FormatTimePoint(due);
    g_reminderIsCountdown = false;
    g_reminderIntervalSeconds = 0;
    std::wstring msg = L"闹钟已设置：" + FormatTimePoint(due);
    MessageBoxW(hwnd, msg.c_str(), L"DesktopTip", MB_OK | MB_ICONINFORMATION);
}

static void CheckReminder(HWND hwnd)
{
    if (!g_reminderArmed) return;

    auto now = system_clock::now();
    if (now < g_reminderDue) return;

    bool repeatCountdown = g_reminderIsCountdown && g_countdownRepeatEnabled && g_reminderIntervalSeconds > 0;
    int repeatSeconds = g_reminderIntervalSeconds;

    auto scheduleNextRepeatCountdown = [&](const system_clock::time_point& fromTime) {
        if (!repeatCountdown) return;
        auto nextDue = g_reminderDue + std::chrono::seconds(repeatSeconds);
        while (nextDue <= fromTime) {
            nextDue += std::chrono::seconds(repeatSeconds);
        }
        g_reminderArmed = true;
        g_reminderDue = nextDue;
        g_reminderTitle = L"倒计时结束";
        g_reminderDetail = L"重复倒计时 " + FormatDuration(repeatSeconds) + L" 已到";
        g_reminderIsCountdown = true;
        g_reminderIntervalSeconds = repeatSeconds;
    };

    // 重复倒计时命中时，如果黑洞动画尚未结束，则跳过本次提醒，直接进入下一轮计时。
    if (g_alertActive) {
        if (repeatCountdown) {
            scheduleNextRepeatCountdown(now);
        }
        return;
    }

    auto previousDue = g_reminderDue;
    std::wstring title = g_reminderTitle;
    std::wstring detail = g_reminderDetail;

    g_reminderArmed = false;
    StartReminderAlert(hwnd, title, detail);

    if (repeatCountdown) {
        auto nextDue = previousDue + std::chrono::seconds(repeatSeconds);
        while (nextDue <= now) {
            nextDue += std::chrono::seconds(repeatSeconds);
        }
        g_reminderArmed = true;
        g_reminderDue = nextDue;
        g_reminderTitle = L"倒计时结束";
        g_reminderDetail = L"重复倒计时 " + FormatDuration(repeatSeconds) + L" 已到";
        g_reminderIsCountdown = true;
        g_reminderIntervalSeconds = repeatSeconds;
    } else {
        g_reminderIsCountdown = false;
        g_reminderIntervalSeconds = 0;
    }
}

static std::wstring GetReminderStatusText()
{
    if (!g_reminderArmed) return L"";
    auto now = system_clock::now();
    int remaining = (int)duration_cast<seconds>(g_reminderDue - now).count();
    if (remaining < 0) remaining = 0;
    std::wstring status = L"已设置：" + g_reminderTitle + L"（剩余 " + FormatDuration(remaining) + L"）";
    if (g_reminderIsCountdown && g_countdownRepeatEnabled && g_reminderIntervalSeconds > 0) {
        status += L" · 重复";
    }
    return status;
}

// 右键菜单的消息处理
// ============================================================================
//  设置面板：现代化滑动条控制台（字号 / 行距 / 卡片透明度）
//  拖动滑块实时驱动 RenderTip —— 丝滑无级调节。
// ============================================================================
#define IDC_SL_FONT     3001
#define IDC_SL_SPACE    3002
#define IDC_SL_OPACITY  3003
#define IDC_BTN_TXTCOL  3011
#define IDC_BTN_CARDCOL 3012

#define SP_W    380
#define SP_H    392
#define SP_PAD  28
#define SP_TITLE 60

static HWND   g_hSettings  = NULL;
static HFONT  g_uiTitle    = NULL;
static HFONT  g_uiLabel    = NULL;
static HFONT  g_uiValue    = NULL;
static HBRUSH g_panelBrush = NULL;

void confWrite();
void RenderTip(HWND);
void ShowSettingsPanel(HWND);
void ChooseTextColor(HWND);
void ChooseCardColor(HWND);
void ChooseFontFamily(HWND);

struct SliderRow { const wchar_t* caption; int ctrlId; int y; };
static void SP_Rows(SliderRow rows[3]) {
    rows[0] = { L"字号  Font size",       IDC_SL_FONT,    SP_TITLE + 26 };
    rows[1] = { L"行距  Spacing",          IDC_SL_SPACE,   SP_TITLE + 104 };
    rows[2] = { L"卡片透明度  Opacity",    IDC_SL_OPACITY, SP_TITLE + 182 };
}

static LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_panelBrush = CreateSolidBrush(RGB(26, 31, 42));
        g_uiTitle = CreateFontW(-21, 0,0,0, FW_SEMIBOLD, 0,0,0, DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0, L"Microsoft YaHei UI");
        g_uiLabel = CreateFontW(-15, 0,0,0, FW_NORMAL,   0,0,0, DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0, L"Microsoft YaHei UI");
        g_uiValue = CreateFontW(-17, 0,0,0, FW_SEMIBOLD, 0,0,0, DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0, L"Microsoft YaHei UI");

        SliderRow rows[3]; SP_Rows(rows);
        int sx = SP_PAD, sw = SP_W - SP_PAD*2;
        int lo[3] = {10, 0, 0}, hi[3] = {60, 90, 255};
        int vals[3] = { fontHeight, lineHeight, g_bgAlpha };
        for (int i=0;i<3;i++) {
            HWND tb = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                WS_CHILD|WS_VISIBLE|TBS_HORZ|TBS_NOTICKS,
                sx, rows[i].y + 28, sw, 32, hwnd, (HMENU)(INT_PTR)rows[i].ctrlId, GetModuleHandle(NULL), NULL);
            SendMessageW(tb, TBM_SETRANGE, TRUE, MAKELONG(lo[i], hi[i]));
            int v = vals[i]; if (v<lo[i]) v=lo[i]; if (v>hi[i]) v=hi[i];
            SendMessageW(tb, TBM_SETPOS, TRUE, v);
            SendMessageW(tb, TBM_SETPAGESIZE, 0, 4);
        }
        int by = SP_TITLE + 264, bw = (sw - 16) / 2;
        CreateWindowExW(0, L"BUTTON", L"", WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            sx, by, bw, 44, hwnd, (HMENU)(INT_PTR)IDC_BTN_TXTCOL, GetModuleHandle(NULL), NULL);
        CreateWindowExW(0, L"BUTTON", L"", WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            sx+bw+16, by, bw, 44, hwnd, (HMENU)(INT_PTR)IDC_BTN_CARDCOL, GetModuleHandle(NULL), NULL);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = (HDC)wParam; SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(230,233,240));
        return (LRESULT)g_panelBrush;
    }
    case WM_HSCROLL: {
        HWND tb = (HWND)lParam; if (!tb) break;
        int id = GetDlgCtrlID(tb);
        int pos = (int)SendMessageW(tb, TBM_GETPOS, 0, 0);
        if (id == IDC_SL_FONT)         fontHeight = pos;
        else if (id == IDC_SL_SPACE)   lineHeight = pos;
        else if (id == IDC_SL_OPACITY) g_bgAlpha  = pos;
        RenderTip(::hWnd);
        InvalidateRect(hwnd, NULL, FALSE);
        int code = LOWORD(wParam);
        if (code == TB_ENDTRACK || code == SB_ENDSCROLL) confWrite();
        return 0;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)lParam;
        bool isTxt = (d->CtlID == IDC_BTN_TXTCOL);
        COLORREF swatch = isTxt ? g_textColor : g_bgColor;
        RECT r = d->rcItem;
        FillRect(d->hDC, &r, g_panelBrush);
        HBRUSH fill = CreateSolidBrush(RGB(36,42,55));
        HPEN pen = CreatePen(PS_SOLID, 1, (d->itemState & ODS_SELECTED) ? RGB(118,128,146) : RGB(58,66,82));
        HGDIOBJ ob = SelectObject(d->hDC, fill), op = SelectObject(d->hDC, pen);
        RoundRect(d->hDC, r.left, r.top, r.right, r.bottom, 12, 12);
        HBRUSH swb = CreateSolidBrush(swatch);
        HPEN swp = CreatePen(PS_SOLID, 1, RGB(86,94,110));
        SelectObject(d->hDC, swb); SelectObject(d->hDC, swp);
        RoundRect(d->hDC, r.left+13, r.top+12, r.left+13+20, r.top+12+20, 6, 6);
        SetBkMode(d->hDC, TRANSPARENT); SetTextColor(d->hDC, RGB(230,233,240));
        HGDIOBJ of = SelectObject(d->hDC, g_uiLabel);
        RECT tr = { r.left+45, r.top, r.right-8, r.bottom };
        DrawTextW(d->hDC, isTxt ? L"文字颜色" : L"卡片颜色", -1, &tr, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        SelectObject(d->hDC, of); SelectObject(d->hDC, ob); SelectObject(d->hDC, op);
        DeleteObject(fill); DeleteObject(pen); DeleteObject(swb); DeleteObject(swp);
        return TRUE;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect(dc, &rc, g_panelBrush);
        HPEN bp = CreatePen(PS_SOLID, 1, RGB(56,64,80));
        HGDIOBJ ob = SelectObject(dc, bp), obr = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, rc.left, rc.top, rc.right-1, rc.bottom-1, 18, 18);
        SelectObject(dc, obr); SelectObject(dc, ob); DeleteObject(bp);
        SetBkMode(dc, TRANSPARENT);
        SelectObject(dc, g_uiTitle); SetTextColor(dc, RGB(244,245,248));
        TextOutW(dc, SP_PAD, 19, L"DesktopTip 设置", 13);
        SetTextColor(dc, RGB(150,158,172));
        RECT xr = { SP_W-48, 15, SP_W-18, 45 };
        DrawTextW(dc, L"\u2715", -1, &xr, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        HPEN lp = CreatePen(PS_SOLID, 1, RGB(44,51,66));
        HGDIOBJ ol = SelectObject(dc, lp);
        MoveToEx(dc, SP_PAD, SP_TITLE-1, NULL); LineTo(dc, SP_W-SP_PAD, SP_TITLE-1);
        SelectObject(dc, ol); DeleteObject(lp);
        SliderRow rows[3]; SP_Rows(rows);
        int sx = SP_PAD, sw = SP_W - SP_PAD*2;
        int vals[3] = { fontHeight, lineHeight, (int)((long)g_bgAlpha*100/255) };
        const wchar_t* unit[3] = { L" px", L" px", L"%" };
        wchar_t buf[40];
        for (int i=0;i<3;i++) {
            SelectObject(dc, g_uiLabel); SetTextColor(dc, RGB(150,158,172));
            TextOutW(dc, sx, rows[i].y, rows[i].caption, (int)wcslen(rows[i].caption));
            SelectObject(dc, g_uiValue); SetTextColor(dc, RGB(232,176,150));
            swprintf(buf, 40, L"%d%s", vals[i], unit[i]);
            RECT vr = { sx, rows[i].y-2, sx+sw, rows[i].y+24 };
            DrawTextW(dc, buf, -1, &vr, DT_RIGHT|DT_TOP|DT_SINGLELINE);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCHITTEST: {
        int x = (short)LOWORD(lParam), y = (short)HIWORD(lParam);
        POINT p = { x, y }; ScreenToClient(hwnd, &p);
        if (p.y < SP_TITLE && p.x < SP_W-52) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(lParam), y = (short)HIWORD(lParam);
        if (x >= SP_W-50 && x <= SP_W-14 && y >= 12 && y <= 48) { DestroyWindow(hwnd); return 0; }
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_BTN_TXTCOL)  { ChooseTextColor(::hWnd); InvalidateRect(hwnd, NULL, FALSE); }
        else if (id == IDC_BTN_CARDCOL) { ChooseCardColor(::hWnd); InvalidateRect(hwnd, NULL, FALSE); }
        return 0;
    }
    case WM_DESTROY:
        if (g_panelBrush) { DeleteObject(g_panelBrush); g_panelBrush = NULL; }
        if (g_uiTitle)    { DeleteObject(g_uiTitle);    g_uiTitle = NULL; }
        if (g_uiLabel)    { DeleteObject(g_uiLabel);    g_uiLabel = NULL; }
        if (g_uiValue)    { DeleteObject(g_uiValue);    g_uiValue = NULL; }
        g_hSettings = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowSettingsPanel(HWND owner) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc = SettingsProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"DesktopTipSettings";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        RegisterClassW(&wc);
        reg = true;
    }
    if (g_hSettings && IsWindow(g_hSettings)) {
        SetForegroundWindow(g_hSettings);
        return;
    }
    RECT wa; SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
    POINT anchor = g_hasLastContextMenuPt ? g_lastContextMenuPt : POINT{ wa.right - SP_W - 24, wa.bottom - SP_H - 24 };
    int px = anchor.x;
    int py = anchor.y;
    if (px + SP_W > wa.right) px = wa.right - SP_W;
    if (py + SP_H > wa.bottom) py = wa.bottom - SP_H;
    if (px < wa.left) px = wa.left;
    if (py < wa.top) py = wa.top;
    g_hSettings = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"DesktopTipSettings", L"DesktopTip 设置",
        WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
        px, py, SP_W, SP_H, NULL, NULL, GetModuleHandle(NULL), NULL);
    HRGN rgn = CreateRoundRectRgn(0, 0, SP_W+1, SP_H+1, 20, 20);
    SetWindowRgn(g_hSettings, rgn, TRUE);
    SetForegroundWindow(g_hSettings);
}

void ShowContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    g_lastContextMenuPt = pt;
    g_hasLastContextMenuPt = true;

    HMENU hMenu = CreatePopupMenu();
    // 主入口：打开滑动条设置面板（粗体默认项）
    AppendMenuW(hMenu, MF_STRING, ID_SETTINGS,     L"设置…  Settings");
    std::wstring fontLabel = L"字体：" + (g_fontFamily.empty() ? std::wstring(L"默认") : g_fontFamily) + L"…  Font";
    AppendMenuW(hMenu, MF_STRING, ID_FONT_SELECT, fontLabel.c_str());
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    HMENU hCountdownMenu = CreatePopupMenu();
    AppendMenuW(hCountdownMenu, MF_STRING, ID_COUNTDOWN_1MIN, L"1 分钟");
    AppendMenuW(hCountdownMenu, MF_STRING, ID_COUNTDOWN_5MIN, L"5 分钟");
    AppendMenuW(hCountdownMenu, MF_STRING, ID_COUNTDOWN_10MIN, L"10 分钟");
    AppendMenuW(hCountdownMenu, MF_STRING, ID_COUNTDOWN_1HOUR, L"1 小时");
    AppendMenuW(hCountdownMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hCountdownMenu, MF_STRING | (g_countdownRepeatEnabled ? MF_CHECKED : MF_UNCHECKED),
        ID_COUNTDOWN_REPEAT, L"重复计时  Repeat");
    AppendMenuW(hCountdownMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hCountdownMenu, MF_STRING, ID_COUNTDOWN_CUSTOM, L"自定义…  Custom");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hCountdownMenu, L"倒计时  Countdown");
    AppendMenuW(hMenu, MF_STRING, ID_ALARM_CUSTOM, L"设置闹钟…  Alarm");
    if (g_reminderArmed) {
        std::wstring status = GetReminderStatusText();
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, status.c_str());
        if (!g_alertActive) {
            AppendMenuW(hMenu, MF_STRING, ID_REMINDER_CANCEL, L"取消倒计时/闹钟  Cancel");
        }
    }
    if (g_alertActive) {
        AppendMenuW(hMenu, MF_STRING, ID_REMINDER_CANCEL, L"停止提醒  Stop alert");
    }
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    HMENU hModeMenu = CreatePopupMenu();
    AppendMenuW(hModeMenu, MF_STRING | (g_displayMode == 0 ? MF_CHECKED : MF_UNCHECKED),
        ID_MODE_DESKTOP, L"附着桌面（Win+D 不最小化）  Attach to desktop");
    AppendMenuW(hModeMenu, MF_STRING | (g_displayMode == 1 ? MF_CHECKED : MF_UNCHECKED),
        ID_MODE_TOPMOST, L"始终置顶（Win+D 不影响）  Always on top");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hModeMenu, L"显示模式  Display mode");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EDIT,    L"编辑 TODO  Edit");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_REFRESH, L"刷新  Refresh");
    AppendMenuW(hMenu, MF_STRING, ID_TODO_CHANGE,  L"更换 TODO 文件  Change file");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT,    L"退出  Exit");
    SetMenuDefaultItem(hMenu, ID_SETTINGS, FALSE);
  
    // 显示菜单
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTBUTTON, pt.x, pt.y, 0, hwnd, NULL);

    DestroyMenu(hMenu);
}

// 这个函数用于创建托盘图标
void AddTrayIcon(HWND hwnd) {

    NOTIFYICONDATA nid = { 0 };
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TASKBAR;
    //nid.hIcon = (HICON)LoadImage(NULL, "Tip.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE);
    nid.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_DESKTOPTIP));
    lstrcpy(nid.szTip, "Tips");  // 托盘图标提示信息

    Shell_NotifyIcon(NIM_ADD, &nid);
}

void confWrite();
void ChooseTextColor(HWND hwnd) {
    CHOOSECOLOR cc = { 0 };
    static COLORREF customColors[16] = { 0 };
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hwnd;
    cc.lpCustColors = customColors;
    cc.rgbResult = g_textColor;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;

    if (ChooseColor(&cc)) {
        g_textColor = cc.rgbResult;
        confWrite();  // 保存新的颜色设置
        RenderTip(hwnd);
    }
}

void ChooseCardColor(HWND hwnd) {
    CHOOSECOLOR cc = { 0 };
    static COLORREF customColors[16] = { 0 };
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hwnd;
    cc.lpCustColors = customColors;
    cc.rgbResult = g_bgColor;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColor(&cc)) {
        g_bgColor = cc.rgbResult;
        confWrite();
        RenderTip(hwnd);
    }
}

void ChooseFontFamily(HWND hwnd)
{
    LOGFONTW lf = { 0 };
    std::wstring face = g_fontFamily.empty() ? L"Microsoft YaHei UI" : g_fontFamily;
    wcsncpy_s(lf.lfFaceName, face.c_str(), _TRUNCATE);
    lf.lfHeight = -(fontHeight > 8 ? fontHeight : 18);
    lf.lfCharSet = DEFAULT_CHARSET;

    CHOOSEFONTW cf = { 0 };
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = hwnd;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_FORCEFONTEXIST |
        CF_NOSIZESEL | CF_NOSTYLESEL | CF_NOVERTFONTS;

    if (ChooseFontW(&cf)) {
        if (lf.lfFaceName[0] != L'\0') {
            g_fontFamily = lf.lfFaceName;
            EnsureSystemFontsLoaded();
            confWrite();
            RenderTip(hwnd);
        }
    }
}

static std::vector<std::wstring> ReadLinesFromFileW()
{
    std::vector<std::wstring> lines;

    std::ifstream file(TODO_PATH, std::ios::binary);
    if (!file.is_open()) {
        lines.push_back(L"Error: Unable to open file!");
        return lines;
    }

    std::string bytes((std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());

    bytes = ProcessPlaceholders(bytes);

    std::wstring w = DecodeAutoToUtf16(bytes);

    // 统一换行：把 \r\n 或 \r 转成 \n
    for (size_t i = 0; i < w.size(); ++i) {
        if (w[i] == L'\r') w[i] = L'\n';
    }

    // 按 \n 分割
    size_t start = 0;
    while (start <= w.size()) {
        size_t pos = w.find(L'\n', start);
        if (pos == std::wstring::npos) pos = w.size();
        std::wstring one = w.substr(start, pos - start);
        if (!one.empty() && one.back() == L'\n') one.pop_back();
        lines.push_back(one);
        start = pos + 1;
        if (pos == w.size()) break;
    }

    return lines;
}


// 获取当前窗口位置
void getCurrentWindowPosition(int& x, int& y) {
    RECT rect;
    if (GetWindowRect(hWnd, &rect)) {
        x = rect.left;
        y = rect.top;
    }
    else {
        x = y = 0;
    }
}

// 配置文件位置
static const std::string CONFIG_PATH = "C:\\ProgramData\\DesktopTip\\config";
static const std::string BASE_PATH = "C:\\PROGRAMDATA\\Desktoptip";

void confWrite() {
    if (g_alertActive || g_restoringAlert) return;
    int x, y;
    getCurrentWindowPosition(x, y);
    //if (x == 0 && y == 0)
    //    return;
    std::ofstream ofs(CONFIG_PATH);
    ofs << "Window_X: " << x << std::endl;
    ofs << "Window_Y: " << y << std::endl;
    ofs << "TODO_path: " << TODO_PATH << std::endl;
    ofs << "FontHeight: " << fontHeight << std::endl;
    ofs << "LineHeight: " << lineHeight << std::endl;

    // 保存文本颜色的 R、G、B 分量
    ofs << "TextColor_R: " << (int)GetRValue(g_textColor) << std::endl;
    ofs << "TextColor_G: " << (int)GetGValue(g_textColor) << std::endl;
    ofs << "TextColor_B: " << (int)GetBValue(g_textColor) << std::endl;
    ofs << "BgAlpha: " << g_bgAlpha << std::endl;
    ofs << "BgColor_R: " << (int)GetRValue(g_bgColor) << std::endl;
    ofs << "BgColor_G: " << (int)GetGValue(g_bgColor) << std::endl;
    ofs << "BgColor_B: " << (int)GetBValue(g_bgColor) << std::endl;
    ofs << "FontFamily: " << WideToUtf8(g_fontFamily) << std::endl;
    ofs << "DisplayMode: " << g_displayMode << std::endl;
    ofs << "CountdownRepeat: " << (g_countdownRepeatEnabled ? 1 : 0) << std::endl;
}

// 简单的窗口过程，用于绘制文本
std::string OpenFileDialog();

// ============================================================================
//  现代化渲染：GDI+ 逐像素 Alpha 分层窗口 (UpdateLayeredWindow)
//  支持半透明磨砂卡片、柔和阴影、发丝描边、抗锯齿微软雅黑文字。
//  行角色：'# 标题(大字时钟)  > 次要(灰字日期)  - 待办(圆点)  空行=间隔'
// ============================================================================
static Gdiplus::FontFamily* PickFamily(std::initializer_list<const wchar_t*> names) {
    for (auto n : names) {
        if (!n || !*n) continue;
        Gdiplus::FontFamily* ff = new Gdiplus::FontFamily(n);
        if (ff->IsAvailable()) return ff;
        delete ff;
    }
    return new Gdiplus::FontFamily(L"Microsoft Sans Serif");
}

static void RoundRectPath(Gdiplus::GraphicsPath& p,
    Gdiplus::REAL x, Gdiplus::REAL y, Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL r) {
    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    Gdiplus::REAL d = 2 * r;
    p.Reset();
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}


// ============================================================================
//  Blackhole alert renderer
//  CPU/GDI+ port of the ghostty-blackhole shader structure:
//  - captured desktop as iChannel0 equivalent
//  - Schwarzschild-style shadow / analytic lensing displacement
//  - procedural accretion disk, photon ring, starfield, Lissajous drift
// ============================================================================
struct BhVec2 { double x, y; };
struct BhVec3 { double x, y, z; };
struct BhColor { double r, g, b; };

static const double BH_PI = 3.1415926535897932384626433832795;
static const double BH_B_CRIT = 2.5980762;

static double BhClamp(double v, double lo, double hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static double BhMin(double a, double b)
{
    return a < b ? a : b;
}

static double BhMax(double a, double b)
{
    return a > b ? a : b;
}

static double BhMix(double a, double b, double t)
{
    return a + (b - a) * t;
}

static double BhSmoothstep(double edge0, double edge1, double x)
{
    if (edge0 == edge1) return x < edge0 ? 0.0 : 1.0;
    double t = BhClamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

static double BhSmootherstep01(double t)
{
    t = BhClamp(t, 0.0, 1.0);
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

static double BhFract(double x)
{
    return x - std::floor(x);
}

static double BhMod(double x, double y)
{
    return x - y * std::floor(x / y);
}

static BhVec2 BhAdd(BhVec2 a, BhVec2 b) { return { a.x + b.x, a.y + b.y }; }
static BhVec2 BhSub(BhVec2 a, BhVec2 b) { return { a.x - b.x, a.y - b.y }; }
static BhVec2 BhMul(BhVec2 a, double s) { return { a.x * s, a.y * s }; }
static BhVec3 BhAdd3(BhVec3 a, BhVec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
static BhVec3 BhMul3(BhVec3 a, double s) { return { a.x * s, a.y * s, a.z * s }; }
static BhColor BhColorAdd(BhColor a, BhColor b) { return { a.r + b.r, a.g + b.g, a.b + b.b }; }
static BhColor BhColorMul(BhColor a, double s) { return { a.r * s, a.g * s, a.b * s }; }
static BhColor BhColorMix(BhColor a, BhColor b, double t)
{
    return { BhMix(a.r, b.r, t), BhMix(a.g, b.g, t), BhMix(a.b, b.b, t) };
}

static double BhDot(BhVec2 a, BhVec2 b) { return a.x * b.x + a.y * b.y; }
static double BhDot3(BhVec3 a, BhVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static double BhLen(BhVec2 v) { return std::sqrt(BhDot(v, v)); }
static double BhLen3(BhVec3 v) { return std::sqrt(BhDot3(v, v)); }

static BhVec2 BhNormalize(BhVec2 v)
{
    double l = BhLen(v);
    if (l < 1e-9) return { 0.0, 0.0 };
    return { v.x / l, v.y / l };
}

static BhVec3 BhNormalize3(BhVec3 v)
{
    double l = BhLen3(v);
    if (l < 1e-9) return { 0.0, 0.0, -1.0 };
    return { v.x / l, v.y / l, v.z / l };
}

static BhVec2 BhRot(BhVec2 v, double a)
{
    double c = std::cos(a), s = std::sin(a);
    return { c * v.x - s * v.y, s * v.x + c * v.y };
}

static double BhHash21(BhVec2 p)
{
    p.x = BhFract(p.x * 234.34);
    p.y = BhFract(p.y * 435.345);
    double d = p.x * (p.x + 34.23) + p.y * (p.y + 34.23);
    p.x += d;
    p.y += d;
    return BhFract(p.x * p.y);
}

static double BhVNoiseWrapY(BhVec2 p, double perY)
{
    BhVec2 i = { std::floor(p.x), std::floor(p.y) };
    BhVec2 f = { BhFract(p.x), BhFract(p.y) };
    f.x = f.x * f.x * (3.0 - 2.0 * f.x);
    f.y = f.y * f.y * (3.0 - 2.0 * f.y);
    double y0 = BhMod(i.y, perY);
    double y1 = BhMod(i.y + 1.0, perY);
    double a = BhMix(BhHash21({ i.x, y0 }), BhHash21({ i.x + 1.0, y0 }), f.x);
    double b = BhMix(BhHash21({ i.x, y1 }), BhHash21({ i.x + 1.0, y1 }), f.x);
    return BhMix(a, b, f.y);
}

static BhVec2 BhLissa(double t)
{
    return {
        0.75 * std::sin(t * 0.37) + 0.25 * std::sin(t * 0.83 + 1.0),
        0.70 * std::sin(t * 0.54 + 2.1) + 0.30 * std::sin(t * 1.07)
    };
}

static BhColor BhBlackbody(double T)
{
    double t = BhClamp(T, 1500.0, 40000.0) / 100.0;
    double r = t <= 66.0 ? 1.0 : BhClamp(1.292936 * std::pow(t - 60.0, -0.1332047), 0.0, 1.0);
    double g = t <= 66.0 ? BhClamp(0.3900816 * std::log(t) - 0.6318414, 0.0, 1.0)
                         : BhClamp(1.1298909 * std::pow(t - 60.0, -0.0755148), 0.0, 1.0);
    double b = t >= 66.0 ? 1.0 : (t <= 19.0 ? 0.0 : BhClamp(0.5432068 * std::log(t - 10.0) - 1.1962540, 0.0, 1.0));
    return { r, g, b };
}

static BhColor BhStars(BhVec3 d, double time)
{
    double ax = std::atan2(d.x, -d.z);
    double ay = std::asin(BhClamp(d.y, -1.0, 1.0));
    BhVec2 g = { ax * 40.0, ay * 40.0 };
    BhVec2 id = { std::floor(g.x), std::floor(g.y) };
    double h = BhHash21(id);
    if (h < 0.92) return { 0.0, 0.0, 0.0 };
    BhVec2 f = { BhFract(g.x) - 0.5, BhFract(g.y) - 0.5 };
    BhVec2 off = { (BhHash21({ id.x + 17.3, id.y + 17.3 }) - 0.5) * 0.7,
                   (BhHash21({ id.x + 31.7, id.y + 31.7 }) - 0.5) * 0.7 };
    double spark = BhSmoothstep(0.10, 0.0, BhLen(BhSub(f, off)));
    double tw = 0.7 + 0.3 * std::sin(time * (0.5 + 2.0 * BhHash21({ id.x + 5.1, id.y + 5.1 })) + 40.0 * h);
    double tintK = BhHash21({ id.x + 2.9, id.y + 2.9 });
    BhColor warm = { 1.0, 0.82, 0.60 };
    BhColor cool = { 0.75, 0.85, 1.0 };
    return BhColorMul(BhColorMix(warm, cool, tintK), spark * tw * ((h - 0.92) / 0.08));
}

static double BhMirror01(double u)
{
    return 1.0 - std::fabs(1.0 - BhMod(u, 2.0));
}

static BhColor BhSampleBackground(double u, double v)
{
    if (!g_blackholeBgReady || g_blackholeBg.empty() || g_blackholeW <= 0 || g_blackholeH <= 0) {
        return { 0.0, 0.0, 0.0 };
    }
    u = BhMirror01(u);
    v = BhMirror01(v);
    double sx = BhClamp(u * (g_blackholeW - 1), 0.0, (double)(g_blackholeW - 1));
    double sy = BhClamp(v * (g_blackholeH - 1), 0.0, (double)(g_blackholeH - 1));
    int x0 = (int)std::floor(sx);
    int y0 = (int)std::floor(sy);
    int x1 = (x0 + 1 < g_blackholeW) ? x0 + 1 : x0;
    int y1 = (y0 + 1 < g_blackholeH) ? y0 + 1 : y0;
    double fx = sx - x0;
    double fy = sy - y0;

    auto sample = [&](int x, int y) -> BhColor {
        const BYTE* px = &g_blackholeBg[((size_t)y * (size_t)g_blackholeW + (size_t)x) * 4];
        return { px[2] / 255.0, px[1] / 255.0, px[0] / 255.0 };
    };

    BhColor c00 = sample(x0, y0);
    BhColor c10 = sample(x1, y0);
    BhColor c01 = sample(x0, y1);
    BhColor c11 = sample(x1, y1);
    BhColor cx0 = BhColorMix(c00, c10, fx);
    BhColor cx1 = BhColorMix(c01, c11, fx);
    return BhColorMix(cx0, cx1, fy);
}

static int BhByte(double v)
{
    return (int)BhClamp(v * 255.0 + 0.5, 0.0, 255.0);
}

static double BlackHoleEffectRadius(double rh)
{
    const double rin = 1.8;
    const double rout = 8.0;
    return BhMin(0.54, BhMax(4.18 * rh, (rout / BH_B_CRIT) * rh * 1.14));
}

static double BlackHoleCurrentRh(const BlackHoleInstance& hole, double elapsed)
{
    double wave = 0.5 + 0.5 * std::sin((elapsed / BhMax(hole.period, 0.25)) * 2.0 * BH_PI + hole.phase);
    double eased = BhSmootherstep01(wave);
    return BhMix(hole.minRh, hole.maxRh, eased);
}

static double BhBounceCoord(double value, double velocity, double elapsed, double lo, double hi)
{
    if (hi <= lo) return (lo + hi) * 0.5;
    double span = hi - lo;
    double local = (value - lo) + velocity * elapsed;
    return lo + BhMirror01(local / span) * span;
}

static BhVec2 BlackHoleCurrentCenter(const BlackHoleInstance& hole, double elapsed, double aspect)
{
    double marginY = BhClamp(hole.maxEffectR + 0.018, 0.060, 0.46);
    double marginX = BhClamp(hole.maxEffectR / BhMax(aspect, 0.1) + 0.018, 0.060, 0.46);
    return {
        BhBounceCoord(hole.cx, hole.moveVx, elapsed, marginX, 1.0 - marginX),
        BhBounceCoord(hole.cy, hole.moveVy, elapsed, marginY, 1.0 - marginY)
    };
}

static BlackHoleInstance MakeBlackHoleInstance(double cx, double cy, double maxRh, int count = 3)
{
    BlackHoleInstance hole;
    hole.cx = cx;
    hole.cy = cy;
    {
        double angle = RandDouble(0.0, 2.0 * BH_PI);
        for (int i = 0; i < 8; ++i) {
            if (std::fabs(std::cos(angle)) >= 0.30 && std::fabs(std::sin(angle)) >= 0.30) break;
            angle = RandDouble(0.0, 2.0 * BH_PI);
        }
        double speed = BhClamp(0.0072 / BhMax(maxRh, 0.01), 0.075, 0.22);
        hole.moveVx = std::cos(angle) * speed;
        hole.moveVy = std::sin(angle) * speed;
    }
    if (count <= 2) {
        hole.moveAmpX = RandDouble(0.095, 0.160);
        hole.moveAmpY = RandDouble(0.075, 0.130);
    } else if (count == 3) {
        hole.moveAmpX = RandDouble(0.075, 0.125);
        hole.moveAmpY = RandDouble(0.060, 0.105);
    } else if (count == 4) {
        hole.moveAmpX = RandDouble(0.055, 0.095);
        hole.moveAmpY = RandDouble(0.045, 0.080);
    } else {
        hole.moveAmpX = RandDouble(0.040, 0.072);
        hole.moveAmpY = RandDouble(0.035, 0.065);
    }
    hole.movePeriodX = RandDouble(10.0, 18.0);
    hole.movePeriodY = RandDouble(12.0, 22.0);
    hole.movePhaseX = RandDouble(0.0, 2.0 * BH_PI);
    hole.movePhaseY = RandDouble(0.0, 2.0 * BH_PI);
    hole.maxRh = maxRh;
    hole.minRh = maxRh * RandDouble(0.38, 0.56);
    hole.maxEffectR = BlackHoleEffectRadius(hole.maxRh);
    hole.period = RandDouble(12.0, 26.0);
    hole.phase = RandDouble(0.0, 2.0 * BH_PI);
    hole.roll = RandDouble(-0.85, 0.85);
    hole.incl = RandDouble(1.16, 1.68);
    hole.diskSpeed = RandDouble(3.4, 6.8);
    hole.seed = RandDouble(0.0, 1000.0);
    return hole;
}

static bool BlackHoleOverlapsAny(const BlackHoleInstance& candidate,
    const std::vector<BlackHoleInstance>& holes, double aspect)
{
    auto visibleR = [](const BlackHoleInstance& hole) {
        return hole.maxRh * 1.85;
    };

    for (const auto& other : holes) {
        double dx = (candidate.cx - other.cx) * aspect;
        double dy = candidate.cy - other.cy;
        double d = std::sqrt(dx * dx + dy * dy);
        double moveX = (candidate.moveAmpX + other.moveAmpX) * aspect;
        double moveY = candidate.moveAmpY + other.moveAmpY;
        double movePad = std::sqrt(moveX * moveX + moveY * moveY);
        double minD = visibleR(candidate) + visibleR(other) + movePad * 0.35 + 0.025;
        if (d < minD) return true;
    }
    return false;
}

static double BlackHoleSizeCapForCount(int count)
{
    if (count <= 2) return 0.064;
    return 0.054;
}

static void InitBlackHoleInstancesGrid(int count, double aspect)
{
    std::vector<BhVec2> cells = {
        { 0.20, 0.25 }, { 0.50, 0.23 }, { 0.80, 0.26 },
        { 0.25, 0.72 }, { 0.57, 0.74 }, { 0.84, 0.70 }
    };
    std::shuffle(cells.begin(), cells.end(), g_rng);

    g_blackHoles.clear();
    double cap = BlackHoleSizeCapForCount(count);
    for (int i = 0; i < count && i < (int)cells.size(); ++i) {
        double maxRh = RandDouble(cap * 0.72, cap);
        BlackHoleInstance hole = MakeBlackHoleInstance(cells[(size_t)i].x, cells[(size_t)i].y, maxRh, count);
        double marginY = BhClamp(hole.maxEffectR + 0.018, 0.060, 0.46);
        double marginX = BhClamp(hole.maxEffectR / BhMax(aspect, 0.1) + 0.018, 0.060, 0.46);
        hole.cx = BhClamp(hole.cx + RandDouble(-0.045, 0.045), marginX, 1.0 - marginX);
        hole.cy = BhClamp(hole.cy + RandDouble(-0.040, 0.040), marginY, 1.0 - marginY);
        g_blackHoles.push_back(hole);
    }
}

static void InitBlackHoleInstances(int W, int H)
{
    g_blackHoles.clear();
    if (W <= 0 || H <= 0) return;

    int count = 1;
    double aspect = (double)W / (double)H;
    std::vector<BlackHoleInstance> holes;
    holes.reserve((size_t)count);

    double cap = BlackHoleSizeCapForCount(count);
    for (int i = 0; i < count; ++i) {
        bool placed = false;
        for (int attempt = 0; attempt < 220; ++attempt) {
            double shrink = attempt < 140 ? 1.0 : BhMix(0.96, 0.70, (attempt - 140) / 80.0);
            double maxRh = RandDouble(cap * 0.64 * shrink, cap * shrink);
            BlackHoleInstance hole = MakeBlackHoleInstance(0.5, 0.5, maxRh, count);
            double marginY = BhClamp(hole.maxEffectR + 0.018, 0.060, 0.46);
            double marginX = BhClamp(hole.maxEffectR / BhMax(aspect, 0.1) + 0.018, 0.060, 0.46);
            if (marginX >= 0.49 || marginY >= 0.49) continue;

            hole.cx = RandDouble(marginX, 1.0 - marginX);
            hole.cy = RandDouble(marginY, 1.0 - marginY);
            if (!BlackHoleOverlapsAny(hole, holes, aspect)) {
                holes.push_back(hole);
                placed = true;
                break;
            }
        }

        if (!placed) {
            InitBlackHoleInstancesGrid(count, aspect);
            return;
        }
    }

    g_blackHoles = holes;
}

static RECT BlackHoleDirtyRect(BhVec2 center, double rh, int W, int H, double aspect)
{
    double effectR = BlackHoleEffectRadius(rh);
    int x0 = (int)BhClamp(std::floor((center.x - effectR / aspect) * W) - 8.0, 0.0, (double)(W - 1));
    int x1 = (int)BhClamp(std::ceil ((center.x + effectR / aspect) * W) + 8.0, 0.0, (double)(W - 1));
    int y0 = (int)BhClamp(std::floor((center.y - effectR) * H) - 8.0, 0.0, (double)(H - 1));
    int y1 = (int)BhClamp(std::ceil ((center.y + effectR) * H) + 8.0, 0.0, (double)(H - 1));
    RECT r = { x0, y0, x1 + 1, y1 + 1 };
    return r;
}

static void CaptureBlackHoleBackground(HWND hwnd)
{
    g_blackholeBgReady = false;
    g_blackholeBg.clear();
    g_blackholeVX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    g_blackholeVY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    g_blackholeW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    g_blackholeH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (g_blackholeW <= 0 || g_blackholeH <= 0) return;

    BOOL wasVisible = IsWindowVisible(hwnd);
    if (wasVisible) {
        g_reparenting = true;
        ShowWindow(hwnd, SW_HIDE);
        g_reparenting = false;
        // Wait for DWM to commit the hidden state. Without this, BitBlt can capture
        // stale border/title pixels from the previous composition frame.
        DwmFlush();
        Sleep(16);
    }

    HDC screen = GetDC(NULL);
    HDC memDC = screen ? CreateCompatibleDC(screen) : NULL;
    HBITMAP hbm = NULL;
    HBITMAP oldbm = NULL;
    void* bits = nullptr;

    if (screen && memDC) {
        BITMAPINFO bmi; ZeroMemory(&bmi, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = g_blackholeW;
        bmi.bmiHeader.biHeight = -g_blackholeH;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        hbm = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (hbm && bits) {
            oldbm = (HBITMAP)SelectObject(memDC, hbm);
            BitBlt(memDC, 0, 0, g_blackholeW, g_blackholeH, screen, g_blackholeVX, g_blackholeVY, SRCCOPY | CAPTUREBLT);
            size_t bytes = (size_t)g_blackholeW * (size_t)g_blackholeH * 4;
            g_blackholeBg.resize(bytes);
            memcpy(g_blackholeBg.data(), bits, bytes);
            // BitBlt into a 32-bit DIB often leaves alpha undefined/zero. Force it to
            // opaque so the cached desktop can become the full-screen layered frame.
            for (size_t i = 3; i < bytes; i += 4) {
                g_blackholeBg[i] = 255;
            }
            g_blackholeBgReady = true;
            ++g_blackholeBgSerial;
        }
    }

    if (oldbm) SelectObject(memDC, oldbm);
    if (hbm) DeleteObject(hbm);
    if (memDC) DeleteDC(memDC);
    if (screen) ReleaseDC(NULL, screen);

    if (wasVisible) {
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    }
}


static bool BhRectEmptyPx(const RECT& r)
{
    return r.right <= r.left || r.bottom <= r.top;
}

static RECT BhClampRectPx(RECT r, int W, int H)
{
    if (r.left < 0) r.left = 0;
    if (r.top < 0) r.top = 0;
    if (r.right > W) r.right = W;
    if (r.bottom > H) r.bottom = H;
    if (r.right < r.left) r.right = r.left;
    if (r.bottom < r.top) r.bottom = r.top;
    return r;
}

static RECT BhUnionRectPx(RECT a, RECT b)
{
    if (BhRectEmptyPx(a)) return b;
    if (BhRectEmptyPx(b)) return a;
    RECT r;
    r.left = min(a.left, b.left);
    r.top = min(a.top, b.top);
    r.right = max(a.right, b.right);
    r.bottom = max(a.bottom, b.bottom);
    return r;
}

static RECT BhInflateRectPx(RECT r, int pad, int W, int H)
{
    r.left -= pad;
    r.top -= pad;
    r.right += pad;
    r.bottom += pad;
    return BhClampRectPx(r, W, H);
}

static void ReleaseBlackHoleRenderBuffer()
{
    if (g_blackholeRenderDC && g_blackholeRenderOldBitmap) {
        SelectObject(g_blackholeRenderDC, g_blackholeRenderOldBitmap);
    }
    if (g_blackholeRenderBitmap) {
        DeleteObject(g_blackholeRenderBitmap);
    }
    if (g_blackholeRenderDC) {
        DeleteDC(g_blackholeRenderDC);
    }
    g_blackholeRenderDC = NULL;
    g_blackholeRenderBitmap = NULL;
    g_blackholeRenderOldBitmap = NULL;
    g_blackholeRenderBits = nullptr;
    g_blackholeRenderW = 0;
    g_blackholeRenderH = 0;
    g_blackholeRenderVX = 0;
    g_blackholeRenderVY = 0;
    g_blackholeFrameInitialized = false;
    g_blackholeFrameBgSerial = 0;
    g_blackholePrevDirty = { 0, 0, 0, 0 };
    g_blackholePrevDirtyValid = false;
}

static bool EnsureBlackHoleRenderBuffer(HDC screen, int W, int H, int vx, int vy)
{
    if (g_blackholeRenderDC && g_blackholeRenderBitmap && g_blackholeRenderBits &&
        g_blackholeRenderW == W && g_blackholeRenderH == H &&
        g_blackholeRenderVX == vx && g_blackholeRenderVY == vy) {
        return true;
    }

    ReleaseBlackHoleRenderBuffer();

    BITMAPINFO bmi; ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = W;
    bmi.bmiHeader.biHeight = -H; // top-down DIB; matches g_blackholeBg indexing
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    g_blackholeRenderDC = CreateCompatibleDC(screen);
    if (!g_blackholeRenderDC) {
        ReleaseBlackHoleRenderBuffer();
        return false;
    }

    g_blackholeRenderBitmap = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS,
        &g_blackholeRenderBits, NULL, 0);
    if (!g_blackholeRenderBitmap || !g_blackholeRenderBits) {
        ReleaseBlackHoleRenderBuffer();
        return false;
    }

    g_blackholeRenderOldBitmap = (HBITMAP)SelectObject(g_blackholeRenderDC, g_blackholeRenderBitmap);
    g_blackholeRenderW = W;
    g_blackholeRenderH = H;
    g_blackholeRenderVX = vx;
    g_blackholeRenderVY = vy;
    g_blackholeFrameInitialized = false;
    g_blackholeFrameBgSerial = 0;
    g_blackholePrevDirtyValid = false;
    return true;
}

static void RestoreBlackHoleFrameRect(const RECT& rect, int W, int H)
{
    if (!g_blackholeRenderBits || W <= 0 || H <= 0) return;
    RECT r = BhClampRectPx(rect, W, H);
    if (BhRectEmptyPx(r)) return;

    BYTE* dstBase = (BYTE*)g_blackholeRenderBits;
    const int stride = W * 4;
    const size_t rowBytes = (size_t)(r.right - r.left) * 4;

    // Clear the dirty area to transparent. The renderer writes an aligned captured
    // base color only inside each feathered field, so the boundary can blend back
    // to the real desktop below without leaving a stale frame.
    for (int y = r.top; y < r.bottom; ++y) {
        BYTE* dst = dstBase + (size_t)y * stride + (size_t)r.left * 4;
        memset(dst, 0, rowBytes);
    }
}

static void ResetBlackHoleAlertState()
{
    ReleaseBlackHoleRenderBuffer();
    g_blackholeBg.clear();
    g_blackholeBgReady = false;
    g_blackholeW = 0;
    g_blackholeH = 0;
    g_blackholeVX = 0;
    g_blackholeVY = 0;
    g_blackHoles.clear();
    ++g_blackholeBgSerial;
}

static void EnsureBlackHoleWindowBounds(HWND hwnd)
{
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw <= 0 || vh <= 0) return;
    SetWindowPos(hwnd, HWND_TOPMOST, vx, vy, vw, vh,
        SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

static void RenderBlackHoleAlert(HWND hwnd)
{
    using namespace Gdiplus;

    int W = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int H = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    if (W <= 0 || H <= 0) return;

    RECT wr{};
    if (!GetWindowRect(hwnd, &wr) || wr.left != vx || wr.top != vy ||
        (wr.right - wr.left) != W || (wr.bottom - wr.top) != H) {
        SetWindowPos(hwnd, HWND_TOPMOST, vx, vy, W, H,
            SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }

    if (!g_blackholeBgReady || g_blackholeW != W || g_blackholeH != H ||
        g_blackholeVX != vx || g_blackholeVY != vy) {
        CaptureBlackHoleBackground(hwnd);
    }

    HDC screen = GetDC(NULL);
    if (!screen) return;
    if (!EnsureBlackHoleRenderBuffer(screen, W, H, vx, vy)) {
        ReleaseDC(NULL, screen);
        return;
    }

    BhVec2 alertCenter = { 0.5, 0.5 };
    double alertRh = 0.012;
    const int stride = W * 4;
    BYTE* frameBits = (BYTE*)g_blackholeRenderBits;

    {
        double elapsed = duration<double>(steady_clock::now() - g_alertStarted).count();
        double aspect = (double)W / (double)H;
        double t = elapsed;
        if (g_blackHoles.empty()) {
            InitBlackHoleInstances(W, H);
        }
        if (g_blackHoles.empty()) {
            g_blackHoles.push_back(MakeBlackHoleInstance(0.5, 0.5, 0.060, 1));
        }

        struct HoleFrame {
            const BlackHoleInstance* hole;
            BhVec2 center;
            double rh;
            double effectR;
            RECT dirty;
        };

        std::vector<HoleFrame> frames;
        frames.reserve(g_blackHoles.size());
        RECT currentDirty = { 0, 0, 0, 0 };
        double textScore = -1.0;
        for (const auto& hole : g_blackHoles) {
            BhVec2 center = BlackHoleCurrentCenter(hole, elapsed, aspect);
            double rh = BlackHoleCurrentRh(hole, elapsed);
            double effectR = BlackHoleEffectRadius(rh);
            RECT dirty = BlackHoleDirtyRect(center, rh, W, H, aspect);
            frames.push_back({ &hole, center, rh, effectR, dirty });
            currentDirty = BhUnionRectPx(currentDirty, dirty);
            if (hole.maxRh > textScore) {
                textScore = hole.maxRh;
                alertCenter = center;
                alertRh = rh;
            }
        }

        RECT restoreDirty = currentDirty;
        if (g_blackholePrevDirtyValid) {
            restoreDirty = BhUnionRectPx(restoreDirty, g_blackholePrevDirty);
        }

        if (!g_blackholeFrameInitialized || g_blackholeFrameBgSerial != g_blackholeBgSerial) {
            RECT full = { 0, 0, W, H };
            RestoreBlackHoleFrameRect(full, W, H);
            g_blackholeFrameInitialized = true;
            g_blackholeFrameBgSerial = g_blackholeBgSerial;
            g_blackholePrevDirtyValid = false;
        } else {
            RestoreBlackHoleFrameRect(restoreDirty, W, H);
        }

        const double LENS_DEPTH = 13.0;
        const double DISK_INNER = 1.8;
        const double DISK_OUTER = 8.0;
        const double DISK_GAIN = 2.2;
        const double DISK_OPACITY = 0.90;
        const double DISK_TEMP = 5500.0;
        const double DOPPLER_MIX = 0.60;
        const double DISK_BEAM = 2.5;
        const double DISK_WIND = 7.0;
        const double DISK_CONTRAST = 1.6;
        const double EXPOSURE = 1.4;
        const double DILATION_MIN = 0.20;
        const double STAR_GAIN = 0.08;

        for (const auto& frame : frames) {
            const BlackHoleInstance& hole = *frame.hole;
            double rh = frame.rh;
            double effectR = frame.effectR;
            BhVec2 center = frame.center;
            double sizePulse = BhClamp((rh - hole.minRh) / BhMax(hole.maxRh - hole.minRh, 1e-5), 0.0, 1.0);
            double I = BhMix(0.36, 1.0, sizePulse);
            double Wscale = BH_B_CRIT / BhMax(rh, 1e-4);
            double rin = BhMax(DISK_INNER, 1.6);
            double rout = BhMax(DISK_OUTER, rin + 0.5);
            double Z0 = BhMax(14.0, rout + 5.0);
            double dil = BhMix(1.0, DILATION_MIN, I);

            for (int y = frame.dirty.top; y < frame.dirty.bottom; ++y) {
                BYTE* row = frameBits + (size_t)y * stride;
                for (int x = frame.dirty.left; x < frame.dirty.right; ++x) {
                    double uvx = ((double)x + 0.5) / (double)W;
                    double uvy = ((double)y + 0.5) / (double)H;
                    BhVec2 p = { (uvx - center.x) * aspect, (uvy - center.y) };
                    double plen = BhLen(p);
                    if (plen > effectR) continue;

                    double fieldMaskRaw = 1.0 - BhSmoothstep(effectR * BH_FIELD_FEATHER_START, effectR, plen);
                    double fieldAlpha = std::pow(BhSmootherstep01(fieldMaskRaw), BH_FIELD_ALPHA_POWER);
                    if (fieldAlpha <= 0.0008) continue;

                    double edgeAlign = BhSmootherstep01(BhSmoothstep(effectR * 0.76, effectR * 0.985, plen));
                    double effectMask = BhSmootherstep01(1.0 - BhSmoothstep(effectR * 0.56, effectR * 0.96, plen));
                    double window = std::exp(-std::pow(plen / BhMax(4.10 * rh, 1e-4), 2.25));
                    double warpBlend = BhClamp(window * BH_WARP_VIS_GAIN * effectMask, 0.0, 0.94);

                    BhColor base = BhSampleBackground(uvx, uvy);
                    BhVec2 pr2 = BhRot({ p.x, -p.y }, hole.roll);
                    pr2 = BhMul(pr2, Wscale);
                    double b = BhLen(pr2);
                    BhVec2 dir = BhNormalize(p);

                    double darkMask = 1.0 - BhSmoothstep(effectR * BH_DARK_CORE_END, effectR * BH_DARK_FEATHER_END, plen);
                    double well = 1.0 - BhSmoothstep(rh * 1.10, effectR * 0.86, plen);
                    double carrier = darkMask * (1.0 + BH_LOCAL_FIELD_VIGNETTE * well);
                    BhColor col = {
                        BH_LOCAL_FIELD_TINT_R * carrier * (1.0 + 0.30 * sizePulse),
                        BH_LOCAL_FIELD_TINT_G * carrier * (1.0 + 0.30 * sizePulse),
                        BH_LOCAL_FIELD_TINT_B * carrier * (1.0 + 0.30 * sizePulse)
                    };
                    double alpha = BhClamp(fieldAlpha * (0.16 + 0.50 * effectMask), 0.0, 0.82);

                    if (warpBlend > 0.002) {
                        double u = Z0 / std::sqrt(Z0 * Z0 + b * b);
                        double defl = (2.0 / (Wscale * Wscale)) / BhMax(plen, 1e-4)
                            * (1.29 * u + 0.07) * BhMax(LENS_DEPTH - 2.14 * u + 0.75, 0.0) * window;

                        BhColor warped = { 0.0, 0.0, 0.0 };
                        for (int c = 0; c < 3; ++c) {
                            double ab = 0.035 * BhSmoothstep(0.9, 2.0, b / (rout + 3.0));
                            double k = 1.0 + ((double)c - 1.0) * ab;
                            BhVec2 sp = BhSub(p, BhMul(dir, defl * k));
                            double su = center.x + sp.x / aspect;
                            double sv = center.y + sp.y;
                            BhColor sc = BhSampleBackground(su, sv);
                            if (c == 0) warped.r = sc.r;
                            else if (c == 1) warped.g = sc.g;
                            else warped.b = sc.b;
                        }
                        col = BhColorAdd(col, BhColorMul(warped, warpBlend));
                        alpha = BhMax(alpha, BhClamp(fieldAlpha * warpBlend * BH_DISTORTION_ALPHA_GAIN, 0.0, BH_DISTORTION_MAX_ALPHA));
                    }

                    if (STAR_GAIN > 0.0 && b > 0.25) {
                        BhVec3 sd = BhNormalize3({ -(pr2.x / b) * (2.0 / b), -(pr2.y / b) * (2.0 / b), -1.0 });
                        BhColor st = BhColorMul(BhStars(sd, t + hole.seed), STAR_GAIN * window * fieldAlpha);
                        col = BhColorAdd(col, st);
                        alpha = BhMax(alpha, fieldAlpha * BhClamp((st.r + st.g + st.b) * 0.45, 0.0, 0.82));
                    }

                    BhVec2 diskLocal = BhRot(p, -hole.roll);
                    double flatten = BhMax(0.18, std::cos(hole.incl) + 0.12);
                    double dx = diskLocal.x;
                    double dy = diskLocal.y / flatten;
                    double rdScreen = std::sqrt(dx * dx + dy * dy);
                    double rc = rdScreen * Wscale;
                    if (rc > rin && rc < rout) {
                        double band = BhSmoothstep(rin, rin * 1.25, rc) * (1.0 - BhSmoothstep(rout * 0.70, rout, rc));
                        double phi = std::atan2(dy, dx);
                        double turns = phi / (2.0 * BH_PI);
                        double kep = std::pow(rin / rc, 1.5);
                        double gloc = std::sqrt(BhMax(1.0 - 1.5 / rc, 0.02));
                        double swirl = rc * DISK_WIND * 0.12 - (t + hole.seed) * kep * std::fabs(hole.diskSpeed) * gloc * dil;
                        double streaks = BhVNoiseWrapY({ rc * 2.8, turns * 19.0 + swirl * 3.0 }, 19.0) * 0.65
                                        + BhVNoiseWrapY({ rc * 1.0, turns *  9.0 + swirl * 1.5 + 7.0 }, 9.0) * 0.35;
                        streaks = 0.35 + DISK_CONTRAST * streaks * streaks;

                        double beta = BhClamp(1.0 / std::sqrt(BhMax(2.0 * (rc - 1.0), 0.2)), 0.0, 0.99);
                        double approach = std::cos(phi - 0.55);
                        double gg = gloc / BhMax(1.0 - beta * approach * 0.72, 0.05);
                        gg = BhMix(1.0, gg, DOPPLER_MIX);
                        double xpr = BhMax(1.0 - std::sqrt(rin / rc), 0.0);
                        double tprof = std::pow(rin / rc, 0.75) * std::pow(xpr, 0.25) / 0.488;
                        BhColor cbb = BhBlackbody(DISK_TEMP * tprof * gg);
                        double boost = std::pow(gg, DISK_BEAM);
                        double density = band * streaks;

                        double shadowCut = BhSmoothstep(rh * 0.84, rh * 1.06, plen);
                        double farSideLift = 0.70 + 0.30 * BhSmoothstep(-0.15, 0.45, std::sin(phi));
                        density *= shadowCut * farSideLift;

                        BhColor emit = BhColorMul(cbb, DISK_GAIN * 2.2 * density * tprof * tprof * boost);
                        BhColor disk = { 1.0 - std::exp(-emit.r * EXPOSURE),
                                         1.0 - std::exp(-emit.g * EXPOSURE),
                                         1.0 - std::exp(-emit.b * EXPOSURE) };
                        double diskA = BhClamp(DISK_OPACITY * density * 0.62 + (disk.r + disk.g + disk.b) * 0.20, 0.0, 1.0);
                        col = BhColorAdd(BhColorMul(col, 1.0 - diskA * 0.56), BhColorMul(disk, 1.18));
                        alpha = BhMax(alpha, fieldAlpha * diskA);
                    }

                    double ring = std::exp(-std::pow((plen - rh * 1.045) / BhMax(rh * 0.038, 1e-4), 2.0));
                    if (ring > 0.002) {
                        BhColor ringCol = { 1.0, 0.78, 0.42 };
                        col = BhColorAdd(col, BhColorMul(ringCol, ring * (1.38 + 0.55 * I)));
                        alpha = BhMax(alpha, fieldAlpha * BhClamp(ring * 0.88, 0.0, 1.0));
                    }
                    double shadow = 1.0 - BhSmoothstep(rh * 0.92 * BH_EVENT_HORIZON_RADIUS_SCALE,
                        rh * 1.03 * BH_EVENT_HORIZON_RADIUS_SCALE, plen);
                    if (shadow > 0.0) {
                        double shadowVisual = BhClamp(shadow * BH_EVENT_HORIZON_OPACITY, 0.0, 1.0);
                        col = BhColorMul(col, 1.0 - shadowVisual);
                        alpha = BhMax(alpha, fieldAlpha * BhClamp(shadowVisual * 0.995, 0.0, 1.0));
                    }

                    col = BhColorMix(col, base, edgeAlign * 0.72);
                    alpha = BhMix(alpha, BhClamp(fieldAlpha * 0.10, 0.0, 0.20), edgeAlign);

                    col.r = BhClamp(col.r, 0.0, 1.0);
                    col.g = BhClamp(col.g, 0.0, 1.0);
                    col.b = BhClamp(col.b, 0.0, 1.0);
                    alpha = BhClamp(alpha, 0.0, 0.995);

                    BYTE* px = row + (size_t)x * 4;
                    px[0] = (BYTE)BhByte(col.b * alpha);
                    px[1] = (BYTE)BhByte(col.g * alpha);
                    px[2] = (BYTE)BhByte(col.r * alpha);
                    px[3] = (BYTE)BhByte(alpha);
                }
            }
        }

        g_blackholePrevDirty = currentDirty;
        g_blackholePrevDirtyValid = true;
    }

    // 将提示内容绑定到本次生成的主黑洞，并随其当前大小动态缩放。
    {
        Bitmap frameBitmap(W, H, stride, PixelFormat32bppPARGB, frameBits);
        Graphics g(&frameBitmap);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        FontFamily ff(L"Microsoft YaHei UI");
        FontFamily ffFallback(L"Segoe UI");
        FontFamily* use = ff.IsAvailable() ? &ff : &ffFallback;

        std::wstring detail = g_alertDetail;
        double elapsedHint = duration<double>(steady_clock::now() - g_alertStarted).count();
        std::wstring closeHint = (elapsedHint >= 10.0) ? L"按任意键关闭黑洞" : L"10 秒后可按任意键关闭";

        float shadowRadiusPx = (float)(alertRh * H * 0.96);
        float safeRadius = shadowRadiusPx * 0.72f;
        if (safeRadius >= 42.0f) {
            float layoutW = safeRadius * 1.46f;
            float layoutH = safeRadius * 0.96f;
            RectF layout(
                (float)(alertCenter.x * W) - layoutW * 0.5f,
                (float)(alertCenter.y * H) - layoutH * 0.5f,
                layoutW,
                layoutH);

            auto fitSingleLine = [&](const std::wstring& textValue, float startSize, float minSize, float maxWidth, float maxHeight, int styleFlags) {
                float size = startSize;
                RectF measured;
                while (size > minSize) {
                    Font tryFont(use, size, styleFlags, UnitPixel);
                    g.MeasureString(textValue.c_str(), (INT)textValue.size(), &tryFont, PointF(0.0f, 0.0f), &measured);
                    if (measured.Width <= maxWidth && measured.Height <= maxHeight) break;
                    size -= 1.0f;
                }
                if (size < minSize) size = minSize;
                return size;
            };

            auto fitWrapped = [&](const std::wstring& textValue, float startSize, float minSize, const RectF& target) {
                StringFormat fmt(StringFormat::GenericTypographic());
                fmt.SetAlignment(StringAlignmentCenter);
                fmt.SetLineAlignment(StringAlignmentCenter);
                fmt.SetFormatFlags(0);
                fmt.SetTrimming(StringTrimmingEllipsisWord);
                float size = startSize;
                RectF measured;
                while (size > minSize) {
                    Font tryFont(use, size, FontStyleRegular, UnitPixel);
                    RectF bound = target;
                    g.MeasureString(textValue.c_str(), (INT)textValue.size(), &tryFont, bound, &fmt, &measured);
                    if (measured.Width <= target.Width + 1.0f && measured.Height <= target.Height + 1.0f) break;
                    size -= 1.0f;
                }
                if (size < minSize) size = minSize;
                return size;
            };

            float titleAreaH = layoutH * 0.26f;
            float detailAreaH = layoutH * 0.33f;
            float hintAreaH = layoutH * 0.18f;
            float gapH = layoutH * 0.06f;
            float blockTop = layout.Y + (layoutH - (titleAreaH + detailAreaH + hintAreaH + gapH * 2.0f)) * 0.5f;

            RectF titleRect(layout.X, blockTop, layout.Width, titleAreaH);
            RectF detailRect(layout.X, titleRect.Y + titleRect.Height + gapH, layout.Width, detailAreaH);
            RectF hintRect(layout.X, detailRect.Y + detailRect.Height + gapH, layout.Width, hintAreaH);

            float titleSize = fitSingleLine(g_alertTitle, (float)BhClamp(safeRadius * 0.34f, 14.0f, 56.0f), 10.0f,
                titleRect.Width * 0.96f, titleRect.Height * 0.96f, FontStyleBold);
            float detailSize = fitWrapped(detail, (float)BhClamp(safeRadius * 0.19f, 10.0f, 30.0f), 9.0f, detailRect);
            float hintSize = fitSingleLine(closeHint, (float)BhClamp(safeRadius * 0.16f, 9.0f, 24.0f), 8.0f,
                hintRect.Width * 0.96f, hintRect.Height * 0.96f, FontStyleRegular);

            Font titleFont(use, titleSize, FontStyleBold, UnitPixel);
            Font detailFont(use, detailSize, FontStyleRegular, UnitPixel);
            Font hintFont(use, hintSize, FontStyleRegular, UnitPixel);

            StringFormat fmtCenter(StringFormat::GenericTypographic());
            fmtCenter.SetAlignment(StringAlignmentCenter);
            fmtCenter.SetLineAlignment(StringAlignmentCenter);
            fmtCenter.SetTrimming(StringTrimmingEllipsisWord);

            SolidBrush titleShadow(Color(190, 0, 0, 0));
            SolidBrush metaShadow(Color(170, 0, 0, 0));
            SolidBrush titleBrush(Color(255, 248, 250, 255));
            SolidBrush detailBrush(Color(255, 225, 232, 242));
            SolidBrush hintBrush(Color(240, 205, 216, 232));

            auto drawCentered = [&](const std::wstring& s, Font& font, const RectF& rect, SolidBrush& mainBrush, SolidBrush& shadowBrush) {
                RectF shadowRect = rect;
                shadowRect.X += 1.6f;
                shadowRect.Y += 1.6f;
                g.DrawString(s.c_str(), (INT)s.size(), &font, shadowRect, &fmtCenter, &shadowBrush);
                g.DrawString(s.c_str(), (INT)s.size(), &font, rect, &fmtCenter, &mainBrush);
            };

            drawCentered(g_alertTitle, titleFont, titleRect, titleBrush, titleShadow);
            drawCentered(detail, detailFont, detailRect, detailBrush, metaShadow);
            drawCentered(closeHint, hintFont, hintRect, hintBrush, metaShadow);
        }
    }

    POINT ptDst = { vx, vy };
    SIZE sz = { W, H };
    POINT ptSrc = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, BH_ALERT_LAYER_ALPHA, AC_SRC_ALPHA };
    UpdateLayeredWindow(hwnd, screen, &ptDst, &sz, g_blackholeRenderDC, &ptSrc, 0, &bf, ULW_ALPHA);

    ReleaseDC(NULL, screen);
}

enum TipRowType { RT_SPACER, RT_HEADING, RT_META, RT_TODO };
struct TipRow {
    TipRowType type;
    std::wstring text;
    float w;
    float h;
    float y;
    int sourceIndex;
    bool completed;
};

void RenderTip(HWND hwnd)
{
    using namespace Gdiplus;

    if (g_alertActive) {
        g_todoHitRects.clear();
        RenderBlackHoleAlert(hwnd);
        return;
    }

    // ---- 读取并解析行 ----
    std::vector<std::wstring> raw;
    bool fileOk = ReadTodoFileLinesW(raw);
    if (!fileOk) raw.push_back(L"Error: Unable to open file!");

    // ---- 基础尺寸（由配置的字号驱动，托盘的字号/行距按钮依然有效）----
    float base   = (float)(fontHeight > 8 ? fontHeight : 24); // 待办文字像素高
    float headSz = base * 2.0f;   // 时钟/标题
    float metaSz = base * 0.64f;  // 日期/页脚

    int PADX    = (int)(base * 1.15f);
    int PADTOP  = (int)(base * 0.95f);
    int PADBOT  = (int)(base * 1.05f);
    int checkD  = (int)(base * 0.58f); if (checkD < 11) checkD = 11;
    int checkGap = (int)(base * 0.52f);
    int divGap  = (int)(base * 0.80f);
    int metaGap = (int)(base * 0.28f);
    int headGap = (int)(base * 0.12f);
    int todoExtra = lineHeight - fontHeight; if (todoExtra < 0) todoExtra = 0;
    int todoGap = (int)(base * 0.45f) + todoExtra;
    int M       = (int)(base * 2.10f); // 阴影外扩边距（需 > 模糊半径）
    if (g_alertActive && M > 180) M = 180;
    float radius = base * 0.70f;

    // 卡片背景由 g_bgAlpha / g_bgColor 控制（可在托盘菜单调整）

    const std::wstring& activeFont = (g_alertActive && !g_alertFontFamily.empty()) ? g_alertFontFamily : g_fontFamily;
    FontFamily* famText = PickFamily({ activeFont.c_str(), L"Microsoft YaHei UI", L"Microsoft YaHei", L"\u5fae\u8f6f\u96c5\u9ed1" });
    FontFamily* famHead = PickFamily({ activeFont.c_str(), L"Microsoft YaHei Light", L"Microsoft YaHei UI Light", L"Microsoft YaHei UI", L"Microsoft YaHei" });
    Font fHead(famHead, headSz, FontStyleRegular, UnitPixel);
    Font fText(famText, base,   FontStyleRegular, UnitPixel);
    Font fTextDone(famText, base, FontStyleStrikeout, UnitPixel);
    Font fMeta(famText, metaSz, FontStyleRegular, UnitPixel);

    StringFormat fmt(StringFormat::GenericTypographic());
    fmt.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);

    HDC screen = GetDC(NULL);
    Graphics mg(screen);
    mg.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    // ---- 构建行模型 ----
    std::vector<TipRow> rows;
    rows.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        std::wstring s2 = raw[i];
        TrimLineRight(s2);
        size_t b = 0; while (b < s2.size() && (s2[b] == L' ' || s2[b] == L'\t')) b++;
        std::wstring t = s2.substr(b);

        TipRow r;
        r.w = 0;
        r.h = 0;
        r.y = 0;
        r.sourceIndex = fileOk ? (int)i : -1;
        r.completed = false;

        if (t.empty()) {
            r.type = RT_SPACER;
            r.text = L"";
        } else if (t[0] == L'#') {
            r.type = RT_HEADING;
            r.text = ProcessPlaceholdersW((t.size() > 1 && t[1] == L' ') ? t.substr(2) : t.substr(1));
        } else if (t[0] == L'>') {
            r.type = RT_META;
            r.text = ProcessPlaceholdersW((t.size() > 1 && t[1] == L' ') ? t.substr(2) : t.substr(1));
        } else {
            std::wstring todoText;
            bool completed = false;
            ParseTodoLineState(raw[i], &todoText, &completed);
            r.type = fileOk ? RT_TODO : RT_META;
            r.text = ProcessPlaceholdersW(todoText.empty() && !fileOk ? raw[i] : todoText);
            r.completed = completed;
        }
        rows.push_back(r);
    }

    std::vector<TipRow> todos;
    for (const auto& r : rows) {
        if (r.type == RT_TODO) todos.push_back(r);
    }
    std::stable_sort(todos.begin(), todos.end(), [](const TipRow& a, const TipRow& b) {
        return a.completed < b.completed;
    });
    size_t nextTodo = 0;
    for (auto& r : rows) {
        if (r.type == RT_TODO && nextTodo < todos.size()) {
            r = todos[nextTodo++];
        }
    }

    auto measure = [&](const std::wstring& str, Font& f) -> RectF {
        RectF bb;
        if (str.empty()) return bb;
        mg.MeasureString(str.c_str(), (INT)str.size(), &f, PointF(0, 0), &fmt, &bb);
        return bb;
    };

    // ---- 第一遍：测量 + 计算布局 ----
    float contentW = base * 4.0f;
    for (auto& r : rows) {
        if (r.type == RT_SPACER) { r.h = base * 0.45f; continue; }
        Font* f = (r.type == RT_HEADING) ? &fHead : (r.type == RT_META) ? &fMeta : &fText;
        RectF bb = measure(r.text, *f);
        r.h = bb.Height;
        r.w = bb.Width + (r.type == RT_TODO ? (float)(checkD + checkGap) : 0.0f);
        if (r.w > contentW) contentW = r.w;
    }

    float y = (float)PADTOP;
    float dividerY = -1.0f;
    bool placedDivider = false, seenContent = false;
    float lastGap = 0.0f;
    for (auto& r : rows) {
        if (r.type == RT_SPACER) { y += r.h; lastGap = 0; continue; }
        if (r.type == RT_TODO && !placedDivider) {
            if (seenContent) { y += divGap; dividerY = y; y += 1 + divGap; }
            placedDivider = true;
        }
        r.y = y;
        if (r.type == RT_HEADING) { y += r.h + headGap; lastGap = (float)headGap; }
        else if (r.type == RT_META) { y += r.h + metaGap; lastGap = (float)metaGap; }
        else { float rh = (r.h > checkD ? r.h : (float)checkD); y += rh + todoGap; lastGap = (float)todoGap; }
        seenContent = true;
    }
    y -= lastGap;
    int cardW = (int)(contentW + PADX * 2 + 0.5f);
    int cardH = (int)(y + PADBOT + 0.5f);

    if (g_alertActive) {
        int virtualW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int virtualH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        int maxCardW = virtualW - 2 * M - 20;
        int maxCardH = virtualH - 2 * M - 20;
        if (maxCardW < 220) maxCardW = 220;
        if (maxCardH < 140) maxCardH = 140;

        int targetW = (int)(g_alertCardW + 0.5);
        int targetH = (int)(g_alertCardH + 0.5);
        if (targetW > maxCardW) targetW = maxCardW;
        if (targetH > maxCardH) targetH = maxCardH;
        if (cardW < targetW) cardW = targetW;
        if (cardH < targetH) cardH = targetH;
        if (cardW > maxCardW) cardW = maxCardW;
        if (cardH > maxCardH) cardH = maxCardH;
    }

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    if (cardW > screenW - 20) cardW = screenW - 20;

    int W = cardW + 2 * M;
    int H = cardH + 2 * M;

    // ---- 创建 DIB（供 UpdateLayeredWindow 使用）----
    BITMAPINFO bmi; ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = W;
    bmi.bmiHeader.biHeight = -H; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* pBits = nullptr;
    HDC memDC = CreateCompatibleDC(screen);
    HBITMAP hbm = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    HBITMAP oldbm = (HBITMAP)SelectObject(memDC, hbm);

    // ---- 在独立的 GDI+ 位图上绘制（保证 alpha 正确）----
    Bitmap layer(W, H, PixelFormat32bppARGB);
    Graphics g(&layer);
    g.Clear(Color(0, 0, 0, 0));
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    // 柔和阴影：独立位图画卡片轮廓 → 高斯模糊 → 挖空卡片内部 → 叠到底层
    {
        float blurR = base * 0.95f; if (blurR < 8) blurR = 8; if (blurR > 56) blurR = 56;
        float shDy = base * 0.35f;
        Bitmap shadowBmp(W, H, PixelFormat32bppARGB);
        Graphics sg(&shadowBmp);
        sg.Clear(Color(0, 0, 0, 0));
        sg.SetSmoothingMode(SmoothingModeAntiAlias);
        GraphicsPath sp;
        RoundRectPath(sp, (float)M, (float)M + shDy, (float)cardW, (float)cardH, radius);
        SolidBrush sb(Color(130, 0, 0, 0));
        sg.FillPath(&sb, &sp);
        Blur blur;
        BlurParams bp; bp.radius = blurR; bp.expandEdge = FALSE;
        blur.SetParameters(&bp);
        shadowBmp.ApplyEffect(&blur, NULL);
        // 挖掉卡片正下方的阴影，避免半透明卡片中心发灰
        GraphicsPath hole;
        RoundRectPath(hole, (float)M, (float)M, (float)cardW, (float)cardH, radius);
        sg.SetCompositingMode(CompositingModeSourceCopy);
        SolidBrush clearB(Color(0, 0, 0, 0));
        sg.FillPath(&clearB, &hole);
        g.DrawImage(&shadowBmp, 0, 0);
    }

    // 卡片底 + 发丝描边
    GraphicsPath cp;
    RoundRectPath(cp, (float)M, (float)M, (float)cardW, (float)cardH, radius);
    SolidBrush cardBrush(Color((BYTE)g_bgAlpha, GetRValue(g_bgColor), GetGValue(g_bgColor), GetBValue(g_bgColor)));
    g.FillPath(&cardBrush, &cp);
    Pen border(Color(24, 255, 255, 255), 1.0f);
    g.DrawPath(&border, &cp);

    // 颜色
    Color ink(255, GetRValue(g_textColor), GetGValue(g_textColor), GetBValue(g_textColor));
    Color checkc(235, GetRValue(g_textColor), GetGValue(g_textColor), GetBValue(g_textColor));
    Color doneInk(155,
        ClampByte(GetRValue(g_textColor) * 0.55),
        ClampByte(GetGValue(g_textColor) * 0.55),
        ClampByte(GetBValue(g_textColor) * 0.55));
    Color doneCheck(165,
        ClampByte(GetRValue(g_textColor) * 0.52),
        ClampByte(GetGValue(g_textColor) * 0.52),
        ClampByte(GetBValue(g_textColor) * 0.52));
    Color doneFill(36,
        ClampByte(GetRValue(g_textColor) * 0.45),
        ClampByte(GetGValue(g_textColor) * 0.45),
        ClampByte(GetBValue(g_textColor) * 0.45));
    Color meta(255, 169, 175, 189);
    Color shadowc(90, 0, 0, 0);
    float sdy = base * 0.045f; if (sdy < 1) sdy = 1;

    auto drawText = [&](const std::wstring& str, Font& f, float tx, float ty, Color c) {
        if (str.empty()) return;
        SolidBrush shb(shadowc);
        g.DrawString(str.c_str(), (INT)str.size(), &f, PointF(tx, ty + sdy), &fmt, &shb);
        SolidBrush br(c);
        g.DrawString(str.c_str(), (INT)str.size(), &f, PointF(tx, ty), &fmt, &br);
    };

    float bx = (float)(M + PADX);
    if (dividerY >= 0) {
        Pen dp(Color(30, 255, 255, 255), 1.0f);
        float dyv = M + dividerY;
        g.DrawLine(&dp, bx, dyv, (float)(M + cardW - PADX), dyv);
    }

    g_todoHitRects.clear();
    for (auto& r : rows) {
        if (r.type == RT_SPACER) continue;
        float ry = M + r.y;
        if (r.type == RT_HEADING) drawText(r.text, fHead, bx, ry, ink);
        else if (r.type == RT_META) drawText(r.text, fMeta, bx, ry, meta);
        else {
            float rh = (r.h > checkD ? r.h : (float)checkD);
            float cy = ry + rh / 2.0f - checkD / 2.0f;
            float stroke = base * 0.06f; if (stroke < 1.25f) stroke = 1.25f;
            Color boxColor = r.completed ? doneCheck : checkc;

            if (r.completed) {
                SolidBrush fill(doneFill);
                g.FillRectangle(&fill, bx, cy, (REAL)checkD, (REAL)checkD);
            }

            Pen boxPen(boxColor, stroke);
            g.DrawRectangle(&boxPen, bx, cy, (REAL)checkD, (REAL)checkD);

            if (r.completed) {
                Pen tickPen(doneCheck, stroke + 0.35f);
                tickPen.SetStartCap(LineCapRound);
                tickPen.SetEndCap(LineCapRound);
                g.DrawLine(&tickPen,
                    bx + checkD * 0.23f, cy + checkD * 0.53f,
                    bx + checkD * 0.43f, cy + checkD * 0.72f);
                g.DrawLine(&tickPen,
                    bx + checkD * 0.43f, cy + checkD * 0.72f,
                    bx + checkD * 0.78f, cy + checkD * 0.28f);
            }

            if (r.sourceIndex >= 0) {
                int inflate = (int)(base * 0.20f); if (inflate < 4) inflate = 4;
                RECT hit = {
                    (LONG)(bx - inflate),
                    (LONG)(cy - inflate),
                    (LONG)(bx + checkD + inflate),
                    (LONG)(cy + checkD + inflate)
                };
                g_todoHitRects.push_back({ hit, r.sourceIndex });
            }

            Font& todoFont = r.completed ? fTextDone : fText;
            drawText(r.text, todoFont, bx + checkD + checkGap, ry, r.completed ? doneInk : ink);
        }
    }

    // ---- 复制到 DIB 并做 alpha 预乘 ----
    BitmapData bd;
    Rect lockR(0, 0, W, H);
    if (layer.LockBits(&lockR, ImageLockModeRead, PixelFormat32bppARGB, &bd) == Ok) {
        BYTE* src = (BYTE*)bd.Scan0;
        BYTE* dst = (BYTE*)pBits;
        int dstStride = W * 4;
        for (int row = 0; row < H; row++) {
            BYTE* sp2 = src + row * bd.Stride;
            BYTE* dp2 = dst + row * dstStride;
            for (int col = 0; col < W; col++) {
                BYTE B2 = sp2[0], G2 = sp2[1], R2 = sp2[2], A2 = sp2[3];
                dp2[0] = (BYTE)(B2 * A2 / 255);
                dp2[1] = (BYTE)(G2 * A2 / 255);
                dp2[2] = (BYTE)(R2 * A2 / 255);
                dp2[3] = A2;
                sp2 += 4; dp2 += 4;
            }
        }
        layer.UnlockBits(&bd);
    }

    // ---- 推送到分层窗口 ----
    RECT wr; GetWindowRect(hwnd, &wr);

    // ★ 防止卡片落在可视区域之外导致“什么都看不到”：
    //   配置里保存的窗口坐标可能来自另一台机器/另一种显示器布局（如笔记本外接副屏，
    //   x=2959），换到分辨率更小或单显示器的机器上时就完全跑到屏幕外，两种显示模式
    //   都会看不到任何内容。这里用虚拟屏幕边界把整张 W×H 位图夹回可见范围，必要时
    //   真正移动窗口（保持拖动/点击命中与绘制位置一致），从而自愈这种离屏状态。
    {
        int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        int nx = wr.left, ny = wr.top;
        if (nx + W > vx + vw) nx = vx + vw - W;   // 不越过右/下边缘
        if (ny + H > vy + vh) ny = vy + vh - H;
        if (nx < vx) nx = vx;                      // 不越过左/上边缘
        if (ny < vy) ny = vy;
        if (nx != wr.left || ny != wr.top) {
            SetWindowPos(hwnd, NULL, nx, ny, 0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            wr.left = nx; wr.top = ny;
        }
    }

    POINT ptDst = { wr.left, wr.top };
    SIZE  sz = { W, H };
    POINT ptSrc = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, BH_ALERT_LAYER_ALPHA, AC_SRC_ALPHA };
    UpdateLayeredWindow(hwnd, screen, &ptDst, &sz, memDC, &ptSrc, 0, &bf, ULW_ALPHA);

    // ---- 清理 ----
    SelectObject(memDC, oldbm);
    DeleteObject(hbm);
    DeleteDC(memDC);
    ReleaseDC(NULL, screen);
    delete famText;
    delete famHead;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        // Create a custom font:
        // Example: Create a 24-point Arial font.
        // Height = -MulDiv(pointSize, GetDeviceCaps(hdc, LOGPIXELSY), 72)
        // For simplicity, assume a fixed size, say 24 points.
        HDC hdc = GetDC(hWnd);
        ReleaseDC(hWnd, hdc);
        SetTimer(hWnd, TIMER_REFRESH, 60000, nullptr);
        SetTimer(hWnd, TIMER_REMINDER_CHECK, 1000, nullptr);
        return 0;
    }
    case WM_WINDOWPOSCHANGING:
    {
        // 阻止“显示桌面 / Win+D”把窗口隐藏——剥掉 SWP_HIDEWINDOW 即可常驻桌面/置顶。
        // 但在 SetParent 重挂父子关系期间(g_reparenting)绝不能剥：那会打断系统的
        // 隐藏→重挂→显示流程，使 SetParent 静默失败、卡片无法附着到桌面。
        WINDOWPOS* wp = (WINDOWPOS*)lParam;
        if (wp && !g_reparenting) wp->flags &= ~SWP_HIDEWINDOW;
        break;   // 交给 DefWindowProc 按修改后的 WINDOWPOS 继续处理
    }
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_MINIMIZE) return 0;   // 不允许被最小化
        break;
    case WM_SHOWWINDOW:
        if (lParam == SW_PARENTCLOSING) return 0;          // 不随其它窗口一起被隐藏
        break;
    case WM_LBUTTONDOWN:
    {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        if (!g_alertActive && !g_restoringAlert && ToggleTodoAtPoint(hWnd, mx, my)) {
            return 0;
        }

        // 开始拖动：记录光标相对窗口左上角的偏移（屏幕坐标）
        POINT cur; GetCursorPos(&cur);
        RECT wr; GetWindowRect(hWnd, &wr);
        g_dragOffset.x = cur.x - wr.left;
        g_dragOffset.y = cur.y - wr.top;
        g_dragging = true;
        SetCapture(hWnd);
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        if (g_dragging && (wParam & MK_LBUTTON)) {
            POINT cur; GetCursorPos(&cur);
            int nx = cur.x - g_dragOffset.x;
            int ny = cur.y - g_dragOffset.y;
            HWND parent = GetParent(hWnd);
            LONG_PTR st = GetWindowLongPtr(hWnd, GWL_STYLE);
            if ((st & WS_CHILD) && parent) {
                POINT p = { nx, ny };
                ScreenToClient(parent, &p);   // 子窗口：换算为父窗口客户区坐标
                SetWindowPos(hWnd, NULL, p.x, p.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            } else {
                SetWindowPos(hWnd, NULL, nx, ny, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
        return 0;
    }
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
    {
        ShowContextMenu(hWnd);
        return 0;
    }
    case WM_LBUTTONUP:
    {
        if (g_dragging) {
            g_dragging = false;
            ReleaseCapture();
            confWrite();  // 保存新位置
        }
        return 0;
    }
    case WM_MOVE:
    {
        if (!g_dragging && !g_alertActive && !g_restoringAlert) confWrite();   // 拖动中不反复写盘，松开时(WM_LBUTTONUP)统一保存
        break;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
        RenderTip(hWnd);
        return 0;
    }
    case WM_DESTROY:
        if (g_hFont) { DeleteObject(g_hFont); g_hFont = NULL; }
        KillTimer(hWnd, TIMER_REFRESH);
        KillTimer(hWnd, TIMER_REMINDER_CHECK);
        KillTimer(hWnd, TIMER_REMINDER_ANIMATE);
        UninstallReminderKeyboardHook();
        PostQuitMessage(0);
        return 0;
    case WM_TASKBAR:
        if (lParam == WM_RBUTTONUP) {
            // 用户右键点击托盘图标，显示菜单
            ShowContextMenu(hWnd);
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TRAY_EXIT) {
            // 用户选择退出，退出程序
            PostMessage(hWnd, WM_CLOSE, 0, 0);
        }
        if (LOWORD(wParam) == ID_SETTINGS) {
            ShowSettingsPanel(hWnd);   // 打开滑动条设置面板
        }
        if (LOWORD(wParam) == ID_FONT_SELECT) {
            ChooseFontFamily(hWnd);
        }
        if (LOWORD(wParam) == ID_MODE_DESKTOP) {
            g_displayMode = 0;          // 附着桌面（默认）
            confWrite();
            ApplyDisplayMode(hWnd, 0);
        }
        if (LOWORD(wParam) == ID_MODE_TOPMOST) {
            g_displayMode = 1;          // 始终置顶
            confWrite();
            ApplyDisplayMode(hWnd, 1);
        }
        if (LOWORD(wParam) == ID_COUNTDOWN_1MIN) {
            ArmCountdown(hWnd, 60);
        }
        if (LOWORD(wParam) == ID_COUNTDOWN_5MIN) {
            ArmCountdown(hWnd, 5 * 60);
        }
        if (LOWORD(wParam) == ID_COUNTDOWN_10MIN) {
            ArmCountdown(hWnd, 10 * 60);
        }
        if (LOWORD(wParam) == ID_COUNTDOWN_1HOUR) {
            ArmCountdown(hWnd, 60 * 60);
        }
        if (LOWORD(wParam) == ID_COUNTDOWN_REPEAT) {
            g_countdownRepeatEnabled = !g_countdownRepeatEnabled;
            confWrite();
        }
        if (LOWORD(wParam) == ID_COUNTDOWN_CUSTOM) {
            ShowCountdownPrompt(hWnd);
        }
        if (LOWORD(wParam) == ID_ALARM_CUSTOM) {
            ShowAlarmPrompt(hWnd);
        }
        if (LOWORD(wParam) == ID_REMINDER_CANCEL) {
            if (g_alertActive) {
                StopReminderAlert(hWnd);
            } else {
                g_reminderArmed = false;
                g_reminderIsCountdown = false;
                g_reminderIntervalSeconds = 0;
            }
        }
        if (LOWORD(wParam) == ID_FONTSIZE_INCREASE) {
            fontHeight+=5;
            confWrite();  // 保存配置
            RenderTip(hWnd); // 清除整个窗口并重绘
        }
        if (LOWORD(wParam) == ID_FONTSIZE_DECREASE) {
            fontHeight-= 5;
            confWrite();  // 保存配置
            if (fontHeight < 0)
                fontHeight = 0;
            RenderTip(hWnd); // 清除整个窗口并重绘
        }
        if (LOWORD(wParam) == ID_SPACING_INCREASE) {
            lineHeight+= 5;
            confWrite();  // 保存配置
            RenderTip(hWnd); // 清除整个窗口并重绘
        }
        if (LOWORD(wParam) == ID_SPACING_DECREASE) {
            lineHeight-= 5;
            confWrite();  // 保存配置
            if (lineHeight < 0)
                lineHeight = 0;
            RenderTip(hWnd); // 清除整个窗口并重绘
        }
        if (LOWORD(wParam) == ID_TEXT_COLOR) {
            ChooseTextColor(hWnd);
            RenderTip(hWnd); // 清除整个窗口并重绘
        }
        if (LOWORD(wParam) == ID_BG_COLOR) {
            ChooseCardColor(hWnd);
            RenderTip(hWnd);
        }
        if (LOWORD(wParam) == ID_OPACITY_INCREASE) {
            g_bgAlpha += 20; if (g_bgAlpha > 255) g_bgAlpha = 255;
            confWrite();
            RenderTip(hWnd);
        }
        if (LOWORD(wParam) == ID_OPACITY_DECREASE) {
            g_bgAlpha -= 20; if (g_bgAlpha < 0) g_bgAlpha = 0;
            confWrite();
            RenderTip(hWnd);
        }
        if (LOWORD(wParam) == ID_TODO_CHANGE) {
            TODO_PATH = OpenFileDialog();
            confWrite();
            RenderTip(hWnd); // 清除整个窗口并重绘
        }
        if (LOWORD(wParam) == ID_TRAY_REFRESH) {
            // 强制刷新窗口内容
            RenderTip(hWnd); // 清除整个窗口并重绘
        }
        if (LOWORD(wParam) == ID_TRAY_EDIT) {

            // Prepare SHELLEXECUTEINFO to open Notepad.exe
            SHELLEXECUTEINFO sei = { 0 };
            sei.cbSize = sizeof(sei);
            sei.fMask = SEE_MASK_NOCLOSEPROCESS;
            sei.lpVerb = "open";
            sei.lpFile = "notepad.exe";               // explicitly Notepad
            sei.lpParameters = TODO_PATH.c_str();         // your config path
            sei.nShow = SW_SHOWNORMAL;

            if (ShellExecuteEx(&sei) && sei.hProcess)
            {
                // Wait until Notepad exits (user closed the editor)
                WaitForSingleObject(sei.hProcess, INFINITE);
                CloseHandle(sei.hProcess);

                // === YOUR POST-EDIT LOGIC HERE ===
                // e.g., reload the config, reapply settings, log a message, etc.
                RenderTip(hWnd); // 清除整个窗口并重绘
            }
        }
        break;
    case WM_LBUTTONDBLCLK:
    {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        if (HitTestTodoCheckbox(mx, my) >= 0) return 0;

        // Prepare SHELLEXECUTEINFO to open Notepad.exe
        SHELLEXECUTEINFO sei = { 0 };
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = "open";
        sei.lpFile = "notepad.exe";               // explicitly Notepad
        sei.lpParameters = TODO_PATH.c_str();         // your config path
        sei.nShow = SW_SHOWNORMAL;

        if (ShellExecuteEx(&sei) && sei.hProcess)
        {
            // Wait until Notepad exits (user closed the editor)
            WaitForSingleObject(sei.hProcess, INFINITE);
            CloseHandle(sei.hProcess);

            // === YOUR POST-EDIT LOGIC HERE ===
            // e.g., reload the config, reapply settings, log a message, etc.
            RenderTip(hWnd); // 清除整个窗口并重绘
        }
        break;
    }
    case WM_NCLBUTTONDBLCLK:
    {
        // Prepare SHELLEXECUTEINFO to open Notepad.exe
        SHELLEXECUTEINFO sei = { 0 };
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = "open";
        sei.lpFile = "notepad.exe";               // explicitly Notepad
        sei.lpParameters = TODO_PATH.c_str();         // your config path
        sei.nShow = SW_SHOWNORMAL;

        if (ShellExecuteEx(&sei) && sei.hProcess)
        {
            // Wait until Notepad exits (user closed the editor)
            WaitForSingleObject(sei.hProcess, INFINITE);
            CloseHandle(sei.hProcess);

            // === YOUR POST-EDIT LOGIC HERE ===
            // e.g., reload the config, reapply settings, log a message, etc.
            RenderTip(hWnd); // 清除整个窗口并重绘
        }
        break;
    }
    case WM_REMINDER_STOP:
        StopReminderAlert(hWnd);
        return 0;
    case WM_TIMER:
        if (wParam == TIMER_REFRESH) {
            if (g_displayMode == 1) {
                // 周期性重新置顶：防止个别全屏/置顶程序抢走最上层
                SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            RenderTip(hWnd);
        }
        if (wParam == TIMER_REMINDER_CHECK) {
            CheckReminder(hWnd);
        }
        if (wParam == TIMER_REMINDER_ANIMATE) {
            AnimateReminderAlert(hWnd);
        }
        break;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

// 枚举顶层窗口，定位真正承载桌面图标的 WorkerW：
// 含有 SHELLDLL_DefView 的那个窗口，其紧邻的 WorkerW 兄弟即壁纸绘制层，可稳定附着。
BOOL CALLBACK FindWorkerWProc(HWND topHandle, LPARAM lParam) {
    HWND defView = FindWindowExA(topHandle, NULL, "SHELLDLL_DefView", NULL);
    if (defView != NULL) {
        HWND worker = FindWindowExA(NULL, topHandle, "WorkerW", NULL);
        if (worker != NULL) {
            *(HWND*)lParam = worker;
            return FALSE;   // 找到即停止
        }
    }
    return TRUE;            // 继续枚举
}

// 兜底（沿用 DesktopTip_backup.cpp 的做法）：找 Progman 下的 WorkerW 子窗口。
// 在“壁纸由 Progman/DWM 合成、没有独立顶层壁纸 WorkerW”的机器(本机/部分 RDP/显卡环境)上，
// 这是唯一能拿到的桌面层句柄。把卡片设为它的“拥有者窗口(owned popup)”即可常驻桌面。
static BOOL CALLBACK FindChildWorkerProc(HWND hwnd, LPARAM lParam) {
    char cls[64] = { 0 };
    GetClassNameA(hwnd, cls, sizeof(cls));
    if (strcmp(cls, "WorkerW") == 0) { *(HWND*)lParam = hwnd; return FALSE; }
    return TRUE;
}
static HWND FindChildWorkerW() {
    HWND progman = FindWindowA("Progman", NULL);
    if (!progman) return NULL;
    SendMessageTimeout(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, NULL);
    HWND child = NULL;
    EnumChildWindows(progman, FindChildWorkerProc, (LPARAM)&child);
    return child ? child : progman;   // 连子 WorkerW 都没有时退而用 Progman 自身
}


// 应用显示模式：
//   mode 0 = 附着桌面（默认）：把卡片绑定到 WorkerW(桌面壁纸层)，像挂件一样常驻桌面、
//            免疫 Win+D / “显示桌面”。
//   mode 1 = 始终置顶：解除桌面绑定，作为独立顶层窗口浮在所有窗口最上层。
//
// 桌面绑定使用 owned-popup：卡片始终保持 WS_POPUP 顶层窗口，不加 WS_CHILD，
// 通过 GWLP_HWNDPARENT 指向 Progman 的子 WorkerW（没有则退到 Progman）。
// 这样卡片仍按 UpdateLayeredWindow 正常逐像素透明绘制，又因“被桌面层拥有”而随桌面常驻、
// 免疫 Win+D。实测本机上 SetParent 成子级会被合成到壁纸之下，或出现层级行为异常。
void ApplyDisplayMode(HWND hwnd, int mode) {
    LONG_PTR ex = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    ex |= (WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE); // 保持 工具/分层/不激活

    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    style &= ~WS_CHILD;
    style |= WS_POPUP;
    SetWindowLongPtr(hwnd, GWL_STYLE, style);

    // 每次切换/重新选择显示模式时，都先彻底解除旧的父子/owner 关系，
    // 再重新寻找当前有效的桌面 parent 并贴合，避免睡眠恢复、切屏后沿用失效句柄。
    if (GetParent(hwnd) != NULL) {
        g_reparenting = true;
        SetParent(hwnd, NULL);
        g_reparenting = false;
    }
    SetWindowLongPtr(hwnd, GWLP_HWNDPARENT, 0);

    if (mode == 1) {
        // —— 始终置顶 ——
        ex |= WS_EX_TOPMOST;
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        // —— 附着桌面（默认）——
        ex &= ~WS_EX_TOPMOST;
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex);

        // 直接使用稳定的 owned-popup 桌面层绑定。SetParent 到现代 WorkerW 在部分
        // Explorer/DPI/壁纸合成环境里会“父窗口校验成功但 Win+D/层级行为异常”。
        if (GetParent(hwnd) != NULL) {
            g_reparenting = true;
            SetParent(hwnd, NULL);
            g_reparenting = false;
        }
        HWND deskLayer = FindChildWorkerW();          // Progman 的子 WorkerW（实在没有则 Progman）
        SetWindowLongPtr(hwnd, GWLP_HWNDPARENT, (LONG_PTR)deskLayer);
        SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0,    // 压到底层，被前台窗口正常覆盖；owner 使其仍在壁纸之上
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    RenderTip(hwnd);   // 绑定/层级变化后重绘（UpdateLayeredWindow 会按屏幕坐标重新定位）
}

// 探测“现代独立壁纸层 WorkerW”：即 0x052C 之后由 Explorer 创建、与承载 SHELLDLL_DefView 的
// 窗口相邻、位于桌面图标之下的那个 WorkerW。当前附着桌面逻辑不再把卡片 SetParent 到这里，
// 但保留该探测，便于后续诊断 Explorer 桌面层结构。
void FindWorkerW() {
    g_hWorkerW = NULL;

    // 让 Explorer 创建用于承载壁纸的 WorkerW（Win10/11 关键消息）
    HWND progman = FindWindowA("Progman", NULL);
    if (progman) {
        SendMessageTimeout(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, NULL);
    }

    // 枚举顶层窗口，找到承载 SHELLDLL_DefView 的窗口及其相邻 WorkerW 兄弟（真正的壁纸层）。
    EnumWindows(FindWorkerWProc, (LPARAM)&g_hWorkerW);
}

void MoveWindowToTopRight(HWND hWnd) {
    // 获取屏幕的分辨率
    RECT rectScreen;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &rectScreen, 0);

    // 获取窗口的宽度和高度
    RECT rectWindow;
    GetWindowRect(hWnd, &rectWindow);
    int windowWidth = rectWindow.right - rectWindow.left;
    int windowHeight = rectWindow.bottom - rectWindow.top;

    // 计算窗口的新位置
    int posX = rectScreen.right - windowWidth; // 屏幕右边缘减去窗口宽度
    int posY = rectScreen.top;                // 屏幕顶部

    // 设置窗口的位置
    SetWindowPos(hWnd, HWND_TOP, posX, posY, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
}

// 读取配置文件：返回存储的 TODO 路径，若无效或不存在则返回空字符串
std::string readConfig()
{
    std::ifstream fin(CONFIG_PATH);
    if (!fin.is_open()) return "";
    std::string path;
    std::getline(fin, path);
    std::getline(fin, path);
    if (path.empty() || !std::filesystem::exists(path)) return "";
    return path;
}

// 打开文件夹选择对话框，返回用户选中的路径；若取消则返回空字符串
std::string browseForFolder()
{
    BROWSEINFO bi = { 0 };
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
    if (!pidl) return "";
    char folder[MAX_PATH];
    SHGetPathFromIDListA(pidl, folder);
    CoTaskMemFree(pidl);
    return folder;
}

#include <windows.h>
#include <commdlg.h>
// 文件选择函数
std::string OpenFileDialog() {
    // 初始化结构体
    OPENFILENAME ofn;       // 文件对话框结构体
    char szFile[260] = { 0 }; // 缓冲区，用于存储文件路径

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;  // 无所有者窗口
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "TODO List File\0*.TXT\0";
    ofn.nFilterIndex = 1;  // 默认使用第一个过滤器
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL; // 初始目录（NULL表示当前目录）
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    // 打开文件选择对话框
    if (GetOpenFileName(&ofn)) {
        return std::string(ofn.lpstrFile);
    }
    else {
        return ""; // 用户取消对话框时返回空字符串
    }
}

void confCreate() {
    // 检查并创建 basePath
    if (!fs::exists(BASE_PATH))
        fs::create_directories(BASE_PATH);
    // 检查并创建 config 文件
    if (!fs::exists(CONFIG_PATH)) {
        //std::filesystem::path homeDir = std::getenv("OneDrive");
        TODO_PATH = string("TODO.txt");
        std::ofstream ofs(CONFIG_PATH);
        fontHeight = MulDiv(18, GetDeviceCaps(GetDC(hWnd), LOGPIXELSY), 72);
        lineHeight = (int)(fontHeight * 1.5);
        ofs << "Window_X: " << 0 << std::endl;
        ofs << "Window_Y: " << 0 << std::endl;
        ofs << "TODO_path: " << TODO_PATH << std::endl;
        ofs << "FontHeight: " << fontHeight << std::endl;
        ofs << "LineHeight: " << lineHeight << std::endl;

        // 保存默认文本颜色
        ofs << "TextColor_R: " << (int)GetRValue(g_textColor) << std::endl;
        ofs << "TextColor_G: " << (int)GetGValue(g_textColor) << std::endl;
        ofs << "TextColor_B: " << (int)GetBValue(g_textColor) << std::endl;
        ofs << "BgAlpha: " << g_bgAlpha << std::endl;
        ofs << "BgColor_R: " << (int)GetRValue(g_bgColor) << std::endl;
        ofs << "BgColor_G: " << (int)GetGValue(g_bgColor) << std::endl;
        ofs << "BgColor_B: " << (int)GetBValue(g_bgColor) << std::endl;
        ofs << "FontFamily: " << WideToUtf8(g_fontFamily) << std::endl;
        ofs << "DisplayMode: " << g_displayMode << std::endl;
        ofs << "CountdownRepeat: " << (g_countdownRepeatEnabled ? 1 : 0) << std::endl;
    }
}

void readFromFile(int& x, int& y) {
    std::ifstream ifs(CONFIG_PATH);
    std::string line;

    // 默认颜色分量值（如果配置文件中没有）
    int r = GetRValue(g_textColor);
    int g = GetGValue(g_textColor);
    int b = GetBValue(g_textColor);
    int ba = g_bgAlpha;
    int br = GetRValue(g_bgColor);
    int bgc = GetGValue(g_bgColor);
    int bb = GetBValue(g_bgColor);

    while (std::getline(ifs, line)) {
        if (line.find("Window_X:") == 0) {
            x = std::stoi(line.substr(line.find(":") + 2));
        }
        else if (line.find("Window_Y:") == 0) {
            y = std::stoi(line.substr(line.find(":") + 2));
        }
        else if (line.find("TODO_path:") == 0) {
            TODO_PATH = line.substr(line.find(":") + 2); // +2 to skip ": "
        }
        else if (line.find("FontHeight:") == 0) {
            fontHeight = std::stoi(line.substr(line.find(":") + 2));
        }
        else if (line.find("LineHeight:") == 0) {
            lineHeight = std::stoi(line.substr(line.find(":") + 2));
        }
        else if (line.find("TextColor_R:") == 0) {
            r = std::stoi(line.substr(line.find(":") + 2));
        }
        else if (line.find("TextColor_G:") == 0) {
            g = std::stoi(line.substr(line.find(":") + 2));
        }
        else if (line.find("TextColor_B:") == 0) {
            b = std::stoi(line.substr(line.find(":") + 2));
        }
        else if (line.find("BgAlpha:") == 0) {
            ba = std::stoi(line.substr(line.find(":") + 2));
        }
        else if (line.find("BgColor_R:") == 0) {
            br = std::stoi(line.substr(line.find(":") + 2));
        }
        else if (line.find("BgColor_G:") == 0) {
            bgc = std::stoi(line.substr(line.find(":") + 2));
        }
        else if (line.find("BgColor_B:") == 0) {
            bb = std::stoi(line.substr(line.find(":") + 2));
        }
        else if (line.find("FontFamily:") == 0) {
            g_fontFamily = DecodeAutoToUtf16(line.substr(line.find(":") + 2));
            if (g_fontFamily.empty()) g_fontFamily = L"Microsoft YaHei UI";
        }
        else if (line.find("DisplayMode:") == 0) {
            g_displayMode = std::stoi(line.substr(line.find(":") + 2));
        }
        else if (line.find("CountdownRepeat:") == 0) {
            g_countdownRepeatEnabled = (std::stoi(line.substr(line.find(":") + 2)) != 0);
        }
    }

    // 重建颜色值
    g_textColor = RGB(r, g, b);
    g_bgColor = RGB(br, bgc, bb);
    g_bgAlpha = (ba < 0) ? 0 : (ba > 255 ? 255 : ba);

    if (!fs::exists(TODO_PATH)){
        std::string localTodo = (std::filesystem::current_path() / "TODO.txt").string();
        // 2. 若无有效配置，则检查当前目录是否有 TODO.txt
        // 在 readFromFile 函数中 localTodo 检查后添加如下代码：
        if (std::filesystem::exists(localTodo)) {
            TODO_PATH = localTodo;
        } else {
            // 新增：如果 localTodo 不存在，则创建并写入初始内容
            std::ofstream todoFile(localTodo);
            if (todoFile.is_open()) {
                // 行角色：# 大字标题(时钟)  > 灰字次要(日期/页脚)  - 圆点待办  空行=间隔
                todoFile << "# {{TIME}}\n";
                todoFile << "> {{DATE}}  {{WEEK}}\n";
                todoFile << "- 项目：撰写结题报告\n";
                todoFile << "- 论文：提升论文质量\n";
                todoFile << "> 本周剩 {{DAYS_LEFT_WEEK}} · 本月剩 {{DAYS_LEFT_MONTH}} · 本年剩 {{DAYS_LEFT_YEAR}} 天\n";
                todoFile.close();
            }
            
        }
        TODO_PATH = localTodo;
        confWrite();
    }
}

int APIENTRY _tWinMain(HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPTSTR    lpCmdLine,
    int       nCmdShow)
{
    // DPI 识别：尽量与 Explorer（壁纸层 Progman/WorkerW 的属主，Win10/11 上为 Per-Monitor-V2）
    // 保持一致。否则附着桌面相关的坐标/层级操作容易在多屏或缩放变化后表现异常。
    // 旧代码这里调用 SetProcessDPIAware()（= System 识别），正是造成与 Explorer 不一致的根因。
    // 注意：本调用必须在创建任何窗口/获取任何 DC 之前执行；用动态加载以兼容旧系统并安全回退。
    {
        BOOL dpiOk = FALSE;
        HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
        if (hUser32) {
            typedef BOOL(WINAPI* PFN_SetCtx)(HANDLE);
            PFN_SetCtx pSetCtx = (PFN_SetCtx)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
            if (pSetCtx) dpiOk = pSetCtx((HANDLE)-4); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        }
        if (!dpiOk) {
            // 回退到 Win8.1+ 的 Shcore!SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE)
            HMODULE hShcore = LoadLibraryW(L"Shcore.dll");
            if (hShcore) {
                typedef HRESULT(WINAPI* PFN_SetAw)(int);
                PFN_SetAw pSetAw = (PFN_SetAw)GetProcAddress(hShcore, "SetProcessDpiAwareness");
                if (pSetAw) pSetAw(2); // PROCESS_PER_MONITOR_DPI_AWARE
            }
        }
    }
    hInst = hInstance;
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc); // 启用滑动条等通用控件
    Gdiplus::GdiplusStartupInput gdiInput;
    ULONG_PTR gdiToken = 0;
    Gdiplus::GdiplusStartup(&gdiToken, &gdiInput, NULL);

    confCreate();
    //readFromFile(xx, yy, path);

    // 注册窗口类
    WNDCLASS wc = { 0 };
    wc.style = CS_DBLCLKS;   // 启用双击消息（双击卡片编辑 TODO）
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = _T("DesktopFixedTextClass");
    wc.hbrBackground = NULL; // 不使用背景画刷

    RegisterClass(&wc);

    // 创建窗口样式：无边框、工具窗口、无任务栏按钮、无 Alt+Tab 显示
    // WS_POPUP：无边框
    // WS_VISIBLE：可见
    // WS_EX_TOOLWINDOW：不会在 Alt+Tab 中显示
    // WS_EX_NOACTIVATE：不激活窗口
    // 不使用 WS_EX_APPWINDOW，这样不会在任务栏显示
    DWORD dwStyle = WS_POPUP | WS_VISIBLE;
    DWORD dwExStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED;

    // 指定窗口大小与位置（例如在屏幕中央）
    // 这里假设窗口大小为 300x100，在屏幕中央显示
    RECT desktopRect;
    GetClientRect(GetDesktopWindow(), &desktopRect);
    RECT rectScreen;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &rectScreen, 0);
    int width = rectScreen.right- rectScreen.left;
    int height = rectScreen.bottom- rectScreen.top;
    int x = (desktopRect.right - width) / 2;
    int y = (desktopRect.bottom - height) / 2;

    readFromFile(x, y);

    width = 64; height = 64; // 占位尺寸，首次渲染由卡片实际尺寸决定
    FindWorkerW();
    
    hWnd = CreateWindowEx(
        dwExStyle,
        wc.lpszClassName,
        NULL,
        dwStyle,
        x, y, width, height,
        NULL,            // 顶层窗口：分层渲染(UpdateLayeredWindow)必须是顶层，不能挂为子窗口
        NULL,
        hInstance,
        NULL);

    if (!hWnd)
        return 0;

    AddTrayIcon(hWnd);

    RenderTip(hWnd); // 首次绘制

    // 应用已保存的显示模式（附着桌面 / 始终置顶）
    ApplyDisplayMode(hWnd, g_displayMode);

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    Gdiplus::GdiplusShutdown(gdiToken);
    return (int)msg.wParam;
}

//
//  函数: MyRegisterClass()
//
//  目标: 注册窗口类。
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_DESKTOPTIP));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_DESKTOPTIP);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   函数: InitInstance(HINSTANCE, int)
//
//   目标: 保存实例句柄并创建主窗口
//
//   注释:
//
//        在此函数中，我们在全局变量中保存实例句柄并
//        创建和显示主程序窗口。
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // 将实例句柄存储在全局变量中

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

// “关于”框的消息处理程序。
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
