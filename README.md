# DesktopTip

## Overview (English)
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

## 中文说明 (Chinese)
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
