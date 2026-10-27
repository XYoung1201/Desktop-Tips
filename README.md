<h1 align="center">DesktopTip</h1>

<p align="center"><b>把待办与便签钉在桌面上的轻量工具</b></p>
<p align="center">一个纯文本文件，一张半透明卡片：时钟、日期、待办和倒计时，始终在桌面上。</p>

<p align="center">
  <b>简体中文</b> · <a href="README_EN.md">English</a>
</p>

<p align="center">
  <a href="https://github.com/XYoung1201/Desktop-Tips/releases/latest"><b>下载 macOS 版</b></a> ·
  <a href="#快速上手"><b>快速上手</b></a> ·
  <a href="#todotxt-写法"><b>TODO 写法</b></a> ·
  <a href="https://github.com/XYoung1201/Desktop-Tips/issues"><b>问题反馈</b></a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Windows-10%20%2F%2011-0078D4?logo=windows&logoColor=white" alt="Windows 10 / 11">
  <img src="https://img.shields.io/badge/macOS-13%2B-000000?logo=apple&logoColor=white" alt="macOS 13+">
  <img src="https://img.shields.io/badge/Windows-C%2B%2B%20%2F%20Win32%20%2F%20GDI%2B-00599C?logo=cplusplus&logoColor=white" alt="C++ Win32 GDI+">
  <img src="https://img.shields.io/badge/macOS-Swift%20%2F%20AppKit-F05138?logo=swift&logoColor=white" alt="Swift AppKit">
</p>

<p align="center">
  <img src="docs/screenshots/macos-card.png" alt="DesktopTip 在 macOS 上的卡片：时钟、日期、待办与剩余天数" width="360">
</p>
<p align="center"><sub>macOS 版卡片：大字时钟、灰色日期、可勾选的待办，以及自动计算的剩余天数。</sub></p>

DesktopTip 是一个常驻桌面的便签 / 待办小工具。它读取一个普通文本文件（默认 `TODO.txt`），把内容渲染成一张半透明的圆角卡片，贴在桌面上。你用任何编辑器改文件，卡片就跟着更新；点一下复选框，就把这条待办标为完成并沉到列表底部。

它适合把当天最重要的几件事、倒计时和日期信息放在眼前：没有账号、没有云端、没有数据库，内容就是你自己的一个 `.txt` 文件。Windows 与 macOS 各有一份原生实现，**共用同一种 TODO 文件格式和同一套配置键**，同一个文件可以在两台电脑之间同步使用。

## 目录

