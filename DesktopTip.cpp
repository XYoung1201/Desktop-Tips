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
#define ID_MODE_SMART 16
#define ID_SURFACE_LEAD_FIRST 40   // 40..43：提前 5/10/15/30 分钟浮现
#define ID_SURFACE_EVERY_FIRST 45  // 45..48：每 30/45/60 分钟轻浮现一次 / 关闭
#define ID_FONT_SELECT 22
#define ID_HOVER_HIDE 25
#define ID_REMINDER_CANCEL 21
#define ID_NEW_COUNTDOWN 30
#define ID_SHOW_HIDDEN 31
#define ID_SINK_DONE 32

#define ID_TASK_CD_FIRST 101      // 101..108 = 快速倒计时（换算成截止时间）
#define ID_TASK_DUE_SET 110       // 设置截止时间 / 剩余时长…
#define ID_TASK_STOPWATCH 111
#define ID_TASK_PAUSE 112
#define ID_TASK_RESTART 113
#define ID_TASK_REMOVE 114        // 移除正计时
#define ID_TASK_DUE_CLEAR 115     // 清除截止时间
#define ID_ITEM_HIDE 116          // 隐藏 / 取消隐藏（任务、倒计时、分组）
#define ID_GROUP_FOLD 117         // 折叠 / 展开分组
#define ID_CD_DELETE 118          // 删除独立倒计时
#define ID_ITEM_SHOW 119          // 折叠 / 隐藏分组时仍显示该任务（_SHOW）
#define ID_ITEM_LAST 119

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
static int g_displayMode = 2;   // 0 = 附着桌面，1 = 始终置顶，2 = 智能浮现（新安装默认）

// 手动拖动状态：子窗口(WS_CHILD)模式下无法用 HTCAPTION 拖动，需自行处理
static bool  g_dragging   = false;
static POINT g_dragOffset = { 0, 0 };
static POINT g_lastContextMenuPt = { 0, 0 };
static bool g_hasLastContextMenuPt = false;

struct TodoHitRect {
    RECT rc;
    int sourceIndex;
    std::wstring key;       // 条目键：任务为去掉前缀与尾部标记的正文；倒计时/分组带内部前缀
    bool completed;
    int kind = 0;           // 计时胶囊：0 = 正计时，1 = 截止时间
};

static std::vector<TodoHitRect> g_todoHitRects;   // 复选框
static std::vector<TodoHitRect> g_rowHitRects;    // 整行（右键为该任务设置计时）
static std::vector<TodoHitRect> g_pillHitRects;   // 计时胶囊（正计时：暂停/继续；截止时间：切换显示方式）
static std::vector<TodoHitRect> g_groupHitRects;  // 分组标题（单击折叠/展开）

struct BannerButton { RECT rc; int action; };
enum { BANNER_DONE = 1, BANNER_SNOOZE = 2, BANNER_DISMISS = 3 };
static std::vector<BannerButton> g_bannerButtons;

static bool g_countdownRepeatEnabled = false;   // 新建独立倒计时时「重复」的默认值（配置键 CountdownRepeat）
static bool g_showHidden = false;               // 临时显示带 _HIDE 的条目（淡显），配置键 ShowHidden
static bool g_dueShowAbsolute = false;          // 截止时间胶囊显示「剩余时长」(false) 还是「截止时刻」(true)，配置键 DueDisplay
static int  g_dueInputMode = 1;                 // 倒计时对话框默认页：0 = 截止时间，1 = 剩余时长，配置键 DueInputMode
static bool g_sinkCompleted = false;            // 勾选完成后是否把已完成项沉到段末，配置键 SinkCompleted（默认不移动）
static bool g_hasDuePill = false;               // 当前卡片上是否有截止时间胶囊（需要每秒刷新）

static bool g_alertActive = false;
static bool g_restoringAlert = false;
static steady_clock::time_point g_alertStarted;
static std::wstring g_alertTitle;
static std::wstring g_alertDetail;
static std::wstring g_alertTaskKey;          // 触发提醒的条目键（任务或独立倒计时）
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
static std::wstring g_menuTaskKey;           // 右键菜单所针对的条目
static int g_menuHint = -1;                  // 该条目所在行号（提示）

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
// 智能浮现（显示模式 2）：
//   平时附着桌面，不打扰；有事时才以“半透明 + 鼠标穿透”的轻浮层浮到最上面：
//     · 某项截止时间进入提前量（SurfaceLead 分钟）→ 浮现直到处理或到点（到点后走原有的醒目提醒）；
//     · 每隔 SurfaceEvery 分钟（有未完成事项时）轻浮现 8 秒，扫一眼即沉回桌面。
//   浮现时不抢焦点、不挡点击；鼠标移上去会更淡，按住 Ctrl 才变实、可勾选/拖动。
//   前台是全屏程序或演示模式时不浮现（到点提醒除外）。
static int  g_surfaceLeadMin = 10;      // 配置键 SurfaceLead
static int  g_surfaceEveryMin = 45;     // 配置键 SurfaceEvery，0 = 关闭定时轻浮现
static bool g_peekActive = false;       // 当前是否浮在最上层（轻浮层）
static bool g_peekGlance = false;       // true = 定时轻浮现（会自动沉回）
static long long g_peekUntil = 0;
static long long g_lastGlance = 0;
static long long g_nextOpenDue = 0;     // 最近一个未完成且尚未到点的截止时间（0 = 无）
static bool g_hasOpenItems = false;
static const int PEEK_ALPHA = 178;      // 轻浮层的整体不透明度
static const int PEEK_HOVER_ALPHA = 46; // 鼠标停在轻浮层上时
static const int PEEK_GLANCE_SEC = 8;
static const int kSurfaceLeadOpts[] = { 5, 10, 15, 30 };
static const int kSurfaceEveryOpts[] = { 30, 45, 60, 0 };

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
static void UpdatePeek(HWND hwnd);

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

// ============================================================================
//  TODO 行模型
//  行首：# 大标题 / > 次要说明 / ## 分组（### 子分组，#### 再下一级…）/ ! 独立倒计时 /
//        - 或 * 或直接文字 = 待办；单独一行 ## 结束所有分组，单独一行 ### 结束二级及更深的分组。
//  行尾标记（顺序任意）：_OK 已完成、_HIDE 隐藏、_FOLD 折叠分组、_SHOW 分组折叠/隐藏时仍显示该条、
//                        @YYYY-MM-DD HH:MM:SS 截止时间、/30m 到点后按间隔重复（s/m/h/d）。
// ============================================================================
static std::wstring TrimCopy(const std::wstring& text);

enum LineKind { LK_BLANK, LK_HEADING, LK_META, LK_GROUP, LK_GROUP_END, LK_TODO, LK_COUNTDOWN };
struct LineInfo {
    LineKind kind = LK_BLANK;
    std::wstring lead;      // 缩进 + 行首符号（原样保留）
    std::wstring text;      // 正文（去掉行首符号与行尾标记）
    bool ok = false, hide = false, fold = false, show = false;
    int level = 0;          // 分组层级：## = 1，### = 2 …
    bool hasDue = false;
    long long due = 0;      // 截止时间（Unix 秒，本地时间解释）
    long long repeat = 0;   // 重复间隔（秒）；0 = 不重复
};

static const wchar_t KEY_CD_PREFIX[] = { 0x01, L'C', 0 };   // 独立倒计时的键前缀
static const wchar_t KEY_GROUP_PREFIX[] = { 0x01, L'G', 0 }; // 分组的键前缀

static bool IsCountdownKey(const std::wstring& k) { return k.size() >= 2 && k[0] == 0x01 && k[1] == L'C'; }
static bool IsGroupKey(const std::wstring& k) { return k.size() >= 2 && k[0] == 0x01 && k[1] == L'G'; }

static int DaysInMonth(int y, int m)
{
    static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return (m >= 1 && m <= 12) ? d[m - 1] : 30;
}

static long long LocalEpoch(int y, int mo, int d, int h, int mi, int s)
{
    std::tm tmv{};
    tmv.tm_year = y - 1900; tmv.tm_mon = mo - 1; tmv.tm_mday = d;
    tmv.tm_hour = h; tmv.tm_min = mi; tmv.tm_sec = s; tmv.tm_isdst = -1;
    return (long long)std::mktime(&tmv);
}

static std::tm LocalTm(long long epoch)
{
    std::time_t t = (std::time_t)epoch;
    std::tm tmv{};
    localtime_s(&tmv, &t);
    return tmv;
}

// 「YYYY-MM-DD」「YYYY-MM-DD HH:MM」「YYYY-MM-DD HH:MM:SS」；日期分隔符可用 - / .，日期与时间之间可用空格或 T。
// 只写日期时视为当天 23:59:59。
static bool ParseDueText(const std::wstring& in, long long& out)
{
    std::wstring s = TrimCopy(in);
    size_t i = 0;
    auto num = [&](size_t minD, size_t maxD, int& v) -> bool {
        size_t st = i; v = 0;
        while (i < s.size() && i - st < maxD && s[i] >= L'0' && s[i] <= L'9') { v = v * 10 + (s[i] - L'0'); i++; }
        return i - st >= minD;
    };
    auto dateSep = [&]() -> bool {
        if (i < s.size() && (s[i] == L'-' || s[i] == L'/' || s[i] == L'.')) { i++; return true; }
        return false;
    };
    auto timeSep = [&]() -> bool {
        if (i < s.size() && (s[i] == L':' || s[i] == 0xFF1A)) { i++; return true; }
        return false;
    };
    int y, mo, d, h = 23, mi = 59, sec = 59;
    if (!num(4, 4, y) || !dateSep() || !num(1, 2, mo) || !dateSep() || !num(1, 2, d)) return false;
    if (i < s.size()) {
        if (s[i] != L' ' && s[i] != L'T' && s[i] != L'\t') return false;
        while (i < s.size() && (s[i] == L' ' || s[i] == L'T' || s[i] == L'\t')) i++;
        if (!num(1, 2, h) || !timeSep() || !num(2, 2, mi)) return false;
        sec = 0;
        if (i < s.size()) {
            if (!timeSep() || !num(2, 2, sec)) return false;
        }
        if (i != s.size()) return false;
    }
    if (y < 1971 || y > 2999 || mo < 1 || mo > 12 || d < 1 || d > DaysInMonth(y, mo) || h > 23 || mi > 59 || sec > 59) return false;
    long long e = LocalEpoch(y, mo, d, h, mi, sec);
    if (e <= 0) return false;
    out = e;
    return true;
}

