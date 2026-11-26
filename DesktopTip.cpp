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
#define ID_HOVER_HIDE 25
// 单项任务计时（右键某条待办）
#define ID_TASK_CD_FIRST 101      // 101..108 = 预设倒计时
#define ID_TASK_CD_CUSTOM 110
#define ID_TASK_STOPWATCH 111
#define ID_TASK_PAUSE 112
#define ID_TASK_RESTART 113
#define ID_TASK_REMOVE 114

#define TIMER_REFRESH 1
#define TIMER_REMINDER_CHECK 2
#define TIMER_ANIM 3
#define TIMER_HOVER_HIDE 4

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
    std::wstring key;       // 任务文本（去掉 "- " 与 "_OK"），用作计时器的键
    bool completed;
};

static std::vector<TodoHitRect> g_todoHitRects;   // 复选框
static std::vector<TodoHitRect> g_rowHitRects;    // 整行（右键为该任务设置计时）
static std::vector<TodoHitRect> g_pillHitRects;   // 计时胶囊（单击暂停/继续）

struct BannerButton { RECT rc; int action; };
enum { BANNER_DONE = 1, BANNER_SNOOZE = 2, BANNER_DISMISS = 3 };
static std::vector<BannerButton> g_bannerButtons;

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
static std::wstring g_alertTaskKey;          // 触发提醒的任务；空 = 全局倒计时/闹钟
static std::vector<std::wstring> g_systemFonts;

// ---- 每项任务的计时器（倒计时 / 正计时）----
// 保存在 C:\ProgramData\DesktopTip\timers，每行：kind\tduration\tstart\taccum\tfired\t任务文本
// kind: down = 倒计时, up = 正计时；start 为本次开始计时的 Unix 秒（0 = 暂停中）；accum 为此前累计秒数。
struct TaskTimer {
    bool countdown = true;
    long long duration = 0;
    long long start = 0;
    long long accum = 0;
    bool fired = false;
    std::wstring key;
};
static std::vector<TaskTimer> g_taskTimers;
static bool g_taskTimersLoaded = false;
static std::wstring g_menuTaskKey;           // 右键菜单所针对的任务

// ---- 动画 ----
// 完成动画：勾选后复选框弹出填充 → 对勾描绘 → 涟漪与光点 → 删除线划过，结束后才写回文件；
// 写回后已完成项沉底，各行从旧位置平滑滑到新位置（settle）。
static const double COMPLETE_ANIM_SEC = 0.70;
static const double SETTLE_ANIM_SEC = 0.32;
struct CompleteAnim {
    bool active = false;
    int sourceIndex = -1;
    std::wstring key;
    steady_clock::time_point start;
};
static CompleteAnim g_completeAnim;
static std::unordered_map<std::wstring, float> g_lastRowY;     // 上一帧各待办行的 y
static std::unordered_map<std::wstring, float> g_settleFrom;   // 滑动起点
static bool g_settling = false;
static steady_clock::time_point g_settleStart;
static std::wstring g_justCompletedKey;                        // 刚完成的项：滑动期间从强调色淡出
static bool g_animTimerOn = false;
static UINT g_animTimerInterval = 0;

// ---- 到点抖动 ----
static const double SHAKE_SEC = 0.75;
static const int SHAKE_REPEAT_SEC = 6;     // 未处理时每 6 秒再抖一次
static const int SHAKE_MAX_TIMES = 6;
static bool g_shaking = false;
static steady_clock::time_point g_shakeStart;
static POINT g_shakeBase = { 0, 0 };
static int g_shakeTimes = 0;
// 置顶时鼠标悬停隐藏：
//   在「始终置顶」模式下，鼠标移到卡片上时卡片淡出并变为鼠标穿透(WS_EX_TRANSPARENT)，
//   这样既能常驻最上层，又不会挡住下面窗口的关键信息/点击；鼠标移开后自动淡入恢复。
//   按住 Ctrl 时卡片保持显示且可正常操作（拖动、勾选待办、右键菜单）。
//   卡片穿透后收不到 WM_MOUSELEAVE，所以用一个轻量计时器轮询光标位置。
static bool g_hoverHideEnabled = true;   // 设置项（配置键 HoverHide），默认开启
static bool g_hoverHidden = false;       // 当前是否处于“悬停隐藏 + 穿透”状态
static int  g_hoverAlpha = 255;          // 卡片整体不透明度（用于淡入淡出）
static bool g_contextMenuOpen = false;   // 右键菜单弹出期间不隐藏
static const int HOVER_HIDE_POLL_MS = 30;
static const int HOVER_HIDE_FADE_STEP = 64;   // 每次 tick 的 alpha 变化量（约 120ms 完成淡入/淡出）

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

static bool HoverHideActive()
{
    return g_hoverHideEnabled && g_displayMode == 1 && !g_alertActive && !g_restoringAlert;
}

static void SetClickThrough(HWND hwnd, bool on)
{
    LONG_PTR ex = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    LONG_PTR nex = on ? (ex | WS_EX_TRANSPARENT) : (ex & ~(LONG_PTR)WS_EX_TRANSPARENT);
    if (nex != ex) SetWindowLongPtr(hwnd, GWL_EXSTYLE, nex);
}

// 只改整体不透明度，不重新绘制内容（hdcSrc 传 NULL 时 UpdateLayeredWindow 保留现有位图）
static void ApplyHoverAlpha(HWND hwnd)
{
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)g_hoverAlpha, AC_SRC_ALPHA };
    UpdateLayeredWindow(hwnd, NULL, NULL, NULL, NULL, NULL, 0, &bf, ULW_ALPHA);
}

static void ResetHoverHide(HWND hwnd)
{
    KillTimer(hwnd, TIMER_HOVER_HIDE);
    g_hoverHidden = false;
    SetClickThrough(hwnd, false);
    if (g_hoverAlpha != 255) {
        g_hoverAlpha = 255;
        ApplyHoverAlpha(hwnd);
    }
}

// 根据当前显示模式/设置启动或停止悬停检测
static void UpdateHoverHideTimer(HWND hwnd)
{
    if (HoverHideActive()) SetTimer(hwnd, TIMER_HOVER_HIDE, HOVER_HIDE_POLL_MS, nullptr);
    else ResetHoverHide(hwnd);
}

static void TickHoverHide(HWND hwnd)
{
    if (!HoverHideActive()) { ResetHoverHide(hwnd); return; }

    POINT pt; GetCursorPos(&pt);
    RECT wr; GetWindowRect(hwnd, &wr);
    bool inside = PtInRect(&wr, pt) != FALSE;
    bool reveal = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;   // 按住 Ctrl：显示并可操作
    bool busy = g_dragging || g_contextMenuOpen || GetCapture() == hwnd;
    bool wantHidden = inside && !reveal && !busy;

    if (wantHidden != g_hoverHidden) {
        g_hoverHidden = wantHidden;
        SetClickThrough(hwnd, wantHidden);
    }

    int target = wantHidden ? 0 : 255;
    if (g_hoverAlpha != target) {
        if (target > g_hoverAlpha) g_hoverAlpha = (std::min)(target, g_hoverAlpha + HOVER_HIDE_FADE_STEP);
        else                       g_hoverAlpha = (std::max)(target, g_hoverAlpha - HOVER_HIDE_FADE_STEP);
        ApplyHoverAlpha(hwnd);
    }
}

// ============================================================================
//  每项任务的计时器（倒计时 / 正计时）
// ============================================================================
static const char* TIMERS_DIR = "C:\\ProgramData\\DesktopTip";
static const char* TIMERS_PATH = "C:\\ProgramData\\DesktopTip\\timers";
static const int kTaskPresetMinutes[] = { 5, 10, 15, 25, 30, 45, 60, 90 };
static const int kTaskPresetCount = (int)(sizeof(kTaskPresetMinutes) / sizeof(kTaskPresetMinutes[0]));

