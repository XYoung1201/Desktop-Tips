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
#include <regex>
#include <unordered_map>

#include <shlobj.h>
#include <filesystem>

using namespace std;
namespace fs = std::filesystem;

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

#define _CRT_SECURE_NO_WARNINGS

int lineHeight = 30; // 假设每行高度固定
int fontHeight = 10; // 行间距（额外的空白高度）

std::string TODO_PATH = "";
HWND hWnd;

static HFONT g_hFont = NULL;  // Global font handle
static COLORREF g_textColor = RGB(255,255,255);

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

HWND g_hWorkerW = NULL;

using namespace std::chrono;

enum R_DAYS {DLW,DLM,DLY};

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

// 右键菜单的消息处理
void ShowContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, ID_TRAY_EDIT, "Edit");
    AppendMenu(hMenu, MF_STRING, ID_TRAY_REFRESH, "Refresh");
    AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, "Exit");
    AppendMenu(hMenu, MF_STRING, ID_TODO_CHANGE, "Change TODO File");
    AppendMenu(hMenu, MF_STRING, ID_TEXT_COLOR, "Change Text Color...");
    AppendMenu(hMenu, MF_STRING, ID_FONTSIZE_INCREASE, "Font Size +");
    AppendMenu(hMenu, MF_STRING, ID_FONTSIZE_DECREASE, "Font Size -");
    AppendMenu(hMenu, MF_STRING, ID_SPACING_INCREASE, "Spacing Size +");
    AppendMenu(hMenu, MF_STRING, ID_SPACING_DECREASE, "Spacing Size -");
  
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
        InvalidateRect(hwnd, nullptr, TRUE);
    }
}

// 辅助函数：从文件中读取内容并按行存储到 vector
std::vector<std::string> ReadLinesFromFile() {
    std::vector<std::string> lines;
    const std::string TODO_PATH_ = TODO_PATH;
    std::ifstream file(TODO_PATH_); // 使用宽字符文件流
    file.imbue(std::locale(""));  // 设置本地编码（适配 UTF-8 或系统默认编码）

    if (!file.is_open()) {
        lines.push_back("Error: Unable to open file!");
        return lines;
    }

    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }

    file.close();
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
}

// 简单的窗口过程，用于绘制文本
std::string OpenFileDialog();

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
        SetLayeredWindowAttributes(hWnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
        SetTimer(hWnd, 1, 60000, nullptr);
        return 0;
    }
    case WM_NCHITTEST:
    {
        LRESULT hit = DefWindowProc(hWnd, message, wParam, lParam);
        if (hit == HTCLIENT)
        {
            return HTCAPTION;
        }
        return hit;
    }
    case WM_MOVE:
    {
        confWrite();
        break;
    }
    case WM_PAINT:
    {
        g_hFont = CreateFont(
            fontHeight,                // Height
            0,                     // Width
            0,                     // Escapement
            0,                     // Orientation
            1000,             // Weight
            FALSE,                 // Italic
            FALSE,                 // Underline
            FALSE,                 // StrikeOut
            CP_UTF8,       // CharSet
            OUT_TT_PRECIS,    // OutputPrecision
            CLIP_CHARACTER_PRECIS,   // ClipPrecision
            NONANTIALIASED_QUALITY,
            DEFAULT_PITCH | FF_SWISS, // PitchAndFamily
            _T("宋体")            // FaceName
        );

        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        // Use the custom font
        HFONT hOldFont = (HFONT)SelectObject(hdc, g_hFont);

        // Transparent background for text
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, g_textColor);

        std::vector<std::string> lines = ReadLinesFromFile();

        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH)); // 使用窗口背景色填充

        int yOffset = 0;
        RECT lineRect1 = { rc.left, rc.top + yOffset, rc.right, rc.top + yOffset + lineHeight };
        DrawText(hdc, "■■", -1, &lineRect1, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        yOffset += lineHeight;
        for (const auto& rawline : lines) {
			std::string line = ProcessPlaceholders(rawline); // 处理占位符
            RECT lineRect = { rc.left, rc.top + yOffset, rc.right, rc.top + yOffset + lineHeight };
            DrawText(hdc, line.c_str(), -1, &lineRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            yOffset += lineHeight; // 增加偏移，绘制下一行
        }

        SelectObject(hdc, hOldFont);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        if (g_hFont) { DeleteObject(g_hFont); g_hFont = NULL; }
        KillTimer(hWnd, 1);
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
        if (LOWORD(wParam) == ID_FONTSIZE_INCREASE) {
            fontHeight+=5;
            confWrite();  // 保存配置
            InvalidateRect(hWnd, nullptr, TRUE); // 清除整个窗口并重绘
        }
        if (LOWORD(wParam) == ID_FONTSIZE_DECREASE) {
            fontHeight-= 5;
            confWrite();  // 保存配置
            if (fontHeight < 0)
                fontHeight = 0;
            InvalidateRect(hWnd, nullptr, TRUE); // 清除整个窗口并重绘
        }
        if (LOWORD(wParam) == ID_SPACING_INCREASE) {
            lineHeight+= 5;
            confWrite();  // 保存配置
            InvalidateRect(hWnd, nullptr, TRUE); // 清除整个窗口并重绘
        }
        if (LOWORD(wParam) == ID_SPACING_DECREASE) {
            lineHeight-= 5;
            confWrite();  // 保存配置
            if (lineHeight < 0)
                lineHeight = 0;
            InvalidateRect(hWnd, nullptr, TRUE); // 清除整个窗口并重绘
        }
        if (LOWORD(wParam) == ID_TEXT_COLOR) {
            ChooseTextColor(hWnd);
            InvalidateRect(hWnd, nullptr, TRUE); // 清除整个窗口并重绘
        }
        if (LOWORD(wParam) == ID_TODO_CHANGE) {
            TODO_PATH = OpenFileDialog();
            confWrite();
            InvalidateRect(hWnd, nullptr, TRUE); // 清除整个窗口并重绘
        }
        if (LOWORD(wParam) == ID_TRAY_REFRESH) {
            // 强制刷新窗口内容
            InvalidateRect(hWnd, nullptr, TRUE); // 清除整个窗口并重绘
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
                InvalidateRect(hWnd, nullptr, TRUE); // 清除整个窗口并重绘
            }
        }
        break;
    case WM_LBUTTONDBLCLK:
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
            InvalidateRect(hWnd, nullptr, TRUE); // 清除整个窗口并重绘
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
            InvalidateRect(hWnd, nullptr, TRUE); // 清除整个窗口并重绘
        }
        break;
    }
    case WM_TIMER:
        if (wParam == 1) {
            InvalidateRect(hWnd, nullptr, TRUE);
        }
        break;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

