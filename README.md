# DesktopTip

## Overview
DesktopTip is a lightweight Windows application that shows a TODO list or custom notes directly on your desktop. The program reads text from a file (TODO.txt by default) and displays it in a small borderless window. Special placeholders such as `{{DATE}}` or `{{TIME}}` are replaced with the current information when rendered. A tray icon allows you to edit the file, refresh the display, adjust font/spacing and more.

### Features
- Display lines from a user defined text file.
- Supports placeholders: `{{DATE}}`, `{{TIME}}`, `{{WEEK}}`, `{{MONTH}}`, `{{YEAR}}`, `{{DAYS_LEFT_WEEK}}`, `{{DAYS_LEFT_MONTH}}`, `{{DAYS_LEFT_YEAR}}`.
- Tray menu items: **Edit**, **Refresh**, **Change TODO File**, **Change Text Color**, increase/decrease font size and line spacing, and exit.
- Configuration saved to `C:\ProgramData\DesktopTip\config` (window position, text color, font size, line spacing and TODO file path).
- Double clicking the text window or choosing **Edit** opens the TODO file in Notepad.

### Build
1. Open `DesktopTip.sln` with Visual Studio (2019/2022).
2. Build the solution (Win32 desktop application).

### Usage
1. Run the compiled `DesktopTip.exe`.
2. On first launch a default `TODO.txt` will be created with sample placeholders. Edit this file to add your own tasks or notes.
3. Use the tray icon to modify settings or open the TODO file at any time.

## macOS version
A native macOS (AppKit/Swift) port lives in [`macOS/`](macOS). It reads the same `TODO.txt` format and placeholders and offers: click-to-check todos, drag to move, right-click / menu-bar menu, a settings panel (font size, spacing, opacity, colors, font), countdown & alarm reminders, and two display modes (attached to the desktop layer / always on top).

- **Download**: grab `DesktopTip-macOS.zip` from the [Releases](../../releases) page, unzip, and open `DesktopTip.app` (requires macOS 13+; the app is ad-hoc signed, so on first launch right-click → **Open**).
- **Build from source**: `cd macOS && ./build.sh` → `macOS/build/DesktopTip.app` (needs Xcode Command Line Tools).
- Config: `~/Library/Application Support/DesktopTip/config` (same keys as Windows); default TODO file: `~/Library/Application Support/DesktopTip/TODO.txt`.
- Differences: the full-screen black-hole alert is replaced by a bouncing alert card plus sound; click or press any key to stop.

## 中文说明
DesktopTip 是一个简洁的 Windows 桌面便签程序，用于在桌面显示来自文本文件的内容。程序默认读取 `TODO.txt`，并支持在文本中使用占位符（如 `{{DATE}}`、`{{TIME}}` 等），显示时会被替换成实时信息。系统托盘图标提供编辑、刷新、切换文件、修改字体颜色以及调整字号和行距等功能。

### 功能
- 从指定的文本文件读取并显示每一行内容；
- 支持占位符：`{{DATE}}`、`{{TIME}}`、`{{WEEK}}`、`{{MONTH}}`、`{{YEAR}}`、`{{DAYS_LEFT_WEEK}}`、`{{DAYS_LEFT_MONTH}}`、`{{DAYS_LEFT_YEAR}}`；
- 托盘菜单含有 **编辑**、**刷新**、**更换 TODO 文件**、**更改文字颜色**、字号和行距的调节以及 **退出**；
- 配置文件位于 `C:\ProgramData\DesktopTip\config`，保存窗口位置、字体颜色、字号、行距和 TODO 路径；
- 双击窗口或选择 **编辑** 会在记事本中打开 TODO 文件。

### 构建方式
1. 使用 Visual Studio(2019/2022) 打开 `DesktopTip.sln`；
2. 编译生成 Win32 程序。

### 使用方法
1. 运行编译后的 `DesktopTip.exe`；
2. 首次启动会在程序目录生成默认的 `TODO.txt`，可在其中添加自己的待办或笔记；
3. 随时通过托盘图标进入菜单调整设置或编辑文件。

## macOS 版本
[`macOS/`](macOS) 目录提供原生 macOS（Swift/AppKit）移植版，沿用相同的 `TODO.txt` 格式与占位符，支持：点击复选框完成待办、拖动移动、右键/菜单栏菜单、设置面板（字号、行距、透明度、颜色、字体）、倒计时与闹钟提醒，以及两种显示模式（附着桌面层 / 始终置顶）。

- **下载**：在 [Releases](../../releases) 页面下载 `DesktopTip-macOS.zip`，解压后打开 `DesktopTip.app`（需 macOS 13+；应用为临时签名，首次启动请右键 → **打开**）。
- **源码构建**：`cd macOS && ./build.sh`，产物为 `macOS/build/DesktopTip.app`（需安装 Xcode 命令行工具）。
- 配置文件：`~/Library/Application Support/DesktopTip/config`（键名与 Windows 版一致）；默认 TODO 文件在同一目录下的 `TODO.txt`。
- 差异：全屏“黑洞”提醒动画改为弹跳提醒卡片并伴随提示音，点击或按任意键停止。

### 效果预览
![image](https://github.com/user-attachments/assets/ffca57aa-430e-41d0-bb73-e0e94afd1651)