static long long NowEpoch() { return (long long)std::time(nullptr); }

static long long TimerElapsed(const TaskTimer& t)
{
    long long e = t.accum + (t.start > 0 ? NowEpoch() - t.start : 0);
    return e < 0 ? 0 : e;
}

static void LoadTaskTimers()
{
    g_taskTimersLoaded = true;
    g_taskTimers.clear();
    std::ifstream f(TIMERS_PATH, std::ios::binary);
    if (!f.is_open()) return;
    std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    for (const auto& line : SplitLinesW(DecodeAutoToUtf16(bytes))) {
        std::vector<std::wstring> p = SplitWide(line, L'\t');
        if (p.size() < 6) continue;
        TaskTimer t;
        t.countdown = (p[0] == L"down");
        try {
            t.duration = std::stoll(p[1]);
            t.start = std::stoll(p[2]);
            t.accum = std::stoll(p[3]);
        } catch (...) { continue; }
        t.fired = (p[4] == L"1");
        std::wstring key = p[5];
        for (size_t k = 6; k < p.size(); ++k) key += L"\t" + p[k];
        if (key.empty()) continue;
        t.key = key;
        g_taskTimers.push_back(t);
    }
}

static void SaveTaskTimers()
{
    std::error_code ec;
    fs::create_directories(TIMERS_DIR, ec);
    std::ofstream f(TIMERS_PATH, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return;
    for (const auto& t : g_taskTimers) {
        std::ostringstream o;
        o << (t.countdown ? "down" : "up") << '\t' << t.duration << '\t' << t.start << '\t'
          << t.accum << '\t' << (t.fired ? 1 : 0) << '\t' << WideToUtf8(t.key) << '\n';
        f << o.str();
    }
}

static TaskTimer* FindTaskTimer(const std::wstring& key)
{
    if (!g_taskTimersLoaded) LoadTaskTimers();
    for (auto& t : g_taskTimers) {
        if (t.key == key) return &t;
    }
    return nullptr;
}

static TaskTimer& EnsureTaskTimer(const std::wstring& key)
{
    if (TaskTimer* t = FindTaskTimer(key)) return *t;
    TaskTimer t;
    t.key = key;
    g_taskTimers.push_back(t);
    return g_taskTimers.back();
}

static bool AnyTaskTimerRunning()
{
    if (!g_taskTimersLoaded) LoadTaskTimers();
    for (const auto& t : g_taskTimers) {
        if (t.start > 0) return true;
    }
    return false;
}

static void StartTaskCountdown(const std::wstring& key, long long seconds)
{
    TaskTimer& t = EnsureTaskTimer(key);
    t.countdown = true;
    t.duration = seconds;
    t.accum = 0;
    t.start = NowEpoch();
    t.fired = false;
    SaveTaskTimers();
}

static void StartTaskStopwatch(const std::wstring& key)
{
    TaskTimer& t = EnsureTaskTimer(key);
    t.countdown = false;
    t.duration = 0;
    t.accum = 0;
    t.start = NowEpoch();
    t.fired = false;
    SaveTaskTimers();
}

static void PauseTaskTimer(TaskTimer& t)
{
    if (t.start > 0) {
        t.accum = TimerElapsed(t);
        t.start = 0;
    }
}

static void ToggleTaskTimerPause(const std::wstring& key)
{
    TaskTimer* t = FindTaskTimer(key);
    if (!t) return;
    if (t->start > 0) PauseTaskTimer(*t);
    else t->start = NowEpoch();
    SaveTaskTimers();
}

static void RestartTaskTimer(const std::wstring& key)
{
    TaskTimer* t = FindTaskTimer(key);
    if (!t) return;
    t->accum = 0;
    t->start = NowEpoch();
    t->fired = false;
    SaveTaskTimers();
}

static void RemoveTaskTimer(const std::wstring& key)
{
    if (!g_taskTimersLoaded) LoadTaskTimers();
    g_taskTimers.erase(std::remove_if(g_taskTimers.begin(), g_taskTimers.end(),
        [&](const TaskTimer& t) { return t.key == key; }), g_taskTimers.end());
    SaveTaskTimers();
}

// 「再等 N 分钟」：把倒计时延长到“从现在起还剩 N 秒”，并继续计时
static void SnoozeTaskTimer(const std::wstring& key, long long seconds)
{
    TaskTimer* t = FindTaskTimer(key);
    if (!t) return;
    long long el = TimerElapsed(*t);
    t->countdown = true;
    t->duration = el + seconds;
    if (t->start == 0) t->start = NowEpoch();
    t->fired = false;
    SaveTaskTimers();
}

// 计时显示：mm:ss 或 h:mm:ss
static std::wstring FormatClock(long long s)
{
    if (s < 0) s = 0;
    wchar_t buf[32];
    long long h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
    if (h > 0) swprintf(buf, 32, L"%lld:%02lld:%02lld", h, m, sec);
    else swprintf(buf, 32, L"%02lld:%02lld", m, sec);
    return buf;
}

static std::wstring FormatShortDuration(long long s)
{
    wchar_t buf[64];
    if (s < 60) swprintf(buf, 64, L"%lld 秒", s);
    else if (s % 60 != 0) swprintf(buf, 64, L"%lld 分 %lld 秒", s / 60, s % 60);
    else {
        long long m = s / 60;
        if (m < 60) swprintf(buf, 64, L"%lld 分钟", m);
        else if (m % 60 == 0) swprintf(buf, 64, L"%lld 小时", m / 60);
        else swprintf(buf, 64, L"%lld 小时 %lld 分钟", m / 60, m % 60);
    }
    return buf;
}

static std::wstring TodoKeyOfLine(const std::wstring& line, bool* completed)
{
    std::wstring text;
    if (!ParseTodoLineState(line, &text, completed)) return L"";
    return TrimCopy(text);
}

// 按任务文本切换完成状态（sourceIndex 只是提示，文件期间被改过也能找到正确的行）
static bool ToggleTodoByKey(int sourceIndex, const std::wstring& key)
{
    std::vector<std::wstring> lines;
    if (!ReadTodoFileLinesW(lines)) return false;
    int idx = -1;
    if (sourceIndex >= 0 && (size_t)sourceIndex < lines.size() &&
        IsTodoLine(lines[(size_t)sourceIndex]) && TodoKeyOfLine(lines[(size_t)sourceIndex], nullptr) == key) {
        idx = sourceIndex;
    } else {
        for (size_t i = 0; i < lines.size(); ++i) {
            if (IsTodoLine(lines[i]) && TodoKeyOfLine(lines[i], nullptr) == key) { idx = (int)i; break; }
        }
    }
    if (idx < 0) return false;
    return ToggleTodoLineCompletion(idx);
}

// ============================================================================
//  到点提醒：卡片升到最前端 + 抖动 + 卡片顶部的提醒横幅（替代原先的全屏黑洞动画）
//  提醒期间暂停「悬停隐藏」，保证卡片可见、可点击；处理后恢复原显示模式。
// ============================================================================
static void SetAnimTimer(HWND hwnd);
static void BeginToggle(HWND hwnd, const TodoHitRect& hit);

static void RaiseForAlert(HWND hwnd)
{
    if (GetParent(hwnd) != NULL) {
        g_reparenting = true;
        SetParent(hwnd, NULL);
        g_reparenting = false;
    }
    SetWindowLongPtr(hwnd, GWLP_HWNDPARENT, 0);   // 解除与桌面层的 owner 关系，才能浮到最上层
    LONG_PTR ex = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    ex |= WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

static void StartShake(HWND hwnd)
{
    if (g_dragging) return;
    if (!g_shaking) {
        RECT wr; GetWindowRect(hwnd, &wr);
        g_shakeBase = { wr.left, wr.top };
    }
    g_shaking = true;
    g_shakeStart = steady_clock::now();
    g_shakeTimes++;
    MessageBeep(MB_ICONEXCLAMATION);
}

static void StopShake(HWND hwnd)
{
    if (!g_shaking) return;
    g_shaking = false;
    SetWindowPos(hwnd, NULL, g_shakeBase.x, g_shakeBase.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void TickShake(HWND hwnd)
{
    if (!g_shaking) return;
    if (g_dragging) { g_shaking = false; return; }   // 用户开始拖动：不再拉回
    double t = duration<double>(steady_clock::now() - g_shakeStart).count();
    if (t >= SHAKE_SEC) { StopShake(hwnd); return; }
    double base = (double)(fontHeight > 8 ? fontHeight : 24);
    double amp = (std::max)(10.0, base * 0.6);
    double k = 1.0 - t / SHAKE_SEC;                     // 衰减包络，收尾柔和
    int dx = (int)std::lround(amp * k * k * std::sin(2.0 * 3.14159265358979 * 7.5 * t));
    SetWindowPos(hwnd, NULL, g_shakeBase.x + dx, g_shakeBase.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void StartCardAlert(HWND hwnd, const std::wstring& title, const std::wstring& detail, const std::wstring& taskKey)
{
    g_alertTitle = title;
    g_alertDetail = detail;
    g_alertTaskKey = taskKey;
    g_alertStarted = steady_clock::now();
    g_shakeTimes = 0;
    if (!g_alertActive) {
        g_alertActive = true;
        ResetHoverHide(hwnd);   // 覆盖悬停隐藏：立即完全显示且可点击
    }
    RaiseForAlert(hwnd);
    StartShake(hwnd);
    SetAnimTimer(hwnd);
    RenderTip(hwnd);
}

static void StopCardAlert(HWND hwnd)
{
    if (!g_alertActive) return;
    StopShake(hwnd);
    g_restoringAlert = true;
    g_alertActive = false;
    g_alertTaskKey.clear();
    g_bannerButtons.clear();
    ApplyDisplayMode(hwnd, g_displayMode);   // 恢复原来的层级（附着桌面 / 始终置顶），并重绘
    g_restoringAlert = false;
    UpdateHoverHideTimer(hwnd);
    confWrite();
    SetAnimTimer(hwnd);
}

// 每秒检查任务倒计时是否到点
static void CheckTaskTimers(HWND hwnd)
{
    if (!g_taskTimersLoaded) LoadTaskTimers();
    std::vector<std::wstring> lines;
    bool fileOk = ReadTodoFileLinesW(lines);
    std::unordered_map<std::wstring, bool> openTasks;   // key -> 是否未完成
    for (const auto& l : lines) {
        bool done = false;
        if (!IsTodoLine(l)) continue;
        std::wstring k = TodoKeyOfLine(l, &done);
        if (!done) openTasks[k] = true;
        else if (!openTasks.count(k)) openTasks[k] = false;
    }

    bool changed = false;
    std::vector<TaskTimer> due;
    for (auto& t : g_taskTimers) {
        if (!t.countdown || t.fired || t.start == 0) continue;
        if (TimerElapsed(t) < t.duration) continue;
        t.fired = true;
        changed = true;
        auto it = openTasks.find(t.key);
        if (fileOk && (it == openTasks.end() || !it->second)) continue;   // 任务已完成或已删除：静默
        due.push_back(t);
    }
    if (changed) SaveTaskTimers();
    for (const auto& t : due) {
        StartCardAlert(hwnd, L"时间到 · " + FormatShortDuration(t.duration), ProcessPlaceholdersW(t.key), t.key);
    }
}

static std::vector<std::pair<int, std::wstring>> BannerButtonSpec()
{
    std::vector<std::pair<int, std::wstring>> v;
    bool task = !g_alertTaskKey.empty();
    if (task) v.push_back({ BANNER_DONE, L"完成" });
    if (task || !g_reminderArmed) v.push_back({ BANNER_SNOOZE, L"+5 分钟" });
    v.push_back({ BANNER_DISMISS, L"知道了" });
    return v;
}

static void HandleBannerAction(HWND hwnd, int action)
{
    std::wstring key = g_alertTaskKey;
    if (action == BANNER_DONE && !key.empty()) {
        StopCardAlert(hwnd);
        for (const auto& h : g_todoHitRects) {
            if (h.key == key && !h.completed) {
                TodoHitRect copy = h;
                BeginToggle(hwnd, copy);   // 播放完成动画
                return;
            }
        }
        if (ToggleTodoByKey(-1, key)) RenderTip(hwnd);
        return;
    }
    if (action == BANNER_SNOOZE) {
        if (!key.empty()) {
            SnoozeTaskTimer(key, 5 * 60);
        } else if (!g_reminderArmed) {
            g_reminderArmed = true;
            g_reminderDue = system_clock::now() + minutes(5);
            g_reminderTitle = L"倒计时结束";
            g_reminderDetail = L"稍后提醒（5 分钟）已到";
            g_reminderIsCountdown = false;
            g_reminderIntervalSeconds = 0;
        }
    }
    StopCardAlert(hwnd);
}

// ============================================================================
//  完成动画 / 行滑动
// ============================================================================
static void SetAnimTimer(HWND hwnd)
{
    bool fast = g_completeAnim.active || g_settling || g_shaking;
    bool on = fast || g_alertActive;
    if (!on) {
        if (g_animTimerOn) { KillTimer(hwnd, TIMER_ANIM); g_animTimerOn = false; }
        return;
    }
    UINT iv = fast ? 15 : 50;   // 动画 ~60fps；提醒横幅呼吸光 ~20fps
    if (!g_animTimerOn || iv != g_animTimerInterval) {
        SetTimer(hwnd, TIMER_ANIM, iv, nullptr);
        g_animTimerOn = true;
        g_animTimerInterval = iv;
    }
}

static void SnapshotRowsForSettle()
{
    g_settleFrom = g_lastRowY;
    g_settling = true;
    g_settleStart = steady_clock::now();
}

static void CommitCompletion(HWND hwnd)
{
    if (!g_completeAnim.active) return;
    CompleteAnim a = g_completeAnim;
    g_completeAnim.active = false;
    SnapshotRowsForSettle();
    if (ToggleTodoByKey(a.sourceIndex, a.key)) {
        g_justCompletedKey = a.key;
        if (TaskTimer* t = FindTaskTimer(a.key)) {   // 完成即停表，保留最终用时
            PauseTaskTimer(*t);
            t->fired = true;
            SaveTaskTimers();
        }
        if (g_alertActive && g_alertTaskKey == a.key) StopCardAlert(hwnd);
    } else {
        g_settling = false;
        g_settleFrom.clear();
    }
    SetAnimTimer(hwnd);
}

static void BeginToggle(HWND hwnd, const TodoHitRect& hit)
{
    if (g_completeAnim.active) CommitCompletion(hwnd);   // 上一个动画未结束：直接落定
    if (!hit.completed) {
        g_completeAnim.active = true;
        g_completeAnim.sourceIndex = hit.sourceIndex;
        g_completeAnim.key = hit.key;
        g_completeAnim.start = steady_clock::now();
    } else {
        SnapshotRowsForSettle();
        g_justCompletedKey.clear();
        if (!ToggleTodoByKey(hit.sourceIndex, hit.key)) {
            g_settling = false;
            g_settleFrom.clear();
        }
    }
    SetAnimTimer(hwnd);
    RenderTip(hwnd);
}

static void TickAnimations(HWND hwnd)
{
    auto now = steady_clock::now();
    if (g_completeAnim.active && duration<double>(now - g_completeAnim.start).count() >= COMPLETE_ANIM_SEC) {
        CommitCompletion(hwnd);
    }
    if (g_settling && duration<double>(now - g_settleStart).count() >= SETTLE_ANIM_SEC) {
        g_settling = false;
        g_settleFrom.clear();
        g_justCompletedKey.clear();
    }
    if (g_alertActive && !g_shaking && g_shakeTimes < SHAKE_MAX_TIMES &&
        duration<double>(now - g_shakeStart).count() >= SHAKE_REPEAT_SEC) {
        StartShake(hwnd);
    }
    TickShake(hwnd);
    RenderTip(hwnd);
    SetAnimTimer(hwnd);
}

static std::wstring Ellipsize(const std::wstring& s, size_t maxChars)
{
    if (s.size() <= maxChars) return s;
    return s.substr(0, maxChars) + L"…";
}

static void PromptTaskCountdown(HWND hwnd, const std::wstring& key)
{
    std::wstring value;
    if (!ShowTextPrompt(hwnd, L"任务倒计时", L"输入分钟数，或 MM:SS / HH:MM:SS", L"25", value)) return;
    int seconds = 0;
    if (!TryParseCountdownSeconds(value, seconds)) {
        MessageBoxW(hwnd, L"请输入有效的倒计时时长。", L"DesktopTip", MB_OK | MB_ICONWARNING);
        return;
    }
    StartTaskCountdown(key, seconds);
}

static void HandleTaskTimerCommand(HWND hwnd, int id)
{
    std::wstring key = g_menuTaskKey;
    if (key.empty()) return;
    if (id >= ID_TASK_CD_FIRST && id < ID_TASK_CD_FIRST + kTaskPresetCount) {
        StartTaskCountdown(key, (long long)kTaskPresetMinutes[id - ID_TASK_CD_FIRST] * 60);
    } else if (id == ID_TASK_CD_CUSTOM) {
        PromptTaskCountdown(hwnd, key);
    } else if (id == ID_TASK_STOPWATCH) {
        StartTaskStopwatch(key);
    } else if (id == ID_TASK_PAUSE) {
        ToggleTaskTimerPause(key);
    } else if (id == ID_TASK_RESTART) {
        RestartTaskTimer(key);
    } else if (id == ID_TASK_REMOVE) {
        RemoveTaskTimer(key);
        if (g_alertActive && g_alertTaskKey == key) StopCardAlert(hwnd);
    }
    RenderTip(hwnd);
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

    // 重复倒计时命中时，如果上一次提醒还没处理，则跳过本次提醒，直接进入下一轮计时。
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
    StartCardAlert(hwnd, title, detail, L"");

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

void ShowContextMenu(HWND hwnd, bool fromCard = false) {
    POINT pt;
    GetCursorPos(&pt);
    g_lastContextMenuPt = pt;
    g_hasLastContextMenuPt = true;

    HMENU hMenu = CreatePopupMenu();

    // 在某条待办上右键：顶部出现「任务计时」子菜单（倒计时 / 正计时 / 暂停 / 重新开始 / 移除）
    g_menuTaskKey.clear();
    if (fromCard) {
        POINT cp = pt;
        ScreenToClient(hwnd, &cp);
        for (const auto& row : g_rowHitRects) {
            if (!PtInRect(&row.rc, cp)) continue;
            g_menuTaskKey = row.key;
            HMENU hTask = CreatePopupMenu();
            HMENU hCd = CreatePopupMenu();
            for (int i = 0; i < kTaskPresetCount; ++i) {
                wchar_t buf[48];
                if (kTaskPresetMinutes[i] == 25) swprintf(buf, 48, L"25 分钟（番茄钟）");
                else if (kTaskPresetMinutes[i] % 60 == 0) swprintf(buf, 48, L"%d 小时", kTaskPresetMinutes[i] / 60);
                else swprintf(buf, 48, L"%d 分钟", kTaskPresetMinutes[i]);
                AppendMenuW(hCd, MF_STRING, ID_TASK_CD_FIRST + i, buf);
            }
            AppendMenuW(hCd, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hCd, MF_STRING, ID_TASK_CD_CUSTOM, L"自定义…  Custom");
            AppendMenuW(hTask, MF_POPUP, (UINT_PTR)hCd, L"倒计时  Countdown");
            AppendMenuW(hTask, MF_STRING, ID_TASK_STOPWATCH, L"正计时  Stopwatch");
            if (TaskTimer* t = FindTaskTimer(row.key)) {
                AppendMenuW(hTask, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hTask, MF_STRING, ID_TASK_PAUSE, t->start > 0 ? L"暂停  Pause" : L"继续  Resume");
                AppendMenuW(hTask, MF_STRING, ID_TASK_RESTART, L"重新开始  Restart");
                AppendMenuW(hTask, MF_STRING, ID_TASK_REMOVE, L"移除计时  Remove timer");
            }
            std::wstring title = L"任务计时：" + Ellipsize(ProcessPlaceholdersW(row.key), 16) + L"  Timer";
            AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hTask, title.c_str());
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            break;
        }
    }
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
    AppendMenuW(hModeMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hModeMenu, MF_STRING | (g_hoverHideEnabled ? MF_CHECKED : MF_UNCHECKED),
        ID_HOVER_HIDE, L"置顶时鼠标悬停隐藏（按住 Ctrl 可操作）  Hide on hover when on top");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hModeMenu, L"显示模式  Display mode");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EDIT,    L"编辑 TODO  Edit");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_REFRESH, L"刷新  Refresh");
    AppendMenuW(hMenu, MF_STRING, ID_TODO_CHANGE,  L"更换 TODO 文件  Change file");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT,    L"退出  Exit");
    SetMenuDefaultItem(hMenu, ID_SETTINGS, FALSE);
  
    // 显示菜单（弹出期间暂停悬停隐藏，避免菜单还开着卡片就消失）
    g_contextMenuOpen = true;
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    g_contextMenuOpen = false;

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
    ofs << "HoverHide: " << (g_hoverHideEnabled ? 1 : 0) << std::endl;
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


enum TipRowType { RT_SPACER, RT_HEADING, RT_META, RT_TODO };
struct TipRow {
    TipRowType type;
    std::wstring text;
    float w;
    float tw;          // 纯文字宽度（删除线动画用）
    float h;
    float y;
    int sourceIndex;
    bool completed;
    std::wstring key;  // 待办的原始文本（计时器的键）
};

static const double ANIM_PI = 3.14159265358979323846;
static double Clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
static double EaseOutCubic(double t) { t = Clamp01(t); double u = 1.0 - t; return 1.0 - u * u * u; }
static double EaseInOutCubic(double t) { t = Clamp01(t); return t < 0.5 ? 4.0 * t * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 3.0) / 2.0; }
static double EaseOutBack(double t)
{
    t = Clamp01(t);
    const double c1 = 1.70158, c3 = c1 + 1.0;
    return 1.0 + c3 * std::pow(t - 1.0, 3.0) + c1 * std::pow(t - 1.0, 2.0);
}
static Gdiplus::Color LerpColor(const Gdiplus::Color& a, const Gdiplus::Color& b, double t)
{
    t = Clamp01(t);
    auto L = [&](BYTE x, BYTE y) { return (BYTE)ClampByte(x + (y - x) * t); };
    return Gdiplus::Color(L(a.GetA(), b.GetA()), L(a.GetR(), b.GetR()), L(a.GetG(), b.GetG()), L(a.GetB(), b.GetB()));
}
static Gdiplus::Color WithAlpha(const Gdiplus::Color& c, double a)
{
    return Gdiplus::Color((BYTE)ClampByte(a), c.GetR(), c.GetG(), c.GetB());
}

// 对勾（按路径长度比例 t 逐步描绘）
static void DrawTick(Gdiplus::Graphics& g, float x, float y, float d, double t, const Gdiplus::Color& c, float width)
{
    using namespace Gdiplus;
    if (t <= 0.0) return;
    PointF p0(x + d * 0.23f, y + d * 0.53f), p1(x + d * 0.43f, y + d * 0.72f), p2(x + d * 0.78f, y + d * 0.28f);
    double l1 = std::hypot(p1.X - p0.X, p1.Y - p0.Y), l2 = std::hypot(p2.X - p1.X, p2.Y - p1.Y);
    double len = (l1 + l2) * Clamp01(t);
    Pen pen(c, width);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);
    if (len <= l1) {
        float k = (float)(len / l1);
        g.DrawLine(&pen, p0, PointF(p0.X + (p1.X - p0.X) * k, p0.Y + (p1.Y - p0.Y) * k));
    } else {
        float k = (float)((len - l1) / l2);
        PointF pts[3] = { p0, p1, PointF(p1.X + (p2.X - p1.X) * k, p1.Y + (p2.Y - p1.Y) * k) };
        g.DrawLines(&pen, pts, 3);
    }
}

// 阴影位图缓存：高斯模糊是渲染里最贵的一步，尺寸不变时复用（计时/动画需要频繁重绘）
static Gdiplus::Bitmap* g_shadowCache = nullptr;
static int g_shadowKey[6] = { 0 };

void RenderTip(HWND hwnd)
{
    using namespace Gdiplus;

    if (!g_taskTimersLoaded) LoadTaskTimers();
    auto nowSteady = steady_clock::now();
    double alertT = g_alertActive ? duration<double>(nowSteady - g_alertStarted).count() : 0.0;
    double pulse = g_alertActive ? 0.5 + 0.5 * std::sin(alertT * 2.0 * ANIM_PI / 1.4) : 0.0;

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
    float radius = base * 0.70f;

    // 卡片背景由 g_bgAlpha / g_bgColor 控制（可在托盘菜单调整）

    FontFamily* famText = PickFamily({ g_fontFamily.c_str(), L"Microsoft YaHei UI", L"Microsoft YaHei", L"微软雅黑" });
    FontFamily* famHead = PickFamily({ g_fontFamily.c_str(), L"Microsoft YaHei Light", L"Microsoft YaHei UI Light", L"Microsoft YaHei UI", L"Microsoft YaHei" });
    FontFamily* famNum  = PickFamily({ L"Segoe UI", L"Microsoft YaHei UI", L"Arial" });   // 等宽数字，计时不跳动
    Font fHead(famHead, headSz, FontStyleRegular, UnitPixel);
    Font fText(famText, base,   FontStyleRegular, UnitPixel);
    Font fTextDone(famText, base, FontStyleStrikeout, UnitPixel);
    Font fMeta(famText, metaSz, FontStyleRegular, UnitPixel);
    Font fTimer(famNum, base * 0.66f, FontStyleRegular, UnitPixel);
    Font fBTitle(famText, base * 0.88f, FontStyleBold, UnitPixel);
    Font fBDetail(famText, base * 0.64f, FontStyleRegular, UnitPixel);
    Font fBtn(famText, base * 0.58f, FontStyleBold, UnitPixel);

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
        r.tw = 0;
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
            r.key = TrimCopy(todoText);
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

    // ---- 计时胶囊尺寸：同一列右对齐，宽度按模板测量，数字变化时不抖动 ----
    float pillH = base * 1.08f;
    float ringD = pillH * 0.56f;
    float pillPadX = base * 0.40f;
    float pillInnerGap = base * 0.30f;
    float pillGap = base * 0.75f;
    float pillColW = 0.0f;
    for (const auto& r : rows) {
        if (r.type != RT_TODO) continue;
        TaskTimer* t = FindTaskTimer(r.key);
        if (!t) continue;
        long long el = TimerElapsed(*t);
        long long span = t->countdown ? (std::max)(t->duration, el - t->duration) : el;
        RectF bb = measure(span >= 3600 ? L"+88:88:88" : L"+88:88", fTimer);
        float w = pillPadX * 2 + ringD + pillInnerGap + bb.Width;
        if (w > pillColW) pillColW = w;
    }

    // ---- 第一遍：测量 + 计算布局 ----
    float contentW = base * 4.0f;
    for (auto& r : rows) {
        if (r.type == RT_SPACER) { r.h = base * 0.45f; continue; }
        Font* f = (r.type == RT_HEADING) ? &fHead : (r.type == RT_META) ? &fMeta : &fText;
        RectF bb = measure(r.text, *f);
        r.h = bb.Height;
        r.tw = bb.Width;
        r.w = bb.Width + (r.type == RT_TODO ? (float)(checkD + checkGap) : 0.0f);
        if (r.type == RT_TODO && pillColW > 0 && FindTaskTimer(r.key)) r.w += pillGap + pillColW;
        if (r.w > contentW) contentW = r.w;
    }

    // ---- 提醒横幅尺寸 ----
    float bPad = base * 0.62f, iconD = base * 1.5f, btnH = base * 1.25f, btnPadX = base * 0.72f, btnGap = base * 0.36f;
    float bannerH = 0.0f, bTitleH = 0.0f, bDetailH = 0.0f, bHeadBlock = 0.0f;
    auto btnSpec = BannerButtonSpec();
    std::vector<float> btnW;
    if (g_alertActive) {
        RectF tb = measure(g_alertTitle, fBTitle), db = measure(g_alertDetail, fBDetail);
        bTitleH = tb.Height; bDetailH = db.Height;
        float textW = (std::max)(tb.Width, db.Width);
        float btnsW = 0.0f;
        for (const auto& b : btnSpec) {
            float w = measure(b.second, fBtn).Width + btnPadX * 2;
            btnW.push_back(w);
            btnsW += w;
        }
        btnsW += btnGap * (float)(btnSpec.size() - 1);
        float need = (std::max)(bPad + iconD + bPad * 0.8f + textW + bPad, bPad * 2 + btnsW);
        bHeadBlock = (std::max)(iconD, bTitleH + base * 0.12f + bDetailH);
        bannerH = bPad + bHeadBlock + base * 0.6f + btnH + bPad;
        if (need > contentW) contentW = need;
    }

    float y = (float)PADTOP;
    float bannerY = y;
    if (g_alertActive) y += bannerH + base * 0.85f;
    float dividerY = -1.0f;
    bool placedDivider = false, seenContent = g_alertActive;
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

    // 柔和阴影：独立位图画卡片轮廓 → 高斯模糊 → 挖空卡片内部 → 叠到底层（结果按尺寸缓存）
    {
        int key[6] = { W, H, cardW, cardH, (int)(radius * 100), (int)(base * 100) };
        if (!g_shadowCache || memcmp(key, g_shadowKey, sizeof(key)) != 0) {
            delete g_shadowCache;
            g_shadowCache = new Bitmap(W, H, PixelFormat32bppARGB);
            float blurR = base * 0.95f; if (blurR < 8) blurR = 8; if (blurR > 56) blurR = 56;
            float shDy = base * 0.35f;
            Graphics sg(g_shadowCache);
            sg.Clear(Color(0, 0, 0, 0));
            sg.SetSmoothingMode(SmoothingModeAntiAlias);
            GraphicsPath sp;
            RoundRectPath(sp, (float)M, (float)M + shDy, (float)cardW, (float)cardH, radius);
            SolidBrush sb(Color(130, 0, 0, 0));
            sg.FillPath(&sb, &sp);
            Blur blur;
            BlurParams bp; bp.radius = blurR; bp.expandEdge = FALSE;
            blur.SetParameters(&bp);
            g_shadowCache->ApplyEffect(&blur, NULL);
            // 挖掉卡片正下方的阴影，避免半透明卡片中心发灰
            GraphicsPath hole;
            RoundRectPath(hole, (float)M, (float)M, (float)cardW, (float)cardH, radius);
            sg.SetCompositingMode(CompositingModeSourceCopy);
            SolidBrush clearB(Color(0, 0, 0, 0));
            sg.FillPath(&clearB, &hole);
            memcpy(g_shadowKey, key, sizeof(key));
        }
        g.DrawImage(g_shadowCache, 0, 0);
    }

    // 卡片底 + 发丝描边
    GraphicsPath cp;
    RoundRectPath(cp, (float)M, (float)M, (float)cardW, (float)cardH, radius);
    SolidBrush cardBrush(Color((BYTE)g_bgAlpha, GetRValue(g_bgColor), GetGValue(g_bgColor), GetBValue(g_bgColor)));
    g.FillPath(&cardBrush, &cp);
    Pen border(Color(24, 255, 255, 255), 1.0f);
    g.DrawPath(&border, &cp);

    // 提醒中：卡片边缘呼吸光
    if (g_alertActive) {
        for (int k = 3; k >= 1; --k) {
            Pen gp(Color((BYTE)ClampByte((22.0 + 34.0 * pulse) * (4 - k) / 3.0), 255, 107, 107), (REAL)(k * 2.2f));
            g.DrawPath(&gp, &cp);
        }
        Pen bp(Color((BYTE)ClampByte(110.0 + 120.0 * pulse), 255, 128, 112), 1.6f);
        g.DrawPath(&bp, &cp);
    }

    // 颜色
    BYTE TR = GetRValue(g_textColor), TG = GetGValue(g_textColor), TB = GetBValue(g_textColor);
    Color ink(255, TR, TG, TB);
    Color checkc(235, TR, TG, TB);
    Color doneInk(155, ClampByte(TR * 0.55), ClampByte(TG * 0.55), ClampByte(TB * 0.55));
    Color doneCheck(165, ClampByte(TR * 0.52), ClampByte(TG * 0.52), ClampByte(TB * 0.52));
    Color doneFill(36, ClampByte(TR * 0.45), ClampByte(TG * 0.45), ClampByte(TB * 0.45));
    Color meta(255, 169, 175, 189);
    Color shadowc(90, 0, 0, 0);
    Color accentDone(255, 52, 211, 153);    // 完成：翡翠绿
    Color accentGold(255, 253, 224, 71);
    Color coral(255, 255, 107, 107);        // 到点：珊瑚红
    Color amber(255, 251, 191, 36);         // 快到点：琥珀
    Color sky(255, 125, 211, 252);          // 倒计时：天蓝
    Color violet(255, 196, 181, 253);       // 正计时：淡紫
    float sdy = base * 0.045f; if (sdy < 1) sdy = 1;

    auto drawText = [&](const std::wstring& str, Font& f, float tx, float ty, Color c) {
        if (str.empty()) return;
        SolidBrush shb(WithAlpha(shadowc, shadowc.GetA() * c.GetA() / 255.0));
        g.DrawString(str.c_str(), (INT)str.size(), &f, PointF(tx, ty + sdy), &fmt, &shb);
        SolidBrush br(c);
        g.DrawString(str.c_str(), (INT)str.size(), &f, PointF(tx, ty), &fmt, &br);
    };

    float bx = (float)(M + PADX);
    float rightX = (float)(M + cardW - PADX);
    if (dividerY >= 0) {
        Pen dp(Color(30, 255, 255, 255), 1.0f);
        float dyv = M + dividerY;
        g.DrawLine(&dp, bx, dyv, rightX, dyv);
    }

    // ---- 提醒横幅 ----
    g_bannerButtons.clear();
    if (g_alertActive) {
        double e = EaseOutCubic(alertT / 0.38);              // 入场：下滑 + 淡入
        double A = 255.0 * e;
        float bw = rightX - bx;
        float byT = M + bannerY - (float)((1.0 - e) * base * 0.6);
        float br = base * 0.55f;
        for (int k = 1; k <= 3; ++k) {                         // 外圈呼吸光
            GraphicsPath gp2;
            RoundRectPath(gp2, bx - k * 1.6f, byT - k * 1.6f, bw + k * 3.2f, bannerH + k * 3.2f, br + k * 1.6f);
            Pen pp(Color((BYTE)ClampByte(e * (40.0 + 60.0 * pulse) / k), 255, 120, 100), 1.6f);
            g.DrawPath(&pp, &gp2);
        }
        GraphicsPath bpth;
        RoundRectPath(bpth, bx, byT, bw, bannerH, br);
        LinearGradientBrush lg(PointF(bx, byT), PointF(bx + bw, byT + bannerH),
            Color((BYTE)ClampByte(A * 0.96), 255, 95, 109), Color((BYTE)ClampByte(A * 0.96), 255, 166, 77));
        g.FillPath(&lg, &bpth);
        {
            GraphicsPath hp;
            RoundRectPath(hp, bx + 1, byT + 1, bw - 2, bannerH - 2, br - 1);
            Pen hpen(Color((BYTE)ClampByte(A * 0.25), 255, 255, 255), 1.0f);
            g.DrawPath(&hpen, &hp);
        }

        // 闹钟图标（抖动时轻轻摇摆）
        float icx = bx + bPad + iconD / 2, icy = byT + bPad + bHeadBlock / 2;
        double wig = 0.0;
        if (g_shaking) {
            double st = duration<double>(nowSteady - g_shakeStart).count();
            wig = 18.0 * std::sin(st * 2.0 * ANIM_PI * 6.0) * (1.0 - Clamp01(st / SHAKE_SEC));
        }
        GraphicsState gs = g.Save();
        g.TranslateTransform(icx, icy);
        g.RotateTransform((REAL)wig);
        SolidBrush icBg(Color((BYTE)ClampByte(A * 0.24), 255, 255, 255));
        g.FillEllipse(&icBg, -iconD / 2, -iconD / 2, iconD, iconD);
        float cr = iconD * 0.25f, sw = (std::max)(1.4f, base * 0.075f), cyo = iconD * 0.05f;
        Pen wp(Color((BYTE)ClampByte(A), 255, 255, 255), sw);
        wp.SetStartCap(LineCapRound);
        wp.SetEndCap(LineCapRound);
        g.DrawEllipse(&wp, -cr, cyo - cr, cr * 2, cr * 2);
        g.DrawLine(&wp, 0.0f, cyo, 0.0f, cyo - cr * 0.55f);
        g.DrawLine(&wp, 0.0f, cyo, cr * 0.42f, cyo + cr * 0.22f);
        float er = cr * 0.48f;
        g.DrawArc(&wp, -cr * 0.85f - er, cyo - cr * 0.95f - er, er * 2, er * 2, 190, 100);
        g.DrawArc(&wp, cr * 0.85f - er, cyo - cr * 0.95f - er, er * 2, er * 2, 250, 100);
        g.DrawLine(&wp, -cr * 0.62f, cyo + cr * 0.86f, -cr * 0.86f, cyo + cr * 1.14f);
        g.DrawLine(&wp, cr * 0.62f, cyo + cr * 0.86f, cr * 0.86f, cyo + cr * 1.14f);
        g.Restore(gs);

        float tx = bx + bPad + iconD + bPad * 0.8f;
        float ty = byT + bPad + (bHeadBlock - (bTitleH + base * 0.12f + bDetailH)) / 2;
        SolidBrush tbr(Color((BYTE)ClampByte(A), 255, 255, 255));
        g.DrawString(g_alertTitle.c_str(), (INT)g_alertTitle.size(), &fBTitle, PointF(tx, ty), &fmt, &tbr);
        SolidBrush dbr(Color((BYTE)ClampByte(A * 0.88), 255, 255, 255));
        g.DrawString(g_alertDetail.c_str(), (INT)g_alertDetail.size(), &fBDetail, PointF(tx, ty + bTitleH + base * 0.12f), &fmt, &dbr);

        // 按钮（右对齐；第一个为主按钮）
        float byB = byT + bannerH - bPad - btnH;
        float cx = bx + bw - bPad;
        for (int i = (int)btnSpec.size() - 1; i >= 0; --i) {
            cx -= btnW[(size_t)i];
            bool primary = (i == 0);
            GraphicsPath bpp;
            RoundRectPath(bpp, cx, byB, btnW[(size_t)i], btnH, btnH / 2);
            SolidBrush bfill(primary ? Color((BYTE)ClampByte(A * 0.96), 255, 255, 255) : Color((BYTE)ClampByte(A * 0.22), 255, 255, 255));
            g.FillPath(&bfill, &bpp);
            const std::wstring& label = btnSpec[(size_t)i].second;
            RectF lb = measure(label, fBtn);
            SolidBrush lbr(primary ? Color((BYTE)ClampByte(A), 225, 72, 82) : Color((BYTE)ClampByte(A), 255, 255, 255));
            g.DrawString(label.c_str(), (INT)label.size(), &fBtn,
                PointF(cx + (btnW[(size_t)i] - lb.Width) / 2, byB + (btnH - lb.Height) / 2), &fmt, &lbr);
            RECT rc = { (LONG)cx, (LONG)byB, (LONG)(cx + btnW[(size_t)i]), (LONG)(byB + btnH) };
            g_bannerButtons.push_back({ rc, btnSpec[(size_t)i].first });
            cx -= btnGap;
        }
    }

    // ---- 各行 ----
    g_todoHitRects.clear();
    g_rowHitRects.clear();
    g_pillHitRects.clear();
    std::unordered_map<std::wstring, float> rowY;
    double settleT = g_settling ? EaseOutCubic(duration<double>(nowSteady - g_settleStart).count() / SETTLE_ANIM_SEC) : 1.0;
    double animP = g_completeAnim.active ? Clamp01(duration<double>(nowSteady - g_completeAnim.start).count() / COMPLETE_ANIM_SEC) : 0.0;

    for (auto& r : rows) {
        if (r.type == RT_SPACER) continue;
        float ry = M + r.y;
        if (r.type == RT_HEADING) { drawText(r.text, fHead, bx, ry, ink); continue; }
        if (r.type == RT_META) { drawText(r.text, fMeta, bx, ry, meta); continue; }

        // 已完成项沉底时，从旧位置平滑滑到新位置
        if (g_settling) {
            auto it = g_settleFrom.find(r.key);
            if (it != g_settleFrom.end()) ry = (float)(M + it->second + (r.y - it->second) * settleT);
        }
        if (!rowY.count(r.key)) rowY[r.key] = ry - M;

        float rh = (r.h > checkD ? r.h : (float)checkD);
        float cy = ry + rh / 2.0f - checkD / 2.0f;
        float stroke = base * 0.06f; if (stroke < 1.25f) stroke = 1.25f;
        float textX = bx + checkD + checkGap;
        float rowL = bx - base * 0.35f, rowR = rightX + base * 0.35f;
        float rowT = ry - base * 0.22f, rowHt = rh + base * 0.44f;
        bool animating = g_completeAnim.active && !r.completed && r.key == g_completeAnim.key;
        bool alerting = g_alertActive && !g_alertTaskKey.empty() && !r.completed && r.key == g_alertTaskKey;

        if (alerting || animating) {
            GraphicsPath hp;
            RoundRectPath(hp, rowL, rowT, rowR - rowL, rowHt, base * 0.35f);
            SolidBrush hb(alerting ? WithAlpha(coral, 26.0 + 34.0 * pulse) : WithAlpha(accentDone, 34.0 * std::sin(ANIM_PI * animP)));
            g.FillPath(&hb, &hp);
        }

        if (animating) {
            // 完成动画：涟漪 + 光点 + 复选框弹出填充 + 对勾描绘 + 删除线划过
            double pf = EaseOutBack(animP / 0.32);
            double pt = Clamp01((animP - 0.16) / 0.30);
            double pr = Clamp01(animP / 0.80);
            double ps = EaseInOutCubic((animP - 0.28) / 0.55);
            float ccx = bx + checkD / 2.0f, ccy = cy + checkD / 2.0f;
            {
                float rr = checkD * (0.55f + 1.30f * (float)EaseOutCubic(pr));
                Pen rp(WithAlpha(accentDone, 170.0 * (1.0 - pr)), (std::max)(1.0f, stroke * (float)(1.5 - pr)));
                g.DrawEllipse(&rp, ccx - rr, ccy - rr, rr * 2, rr * 2);
            }
            for (int k = 0; k < 8; ++k) {
                double ang = k * ANIM_PI / 4.0 + 0.39;
                double dist = checkD * (0.6 + 1.5 * EaseOutCubic(pr));
                float ds = (float)(checkD * 0.13 * (1.0 - pr));
                if (ds < 0.3f) continue;
                SolidBrush sb(WithAlpha(k % 2 == 0 ? accentDone : accentGold, 235.0 * (1.0 - pr)));
                float sx = ccx + (float)(std::cos(ang) * dist), sy = ccy + (float)(std::sin(ang) * dist);
                g.FillEllipse(&sb, sx - ds, sy - ds, ds * 2, ds * 2);
            }
            Pen boxPen(LerpColor(checkc, accentDone, animP / 0.2), stroke);
            g.DrawRectangle(&boxPen, bx, cy, (REAL)checkD, (REAL)checkD);
            float fs = (float)(checkD * pf);
            if (fs > 0.5f) {
                SolidBrush fb(accentDone);
                g.FillRectangle(&fb, ccx - fs / 2, ccy - fs / 2, fs, fs);
            }
            DrawTick(g, bx, cy, (float)checkD, pt, Color(255, 255, 255, 255), stroke + 0.35f);
            Color tc = LerpColor(ink, doneInk, ps);
            drawText(r.text, fText, textX, ry, tc);
            if (ps > 0.0) {
                Pen sp(tc, (std::max)(1.2f, base * 0.06f));
                float sy = ry + r.h * 0.54f;
                g.DrawLine(&sp, textX, sy, textX + r.tw * (float)ps, sy);
            }
        } else {
            Color boxColor = r.completed ? doneCheck : checkc;
            if (r.completed) {
                SolidBrush fill(doneFill);
                g.FillRectangle(&fill, bx, cy, (REAL)checkD, (REAL)checkD);
            }
            Pen boxPen(boxColor, stroke);
            g.DrawRectangle(&boxPen, bx, cy, (REAL)checkD, (REAL)checkD);
            if (r.completed) DrawTick(g, bx, cy, (float)checkD, 1.0, doneCheck, stroke + 0.35f);
            // 刚完成的一项：滑动期间强调色逐渐褪为已完成样式
            if (r.completed && g_settling && r.key == g_justCompletedKey) {
                double a = 1.0 - settleT;
                SolidBrush fb(WithAlpha(accentDone, 255.0 * a));
                g.FillRectangle(&fb, bx, cy, (REAL)checkD, (REAL)checkD);
                DrawTick(g, bx, cy, (float)checkD, 1.0, Color((BYTE)ClampByte(255.0 * a), 255, 255, 255), stroke + 0.35f);
            }
            Font& todoFont = r.completed ? fTextDone : fText;
            drawText(r.text, todoFont, textX, ry, r.completed ? doneInk : ink);
        }

        if (r.sourceIndex >= 0) {
            int inflate = (int)(base * 0.20f); if (inflate < 4) inflate = 4;
            RECT hit = { (LONG)(bx - inflate), (LONG)(cy - inflate), (LONG)(bx + checkD + inflate), (LONG)(cy + checkD + inflate) };
            g_todoHitRects.push_back({ hit, r.sourceIndex, r.key, r.completed });
            RECT rowRc = { (LONG)rowL, (LONG)rowT, (LONG)rowR, (LONG)(rowT + rowHt) };
            g_rowHitRects.push_back({ rowRc, r.sourceIndex, r.key, r.completed });
        }

        // ---- 计时胶囊 ----
        TaskTimer* tm = (pillColW > 0) ? FindTaskTimer(r.key) : nullptr;
        if (tm) {
            float px = rightX - pillColW;
            float py = ry + rh / 2.0f - pillH / 2.0f;
            long long el = TimerElapsed(*tm);
            bool running = tm->start > 0;
            bool expired = tm->countdown && el >= tm->duration;
            std::wstring label;
            double frac = 0.0;
            Color ringC = sky;
            if (tm->countdown) {
                long long rem = tm->duration - el;
                if (rem > 0) {
                    label = FormatClock(rem);
                    frac = tm->duration > 0 ? (double)rem / (double)tm->duration : 0.0;
                    ringC = (rem <= 60 || rem * 5 <= tm->duration) ? amber : sky;
                } else {
                    label = L"+" + FormatClock(-rem);   // 超时多久
                    frac = 1.0;
                    ringC = coral;
                }
            } else {
                label = FormatClock(el);
                frac = (double)(el % 60) / 60.0;
                ringC = violet;
            }
            double fade = r.completed ? 120.0 : (running ? 255.0 : 175.0);

            GraphicsPath pp;
            RoundRectPath(pp, px, py, pillColW, pillH, pillH / 2);
            Color pbg = (expired && !r.completed)
                ? WithAlpha(coral, alerting ? 60.0 + 50.0 * pulse : 52.0)
                : Color((BYTE)(r.completed ? 14 : 26), TR, TG, TB);
            SolidBrush pbr(pbg);
            g.FillPath(&pbr, &pp);

            float rcx = px + pillPadX + ringD / 2.0f, rcy = py + pillH / 2.0f;
            float rw = (std::max)(1.5f, ringD * 0.16f);
            float rd = ringD - rw;
            Pen track(Color((BYTE)ClampByte(fade * 0.28), TR, TG, TB), rw);
            g.DrawEllipse(&track, rcx - rd / 2, rcy - rd / 2, rd, rd);
            Pen arc(WithAlpha(ringC, fade), rw);
            arc.SetStartCap(LineCapRound);
            arc.SetEndCap(LineCapRound);
            if (frac >= 0.999) g.DrawEllipse(&arc, rcx - rd / 2, rcy - rd / 2, rd, rd);
            else if (frac > 0.001) g.DrawArc(&arc, rcx - rd / 2, rcy - rd / 2, rd, rd, -90.0f, (REAL)(360.0 * frac));
            if (!running) {   // 暂停：中间两道竖线
                SolidBrush pb(WithAlpha(ringC, fade));
                float bw2 = (std::max)(1.2f, rw * 0.75f), bh2 = ringD * 0.34f;
                g.FillRectangle(&pb, rcx - bw2 * 1.6f, rcy - bh2 / 2, bw2, bh2);
                g.FillRectangle(&pb, rcx + bw2 * 0.6f, rcy - bh2 / 2, bw2, bh2);
            }
            RectF lb = measure(label, fTimer);
            SolidBrush lbr(expired && !r.completed ? WithAlpha(Color(255, 255, 150, 140), fade) : Color((BYTE)ClampByte(fade), TR, TG, TB));
            g.DrawString(label.c_str(), (INT)label.size(), &fTimer,
                PointF(rcx + ringD / 2.0f + pillInnerGap, py + (pillH - lb.Height) / 2.0f), &fmt, &lbr);

            RECT prc = { (LONG)px, (LONG)py, (LONG)(px + pillColW), (LONG)(py + pillH) };
            g_pillHitRects.push_back({ prc, r.sourceIndex, r.key, r.completed });
        }
    }
    g_lastRowY.swap(rowY);

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
    // 普通卡片沿用当前悬停淡入淡出的整体不透明度，避免定时重绘把隐藏中的卡片“闪回来”
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)g_hoverAlpha, AC_SRC_ALPHA };
    UpdateLayeredWindow(hwnd, screen, &ptDst, &sz, memDC, &ptSrc, 0, &bf, ULW_ALPHA);

    // ---- 清理 ----
    SelectObject(memDC, oldbm);
    DeleteObject(hbm);
    DeleteDC(memDC);
    ReleaseDC(NULL, screen);
    delete famText;
    delete famHead;
    delete famNum;
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
        POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (!g_restoringAlert) {
            for (const auto& b : g_bannerButtons) {                 // 提醒横幅按钮
                if (PtInRect(&b.rc, p)) { HandleBannerAction(hWnd, b.action); return 0; }
            }
            for (const auto& h : g_pillHitRects) {                  // 计时胶囊：暂停 / 继续
                if (PtInRect(&h.rc, p)) { ToggleTaskTimerPause(h.key); RenderTip(hWnd); return 0; }
            }
            for (const auto& h : g_todoHitRects) {                  // 复选框：完成动画
                if (PtInRect(&h.rc, p)) { TodoHitRect copy = h; BeginToggle(hWnd, copy); return 0; }
            }
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
        ShowContextMenu(hWnd, true);
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
        KillTimer(hWnd, TIMER_ANIM);
        KillTimer(hWnd, TIMER_HOVER_HIDE);
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
        if (LOWORD(wParam) == ID_HOVER_HIDE) {
            g_hoverHideEnabled = !g_hoverHideEnabled;
            confWrite();
            UpdateHoverHideTimer(hWnd);
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
                StopCardAlert(hWnd);
            } else {
                g_reminderArmed = false;
                g_reminderIsCountdown = false;
                g_reminderIntervalSeconds = 0;
            }
        }
        if (LOWORD(wParam) >= ID_TASK_CD_FIRST && LOWORD(wParam) <= ID_TASK_REMOVE) {
            HandleTaskTimerCommand(hWnd, LOWORD(wParam));
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
        {
            POINT p = { mx, my };
            for (const auto& h : g_pillHitRects) if (PtInRect(&h.rc, p)) return 0;
            for (const auto& b : g_bannerButtons) if (PtInRect(&b.rc, p)) return 0;
        }

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
        StopCardAlert(hWnd);
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
            CheckTaskTimers(hWnd);
            if (!g_animTimerOn && AnyTaskTimerRunning()) RenderTip(hWnd);   // 每秒刷新计时显示
        }
        if (wParam == TIMER_ANIM) {
            TickAnimations(hWnd);
        }
        if (wParam == TIMER_HOVER_HIDE) {
            TickHoverHide(hWnd);
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
    UpdateHoverHideTimer(hwnd);   // 仅「始终置顶」且开启设置时启用悬停隐藏
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
        ofs << "HoverHide: " << (g_hoverHideEnabled ? 1 : 0) << std::endl;
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
        else if (line.find("HoverHide:") == 0) {
            g_hoverHideEnabled = (std::stoi(line.substr(line.find(":") + 2)) != 0);
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