static std::wstring FormatDueText(long long due)
{
    std::tm t = LocalTm(due);
    wchar_t buf[40];
    swprintf(buf, 40, L"%04d-%02d-%02d %02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    return buf;
}

static std::wstring FormatRepeatText(long long rep)
{
    wchar_t buf[32];
    if (rep % 86400 == 0) swprintf(buf, 32, L"%lldd", rep / 86400);
    else if (rep % 3600 == 0) swprintf(buf, 32, L"%lldh", rep / 3600);
    else if (rep % 60 == 0) swprintf(buf, 32, L"%lldm", rep / 60);
    else swprintf(buf, 32, L"%llds", rep);
    return buf;
}

static bool StripSuffixToken(std::wstring& s, const wchar_t* tok)
{
    TrimLineRight(s);
    size_t n = wcslen(tok);
    if (s.size() < n || s.compare(s.size() - n, n, tok) != 0) return false;
    s.erase(s.size() - n);
    TrimLineRight(s);
    return true;
}

// 行尾「@日期 [时间]」；@ 前须为空白或位于正文开头（避免把邮箱等误认成截止时间）
static bool StripDueSuffix(std::wstring& s, long long& due)
{
    TrimLineRight(s);
    size_t at = s.rfind(L'@');
    if (at == std::wstring::npos) return false;
    if (at > 0 && s[at - 1] != L' ' && s[at - 1] != L'\t') return false;
    long long d = 0;
    if (!ParseDueText(s.substr(at + 1), d)) return false;
    due = d;
    s.erase(at);
    TrimLineRight(s);
    return true;
}

// 行尾「/30m」之类的重复间隔
static bool StripRepeatSuffix(std::wstring& s, long long& rep)
{
    TrimLineRight(s);
    size_t sp = s.find_last_of(L" \t");
    std::wstring tok = (sp == std::wstring::npos) ? s : s.substr(sp + 1);
    if (tok.size() < 3 || tok[0] != L'/') return false;
    wchar_t u = tok.back();
    long long mul = (u == L's') ? 1 : (u == L'm') ? 60 : (u == L'h') ? 3600 : (u == L'd') ? 86400 : 0;
    if (!mul) return false;
    long long n = 0;
    for (size_t k = 1; k + 1 < tok.size(); ++k) {
        if (tok[k] < L'0' || tok[k] > L'9') return false;
        n = n * 10 + (tok[k] - L'0');
        if (n > 1000000) return false;
    }
    if (n <= 0) return false;
    rep = n * mul;
    s.erase(sp == std::wstring::npos ? 0 : sp);
    TrimLineRight(s);
    return true;
}

static void StripItemFlags(std::wstring& body, LineInfo& L, bool allowOk, bool allowDue, bool allowFold)
{
    bool changed = true;
    while (changed) {
        changed = false;
        if (allowOk && StripSuffixToken(body, L"_OK")) { L.ok = true; changed = true; continue; }
        if (StripSuffixToken(body, L"_HIDE")) { L.hide = true; changed = true; continue; }
        if (allowOk && StripSuffixToken(body, L"_SHOW")) { L.show = true; changed = true; continue; }
        if (allowFold && StripSuffixToken(body, L"_FOLD")) { L.fold = true; changed = true; continue; }
        if (allowDue && !L.hasDue) {
            std::wstring tmp = body;
            long long rep = 0, due = 0;
            bool hasRep = StripRepeatSuffix(tmp, rep);
            if (StripDueSuffix(tmp, due)) {
                L.hasDue = true; L.due = due; L.repeat = hasRep ? rep : 0;
                body = tmp;
                changed = true;
                continue;
            }
        }
    }
}

static LineInfo ParseLine(const std::wstring& raw)
{
    LineInfo L;
    std::wstring s = raw;
    TrimLineRight(s);
    size_t b = 0;
    while (b < s.size() && (s[b] == L' ' || s[b] == L'\t')) b++;
    std::wstring indent = s.substr(0, b);
    std::wstring t = s.substr(b);
    if (t.empty()) return L;

    if (t.size() >= 2 && t[0] == L'#' && t[1] == L'#') {
        size_t hashes = 0;
        while (hashes < t.size() && t[hashes] == L'#') hashes++;
        std::wstring body = TrimCopy(t.substr(hashes));
        L.level = (int)hashes - 1;
        L.lead = indent + std::wstring(hashes, L'#') + L" ";
        if (body.empty()) { L.kind = LK_GROUP_END; return L; }
        L.kind = LK_GROUP;
        StripItemFlags(body, L, false, false, true);
        L.text = TrimCopy(body);
        return L;
    }
    if (t[0] == L'#' || t[0] == L'>') {
        L.kind = (t[0] == L'#') ? LK_HEADING : LK_META;
        size_t n = (t.size() > 1 && t[1] == L' ') ? 2 : 1;
        L.lead = indent + t.substr(0, n);
        std::wstring body = t.substr(n);
        StripItemFlags(body, L, false, false, false);
        L.text = body;
        return L;
    }
    if (t[0] == L'!') {
        LineInfo C;
        std::wstring body = TrimCopy(t.substr(1));
        StripItemFlags(body, C, true, true, false);
        if (C.hasDue) {
            C.kind = LK_COUNTDOWN;
            C.lead = indent + L"! ";
            C.text = TrimCopy(body);
            return C;
        }
        // 没有截止时间的 ! 行仍按普通待办处理（兼容旧文件）
    }
    L.kind = LK_TODO;
    std::wstring body;
    if ((t[0] == L'-' || t[0] == L'*') && t.size() > 1 && t[1] == L' ') {
        L.lead = indent + t.substr(0, 2);
        body = t.substr(2);
    } else {
        L.lead = indent;
        body = t;
    }
    StripItemFlags(body, L, true, true, false);
    L.text = body;
    return L;
}

static std::wstring FormatLine(const LineInfo& L)
{
    std::wstring s = L.lead + L.text;
    switch (L.kind) {
    case LK_BLANK: return L"";
    case LK_GROUP_END: return std::wstring((size_t)(L.level > 0 ? L.level + 1 : 2), L'#');
    case LK_HEADING:
    case LK_META:
        if (L.hide) s += L" _HIDE";
        return s;
    case LK_GROUP:
        if (L.hide) s += L" _HIDE";
        if (L.fold) s += L" _FOLD";
        return s;
    default: break;
    }
    bool marker = false;
    if (L.hasDue) { s += L" @" + FormatDueText(L.due); marker = true; }
    if (L.hasDue && L.repeat > 0) s += L" /" + FormatRepeatText(L.repeat);
    if (L.hide) { s += L" _HIDE"; marker = true; }
    if (L.show) { s += L" _SHOW"; marker = true; }
    if (L.ok) s += marker ? L" _OK" : L"_OK";
    return s;
}

// 条目键：任务 = 正文；独立倒计时 / 分组带内部前缀，避免与任务重名冲突
static std::wstring ItemKey(const LineInfo& L)
{
    switch (L.kind) {
    case LK_TODO: return TrimCopy(L.text);
    case LK_COUNTDOWN: return KEY_CD_PREFIX + TrimCopy(L.text);
    case LK_GROUP: return KEY_GROUP_PREFIX + TrimCopy(L.text);
    default: return L"";
    }
}

static std::wstring KeyDisplayName(const std::wstring& key)
{
    if (IsCountdownKey(key) || IsGroupKey(key)) return key.substr(2);
    return key;
}

static bool ParseTodoLineState(const std::wstring& line, std::wstring* todoText, bool* completed)
{
    LineInfo L = ParseLine(line);
    if (L.kind != LK_TODO) return false;
    if (todoText) *todoText = L.text;
    if (completed) *completed = L.ok;
    return true;
}

static bool IsTodoLine(const std::wstring& line)
{
    return ParseLine(line).kind == LK_TODO;
}

// 只在同一段内（分组标题 / 分组结束 / # 大标题 都会开启新段）把已完成的待办排到未完成之后
static void SortTodoLinesByCompletion(std::vector<std::wstring>& lines)
{
    std::vector<size_t> slots;
    std::vector<std::wstring> openLines, doneLines;
    auto flush = [&]() {
        size_t n = 0;
        for (const auto& l : openLines) lines[slots[n++]] = l;
        for (const auto& l : doneLines) lines[slots[n++]] = l;
        slots.clear(); openLines.clear(); doneLines.clear();
    };
    for (size_t i = 0; i < lines.size(); ++i) {
        LineInfo L = ParseLine(lines[i]);
        if (L.kind == LK_GROUP || L.kind == LK_GROUP_END || L.kind == LK_HEADING) { flush(); continue; }
        if (L.kind != LK_TODO) continue;
        slots.push_back(i);
        (L.ok ? doneLines : openLines).push_back(lines[i]);
    }
    flush();
}

// 按键找到条目所在行；hint 为行号提示（文件期间被改过也能找到）
static int FindItemLine(const std::vector<std::wstring>& lines, int hint, const std::wstring& key)
{
    if (key.empty()) return -1;
    if (hint >= 0 && (size_t)hint < lines.size() && ItemKey(ParseLine(lines[(size_t)hint])) == key) return hint;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (ItemKey(ParseLine(lines[i])) == key) return (int)i;
    }
    return -1;
}

static bool ToggleTodoLineCompletion(int sourceIndex)
{
    if (sourceIndex < 0) return false;
    std::vector<std::wstring> lines;
    if (!ReadTodoFileLinesW(lines)) return false;
    if ((size_t)sourceIndex >= lines.size()) return false;
    LineInfo L = ParseLine(lines[(size_t)sourceIndex]);
    if (L.kind != LK_TODO) return false;
    L.ok = !L.ok;
    lines[(size_t)sourceIndex] = FormatLine(L);
    if (g_sinkCompleted) SortTodoLinesByCompletion(lines);   // 默认不移动：已完成项留在原位
    return WriteTodoFileLinesW(lines);
}

// 修改某个条目所在的行（找不到返回 false）。fn 返回 false 表示删除该行。
static bool EditItemLine(int hint, const std::wstring& key, const std::function<bool(LineInfo&)>& fn)
{
    std::vector<std::wstring> lines;
    if (!ReadTodoFileLinesW(lines)) return false;
    int idx = FindItemLine(lines, hint, key);
    if (idx < 0) return false;
    LineInfo L = ParseLine(lines[(size_t)idx]);
    if (fn(L)) lines[(size_t)idx] = FormatLine(L);
    else lines.erase(lines.begin() + idx);
    return WriteTodoFileLinesW(lines);
}