- [产品特性](#产品特性)
- [平台与下载](#平台与下载)
- [快速上手](#快速上手)
- [TODO.txt 写法](#todotxt-写法)
- [界面与操作](#界面与操作)
- [倒计时与闹钟](#倒计时与闹钟)
- [配置说明](#配置说明)
- [两个平台的差异](#两个平台的差异)
- [常见问题](#常见问题)
- [从源码构建](#从源码构建)
- [反馈与贡献](#反馈与贡献)

## 产品特性

| 特性 | 可以做什么 | 带来的价值 |
|---|---|---|
| 纯文本驱动 | 用任意编辑器维护 `TODO.txt`，卡片自动刷新；也可随时更换为别的文本文件 | 内容完全在自己手里，可放进网盘或版本管理 |
| 一键勾选 | 点击复选框即在行尾加上 / 去掉 `_OK` 标记，已完成项自动排到后面并显示删除线 | 不必打开文件就能完成待办，文件里仍是可读的纯文本 |
| 动态占位符 | 在文本里写 `{{TIME}}`、`{{DATE}}`、`{{DAYS_LEFT_YEAR}}` 等，渲染时替换为实时信息 | 一张卡片同时是时钟、日历和进度提醒 |
| 半透明卡片 | 圆角、柔和阴影、发丝描边；字号、行距、透明度、文字色、卡片色和字体都可调 | 融入壁纸，不抢眼也看得清 |
| 两种显示模式 | 「附着桌面」留在桌面层，不被「显示桌面」收走；「始终置顶」浮在所有窗口之上 | 按使用习惯选择常驻方式 |
| 倒计时与闹钟 | 1 / 5 / 10 分钟、1 小时或自定义时长，支持重复计时；也可设置闹钟时刻 | 番茄钟、会议提醒、定点提醒一个入口搞定 |
| 全屏提醒 | 时间到后以全屏动画提醒，点击或按键即可停止 | 不会错过，也不会一直打扰 |
| 位置记忆 | 拖动卡片到任意位置，位置与样式写入配置，重启后恢复 | 一次摆放，长期有效 |

## 平台与下载

| 平台 | 系统要求 | 获取方式 |
|---|---|---|
| macOS | macOS 13 及以上，Apple 芯片与 Intel 通用 | 在 [Releases](https://github.com/XYoung1201/Desktop-Tips/releases/latest) 下载 `DesktopTip-macOS.zip`，解压后打开 `DesktopTip.app` |
| Windows | Windows 10 / 11 | 使用 Visual Studio 打开 `DesktopTip.sln` 自行编译，见[从源码构建](#从源码构建) |

macOS 版为临时（ad-hoc）签名。首次启动如被系统拦截，请在访达中**右键 → 打开**，之后即可正常双击运行。程序没有 Dock 图标，菜单栏右上角会出现一个清单图标，右键卡片同样能调出菜单。

## 快速上手

1. 启动 DesktopTip。首次运行会自动生成一份带示例内容的 `TODO.txt`，卡片出现在桌面上。
2. **双击卡片**（或菜单中选择「编辑 TODO」），用默认编辑器打开文件，按[下面的写法](#todotxt-写法)添加自己的内容并保存，卡片会随之刷新。
3. 点击待办前面的方框完成一项；再点一次可恢复。
4. 按住卡片拖动，把它放到合适的位置。
5. 右键卡片打开菜单，选择「设置…」调整字号、行距、透明度、颜色与字体；需要计时就用「倒计时」或「设置闹钟」。

默认文件位置：

| 平台 | TODO 文件 | 配置文件 |
|---|---|---|
| macOS | `~/Library/Application Support/DesktopTip/TODO.txt` | `~/Library/Application Support/DesktopTip/config` |
| Windows | 程序所在目录下的 `TODO.txt` | `C:\ProgramData\DesktopTip\config` |

想使用已有的文件？在菜单中选择「更换 TODO 文件」即可，路径会记入配置。

## TODO.txt 写法

每一行按开头的符号决定它在卡片上的角色：

| 写法 | 角色 | 显示效果 |
|---|---|---|
| `# 文字` | 大字标题 | 大号细体，常用来放时钟 |
| `> 文字` | 次要说明 | 小号灰字，适合日期、页脚 |
| `- 文字`（或 `* 文字`，或直接写文字） | 待办 | 带复选框的一行 |
| 行尾 `_OK` | 已完成标记 | 打勾、淡化并加删除线，排到未完成项之后 |
| 空行 | 间隔 | 留出一段空白 |

待办区域上方的内容与待办之间会自动画一条分隔线。示例：

```text
# {{TIME}}
> {{DATE}}  {{WEEK}}
- 项目：撰写结题报告
- 论文：提升论文质量_OK
> 本周剩 {{DAYS_LEFT_WEEK}} · 本月剩 {{DAYS_LEFT_MONTH}} · 本年剩 {{DAYS_LEFT_YEAR}} 天
```

**可用占位符**（可写在任何行里，渲染时替换）：

| 占位符 | 含义 |
|---|---|
| `{{TIME}}` | 当前时间，`HH:MM` |
| `{{DATE}}` | 当前日期，`YYYY-MM-DD` |
| `{{WEEK}}` | 星期 |
| `{{MONTH}}` | 月份名称 |
| `{{YEAR}}` | 年份 |
| `{{DAYS_LEFT_WEEK}}` | 本周剩余天数（以周日为一周结束） |
| `{{DAYS_LEFT_MONTH}}` | 本月剩余天数 |
| `{{DAYS_LEFT_YEAR}}` | 本年剩余天数 |

未知的占位符会原样保留。文件支持 UTF-8（含 BOM）、UTF-16 以及 GBK 等旧编码的读取；程序写回时统一使用 UTF-8。

## 界面与操作

| 操作 | 效果 |
|---|---|
| 点击待办前的方框 | 切换完成状态，写回文件并重新排序 |
| 按住卡片拖动 | 移动位置，松开后自动记住 |
| 双击卡片 | 用默认编辑器打开 TODO 文件 |
| 右键卡片 | 打开菜单（macOS 也可点菜单栏图标） |

右键菜单：

| 菜单项 | 说明 |
|---|---|
| 设置… | 打开设置面板：字号、行距、卡片透明度、文字色、卡片色、字体，拖动即时生效 |
| 倒计时 | 1 分钟 / 5 分钟 / 10 分钟 / 1 小时 / 自定义，以及「重复计时」开关 |
| 设置闹钟… | 输入 `HH:MM` 或 `HH:MM:SS`，已过的时刻视为次日 |
| 取消倒计时 / 闹钟、停止提醒 | 仅在已设置或提醒中出现，并显示剩余时间 |
| 显示模式 | 附着桌面 / 始终置顶 |
| 编辑 TODO、刷新、更换 TODO 文件 | 文件相关操作；卡片也会定时自动刷新（Windows 每分钟，macOS 每秒检查） |
| 退出 | 保存位置并退出 |

## 倒计时与闹钟

- **倒计时**输入分钟数，或 `MM:SS` / `HH:MM:SS`。开启「重复计时」后，每次到点会自动开始下一轮。
- **闹钟**设置一个具体的时刻；若该时刻今天已过，则顺延到明天。
- 到点时会出现全屏提醒：macOS 上是一张弹跳并不断变换字体的提醒卡片，伴随提示音；Windows 上是黑洞透镜动画。点击或按任意键即可停止，也可在菜单中选择「停止提醒」。
- 同一时间只有一个倒计时 / 闹钟生效；重新设置会替换旧的。

## 配置说明

配置文件是一行一项的 `键: 值` 文本，程序在调整位置、样式时自动写入，也可以手工编辑后重启生效。

| 键 | 含义 | 默认值 |
|---|---|---|
| `Window_X` / `Window_Y` | 卡片左上角位置 | 首次启动时自动 |
| `TODO_path` | TODO 文件路径 | 见[快速上手](#快速上手) |
| `FontHeight` | 正文字号（像素） | `24` |
| `LineHeight` | 行距参数，大于字号的部分作为额外行间距 | `36` |
| `TextColor_R/G/B` | 文字颜色 | `244, 245, 248` |
| `BgColor_R/G/B` | 卡片颜色 | `20, 25, 35` |
| `BgAlpha` | 卡片不透明度，0–255 | `128` |
| `FontFamily` | 字体名称 | macOS：`PingFang SC`；Windows：`Microsoft YaHei UI` |
| `DisplayMode` | `0` 附着桌面，`1` 始终置顶 | `0` |
| `CountdownRepeat` | 倒计时是否重复，`0` / `1` | `0` |

两个平台使用相同的键名。`Window_X/Y` 的坐标含义依赖各自系统的屏幕坐标，不建议在两台电脑间直接复制配置文件；复制 TODO 文件则没有问题。

## 两个平台的差异

| 项目 | Windows | macOS |
|---|---|---|
| 实现 | C++ / Win32 / GDI+ | Swift / AppKit |
| 附着桌面 | 贴在壁纸层，`Win+D` 不会将其最小化 | 位于桌面图标层之上、普通窗口之下，所有桌面空间可见 |
| 常驻入口 | 系统托盘图标 | 菜单栏图标 |
| 编辑文件 | 记事本 | 系统默认的文本编辑器 |
| 全屏提醒 | 黑洞引力透镜动画 | 弹跳提醒卡片 + 提示音 |
| 字体选择 | 菜单中的字体对话框 | 设置面板中的字体下拉列表 |

## 常见问题

**卡片不见了 / 跑到屏幕外了？**
程序启动和显示器布局变化时会把卡片自动拉回可见区域。仍然看不到时，检查「显示模式」：「附着桌面」的卡片会被普通窗口盖住，移开窗口或切换到「始终置顶」即可。

**勾选后文件里发生了什么？**
只会在对应行末增加或去掉 `_OK`，并把已完成的待办排到未完成之后，其他行（标题、说明、空行）原样保留。

**可以多台电脑共用一个 TODO 吗？**
可以。把 `TODO.txt` 放进网盘或同步目录，各台电脑的「更换 TODO 文件」指向它即可；同一份文件在 Windows 和 macOS 上显示一致。

**时间不会自动走吗？**
会。Windows 版每分钟重新渲染一次，macOS 版每秒检查一次，`{{TIME}}` 等占位符会随之更新；也可以随时在菜单中选择「刷新」。

**macOS 提示“无法验证开发者”？**
这是临时签名的正常现象：在访达中右键 `DesktopTip.app` 选择「打开」，确认一次即可。

## 从源码构建

### macOS

需要安装 Xcode 命令行工具（`xcode-select --install`）：

```bash
cd macOS
./build.sh
open build/DesktopTip.app
```

脚本会编译 Apple 芯片与 Intel 两个架构（Intel 工具链不可用时只生成当前架构），生成图标并做临时签名，产物位于 `macOS/build/DesktopTip.app`。源码是单文件 [`macOS/main.swift`](macOS/main.swift)。

### Windows

1. 使用 Visual Studio 2019 / 2022 打开 `DesktopTip.sln`；
2. 选择 x64 / Release 配置编译，生成 `DesktopTip.exe`；
3. 运行即可。首次启动会在程序目录创建默认的 `TODO.txt`。

## 反馈与贡献

欢迎在 [Issues](https://github.com/XYoung1201/Desktop-Tips/issues) 提交问题与建议，也欢迎直接发起 Pull Request。提交问题时请注明平台与系统版本，并尽量附上相关的 TODO 片段或配置内容。