// 回调函数，用于枚举 Program Manager 窗口下的子窗口
BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lParam) {
    char className[256];
    GetClassNameA(hwnd, className, sizeof(className));

    // 检查类名是否为 "WorkerW"
    if (std::string(className) == "WorkerW") {
        *(HWND*)lParam = hwnd; // 保存句柄
        return FALSE;          // 停止枚举
    }
    return TRUE;               // 继续枚举
}

void FindWorkerW() {
    // 查找标题为 "Program Manager" 的顶层窗口
    HWND programManager = FindWindowA("Progman", "Program Manager");
    if (programManager)
    {
        // 发送一个假消息以促使 WorkerW 出现
        SendMessageTimeout(programManager, 0x052C, 0, 0, SMTO_NORMAL, 1000, NULL);
        EnumChildWindows(programManager, EnumChildProc, (LPARAM)&g_hWorkerW);
    }
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
    }
}

void readFromFile(int& x, int& y) {
    std::ifstream ifs(CONFIG_PATH);
    std::string line;

    // 默认颜色分量值（如果配置文件中没有）
    int r = GetRValue(g_textColor);
    int g = GetGValue(g_textColor);
    int b = GetBValue(g_textColor);

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
    }

    // 重建颜色值
    g_textColor = RGB(r, g, b);

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
                todoFile << "欢迎使用 DesktopTip！\n";
                todoFile << "你可以在此文件中添加你的待办事项。\n";
                todoFile << "支持占位符：{{DATE}} {{TIME}} {{WEEK}} {{MONTH}} {{YEAR}}等\n";
                todoFile << "示例：今天是 {{DATE}}\n";
                todoFile << "      本周还剩 {{DAYS_LEFT_WEEK}} 天；\n";
                todoFile << "      本月还剩{{DAYS_LEFT_MONTH}}天；\n";
                todoFile << "      本年还剩{{DAYS_LEFT_YEAR}}天。\n";
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
    confCreate();
    //readFromFile(xx, yy, path);

    // 注册窗口类
    WNDCLASS wc = { 0 };
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

    FindWorkerW();
    
    HWND parent = g_hWorkerW ? g_hWorkerW : GetDesktopWindow();
    hWnd = CreateWindowEx(
        dwExStyle,
        wc.lpszClassName,
        NULL,
        dwStyle,
        x, y, width, height,
        parent,
        NULL,
        hInstance,
        NULL);

    if (!hWnd)
        return 0;

    AddTrayIcon(hWnd);

    SetLayeredWindowAttributes(hWnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

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