// 新建独立倒计时：放在已有倒计时之后；没有的话放在第一条待办 / 分组之前；都没有就追加到末尾
static bool InsertCountdownLine(const std::wstring& name, long long due, long long repeat)
{
    std::vector<std::wstring> lines;
    if (!ReadTodoFileLinesW(lines)) return false;
    LineInfo C;
    C.kind = LK_COUNTDOWN; C.lead = L"! "; C.text = name;
    C.hasDue = true; C.due = due; C.repeat = repeat;
    int lastCd = -1, firstList = -1;
    for (size_t i = 0; i < lines.size(); ++i) {
        LineInfo L = ParseLine(lines[i]);
        if (L.kind == LK_GROUP) { if (firstList < 0) firstList = (int)i; break; }
        if (L.kind == LK_COUNTDOWN) lastCd = (int)i;
        if (L.kind == LK_TODO && firstList < 0) firstList = (int)i;
    }
    size_t pos;
    if (lastCd >= 0) pos = (size_t)lastCd + 1;
    else if (firstList >= 0) pos = (size_t)firstList;
    else {
        pos = lines.size();
        while (pos > 0 && TrimCopy(lines[pos - 1]).empty()) pos--;
    }
    lines.insert(lines.begin() + pos, FormatLine(C));
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

static bool HoverHideActive()
{
    return ((g_hoverHideEnabled && g_displayMode == 1) || g_peekActive) && !g_alertActive && !g_restoringAlert;
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
    bool wantHidden;   // true = 鼠标穿透
    int target;
    if (g_peekActive) {   // 轻浮层：默认半透明且穿透；悬停更淡；按住 Ctrl 才变实可操作
        bool interactive = reveal || busy;
        wantHidden = !interactive;
        target = interactive ? 255 : (inside ? PEEK_HOVER_ALPHA : PEEK_ALPHA);
    } else {
        wantHidden = inside && !reveal && !busy;
        target = wantHidden ? 0 : 255;
    }

    if (wantHidden != g_hoverHidden) {
        g_hoverHidden = wantHidden;
        SetClickThrough(hwnd, wantHidden);
    }

    if (g_hoverAlpha != target) {
        if (target > g_hoverAlpha) g_hoverAlpha = (std::min)(target, g_hoverAlpha + HOVER_HIDE_FADE_STEP);
        else                       g_hoverAlpha = (std::max)(target, g_hoverAlpha - HOVER_HIDE_FADE_STEP);
        ApplyHoverAlpha(hwnd);
    }
}

// ============================================================================
//  正计时（每项任务的秒表）
//  保存在 C:\ProgramData\DesktopTip\timers，每行：kind\tduration\tstart\taccum\tfired\t任务文本
//  kind: up = 正计时；start 为本次开始计时的 Unix 秒（0 = 暂停中）；accum 为此前累计秒数。
//  旧版本的 down（倒计时）记录会在启动时换算成截止时间写进 TODO 文件（见 MigrateLegacyCountdowns）。
// ============================================================================
static const char* TIMERS_DIR = "C:\\ProgramData\\DesktopTip";
static const char* TIMERS_PATH = "C:\\ProgramData\\DesktopTip\\timers";
static const char* DUES_PATH = "C:\\ProgramData\\DesktopTip\\deadlines";
static const int kTaskPresetMinutes[] = { 5, 10, 15, 25, 30, 45, 60, 90 };
static const int kTaskPresetCount = (int)(sizeof(kTaskPresetMinutes) / sizeof(kTaskPresetMinutes[0]));

static long long NowEpoch() { return (long long)std::time(nullptr); }

static long long TimerElapsed(const TaskTimer& t)
{
    long long e = t.accum + (t.start > 0 ? NowEpoch() - t.start : 0);
    return e < 0 ? 0 : e;
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

static void NoteDueSet(const std::wstring& key, long long due);

// 旧版的任务倒计时（timers 文件里的 down 记录）→ 换算成截止时间写进 TODO 文件
static void MigrateLegacyCountdowns()
{
    bool any = false;
    for (const auto& t : g_taskTimers) if (t.countdown) { any = true; break; }
    if (!any) return;
    std::vector<std::wstring> lines;
    if (!ReadTodoFileLinesW(lines)) return;   // TODO 文件暂时读不到：下次再迁移
    long long now = NowEpoch();
    bool fileChanged = false;
    for (const auto& t : g_taskTimers) {
        if (!t.countdown || t.fired) continue;
        int idx = FindItemLine(lines, -1, t.key);
        if (idx < 0) continue;
        LineInfo L = ParseLine(lines[(size_t)idx]);
        if (L.kind != LK_TODO || L.hasDue || L.ok) continue;
        long long rem = t.duration - TimerElapsed(t);
        L.hasDue = true;
        L.due = now + (rem > 0 ? rem : 0);
        L.repeat = 0;
        lines[(size_t)idx] = FormatLine(L);
        fileChanged = true;
        NoteDueSet(t.key, L.due);
    }
    if (fileChanged && !WriteTodoFileLinesW(lines)) return;
    g_taskTimers.erase(std::remove_if(g_taskTimers.begin(), g_taskTimers.end(),
        [](const TaskTimer& t) { return t.countdown; }), g_taskTimers.end());
    SaveTaskTimers();
}

static void LoadTaskTimers()
{
    g_taskTimersLoaded = true;
    g_taskTimers.clear();
    std::ifstream f(TIMERS_PATH, std::ios::binary);
    if (!f.is_open()) return;
    std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
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
    MigrateLegacyCountdowns();
}

// 只返回正计时（旧版倒计时记录在迁移前一律忽略）
static TaskTimer* FindTaskTimer(const std::wstring& key)
{
    if (!g_taskTimersLoaded) LoadTaskTimers();
    for (auto& t : g_taskTimers) {
        if (t.key == key && !t.countdown) return &t;
    }
    return nullptr;
}

static TaskTimer& EnsureTaskTimer(const std::wstring& key)
{
    if (TaskTimer* t = FindTaskTimer(key)) return *t;
    TaskTimer t;
    t.key = key;
    t.countdown = false;
    g_taskTimers.push_back(t);
    return g_taskTimers.back();
}

static bool AnyTaskTimerRunning()
{
    if (!g_taskTimersLoaded) LoadTaskTimers();
    for (const auto& t : g_taskTimers) {
        if (t.start > 0 && !t.countdown) return true;
    }
    return false;
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

// ============================================================================
//  截止时间的运行状态（不写进 TODO 文件的部分）
//  C:\ProgramData\DesktopTip\deadlines，每行：due\tsetAt\tfired\t条目键
//  setAt = 设定（或第一次看到）该截止时间的时刻，用于画进度环；fired = 是否已提醒过。
// ============================================================================
struct DueMeta {
    long long due = 0;
    long long setAt = 0;
    bool fired = false;
    bool seen = false;
    std::wstring key;
};
static std::vector<DueMeta> g_dueMeta;
static bool g_dueMetaLoaded = false;

static std::wstring KeyForFile(const std::wstring& key)
{
    // 内部前缀 0x01 不便写入文本文件：独立倒计时写成 "!C:"、分组写成 "!G:"
    if (IsCountdownKey(key)) return L"!C:" + key.substr(2);
    if (IsGroupKey(key)) return L"!G:" + key.substr(2);
    return L"T:" + key;
}

static std::wstring KeyFromFile(const std::wstring& s)
{
    if (s.compare(0, 3, L"!C:") == 0) return KEY_CD_PREFIX + s.substr(3);
    if (s.compare(0, 3, L"!G:") == 0) return KEY_GROUP_PREFIX + s.substr(3);
    if (s.compare(0, 2, L"T:") == 0) return s.substr(2);
    return s;
}

static void LoadDueMeta()
{
    g_dueMetaLoaded = true;
    g_dueMeta.clear();
    std::ifstream f(DUES_PATH, std::ios::binary);
    if (!f.is_open()) return;
    std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    for (const auto& line : SplitLinesW(DecodeAutoToUtf16(bytes))) {
        std::vector<std::wstring> p = SplitWide(line, L'\t');
        if (p.size() < 4) continue;
        DueMeta m;
        try {
            m.due = std::stoll(p[0]);
            m.setAt = std::stoll(p[1]);
        } catch (...) { continue; }
        m.fired = (p[2] == L"1");
        std::wstring k = p[3];
        for (size_t i = 4; i < p.size(); ++i) k += L"\t" + p[i];
        m.key = KeyFromFile(k);
        g_dueMeta.push_back(m);
    }
}

static void SaveDueMeta()
{
    std::error_code ec;
    fs::create_directories(TIMERS_DIR, ec);
    std::ofstream f(DUES_PATH, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return;
    for (const auto& m : g_dueMeta) {
        std::ostringstream o;
        o << m.due << '\t' << m.setAt << '\t' << (m.fired ? 1 : 0) << '\t' << WideToUtf8(KeyForFile(m.key)) << '\n';
        f << o.str();
    }
}

static int FindDueMeta(const std::wstring& key, long long due)
{
    if (!g_dueMetaLoaded) LoadDueMeta();
    for (size_t i = 0; i < g_dueMeta.size(); ++i) {
        if (g_dueMeta[i].due == due && g_dueMeta[i].key == key) return (int)i;
    }
    return -1;
}

// 通过界面设定 / 修改截止时间后调用：进度环从现在开始走，且可以再次提醒
static void NoteDueSet(const std::wstring& key, long long due)
{
    int i = FindDueMeta(key, due);
    if (i < 0) {
        DueMeta m;
        m.key = key; m.due = due;
        g_dueMeta.push_back(m);
        i = (int)g_dueMeta.size() - 1;
    }
    g_dueMeta[(size_t)i].setAt = NowEpoch();
    g_dueMeta[(size_t)i].fired = false;
    SaveDueMeta();
}

static long long DueSetAt(const std::wstring& key, long long due)
{
    int i = FindDueMeta(key, due);
    return i >= 0 ? g_dueMeta[(size_t)i].setAt : 0;
}

// 在 base 上加 months 个自然月（月末自动收拢，如 1-31 + 1 月 = 2-28/29）
static long long AddMonthsEpoch(long long base, int months)
{
    if (months <= 0) return base;
    std::tm t = LocalTm(base);
    int mon = t.tm_mon + months;
    t.tm_year += mon / 12;
    t.tm_mon = mon % 12;
    int dim = DaysInMonth(t.tm_year + 1900, t.tm_mon + 1);
    if (t.tm_mday > dim) t.tm_mday = dim;
    t.tm_isdst = -1;
    return (long long)std::mktime(&t);
}

// 把 now → due 拆成 月 / 天 / 时 / 分 / 秒
static void SplitRemain(long long now, long long due, long long out[5])
{
    for (int k = 0; k < 5; ++k) out[k] = 0;
    if (due <= now) return;
    int months = 0;
    while (months < 1200 && AddMonthsEpoch(now, months + 1) <= due) months++;
    long long rest = due - AddMonthsEpoch(now, months);
    if (rest < 0) rest = 0;
    out[0] = months;
    out[1] = rest / 86400;
    out[2] = (rest % 86400) / 3600;
    out[3] = (rest % 3600) / 60;
    out[4] = rest % 60;
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

// 剩余时长：< 1 天用时钟格式，>= 1 天用「N天 hh:mm」，很久以后只显示天数
static std::wstring FormatRemain(long long s)
{
    if (s < 0) s = -s;
    if (s < 86400) return FormatClock(s);
    wchar_t buf[48];
    long long d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60;
    if (d < 100) swprintf(buf, 48, L"%lld天 %02lld:%02lld", d, h, m);
    else swprintf(buf, 48, L"%lld天", d);
    return buf;
}

// 截止时刻：今天 / 明天 / 今年内 / 更远
static std::wstring FormatDueShort(long long due)
{
    std::tm d = LocalTm(due), n = LocalTm(NowEpoch());
    std::tm tm1 = LocalTm(NowEpoch() + 86400);
    wchar_t buf[48];
    if (d.tm_year == n.tm_year && d.tm_yday == n.tm_yday) swprintf(buf, 48, L"今天 %02d:%02d", d.tm_hour, d.tm_min);
    else if (d.tm_year == tm1.tm_year && d.tm_yday == tm1.tm_yday) swprintf(buf, 48, L"明天 %02d:%02d", d.tm_hour, d.tm_min);
    else if (d.tm_year == n.tm_year) swprintf(buf, 48, L"%02d-%02d %02d:%02d", d.tm_mon + 1, d.tm_mday, d.tm_hour, d.tm_min);
    else swprintf(buf, 48, L"%04d-%02d-%02d", d.tm_year + 1900, d.tm_mon + 1, d.tm_mday);
    return buf;
}

// 按任务文本切换完成状态（sourceIndex 只是提示，文件期间被改过也能找到正确的行）
static bool ToggleTodoByKey(int sourceIndex, const std::wstring& key)
{
    std::vector<std::wstring> lines;
    if (!ReadTodoFileLinesW(lines)) return false;
    int idx = FindItemLine(lines, sourceIndex, key);
    if (idx < 0 || !IsTodoLine(lines[(size_t)idx])) return false;
    return ToggleTodoLineCompletion(idx);
}

// 设置 / 清除截止时间；repeat < 0 表示保留原来的重复间隔
static bool SetItemDue(int hint, const std::wstring& key, long long due, long long repeat)
{
    bool ok = EditItemLine(hint, key, [&](LineInfo& L) {
        if (L.kind != LK_TODO && L.kind != LK_COUNTDOWN) return true;
        L.hasDue = true;
        L.due = due;
        if (repeat >= 0) L.repeat = repeat;
        return true;
    });
    if (ok) NoteDueSet(key, due);
    return ok;
}

static bool ClearItemDue(int hint, const std::wstring& key)
{
    return EditItemLine(hint, key, [&](LineInfo& L) {
        if (L.kind == LK_COUNTDOWN) return false;   // 独立倒计时没有截止时间就没有意义：直接删除
        L.hasDue = false;
        L.repeat = 0;
        return true;
    });
}

static bool DeleteItem(int hint, const std::wstring& key)
{
    return EditItemLine(hint, key, [](LineInfo&) { return false; });
}

static bool ToggleItemHidden(int hint, const std::wstring& key)
{
    return EditItemLine(hint, key, [](LineInfo& L) { L.hide = !L.hide; return true; });
}

static bool ToggleItemShow(int hint, const std::wstring& key)
{
    return EditItemLine(hint, key, [](LineInfo& L) {
        if (L.kind == LK_TODO || L.kind == LK_COUNTDOWN) L.show = !L.show;
        return true;
    });
}

static bool ToggleGroupFold(int hint, const std::wstring& key)
{
    return EditItemLine(hint, key, [](LineInfo& L) {
        if (L.kind == LK_GROUP) L.fold = !L.fold;
        return true;
    });
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
        g_peekActive = false;   // 醒目提醒接管轻浮层
        g_peekGlance = false;
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

// 前台是否处于全屏 / 演示 / 免打扰（此时不做轻浮现）
static bool UserIsBusy()
{
    QUERY_USER_NOTIFICATION_STATE st;
    if (FAILED(SHQueryUserNotificationState(&st))) return false;
    return st == QUNS_BUSY || st == QUNS_RUNNING_D3D_FULL_SCREEN || st == QUNS_PRESENTATION_MODE;
}

static void EndPeek(HWND hwnd)
{
    if (!g_peekActive) return;
    g_peekActive = false;
    g_peekGlance = false;
    ApplyDisplayMode(hwnd, g_displayMode);   // 沉回桌面层，并恢复完全不透明、可点击
}

// 智能浮现：每秒随截止时间检查一起调用，决定是否以轻浮层浮到最上面
static void UpdatePeek(HWND hwnd)
{
    if (g_displayMode != 2 || g_alertActive || g_restoringAlert) {
        if (g_displayMode != 2) { g_peekActive = false; g_peekGlance = false; }
        return;   // 提醒期间由提醒接管层级，结束后 StopCardAlert 会恢复显示模式
    }
    long long now = NowEpoch();
    if (g_lastGlance == 0) g_lastGlance = now;   // 启动后先等一个完整间隔
    bool busy = UserIsBusy();
    bool upcoming = g_nextOpenDue > 0 && g_nextOpenDue - now <= (long long)g_surfaceLeadMin * 60;
    bool glance = false;
    if (g_peekActive && g_peekGlance && now < g_peekUntil) glance = true;
    else if (!g_peekActive && g_surfaceEveryMin > 0 && g_hasOpenItems
             && now - g_lastGlance >= (long long)g_surfaceEveryMin * 60) {
        g_lastGlance = now;
        if (!busy) { glance = true; g_peekUntil = now + PEEK_GLANCE_SEC; }
    }
    bool want = !busy && (upcoming || glance);
    if (g_dragging || g_contextMenuOpen) want = g_peekActive;   // 操作中不切换层级
    if (want && !g_peekActive) {
        g_peekActive = true;
        g_peekGlance = !upcoming;
        g_hoverAlpha = 0;                 // 从透明淡入到轻浮层
        RaiseForAlert(hwnd);
        RenderTip(hwnd);
        g_hoverHidden = false;
        UpdateHoverHideTimer(hwnd);
        SetClickThrough(hwnd, true);
        g_hoverHidden = true;
    } else if (g_peekActive) {
        if (upcoming) g_peekGlance = false;
        if (!want) EndPeek(hwnd);
    }
}

// 每秒检查截止时间是否已到（任务与独立倒计时共用）
static void CheckDeadlines(HWND hwnd)
{
    if (!g_dueMetaLoaded) LoadDueMeta();
    std::vector<std::wstring> lines;
    if (!ReadTodoFileLinesW(lines)) return;
    long long now = NowEpoch();
    for (auto& m : g_dueMeta) m.seen = false;
    bool metaChanged = false, fileChanged = false;
    struct Fire { std::wstring key, title, detail; };
    std::vector<Fire> fires;

    auto meta = [&](const std::wstring& key, long long due) -> size_t {
        int i = FindDueMeta(key, due);
        if (i < 0) {
            DueMeta m;
            m.key = key; m.due = due; m.setAt = now;
            g_dueMeta.push_back(m);
            metaChanged = true;
            i = (int)g_dueMeta.size() - 1;
        }
        g_dueMeta[(size_t)i].seen = true;
        return (size_t)i;
    };

    long long nextDue = 0;
    bool openItems = false;
    for (size_t i = 0; i < lines.size(); ++i) {
        LineInfo L = ParseLine(lines[i]);
        if ((L.kind == LK_TODO || L.kind == LK_COUNTDOWN) && !L.ok && !L.hide) openItems = true;
        if ((L.kind != LK_TODO && L.kind != LK_COUNTDOWN) || !L.hasDue) continue;
        std::wstring key = ItemKey(L);
        size_t mi = meta(key, L.due);
        if (!L.ok && L.due > now && (nextDue == 0 || L.due < nextDue)) nextDue = L.due;
        if (L.ok || g_dueMeta[mi].fired || now < L.due) continue;
        g_dueMeta[mi].fired = true;
        metaChanged = true;

        Fire f;
        f.key = key;
        std::wstring name = ProcessPlaceholdersW(TrimCopy(L.text));
        if (L.kind == LK_COUNTDOWN) {
            f.title = name.empty() ? L"倒计时结束" : name;
            f.detail = L"倒计时结束 · " + FormatDueText(L.due);
        } else {
            f.title = L"截止时间到";
            f.detail = name;
        }
        if (L.repeat > 0) {   // 重复：截止时间顺延到下一个未来时刻
            long long nd = L.due;
            while (nd <= now) nd += L.repeat;
            L.due = nd;
            lines[i] = FormatLine(L);
            fileChanged = true;
            size_t mj = meta(key, nd);
            g_dueMeta[mj].setAt = nd - L.repeat;
            g_dueMeta[mj].fired = false;
            f.detail += L" · 下次 " + FormatDueShort(nd);
        }
        fires.push_back(f);
    }
    size_t before = g_dueMeta.size();
    g_dueMeta.erase(std::remove_if(g_dueMeta.begin(), g_dueMeta.end(),
        [](const DueMeta& m) { return !m.seen; }), g_dueMeta.end());
    if (g_dueMeta.size() != before) metaChanged = true;
    if (fileChanged) WriteTodoFileLinesW(lines);
    if (metaChanged) SaveDueMeta();
    g_nextOpenDue = nextDue;
    g_hasOpenItems = openItems;
    for (const auto& f : fires) StartCardAlert(hwnd, f.title, f.detail, f.key);
    UpdatePeek(hwnd);
}

static std::vector<std::pair<int, std::wstring>> BannerButtonSpec()
{
    std::vector<std::pair<int, std::wstring>> v;
    bool task = !g_alertTaskKey.empty() && !IsCountdownKey(g_alertTaskKey);
    if (task) v.push_back({ BANNER_DONE, L"完成" });
    v.push_back({ BANNER_SNOOZE, L"+5 分钟" });
    v.push_back({ BANNER_DISMISS, L"知道了" });
    return v;
}

// 「知道了」：不重复的独立倒计时到点后从文件里移除；任务保留截止时间（显示为已超时）
static void DismissDueItem(const std::wstring& key)
{
    if (!IsCountdownKey(key)) return;
    std::vector<std::wstring> lines;
    if (!ReadTodoFileLinesW(lines)) return;
    int idx = FindItemLine(lines, -1, key);
    if (idx < 0) return;
    LineInfo L = ParseLine(lines[(size_t)idx]);
    if (L.kind == LK_COUNTDOWN && L.repeat == 0 && L.due <= NowEpoch()) {
        lines.erase(lines.begin() + idx);
        WriteTodoFileLinesW(lines);
    }
}

static void HandleBannerAction(HWND hwnd, int action)
{
    std::wstring key = g_alertTaskKey;
    if (action == BANNER_DONE && !key.empty() && !IsCountdownKey(key)) {
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
    if (action == BANNER_SNOOZE && !key.empty()) {
        SetItemDue(-1, key, NowEpoch() + 5 * 60, -1);   // 截止时间改为 5 分钟后
    } else if (action == BANNER_DISMISS) {
        DismissDueItem(key);
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
    SnapshotRowsForSettle();   // 开启「已完成项沉底」时各行平滑滑到新位置；否则原位不动
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

// ============================================================================
//  倒计时设定对话框
//  两种设定方式可随时切换、互相换算：「截止时间」（年月日 时分秒）与「剩余时长」（月 天 时 分 秒）。
//  确定后一律换算成截止时间（@YYYY-MM-DD HH:MM:SS）写进 TODO 文件。
// ============================================================================
#define IDC_DUE_NAME     3301
#define IDC_DUE_TAB_ABS  3302
#define IDC_DUE_TAB_REL  3303
#define IDC_DUE_PICKER   3304
#define IDC_DUE_REL0     3310   // 3310..3314：月 天 时 分 秒
#define IDC_DUE_SPIN0    3320
#define IDC_DUE_REPEAT   3330
#define IDC_DUE_PREVIEW  3331

struct DueDialogState {
    std::wstring heading, sub;
    bool askName = false;
    std::wstring name;
    long long due = 0;          // 初始值 / 结果
    long long repeat = 0;       // 初始值 / 结果
    long long fixedRepeat = 0;  // 编辑已有的重复项时保留原间隔
    bool repeatChecked = false;
    int mode = 1;               // 0 = 截止时间，1 = 剩余时长
    bool accepted = false;
    double k = 1.0;             // DPI 缩放
    HFONT font = NULL, fontBold = NULL, fontSmall = NULL;
    HBRUSH bg = NULL;
    HWND nameEdit = NULL, tabAbs = NULL, tabRel = NULL, picker = NULL, preview = NULL, repeatBox = NULL;
    HWND relEdit[5] = {}, relSpin[5] = {}, relLabel[5] = {};
};

static void PickerSet(HWND picker, long long due)
{
    std::tm t = LocalTm(due);
    SYSTEMTIME st = {};
    st.wYear = (WORD)(t.tm_year + 1900); st.wMonth = (WORD)(t.tm_mon + 1); st.wDay = (WORD)t.tm_mday;
    st.wDayOfWeek = (WORD)t.tm_wday;
    st.wHour = (WORD)t.tm_hour; st.wMinute = (WORD)t.tm_min; st.wSecond = (WORD)t.tm_sec;
    SendMessageW(picker, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&st);
}

static long long PickerGet(HWND picker)
{
    SYSTEMTIME st = {};
    if (SendMessageW(picker, DTM_GETSYSTEMTIME, 0, (LPARAM)&st) != GDT_VALID) return 0;
    return LocalEpoch(st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

static long long RelGet(DueDialogState* s, long long now)
{
    long long v[5] = { 0, 0, 0, 0, 0 };
    for (int i = 0; i < 5; ++i) {
        wchar_t buf[32] = { 0 };
        GetWindowTextW(s->relEdit[i], buf, 32);
        long long n = 0;
        for (wchar_t* p = buf; *p; ++p) {
            if (*p >= L'0' && *p <= L'9') { n = n * 10 + (*p - L'0'); if (n > 100000000) break; }
        }
        v[i] = n;
    }
    if (v[0] > 1200) v[0] = 1200;
    return AddMonthsEpoch(now, (int)v[0]) + v[1] * 86400 + v[2] * 3600 + v[3] * 60 + v[4];
}

static void RelSet(DueDialogState* s, long long now, long long due)
{
    long long v[5];
    SplitRemain(now, due, v);
    for (int i = 0; i < 5; ++i) SetWindowTextW(s->relEdit[i], std::to_wstring(v[i]).c_str());
}

static long long DueDlgCurrent(DueDialogState* s)
{
    long long now = NowEpoch();
    return s->mode == 0 ? PickerGet(s->picker) : RelGet(s, now);
}

static void DueDlgPreview(DueDialogState* s)
{
    long long now = NowEpoch();
    long long due = DueDlgCurrent(s);
    std::wstring text;
    if (due <= now) {
        text = L"这个时间已经过去了，请设一个将来的时刻";
    } else if (s->mode == 0) {
        text = L"还剩 " + FormatRemain(due - now);
    } else {
        text = L"将于 " + FormatDueText(due) + L" 到点";
    }
    if (due > now && SendMessageW(s->repeatBox, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        long long rep = s->fixedRepeat > 0 ? s->fixedRepeat : due - now;
        text += L"，之后每隔 " + FormatRemain(rep) + L" 重复";
    }
    SetWindowTextW(s->preview, text.c_str());
}

static void DueDlgShowMode(DueDialogState* s)
{
    ShowWindow(s->picker, s->mode == 0 ? SW_SHOW : SW_HIDE);
    for (int i = 0; i < 5; ++i) {
        ShowWindow(s->relEdit[i], s->mode == 1 ? SW_SHOW : SW_HIDE);
        ShowWindow(s->relSpin[i], s->mode == 1 ? SW_SHOW : SW_HIDE);
        ShowWindow(s->relLabel[i], s->mode == 1 ? SW_SHOW : SW_HIDE);
    }
    SendMessageW(s->tabAbs, BM_SETCHECK, s->mode == 0 ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(s->tabRel, BM_SETCHECK, s->mode == 1 ? BST_CHECKED : BST_UNCHECKED, 0);
}

static LRESULT CALLBACK DueDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    DueDialogState* s = (DueDialogState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        s = (DueDialogState*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)s);
        HINSTANCE hi = GetModuleHandle(NULL);
        double k = s->k;
        auto X = [k](int v) { return (int)(v * k + 0.5); };
        auto mk = [&](DWORD ex, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, HFONT f) {
            HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, X(x), X(y), X(w), X(h),
                hwnd, (HMENU)(INT_PTR)id, hi, NULL);
            SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
            return c;
        };
        const int W = 400, P = 22;
        int y = 18;
        mk(0, L"STATIC", s->heading.c_str(), 0, P, y, W - 2 * P, 24, 0, s->fontBold);
        y += 26;
        if (!s->sub.empty()) { mk(0, L"STATIC", s->sub.c_str(), SS_ENDELLIPSIS | SS_NOPREFIX, P, y, W - 2 * P, 20, 1, s->fontSmall); y += 22; }
        y += 10;
        if (s->askName) {
            mk(0, L"STATIC", L"名称（可留空）", 0, P, y, W - 2 * P, 20, 0, s->font);
            y += 22;
            s->nameEdit = mk(WS_EX_CLIENTEDGE, L"EDIT", s->name.c_str(), WS_TABSTOP | ES_AUTOHSCROLL, P, y, W - 2 * P, 28, IDC_DUE_NAME, s->font);
            y += 40;
        }
        s->tabAbs = mk(0, L"BUTTON", L"截止时间", WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON | BS_PUSHLIKE, P, y, 112, 30, IDC_DUE_TAB_ABS, s->font);
        s->tabRel = mk(0, L"BUTTON", L"剩余时长", BS_AUTORADIOBUTTON | BS_PUSHLIKE, P + 112, y, 112, 30, IDC_DUE_TAB_REL, s->font);
        y += 44;
        s->picker = mk(0, DATETIMEPICK_CLASSW, L"", WS_TABSTOP | WS_GROUP, P, y, 260, 30, IDC_DUE_PICKER, s->font);
        SendMessageW(s->picker, DTM_SETFORMATW, 0, (LPARAM)L"yyyy'-'MM'-'dd'   'HH':'mm':'ss");
        static const wchar_t* units[5] = { L"月", L"天", L"时", L"分", L"秒" };
        static const int maxv[5] = { 1200, 9999, 999, 999, 999 };
        for (int i = 0; i < 5; ++i) {
            int x = P + i * 71;
            s->relEdit[i] = mk(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_TABSTOP | ES_NUMBER | ES_RIGHT, x, y + 1, 48, 28, IDC_DUE_REL0 + i, s->font);
            s->relSpin[i] = CreateWindowExW(0, UPDOWN_CLASSW, L"", WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT |
                UDS_ARROWKEYS | UDS_NOTHOUSANDS | UDS_HOTTRACK, 0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)(IDC_DUE_SPIN0 + i), hi, NULL);
            SendMessageW(s->relSpin[i], UDM_SETBUDDY, (WPARAM)s->relEdit[i], 0);
            SendMessageW(s->relSpin[i], UDM_SETRANGE32, 0, maxv[i]);
            s->relLabel[i] = mk(0, L"STATIC", units[i], SS_CENTERIMAGE, x + 50, y, 20, 30, 0, s->font);
        }
        y += 42;
        s->preview = mk(0, L"STATIC", L"", SS_NOPREFIX | SS_ENDELLIPSIS, P, y, W - 2 * P, 20, IDC_DUE_PREVIEW, s->fontSmall);
        y += 30;
        s->repeatBox = mk(0, L"BUTTON", L"到点后按同样的间隔重复", WS_TABSTOP | BS_AUTOCHECKBOX, P, y, W - 2 * P, 24, IDC_DUE_REPEAT, s->font);
        SendMessageW(s->repeatBox, BM_SETCHECK, s->repeatChecked ? BST_CHECKED : BST_UNCHECKED, 0);
        y += 44;
        mk(0, L"BUTTON", L"确定", WS_TABSTOP | BS_DEFPUSHBUTTON, W - P - 2 * 92 - 10, y, 92, 32, IDOK, s->font);
        mk(0, L"BUTTON", L"取消", WS_TABSTOP, W - P - 92, y, 92, 32, IDCANCEL, s->font);

        long long now = NowEpoch();
        PickerSet(s->picker, s->due);
        RelSet(s, now, s->due);
        DueDlgShowMode(s);
        DueDlgPreview(s);
        SetFocus(s->askName ? s->nameEdit : (s->mode == 0 ? s->picker : s->relEdit[3]));
        if (s->mode == 1) SendMessageW(s->relEdit[3], EM_SETSEL, 0, -1);
        SetTimer(hwnd, 1, 1000, NULL);   // 「还剩…」每秒更新
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wParam;
        SetBkMode(dc, TRANSPARENT);
        HWND c = (HWND)lParam;
        int id = GetDlgCtrlID(c);
        SetTextColor(dc, (id == IDC_DUE_PREVIEW || id == 1) ? RGB(110, 116, 128) : RGB(28, 32, 40));
        return (LRESULT)s->bg;
    }
    case WM_CTLCOLORBTN:
        return (LRESULT)s->bg;
    case WM_TIMER:
        if (s && s->mode == 0) DueDlgPreview(s);
        return 0;
    case WM_NOTIFY: {
        NMHDR* nh = (NMHDR*)lParam;
        if (nh && nh->idFrom == IDC_DUE_PICKER && nh->code == DTN_DATETIMECHANGE) DueDlgPreview(s);
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam), code = HIWORD(wParam);
        if ((id == IDC_DUE_TAB_ABS || id == IDC_DUE_TAB_REL) && code == BN_CLICKED) {
            int nm = (id == IDC_DUE_TAB_ABS) ? 0 : 1;
            if (nm != s->mode) {
                long long now = NowEpoch();
                long long due = DueDlgCurrent(s);
                if (due <= now) due = now + 60;
                s->mode = nm;
                if (nm == 0) PickerSet(s->picker, due); else RelSet(s, now, due);
                DueDlgShowMode(s);
                DueDlgPreview(s);
            }
            return 0;
        }
        if (id >= IDC_DUE_REL0 && id < IDC_DUE_REL0 + 5 && code == EN_CHANGE) { DueDlgPreview(s); return 0; }
        if (id == IDC_DUE_REPEAT) { DueDlgPreview(s); return 0; }
        if (id == IDOK) {
            long long now = NowEpoch();
            long long due = DueDlgCurrent(s);
            if (due <= now) {
                MessageBoxW(hwnd, L"请设一个将来的时刻。", L"DesktopTip", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            s->due = due;
            bool rep = SendMessageW(s->repeatBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
            s->repeat = rep ? (s->fixedRepeat > 0 ? s->fixedRepeat : due - now) : 0;
            if (s->askName) {
                wchar_t buf[512] = { 0 };
                GetWindowTextW(s->nameEdit, buf, 512);
                s->name = TrimCopy(buf);
            }
            s->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == IDCANCEL) { DestroyWindow(hwnd); return 0; }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool ShowDueDialog(HWND owner, DueDialogState& st)
{
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc = DueDialogProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"DesktopTipDueDialog";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
        wc.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_DESKTOPTIP));
        RegisterClassW(&wc);
        registered = true;
    }
    HDC sdc = GetDC(NULL);
    st.k = GetDeviceCaps(sdc, LOGPIXELSY) / 96.0;
    ReleaseDC(NULL, sdc);
    if (st.k < 1.0) st.k = 1.0;
    st.font = CreateFontW(-(int)(14 * st.k), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    st.fontBold = CreateFontW(-(int)(17 * st.k), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    st.fontSmall = CreateFontW(-(int)(12.5 * st.k), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    st.bg = (HBRUSH)GetStockObject(WHITE_BRUSH);

    int clientH = 18 + 26 + (st.sub.empty() ? 0 : 22) + 10 + (st.askName ? 62 : 0) + 44 + 42 + 30 + 44 + 32 + 20;
    RECT rc = { 0, 0, (LONG)(400 * st.k), (LONG)(clientH * st.k) };
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    DWORD ex = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_DLGMODALFRAME;
    AdjustWindowRectEx(&rc, style, FALSE, ex);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    RECT wa;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.left + ((wa.right - wa.left) - w) / 2;
    int y = wa.top + ((wa.bottom - wa.top) - h) / 2;

    HWND dlg = CreateWindowExW(ex, L"DesktopTipDueDialog", st.askName ? L"倒计时" : L"任务倒计时",
        style, x, y, w, h, owner, NULL, GetModuleHandle(NULL), &st);
    if (dlg) {
        ShowWindow(dlg, SW_SHOW);
        SetForegroundWindow(dlg);
        if (owner) EnableWindow(owner, FALSE);
        MSG msg;
        BOOL gm = TRUE;
        while (IsWindow(dlg) && (gm = GetMessageW(&msg, NULL, 0, 0)) > 0) {
            if (!IsDialogMessageW(dlg, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        if (owner) EnableWindow(owner, TRUE);
        if (gm == 0) PostQuitMessage((int)msg.wParam);
    }
    DeleteObject(st.font);
    DeleteObject(st.fontBold);
    DeleteObject(st.fontSmall);
    return st.accepted;
}

// 为任务 / 独立倒计时设定截止时间（对话框）
static void PromptItemDue(HWND hwnd, int hint, const std::wstring& key)
{
    std::vector<std::wstring> lines;
    if (!ReadTodoFileLinesW(lines)) return;
    int idx = FindItemLine(lines, hint, key);
    if (idx < 0) return;
    LineInfo L = ParseLine(lines[(size_t)idx]);
    long long now = NowEpoch();

    DueDialogState st;
    bool isCd = (L.kind == LK_COUNTDOWN);
    st.heading = isCd ? L"修改倒计时" : (L.hasDue ? L"修改截止时间" : L"设置截止时间");
    if (!isCd) st.sub = ProcessPlaceholdersW(TrimCopy(L.text));
    st.askName = isCd;
    st.name = L.text;
    st.due = (L.hasDue && L.due > now) ? L.due : now + 25 * 60;
    st.fixedRepeat = L.repeat;
    st.repeatChecked = L.repeat > 0;
    st.mode = g_dueInputMode;
    if (!ShowDueDialog(hwnd, st)) return;
    g_dueInputMode = st.mode;

    std::wstring newKey = key;
    bool ok = EditItemLine(idx, key, [&](LineInfo& E) {
        if (E.kind != LK_TODO && E.kind != LK_COUNTDOWN) return true;
        if (E.kind == LK_COUNTDOWN) E.text = st.name;
        E.hasDue = true;
        E.due = st.due;
        E.repeat = st.repeat;
        newKey = ItemKey(E);
        return true;
    });
    if (ok) NoteDueSet(newKey, st.due);
    if (g_alertActive && g_alertTaskKey == key) StopCardAlert(hwnd);
    confWrite();
}

// 新建独立倒计时（不挂在任务上），同样写进 TODO 文件
static void PromptNewCountdown(HWND hwnd)
{
    DueDialogState st;
    st.heading = L"新建倒计时";
    st.sub = L"会以「! 名称 @截止时间」的形式写进 TODO 文件";
    st.askName = true;
    st.due = NowEpoch() + 10 * 60;
    st.repeatChecked = g_countdownRepeatEnabled;
    st.mode = g_dueInputMode;
    if (!ShowDueDialog(hwnd, st)) return;
    g_dueInputMode = st.mode;
    g_countdownRepeatEnabled = st.repeat > 0;
    if (InsertCountdownLine(st.name, st.due, st.repeat)) {
        NoteDueSet(KEY_CD_PREFIX + st.name, st.due);
    } else {
        MessageBoxW(hwnd, L"无法写入 TODO 文件。", L"DesktopTip", MB_OK | MB_ICONWARNING);
    }
    confWrite();
}

static void HandleItemCommand(HWND hwnd, int id)
{
    std::wstring key = g_menuTaskKey;
    int hint = g_menuHint;
    if (key.empty()) return;
    bool stopAlert = false;
    if (id >= ID_TASK_CD_FIRST && id < ID_TASK_CD_FIRST + kTaskPresetCount) {
        SetItemDue(hint, key, NowEpoch() + (long long)kTaskPresetMinutes[id - ID_TASK_CD_FIRST] * 60, -1);
        stopAlert = true;
    } else if (id == ID_TASK_DUE_SET) {
        PromptItemDue(hwnd, hint, key);
    } else if (id == ID_TASK_DUE_CLEAR) {
        ClearItemDue(hint, key);
        stopAlert = true;
    } else if (id == ID_TASK_STOPWATCH) {
        StartTaskStopwatch(key);
    } else if (id == ID_TASK_PAUSE) {
        ToggleTaskTimerPause(key);
    } else if (id == ID_TASK_RESTART) {
        RestartTaskTimer(key);
    } else if (id == ID_TASK_REMOVE) {
        RemoveTaskTimer(key);
    } else if (id == ID_ITEM_HIDE) {
        SnapshotRowsForSettle();
        ToggleItemHidden(hint, key);
        SetAnimTimer(hwnd);
    } else if (id == ID_GROUP_FOLD) {
        SnapshotRowsForSettle();
        ToggleGroupFold(hint, key);
        SetAnimTimer(hwnd);
    } else if (id == ID_ITEM_SHOW) {
        SnapshotRowsForSettle();
        ToggleItemShow(hint, key);
        SetAnimTimer(hwnd);
    } else if (id == ID_CD_DELETE) {
        DeleteItem(hint, key);
        stopAlert = true;
    }
    if (stopAlert && g_alertActive && g_alertTaskKey == key) StopCardAlert(hwnd);
    RenderTip(hwnd);
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
    rows[0] = { L"字号",       IDC_SL_FONT,    SP_TITLE + 26 };
    rows[1] = { L"行距",         IDC_SL_SPACE,   SP_TITLE + 104 };
    rows[2] = { L"卡片不透明度",   IDC_SL_OPACITY, SP_TITLE + 182 };
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
        TextOutW(dc, SP_PAD, 19, L"外观", 2);
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

    // 在某个条目上右键：顶部出现针对该条目的操作
    g_menuTaskKey.clear();
    g_menuHint = -1;
    if (fromCard) {
        POINT cp = pt;
        ScreenToClient(hwnd, &cp);
        for (const auto& row : g_rowHitRects) {
            if (!PtInRect(&row.rc, cp)) continue;
            g_menuTaskKey = row.key;
            g_menuHint = row.sourceIndex;
            LineInfo L;
            {
                std::vector<std::wstring> lines;
                if (ReadTodoFileLinesW(lines)) {
                    int idx = FindItemLine(lines, row.sourceIndex, row.key);
                    if (idx >= 0) { L = ParseLine(lines[(size_t)idx]); g_menuHint = idx; }
                }
            }
            std::wstring name = Ellipsize(ProcessPlaceholdersW(KeyDisplayName(row.key)), 18);
            if (name.empty()) name = IsGroupKey(row.key) ? L"未命名分组" : L"倒计时";
            AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, name.c_str());

            if (IsGroupKey(row.key)) {
                AppendMenuW(hMenu, MF_STRING, ID_GROUP_FOLD, L.fold ? L"展开分组" : L"折叠分组");
                AppendMenuW(hMenu, MF_STRING, ID_ITEM_HIDE, L.hide ? L"取消隐藏分组" : L"隐藏分组");
            } else {
                bool isCd = IsCountdownKey(row.key);
                HMENU hQuick = CreatePopupMenu();
                for (int i = 0; i < kTaskPresetCount; ++i) {
                    wchar_t buf[48];
                    if (kTaskPresetMinutes[i] % 60 == 0) swprintf(buf, 48, L"%d 小时后", kTaskPresetMinutes[i] / 60);
                    else swprintf(buf, 48, L"%d 分钟后", kTaskPresetMinutes[i]);
                    AppendMenuW(hQuick, MF_STRING, ID_TASK_CD_FIRST + i, buf);
                }
                if (isCd) {
                    AppendMenuW(hMenu, MF_STRING, ID_TASK_DUE_SET, L"修改倒计时…");
                    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hQuick, L"快速设定");
                    AppendMenuW(hMenu, MF_STRING, ID_CD_DELETE, L"删除倒计时");
                } else {
                    AppendMenuW(hMenu, MF_STRING, ID_TASK_DUE_SET, L.hasDue ? L"修改截止时间…" : L"设置截止时间…");
                    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hQuick, L"快速倒计时");
                    if (L.hasDue) AppendMenuW(hMenu, MF_STRING, ID_TASK_DUE_CLEAR, L"清除截止时间");
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                    if (TaskTimer* t = FindTaskTimer(row.key)) {
                        AppendMenuW(hMenu, MF_STRING, ID_TASK_PAUSE, t->start > 0 ? L"暂停正计时" : L"继续正计时");
                        AppendMenuW(hMenu, MF_STRING, ID_TASK_RESTART, L"重新开始正计时");
                        AppendMenuW(hMenu, MF_STRING, ID_TASK_REMOVE, L"移除正计时");
                    } else {
                        AppendMenuW(hMenu, MF_STRING, ID_TASK_STOPWATCH, L"开始正计时");
                    }
                }
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING | (L.show ? MF_CHECKED : MF_UNCHECKED), ID_ITEM_SHOW, L"分组折叠 / 隐藏时仍显示");
                AppendMenuW(hMenu, MF_STRING, ID_ITEM_HIDE, L.hide ? L"取消隐藏" : L"隐藏");
            }
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            break;
        }
    }
    AppendMenuW(hMenu, MF_STRING, ID_NEW_COUNTDOWN, L"新建倒计时…");
    if (g_alertActive) AppendMenuW(hMenu, MF_STRING, ID_REMINDER_CANCEL, L"停止提醒");
    AppendMenuW(hMenu, MF_STRING | (g_showHidden ? MF_CHECKED : MF_UNCHECKED), ID_SHOW_HIDDEN, L"显示隐藏的条目");
    AppendMenuW(hMenu, MF_STRING | (g_sinkCompleted ? MF_CHECKED : MF_UNCHECKED), ID_SINK_DONE, L"已完成项沉底");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    HMENU hModeMenu = CreatePopupMenu();
    AppendMenuW(hModeMenu, MF_STRING | (g_displayMode == 2 ? MF_CHECKED : MF_UNCHECKED), ID_MODE_SMART, L"智能浮现（推荐）");
    AppendMenuW(hModeMenu, MF_STRING | (g_displayMode == 0 ? MF_CHECKED : MF_UNCHECKED), ID_MODE_DESKTOP, L"附着桌面");
    AppendMenuW(hModeMenu, MF_STRING | (g_displayMode == 1 ? MF_CHECKED : MF_UNCHECKED), ID_MODE_TOPMOST, L"始终置顶");
    AppendMenuW(hModeMenu, MF_SEPARATOR, 0, NULL);
    {
        UINT dis = g_displayMode == 2 ? 0 : MF_GRAYED;
        HMENU hLead = CreatePopupMenu();
        for (int i = 0; i < 4; ++i) {
            std::wstring t = L"提前 " + std::to_wstring(kSurfaceLeadOpts[i]) + L" 分钟";
            AppendMenuW(hLead, MF_STRING | (g_surfaceLeadMin == kSurfaceLeadOpts[i] ? MF_CHECKED : 0), ID_SURFACE_LEAD_FIRST + i, t.c_str());
        }
        AppendMenuW(hModeMenu, MF_POPUP | dis, (UINT_PTR)hLead, L"临近截止时浮现");
        HMENU hEvery = CreatePopupMenu();
        for (int i = 0; i < 4; ++i) {
            std::wstring t = kSurfaceEveryOpts[i] ? (L"每 " + std::to_wstring(kSurfaceEveryOpts[i]) + L" 分钟") : std::wstring(L"关闭");
            AppendMenuW(hEvery, MF_STRING | (g_surfaceEveryMin == kSurfaceEveryOpts[i] ? MF_CHECKED : 0), ID_SURFACE_EVERY_FIRST + i, t.c_str());
        }
        AppendMenuW(hModeMenu, MF_POPUP | dis, (UINT_PTR)hEvery, L"定时轻浮现");
    }
    AppendMenuW(hModeMenu, MF_STRING | (g_hoverHideEnabled ? MF_CHECKED : MF_UNCHECKED) | (g_displayMode == 1 ? 0 : MF_GRAYED),
        ID_HOVER_HIDE, L"置顶时悬停隐藏（按住 Ctrl 可操作）");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hModeMenu, L"显示模式");
    AppendMenuW(hMenu, MF_STRING, ID_SETTINGS, L"外观设置…");
    std::wstring fontLabel = L"字体：" + (g_fontFamily.empty() ? std::wstring(L"默认") : g_fontFamily) + L"…";
    AppendMenuW(hMenu, MF_STRING, ID_FONT_SELECT, fontLabel.c_str());
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EDIT,    L"编辑 TODO");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_REFRESH, L"刷新");
    AppendMenuW(hMenu, MF_STRING, ID_TODO_CHANGE,  L"更换 TODO 文件…");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT,    L"退出");

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
    ofs << "SurfaceLead: " << g_surfaceLeadMin << std::endl;
    ofs << "SurfaceEvery: " << g_surfaceEveryMin << std::endl;
    ofs << "ShowHidden: " << (g_showHidden ? 1 : 0) << std::endl;
    ofs << "SinkCompleted: " << (g_sinkCompleted ? 1 : 0) << std::endl;
    ofs << "DueDisplay: " << (g_dueShowAbsolute ? 1 : 0) << std::endl;
    ofs << "DueInputMode: " << g_dueInputMode << std::endl;
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


enum TipRowType { RT_SPACER, RT_HEADING, RT_META, RT_TODO, RT_COUNTDOWN, RT_GROUP };
struct TipRow {
    TipRowType type = RT_SPACER;
    std::wstring text;
    float w = 0;
    float tw = 0;          // 纯文字宽度（删除线动画用）
    float h = 0;
    float y = 0;
    int sourceIndex = -1;
    bool completed = false;
    std::wstring key;      // 条目键（计时器 / 菜单 / 提醒都用它）
    int seg = 0;           // 所在段（分组），已完成项只在段内沉底
    int depth = 0;         // 嵌套深度（用于缩进）
    double fade = 1.0;     // 隐藏项在「显示隐藏的条目」时淡显
    bool hasDue = false;
    long long due = 0;
    // 分组
    bool folded = false;
    int openCount = 0;     // 折叠时：未完成的待办数量
    long long nearestDue = 0;
    std::wstring summary;  // 折叠时右侧的摘要
    // 计时胶囊
    bool pill = false;
    bool pillDue = false;  // true = 截止时间，false = 正计时
    bool pillRunning = true;
    bool pillExpired = false;
    double pillFrac = 0.0;
    int pillTone = 0;      // 0 = 常态，1 = 快到点，2 = 已到点 / 超时，3 = 正计时
    std::wstring pillLabel;
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
static Gdiplus::Color Faded(const Gdiplus::Color& c, double f)
{
    return Gdiplus::Color((BYTE)ClampByte(c.GetA() * f), c.GetR(), c.GetG(), c.GetB());
}

// 对勾（按路径长度比例 t 逐步描绘）
static void DrawTick(Gdiplus::Graphics& g, float x, float y, float d, double t, const Gdiplus::Color& c, float width)
{
    using namespace Gdiplus;
    if (t <= 0.0) return;
    PointF p0(x + d * 0.27f, y + d * 0.52f), p1(x + d * 0.44f, y + d * 0.68f), p2(x + d * 0.74f, y + d * 0.34f);
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

// 把数字替换成 8，用来按「最宽数字」测量，计时数字跳动时胶囊宽度不抖
static std::wstring DigitsTo8(std::wstring s)
{
    for (auto& ch : s) if (ch >= L'0' && ch <= L'9') ch = L'8';
    return s;
}

// 阴影位图缓存：高斯模糊是渲染里最贵的一步，尺寸不变时复用（计时/动画需要频繁重绘）
static Gdiplus::Bitmap* g_shadowCache = nullptr;
static int g_shadowKey[6] = { 0 };

void RenderTip(HWND hwnd)
{
    using namespace Gdiplus;

    if (!g_taskTimersLoaded) LoadTaskTimers();
    if (!g_dueMetaLoaded) LoadDueMeta();
    auto nowSteady = steady_clock::now();
    long long nowE = NowEpoch();
    double alertT = g_alertActive ? duration<double>(nowSteady - g_alertStarted).count() : 0.0;
    double pulse = g_alertActive ? 0.5 + 0.5 * std::sin(alertT * 2.0 * ANIM_PI / 1.4) : 0.0;

    // ---- 读取并解析行 ----
    std::vector<std::wstring> raw;
    bool fileOk = ReadTodoFileLinesW(raw);
    if (!fileOk) raw.push_back(L"Error: Unable to open file!");

    // ---- 基础尺寸（由配置的字号驱动）----
    float base   = (float)(fontHeight > 8 ? fontHeight : 24); // 待办文字像素高
    float headSz = base * 2.0f;   // 时钟/标题
    float metaSz = base * 0.64f;  // 日期/页脚
    float groupSz = base * 0.66f; // 分组标题

    int PADX    = (int)(base * 1.15f);
    int PADTOP  = (int)(base * 0.95f);
    int PADBOT  = (int)(base * 1.05f);
    int checkD  = (int)(base * 0.62f); if (checkD < 12) checkD = 12;
    int checkGap = (int)(base * 0.50f);
    int divGap  = (int)(base * 0.80f);
    int metaGap = (int)(base * 0.28f);
    int headGap = (int)(base * 0.12f);
    int groupGapTop = (int)(base * 0.62f);
    int groupGapBot = (int)(base * 0.30f);
    int todoExtra = lineHeight - fontHeight; if (todoExtra < 0) todoExtra = 0;
    int todoGap = (int)(base * 0.45f) + todoExtra;
    int M       = (int)(base * 2.10f); // 阴影外扩边距（需 > 模糊半径）
    float indentStep = base * 0.95f;   // 子分组每深一级的缩进
    float radius = base * 0.70f;

    FontFamily* famText = PickFamily({ g_fontFamily.c_str(), L"Microsoft YaHei UI", L"Microsoft YaHei", L"微软雅黑" });
    FontFamily* famHead = PickFamily({ g_fontFamily.c_str(), L"Microsoft YaHei Light", L"Microsoft YaHei UI Light", L"Microsoft YaHei UI", L"Microsoft YaHei" });
    FontFamily* famNum  = PickFamily({ L"Segoe UI", L"Microsoft YaHei UI", L"Arial" });   // 数字字形，计时不跳动
    Font fHead(famHead, headSz, FontStyleRegular, UnitPixel);
    Font fText(famText, base,   FontStyleRegular, UnitPixel);
    Font fTextDone(famText, base, FontStyleStrikeout, UnitPixel);
    Font fMeta(famText, metaSz, FontStyleRegular, UnitPixel);
    Font fGroup(famText, groupSz, FontStyleBold, UnitPixel);
    Font fTimer(famNum, base * 0.64f, FontStyleRegular, UnitPixel);
    Font fBTitle(famText, base * 0.84f, FontStyleBold, UnitPixel);
    Font fBDetail(famText, base * 0.62f, FontStyleRegular, UnitPixel);
    Font fBtn(famText, base * 0.58f, FontStyleRegular, UnitPixel);

    StringFormat fmt(StringFormat::GenericTypographic());
    fmt.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);

    HDC screen = GetDC(NULL);
    Graphics mg(screen);
    mg.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    // ---- 构建行模型：多级分组 / 折叠 / 隐藏 / _SHOW ----
    std::vector<TipRow> rows;
    rows.reserve(raw.size());
    {
        struct GroupFrame { int level; int row; bool hidden; bool folded; double fade; };
        std::vector<GroupFrame> stack;
        int seg = 0;
        auto popTo = [&](int level) { while (!stack.empty() && stack.back().level >= level) stack.pop_back(); };
        for (size_t i = 0; i < raw.size(); ++i) {
            TipRow r;
            r.sourceIndex = fileOk ? (int)i : -1;
            if (!fileOk) { r.type = RT_META; r.text = raw[i]; rows.push_back(r); continue; }
            LineInfo L = ParseLine(raw[i]);
            if (L.kind == LK_GROUP_END) { popTo(L.level); seg++; continue; }
            if (L.kind == LK_HEADING) { stack.clear(); seg++; }

            // 祖先分组的状态
            bool ancHidden = false, ancFolded = false;
            double ancFade = 1.0;
            int summaryRow = -1;   // 最外层、标题可见的折叠分组：被折叠的条目计入它的摘要
            for (const auto& f : stack) {
                if (f.hidden) ancHidden = true;
                if (f.folded) { if (!ancFolded && !ancHidden && f.row >= 0) summaryRow = f.row; ancFolded = true; }
                ancFade *= f.fade;
            }
            bool ancBlocked = (ancHidden && !g_showHidden) || ancFolded;

            if (L.kind == LK_GROUP) {
                popTo(L.level);
                // 重新计算（弹栈后祖先可能变了）
                ancHidden = ancFolded = false; ancFade = 1.0; summaryRow = -1;
                for (const auto& f : stack) {
                    if (f.hidden) ancHidden = true;
                    if (f.folded) { if (!ancFolded && !ancHidden && f.row >= 0) summaryRow = f.row; ancFolded = true; }
                    ancFade *= f.fade;
                }
                ancBlocked = (ancHidden && !g_showHidden) || ancFolded;
                seg++;
                GroupFrame fr = { L.level, -1, L.hide, L.fold, L.hide ? 0.42 : 1.0 };
                bool visible = !ancBlocked && !(L.hide && !g_showHidden);
                if (visible) {
                    r.type = RT_GROUP;
                    r.text = ProcessPlaceholdersW(L.text);
                    if (r.text.empty()) r.text = L"未命名分组";
                    r.key = ItemKey(L);
                    r.folded = L.fold;
                    r.fade = ancFade * fr.fade;
                    r.seg = seg;
                    r.depth = (int)stack.size();
                    rows.push_back(r);
                    fr.row = (int)rows.size() - 1;
                }
                stack.push_back(fr);
                continue;
            }

            bool isItem = (L.kind == LK_TODO || L.kind == LK_COUNTDOWN);
            bool selfHidden = L.hide && !g_showHidden;
            // _SHOW：所在分组折叠或隐藏时，这一条仍显示（自身带 _HIDE 时除外）
            bool pinned = isItem && L.show && !selfHidden;
            if ((ancBlocked && !pinned) || selfHidden) {
                if (ancFolded && summaryRow >= 0 && isItem && !L.ok && !L.hide) {
                    TipRow& gr = rows[(size_t)summaryRow];
                    if (L.kind == LK_TODO) gr.openCount++;
                    if (L.hasDue && (gr.nearestDue == 0 || L.due < gr.nearestDue)) gr.nearestDue = L.due;
                }
                continue;
            }
            r.seg = seg;
            r.depth = (int)stack.size();
            r.fade = (pinned && ancBlocked ? 1.0 : ancFade) * (L.hide ? 0.42 : 1.0);
            switch (L.kind) {
            case LK_BLANK: r.type = RT_SPACER; break;
            case LK_HEADING: r.type = RT_HEADING; r.text = ProcessPlaceholdersW(L.text); r.depth = 0; break;
            case LK_META: r.type = RT_META; r.text = ProcessPlaceholdersW(L.text); break;
            case LK_COUNTDOWN:
                r.type = RT_COUNTDOWN;
                r.text = ProcessPlaceholdersW(TrimCopy(L.text));
                if (r.text.empty()) r.text = L"倒计时";
                r.key = ItemKey(L);
                r.completed = L.ok;
                r.hasDue = true; r.due = L.due;
                break;
            default:
                r.type = RT_TODO;
                r.text = ProcessPlaceholdersW(L.text);
                r.key = ItemKey(L);
                r.completed = L.ok;
                r.hasDue = L.hasDue; r.due = L.due;
                break;
            }
            rows.push_back(r);
        }
    }

    // 开启「已完成项沉底」时：已完成排在后面（只在同一段内，与文件内的排序一致，容错手工编辑）
    if (g_sinkCompleted) {

        std::unordered_map<int, std::vector<size_t>> slotsBySeg;
        std::vector<int> segOrder;
        for (size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].type != RT_TODO) continue;
            if (!slotsBySeg.count(rows[i].seg)) segOrder.push_back(rows[i].seg);
            slotsBySeg[rows[i].seg].push_back(i);
        }
        for (int sg : segOrder) {
            auto& slots = slotsBySeg[sg];
            std::vector<TipRow> todos;
            for (size_t ix : slots) todos.push_back(rows[ix]);
            std::stable_sort(todos.begin(), todos.end(), [](const TipRow& a, const TipRow& b) { return a.completed < b.completed; });
            for (size_t n = 0; n < slots.size(); ++n) rows[slots[n]] = todos[n];
        }
    }

    auto measure = [&](const std::wstring& str, Font& f) -> RectF {
        RectF bb;
        if (str.empty()) return bb;
        mg.MeasureString(str.c_str(), (INT)str.size(), &f, PointF(0, 0), &fmt, &bb);
        return bb;
    };

    // ---- 计时胶囊内容（截止时间优先；任务完成后只保留正计时的最终用时）----
    float pillH = base * 1.0f;
    float ringD = base * 0.62f;
    float pillPadX = base * 0.34f;
    float pillInnerGap = base * 0.28f;
    float pillGap = base * 0.75f;
    float pillColW = 0.0f;
    g_hasDuePill = false;
    auto dueLabel = [&](long long due) -> std::wstring {
        long long rem = due - nowE;
        if (g_dueShowAbsolute) return FormatDueShort(due);
        return rem > 0 ? FormatRemain(rem) : L"+" + FormatRemain(-rem);
    };
    for (auto& r : rows) {
        if (r.type == RT_GROUP && r.folded) {
            std::wstring s;
            if (r.openCount > 0) s = std::to_wstring(r.openCount) + L" 项";
            if (r.nearestDue > 0) { s += (s.empty() ? L"" : L"  ·  ") + dueLabel(r.nearestDue); g_hasDuePill = true; }
            r.summary = s;
            continue;
        }
        if (r.type != RT_TODO && r.type != RT_COUNTDOWN) continue;
        if (r.hasDue && !r.completed) {
            long long rem = r.due - nowE;
            long long setAt = DueSetAt(r.key, r.due);
            long long total = (setAt > 0 && r.due > setAt) ? r.due - setAt : 0;
            r.pill = true; r.pillDue = true; r.pillRunning = true;
            r.pillExpired = rem <= 0;
            r.pillFrac = rem <= 0 ? 1.0 : (total > 0 ? Clamp01((double)rem / (double)total) : 1.0);
            r.pillTone = rem <= 0 ? 2 : ((rem <= 60 || (total > 0 && rem * 5 <= total)
                || (g_displayMode == 2 && rem <= (long long)g_surfaceLeadMin * 60)) ? 1 : 0);
            r.pillLabel = dueLabel(r.due);
            g_hasDuePill = true;
        } else if (r.type == RT_TODO) {
            TaskTimer* t = FindTaskTimer(r.key);
            if (!t) continue;
            long long el = TimerElapsed(*t);
            r.pill = true; r.pillDue = false; r.pillRunning = t->start > 0;
            r.pillFrac = (double)(el % 60) / 60.0;
            r.pillTone = 3;
            r.pillLabel = FormatClock(el);
        }
        if (r.pill) {
            float w = pillPadX * 2 + ringD + pillInnerGap + measure(DigitsTo8(r.pillLabel), fTimer).Width;
            if (w > pillColW) pillColW = w;
        }
    }

    // ---- 第一遍：测量 + 计算布局 ----
    float contentW = base * 4.0f;
    for (auto& r : rows) {
        if (r.type == RT_SPACER) { r.h = base * 0.45f; continue; }
        Font* f = (r.type == RT_HEADING) ? &fHead : (r.type == RT_META) ? &fMeta : (r.type == RT_GROUP) ? &fGroup : &fText;
        RectF bb = measure(r.text, *f);
        r.h = bb.Height;
        r.tw = bb.Width;
        r.w = bb.Width;
        if (r.type == RT_TODO || r.type == RT_COUNTDOWN || r.type == RT_GROUP) r.w += (float)(checkD + checkGap);
        r.w += r.depth * indentStep;
        if (r.pill) r.w += pillGap + pillColW;
        if (r.type == RT_GROUP && !r.summary.empty()) r.w += pillGap + measure(DigitsTo8(r.summary), fMeta).Width;
        if (r.w > contentW) contentW = r.w;
    }

    // ---- 提醒横幅尺寸 ----
    float bPad = base * 0.60f, iconD = base * 1.30f, btnH = base * 1.15f, btnPadX = base * 0.70f, btnGap = base * 0.30f;
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
        bHeadBlock = (std::max)(iconD, bTitleH + base * 0.10f + bDetailH);
        bannerH = bPad + bHeadBlock + base * 0.55f + btnH + bPad;
        if (need > contentW) contentW = need;
    }

    float y = (float)PADTOP;
    float bannerY = y;
    if (g_alertActive) y += bannerH + base * 0.85f;
    float dividerY = -1.0f;
    bool placedDivider = false, seenContent = g_alertActive, seenList = false;
    float lastGap = 0.0f;
    for (auto& r : rows) {
        if (r.type == RT_SPACER) { y += r.h; lastGap = 0; continue; }
        bool listRow = (r.type == RT_TODO || r.type == RT_COUNTDOWN || r.type == RT_GROUP);
        if (listRow && !placedDivider) {
            if (seenContent) { y += divGap; dividerY = y; y += 1 + divGap; }
            placedDivider = true;
        } else if (r.type == RT_GROUP && seenList) {
            y += groupGapTop - lastGap * 0.5f;
        }
        r.y = y;
        if (r.type == RT_HEADING) { y += r.h + headGap; lastGap = (float)headGap; }
        else if (r.type == RT_META) { y += r.h + metaGap; lastGap = (float)metaGap; }
        else if (r.type == RT_GROUP) { y += r.h + groupGapBot; lastGap = (float)groupGapBot; }
        else { float rh = (r.h > checkD ? r.h : (float)checkD); y += rh + todoGap; lastGap = (float)todoGap; }
        seenContent = true;
        if (listRow) seenList = true;
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
            SolidBrush sb(Color(120, 0, 0, 0));
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

    // 卡片底 + 顶部微光 + 发丝描边
    GraphicsPath cp;
    RoundRectPath(cp, (float)M, (float)M, (float)cardW, (float)cardH, radius);
    SolidBrush cardBrush(Color((BYTE)g_bgAlpha, GetRValue(g_bgColor), GetGValue(g_bgColor), GetBValue(g_bgColor)));
    g.FillPath(&cardBrush, &cp);
    {
        LinearGradientBrush sheen(PointF(0, (REAL)M), PointF(0, (REAL)(M + cardH * 0.55f)),
            Color(16, 255, 255, 255), Color(0, 255, 255, 255));
        g.FillPath(&sheen, &cp);
    }
    Pen border(Color(26, 255, 255, 255), 1.0f);
    g.DrawPath(&border, &cp);

    // 提醒中：卡片边缘呼吸光
    if (g_alertActive) {
        for (int k = 3; k >= 1; --k) {
            Pen gp(Color((BYTE)ClampByte((18.0 + 30.0 * pulse) * (4 - k) / 3.0), 255, 107, 107), (REAL)(k * 2.2f));
            g.DrawPath(&gp, &cp);
        }
        Pen bp(Color((BYTE)ClampByte(90.0 + 110.0 * pulse), 255, 128, 112), 1.4f);
        g.DrawPath(&bp, &cp);
    }

    // 颜色
    BYTE TR = GetRValue(g_textColor), TG = GetGValue(g_textColor), TB = GetBValue(g_textColor);
    Color ink(255, TR, TG, TB);
    Color checkc(200, TR, TG, TB);
    Color doneInk(150, ClampByte(TR * 0.62), ClampByte(TG * 0.62), ClampByte(TB * 0.62));
    Color doneCheck(150, ClampByte(TR * 0.58), ClampByte(TG * 0.58), ClampByte(TB * 0.58));
    Color doneFill(40, ClampByte(TR * 0.50), ClampByte(TG * 0.50), ClampByte(TB * 0.50));
    Color meta(255, 160, 167, 182);
    Color groupInk = LerpColor(meta, ink, 0.30);
    Color shadowc(80, 0, 0, 0);
    Color accentDone(255, 52, 211, 153);    // 完成：翡翠绿
    Color accentGold(255, 253, 224, 71);
    Color coral(255, 255, 107, 107);        // 到点：珊瑚红
    Color amber(255, 251, 191, 36);         // 快到点：琥珀
    Color sky(255, 125, 211, 252);          // 倒计时：天蓝
    Color violet(255, 196, 181, 253);       // 正计时：淡紫
    float sdy = base * 0.045f; if (sdy < 1) sdy = 1;
    float stroke = base * 0.058f; if (stroke < 1.2f) stroke = 1.2f;

    auto drawText = [&](const std::wstring& str, Font& f, float tx, float ty, Color c) {
        if (str.empty()) return;
        SolidBrush shb(WithAlpha(shadowc, shadowc.GetA() * c.GetA() / 255.0));
        g.DrawString(str.c_str(), (INT)str.size(), &f, PointF(tx, ty + sdy), &fmt, &shb);
        SolidBrush br(c);
        g.DrawString(str.c_str(), (INT)str.size(), &f, PointF(tx, ty), &fmt, &br);
    };
    auto toneColor = [&](int tone) -> Color {
        return tone == 2 ? coral : tone == 1 ? amber : tone == 3 ? violet : sky;
    };

    float bx = (float)(M + PADX);
    float rightX = (float)(M + cardW - PADX);
    if (dividerY >= 0) {
        LinearGradientBrush dg(PointF(bx, 0), PointF(rightX, 0), Color(34, 255, 255, 255), Color(0, 255, 255, 255));
        float dyv = M + dividerY;
        g.FillRectangle(&dg, bx, dyv, rightX - bx, 1.0f);
    }

    // ---- 提醒横幅：卡片色调的玻璃面板 + 珊瑚色描边 ----
    g_bannerButtons.clear();
    if (g_alertActive) {
        double e = EaseOutCubic(alertT / 0.38);              // 入场：下滑 + 淡入
        double A = 255.0 * e;
        float bw = rightX - bx;
        float byT = M + bannerY - (float)((1.0 - e) * base * 0.6);
        float br = base * 0.50f;
        GraphicsPath bpth;
        RoundRectPath(bpth, bx, byT, bw, bannerH, br);
        SolidBrush bfillBg(Color((BYTE)ClampByte(A * (0.10 + 0.05 * pulse)), 255, 107, 107));
        g.FillPath(&bfillBg, &bpth);
        Pen bpen(Color((BYTE)ClampByte(A * (0.45 + 0.25 * pulse)), 255, 128, 112), 1.2f);
        g.DrawPath(&bpen, &bpth);

        // 图标：细线圆环 + 指针（抖动时轻轻摇摆）
        float icx = bx + bPad + iconD / 2, icy = byT + bPad + bHeadBlock / 2;
        double wig = 0.0;
        if (g_shaking) {
            double st = duration<double>(nowSteady - g_shakeStart).count();
            wig = 16.0 * std::sin(st * 2.0 * ANIM_PI * 6.0) * (1.0 - Clamp01(st / SHAKE_SEC));
        }
        GraphicsState gs = g.Save();
        g.TranslateTransform(icx, icy);
        g.RotateTransform((REAL)wig);
        SolidBrush icBg(Color((BYTE)ClampByte(A * 0.18), 255, 107, 107));
        g.FillEllipse(&icBg, -iconD / 2, -iconD / 2, iconD, iconD);
        float cr = iconD * 0.26f, sw = (std::max)(1.3f, base * 0.065f);
        Pen wp(Color((BYTE)ClampByte(A), 255, 150, 140), sw);
        wp.SetStartCap(LineCapRound);
        wp.SetEndCap(LineCapRound);
        g.DrawEllipse(&wp, -cr, -cr, cr * 2, cr * 2);
        g.DrawLine(&wp, 0.0f, 0.0f, 0.0f, -cr * 0.58f);
        g.DrawLine(&wp, 0.0f, 0.0f, cr * 0.42f, cr * 0.24f);
        g.Restore(gs);

        float tx = bx + bPad + iconD + bPad * 0.8f;
        float ty = byT + bPad + (bHeadBlock - (bTitleH + base * 0.10f + bDetailH)) / 2;
        SolidBrush tbr(WithAlpha(ink, A));
        g.DrawString(g_alertTitle.c_str(), (INT)g_alertTitle.size(), &fBTitle, PointF(tx, ty), &fmt, &tbr);
        SolidBrush dbr(WithAlpha(meta, A * 0.95));
        g.DrawString(g_alertDetail.c_str(), (INT)g_alertDetail.size(), &fBDetail, PointF(tx, ty + bTitleH + base * 0.10f), &fmt, &dbr);

        // 按钮（右对齐；第一个为主按钮）
        float byB = byT + bannerH - bPad - btnH;
        float cx = bx + bw - bPad;
        for (int i = (int)btnSpec.size() - 1; i >= 0; --i) {
            cx -= btnW[(size_t)i];
            bool primary = (i == 0);
            GraphicsPath bpp;
            RoundRectPath(bpp, cx, byB, btnW[(size_t)i], btnH, btnH / 2);
            SolidBrush bfill(primary ? Color((BYTE)ClampByte(A * 0.92), 255, 107, 107) : Color((BYTE)ClampByte(A * 0.10), TR, TG, TB));
            g.FillPath(&bfill, &bpp);
            const std::wstring& label = btnSpec[(size_t)i].second;
            RectF lb = measure(label, fBtn);
            SolidBrush lbr(primary ? Color((BYTE)ClampByte(A), 255, 255, 255) : WithAlpha(ink, A * 0.9));
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
    g_groupHitRects.clear();
    std::unordered_map<std::wstring, float> rowY;
    double settleT = g_settling ? EaseOutCubic(duration<double>(nowSteady - g_settleStart).count() / SETTLE_ANIM_SEC) : 1.0;
    double animP = g_completeAnim.active ? Clamp01(duration<double>(nowSteady - g_completeAnim.start).count() / COMPLETE_ANIM_SEC) : 0.0;

    for (auto& r : rows) {
        if (r.type == RT_SPACER) continue;
        float ry = M + r.y;
        double fd = r.fade;
        if (r.type == RT_HEADING) { drawText(r.text, fHead, bx, ry, Faded(ink, fd)); continue; }
        if (r.type == RT_META) { drawText(r.text, fMeta, bx + r.depth * indentStep, ry, Faded(meta, fd)); continue; }

        // 行位置变化（完成沉底 / 折叠展开 / 隐藏）时，从旧位置平滑滑到新位置
        if (g_settling && !r.key.empty()) {
            auto it = g_settleFrom.find(r.key);
            if (it != g_settleFrom.end()) ry = (float)(M + it->second + (r.y - it->second) * settleT);
        }
        if (!r.key.empty() && !rowY.count(r.key)) rowY[r.key] = ry - M;

        float rh = (r.h > checkD ? r.h : (float)checkD);
        float cy = ry + rh / 2.0f - checkD / 2.0f;
        float ix = bx + r.depth * indentStep;   // 本行左缘（按嵌套深度缩进）
        float textX = ix + checkD + checkGap;
        float rowL = ix - base * 0.35f, rowR = rightX + base * 0.35f;
        float rowT = ry - base * 0.22f, rowHt = rh + base * 0.44f;
        float ccx = ix + checkD / 2.0f, ccy = cy + checkD / 2.0f;

        // ---- 分组标题：细线箭头 + 名称；折叠时右侧显示数量与最近的截止时间 ----
        if (r.type == RT_GROUP) {
            float cs = checkD * 0.30f;
            Pen chev(Faded(groupInk, fd), (std::max)(1.2f, stroke * 0.95f));
            chev.SetStartCap(LineCapRound);
            chev.SetEndCap(LineCapRound);
            chev.SetLineJoin(LineJoinRound);
            float gcy = ry + r.h / 2.0f;
            if (r.folded) {
                PointF pts[3] = { PointF(ccx - cs * 0.45f, gcy - cs), PointF(ccx + cs * 0.55f, gcy), PointF(ccx - cs * 0.45f, gcy + cs) };
                g.DrawLines(&chev, pts, 3);
            } else {
                PointF pts[3] = { PointF(ccx - cs, gcy - cs * 0.45f), PointF(ccx, gcy + cs * 0.55f), PointF(ccx + cs, gcy - cs * 0.45f) };
                g.DrawLines(&chev, pts, 3);
            }
            drawText(r.text, fGroup, textX, ry, Faded(groupInk, fd));
            if (!r.summary.empty()) {
                RectF sb = measure(r.summary, fMeta);
                Color sc = meta;
                if (r.nearestDue > 0 && r.nearestDue <= nowE) sc = coral;
                drawText(r.summary, fMeta, rightX - sb.Width, ry + (r.h - sb.Height) / 2.0f, Faded(sc, fd * 0.9));
            }
            RECT rc = { (LONG)rowL, (LONG)rowT, (LONG)rowR, (LONG)(rowT + rowHt) };
            g_groupHitRects.push_back({ rc, r.sourceIndex, r.key, false });
            g_rowHitRects.push_back({ rc, r.sourceIndex, r.key, false });
            continue;
        }

        bool animating = r.type == RT_TODO && g_completeAnim.active && !r.completed && r.key == g_completeAnim.key;
        bool alerting = g_alertActive && !g_alertTaskKey.empty() && !r.completed && r.key == g_alertTaskKey;

        if (alerting || animating) {
            GraphicsPath hp;
            RoundRectPath(hp, rowL, rowT, rowR - rowL, rowHt, base * 0.35f);
            SolidBrush hb(alerting ? WithAlpha(coral, 22.0 + 30.0 * pulse) : WithAlpha(accentDone, 30.0 * std::sin(ANIM_PI * animP)));
            g.FillPath(&hb, &hp);
        }

        if (r.type == RT_COUNTDOWN) {
            // 独立倒计时：细线小钟代替复选框
            Color ic = Faded(r.completed ? doneCheck : toneColor(r.pillTone), fd);
            Pen ip(ic, stroke);
            ip.SetStartCap(LineCapRound);
            ip.SetEndCap(LineCapRound);
            float cr = checkD / 2.0f - stroke / 2.0f;
            g.DrawEllipse(&ip, ccx - cr, ccy - cr, cr * 2, cr * 2);
            g.DrawLine(&ip, ccx, ccy, ccx, ccy - cr * 0.55f);
            g.DrawLine(&ip, ccx, ccy, ccx + cr * 0.42f, ccy + cr * 0.20f);
            drawText(r.text, fText, textX, ry, Faded(r.completed ? doneInk : ink, fd));
        } else if (animating) {
            // 完成动画：涟漪 + 光点 + 圆形复选框弹出填充 + 对勾描绘 + 删除线划过
            double pf = EaseOutBack(animP / 0.32);
            double pt = Clamp01((animP - 0.16) / 0.30);
            double pr = Clamp01(animP / 0.80);
            double ps = EaseInOutCubic((animP - 0.28) / 0.55);
            {
                float rr = checkD * (0.55f + 1.25f * (float)EaseOutCubic(pr));
                Pen rp(WithAlpha(accentDone, 160.0 * (1.0 - pr)), (std::max)(1.0f, stroke * (float)(1.5 - pr)));
                g.DrawEllipse(&rp, ccx - rr, ccy - rr, rr * 2, rr * 2);
            }
            for (int k = 0; k < 8; ++k) {
                double ang = k * ANIM_PI / 4.0 + 0.39;
                double dist = checkD * (0.6 + 1.5 * EaseOutCubic(pr));
                float ds = (float)(checkD * 0.11 * (1.0 - pr));
                if (ds < 0.3f) continue;
                SolidBrush sb(WithAlpha(k % 2 == 0 ? accentDone : accentGold, 225.0 * (1.0 - pr)));
                float sx = ccx + (float)(std::cos(ang) * dist), sy = ccy + (float)(std::sin(ang) * dist);
                g.FillEllipse(&sb, sx - ds, sy - ds, ds * 2, ds * 2);
            }
            Pen boxPen(LerpColor(checkc, accentDone, animP / 0.2), stroke);
            float cr = checkD / 2.0f - stroke / 2.0f;
            g.DrawEllipse(&boxPen, ccx - cr, ccy - cr, cr * 2, cr * 2);
            float fs = (float)(checkD * pf);
            if (fs > 0.5f) {
                SolidBrush fb(accentDone);
                g.FillEllipse(&fb, ccx - fs / 2, ccy - fs / 2, fs, fs);
            }
            DrawTick(g, ix, cy, (float)checkD, pt, Color(255, 255, 255, 255), stroke + 0.3f);
            Color tc = LerpColor(ink, doneInk, ps);
            drawText(r.text, fText, textX, ry, tc);
            if (ps > 0.0) {
                Pen sp(tc, (std::max)(1.2f, base * 0.055f));
                float sy = ry + r.h * 0.54f;
                g.DrawLine(&sp, textX, sy, textX + r.tw * (float)ps, sy);
            }
        } else {
            float cr = checkD / 2.0f - stroke / 2.0f;
            if (r.completed) {
                SolidBrush fill(Faded(doneFill, fd));
                g.FillEllipse(&fill, ccx - checkD / 2.0f, ccy - checkD / 2.0f, (REAL)checkD, (REAL)checkD);
            }
            Pen boxPen(Faded(r.completed ? doneCheck : checkc, fd), stroke);
            g.DrawEllipse(&boxPen, ccx - cr, ccy - cr, cr * 2, cr * 2);
            if (r.completed) DrawTick(g, ix, cy, (float)checkD, 1.0, Faded(doneCheck, fd), stroke + 0.3f);
            // 刚完成的一项：滑动期间强调色逐渐褪为已完成样式
            if (r.completed && g_settling && r.key == g_justCompletedKey) {
                double a = 1.0 - settleT;
                SolidBrush fb(WithAlpha(accentDone, 255.0 * a));
                g.FillEllipse(&fb, ccx - checkD / 2.0f, ccy - checkD / 2.0f, (REAL)checkD, (REAL)checkD);
                DrawTick(g, ix, cy, (float)checkD, 1.0, Color((BYTE)ClampByte(255.0 * a), 255, 255, 255), stroke + 0.3f);
            }
            Font& todoFont = r.completed ? fTextDone : fText;
            drawText(r.text, todoFont, textX, ry, Faded(r.completed ? doneInk : ink, fd));
        }

        if (r.sourceIndex >= 0) {
            RECT rowRc = { (LONG)rowL, (LONG)rowT, (LONG)rowR, (LONG)(rowT + rowHt) };
            g_rowHitRects.push_back({ rowRc, r.sourceIndex, r.key, r.completed });
            if (r.type == RT_TODO) {
                int inflate = (int)(base * 0.20f); if (inflate < 4) inflate = 4;
                RECT hit = { (LONG)(ix - inflate), (LONG)(cy - inflate), (LONG)(ix + checkD + inflate), (LONG)(cy + checkD + inflate) };
                g_todoHitRects.push_back({ hit, r.sourceIndex, r.key, r.completed });
            }
        }

        // ---- 计时胶囊：细进度环 + 数字；只有到点 / 提醒时才有底色 ----
        if (r.pill && pillColW > 0) {
            float px = rightX - pillColW;
            float py = ry + rh / 2.0f - pillH / 2.0f;
            Color ringC = toneColor(r.pillTone);
            double fade = (r.completed ? 0.45 : (r.pillRunning ? 1.0 : 0.65)) * fd;

            if (r.pillExpired && !r.completed) {
                GraphicsPath pp;
                RoundRectPath(pp, px, py, pillColW, pillH, pillH / 2);
                SolidBrush pbr(WithAlpha(coral, (alerting ? 46.0 + 40.0 * pulse : 36.0) * fd));
                g.FillPath(&pbr, &pp);
            }

            float rcx = px + pillPadX + ringD / 2.0f, rcy = py + pillH / 2.0f;
            float rw = (std::max)(1.3f, ringD * 0.13f);
            float rd = ringD - rw;
            Pen track(Color((BYTE)ClampByte(255.0 * fade * 0.20), TR, TG, TB), rw);
            g.DrawEllipse(&track, rcx - rd / 2, rcy - rd / 2, rd, rd);
            Pen arc(WithAlpha(ringC, 255.0 * fade), rw);
            arc.SetStartCap(LineCapRound);
            arc.SetEndCap(LineCapRound);
            if (r.pillFrac >= 0.999) g.DrawEllipse(&arc, rcx - rd / 2, rcy - rd / 2, rd, rd);
            else if (r.pillFrac > 0.001) g.DrawArc(&arc, rcx - rd / 2, rcy - rd / 2, rd, rd, -90.0f, (REAL)(360.0 * r.pillFrac));
            if (!r.pillRunning) {   // 暂停：中间两道竖线
                SolidBrush pb(WithAlpha(ringC, 255.0 * fade));
                float bw2 = (std::max)(1.2f, rw * 0.75f), bh2 = ringD * 0.34f;
                g.FillRectangle(&pb, rcx - bw2 * 1.6f, rcy - bh2 / 2, bw2, bh2);
                g.FillRectangle(&pb, rcx + bw2 * 0.6f, rcy - bh2 / 2, bw2, bh2);
            }
            RectF lb = measure(r.pillLabel, fTimer);
            Color lc = (r.pillExpired && !r.completed) ? Color(255, 255, 160, 150) : Color(225, TR, TG, TB);
            SolidBrush lbr(Faded(lc, fade));
            g.DrawString(r.pillLabel.c_str(), (INT)r.pillLabel.size(), &fTimer,
                PointF(rcx + ringD / 2.0f + pillInnerGap, py + (pillH - lb.Height) / 2.0f), &fmt, &lbr);

            RECT prc = { (LONG)px, (LONG)py, (LONG)(px + pillColW), (LONG)(py + pillH) };
            TodoHitRect ph = { prc, r.sourceIndex, r.key, r.completed };
            ph.kind = r.pillDue ? 1 : 0;
            g_pillHitRects.push_back(ph);
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
            for (const auto& h : g_pillHitRects) {                  // 计时胶囊
                if (!PtInRect(&h.rc, p)) continue;
                if (h.kind == 1) {                                   // 截止时间：在「剩余时长 / 截止时刻」之间切换
                    g_dueShowAbsolute = !g_dueShowAbsolute;
                    confWrite();
                } else {                                             // 正计时：暂停 / 继续
                    ToggleTaskTimerPause(h.key);
                }
                RenderTip(hWnd);
                return 0;
            }
            for (const auto& h : g_todoHitRects) {                  // 复选框：完成动画
                if (PtInRect(&h.rc, p)) { TodoHitRect copy = h; BeginToggle(hWnd, copy); return 0; }
            }
            for (const auto& h : g_groupHitRects) {                 // 分组标题：折叠 / 展开
                if (PtInRect(&h.rc, p)) {
                    SnapshotRowsForSettle();
                    ToggleGroupFold(h.sourceIndex, h.key);
                    SetAnimTimer(hWnd);
                    RenderTip(hWnd);
                    return 0;
                }
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
            g_displayMode = 0;          // 附着桌面
            g_peekActive = false;
            confWrite();
            ApplyDisplayMode(hWnd, 0);
        }
        if (LOWORD(wParam) == ID_MODE_SMART) {
            g_displayMode = 2;          // 智能浮现
            g_peekActive = false;
            g_lastGlance = NowEpoch();
            confWrite();
            ApplyDisplayMode(hWnd, 2);
            UpdatePeek(hWnd);
        }
        if (LOWORD(wParam) >= ID_SURFACE_LEAD_FIRST && LOWORD(wParam) < ID_SURFACE_LEAD_FIRST + 4) {
            g_surfaceLeadMin = kSurfaceLeadOpts[LOWORD(wParam) - ID_SURFACE_LEAD_FIRST];
            confWrite();
            UpdatePeek(hWnd);
            RenderTip(hWnd);
        }
        if (LOWORD(wParam) >= ID_SURFACE_EVERY_FIRST && LOWORD(wParam) < ID_SURFACE_EVERY_FIRST + 4) {
            g_surfaceEveryMin = kSurfaceEveryOpts[LOWORD(wParam) - ID_SURFACE_EVERY_FIRST];
            g_lastGlance = NowEpoch();
            confWrite();
        }
        if (LOWORD(wParam) == ID_MODE_TOPMOST) {
            g_displayMode = 1;          // 始终置顶
            g_peekActive = false;
            confWrite();
            ApplyDisplayMode(hWnd, 1);
        }
        if (LOWORD(wParam) == ID_HOVER_HIDE) {
            g_hoverHideEnabled = !g_hoverHideEnabled;
            confWrite();
            UpdateHoverHideTimer(hWnd);
        }
        if (LOWORD(wParam) == ID_NEW_COUNTDOWN) {
            PromptNewCountdown(hWnd);
            RenderTip(hWnd);
        }
        if (LOWORD(wParam) == ID_SHOW_HIDDEN) {
            SnapshotRowsForSettle();
            g_showHidden = !g_showHidden;
            confWrite();
            SetAnimTimer(hWnd);
            RenderTip(hWnd);
        }
        if (LOWORD(wParam) == ID_SINK_DONE) {
            g_sinkCompleted = !g_sinkCompleted;
            confWrite();
            RenderTip(hWnd);
        }
        if (LOWORD(wParam) == ID_REMINDER_CANCEL) {
            if (g_alertActive) {
                DismissDueItem(g_alertTaskKey);
                StopCardAlert(hWnd);
            }
        }
        if (LOWORD(wParam) >= ID_TASK_CD_FIRST && LOWORD(wParam) <= ID_ITEM_LAST) {
            HandleItemCommand(hWnd, LOWORD(wParam));
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
            for (const auto& h : g_groupHitRects) if (PtInRect(&h.rc, p)) return 0;
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
            if (g_displayMode == 1 || g_peekActive) {
                // 周期性重新置顶：防止个别全屏/置顶程序抢走最上层
                SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            RenderTip(hWnd);
        }
        if (wParam == TIMER_REMINDER_CHECK) {
            CheckDeadlines(hWnd);
            if (!g_animTimerOn && (AnyTaskTimerRunning() || g_hasDuePill)) RenderTip(hWnd);   // 每秒刷新计时显示
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

    if (mode == 2 && g_peekActive) {
        // —— 智能浮现：正处于轻浮层 ——
        RaiseForAlert(hwnd);
    } else if (mode == 1) {
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
    UpdateHoverHideTimer(hwnd);   // 「始终置顶」+ 设置开启，或智能浮现的轻浮层时启用
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
        ofs << "SurfaceLead: " << g_surfaceLeadMin << std::endl;
        ofs << "SurfaceEvery: " << g_surfaceEveryMin << std::endl;
    ofs << "ShowHidden: " << (g_showHidden ? 1 : 0) << std::endl;
    ofs << "SinkCompleted: " << (g_sinkCompleted ? 1 : 0) << std::endl;
    ofs << "DueDisplay: " << (g_dueShowAbsolute ? 1 : 0) << std::endl;
    ofs << "DueInputMode: " << g_dueInputMode << std::endl;
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
        else if (line.find("SurfaceLead:") == 0) {
            try { g_surfaceLeadMin = (std::max)(1, std::stoi(line.substr(line.find(":") + 1))); } catch (...) {}
        }
        else if (line.find("SurfaceEvery:") == 0) {
            try { g_surfaceEveryMin = (std::max)(0, std::stoi(line.substr(line.find(":") + 1))); } catch (...) {}
        }
        else if (line.find("HoverHide:") == 0) {
            g_hoverHideEnabled = (std::stoi(line.substr(line.find(":") + 2)) != 0);
        }
        else if (line.find("ShowHidden:") == 0) {
            g_showHidden = (std::atoi(line.substr(line.find(":") + 1).c_str()) != 0);
        }
        else if (line.find("SinkCompleted:") == 0) {
            g_sinkCompleted = (std::atoi(line.substr(line.find(":") + 1).c_str()) != 0);
        }
        else if (line.find("DueDisplay:") == 0) {
            g_dueShowAbsolute = (std::atoi(line.substr(line.find(":") + 1).c_str()) != 0);
        }
        else if (line.find("DueInputMode:") == 0) {
            g_dueInputMode = (std::atoi(line.substr(line.find(":") + 1).c_str()) == 0) ? 0 : 1;
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
                // 行角色：# 大字标题(时钟)  > 灰字次要(日期/页脚)  ## 分组  - 待办  ! 独立倒计时  空行=间隔
                todoFile << "# {{TIME}}\n";
                todoFile << "> {{DATE}}  {{WEEK}}\n";
                todoFile << "## 工作\n";
                todoFile << "- 项目：撰写结题报告\n";
                todoFile << "- 论文：提升论文质量\n";
                todoFile << "## 生活 _FOLD\n";
                todoFile << "- 买菜\n";
                todoFile << "##\n";
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
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_DATE_CLASSES | ICC_UPDOWN_CLASS };
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
