// DesktopTip for macOS — native AppKit port of the Win32 version.
// Same TODO.txt format ("# 标题", "> 次要", "- 待办", "_OK" 完成标记, {{DATE}} 等占位符)
// and the same config keys, stored in ~/Library/Application Support/DesktopTip/config.
import Cocoa

// MARK: - Paths & config

let supportDir = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
    .appendingPathComponent("DesktopTip")
let configURL = supportDir.appendingPathComponent("config")

struct Config {
    var x = 0, y = 0
    var hasPosition = false
    var todoPath = ""
    var fontHeight = 24
    var lineHeight = 36
    var text = (r: 244, g: 245, b: 248)
    var bg = (r: 20, g: 25, b: 35)
    var bgAlpha = 128
    var fontFamily = "PingFang SC"
    var displayMode = 0          // 0 = 附着桌面, 1 = 始终置顶
    var countdownRepeat = false
    var hoverHide = true         // 始终置顶时鼠标悬停隐藏（按住 ⌘ 可操作）
    var hoverHideDelay = 1000    // 悬停多久后才隐藏（毫秒）；隐藏后鼠标离开卡片即恢复

    var textColor: NSColor { NSColor(srgbRed: CGFloat(text.r) / 255, green: CGFloat(text.g) / 255, blue: CGFloat(text.b) / 255, alpha: 1) }
    var bgColor: NSColor { NSColor(srgbRed: CGFloat(bg.r) / 255, green: CGFloat(bg.g) / 255, blue: CGFloat(bg.b) / 255, alpha: 1) }

    static func load() -> Config {
        var c = Config()
        c.todoPath = supportDir.appendingPathComponent("TODO.txt").path
        guard let s = try? String(contentsOf: configURL, encoding: .utf8) else { return c }
        for line in s.split(whereSeparator: \.isNewline) {
            guard let i = line.firstIndex(of: ":") else { continue }
            let key = line[..<i].trimmingCharacters(in: .whitespaces)
            let val = line[line.index(after: i)...].trimmingCharacters(in: .whitespaces)
            let n = Int(val) ?? 0
            switch key {
            case "Window_X": c.x = n; c.hasPosition = true
            case "Window_Y": c.y = n
            case "TODO_path": if !val.isEmpty, !val.contains("\\") { c.todoPath = val }
            case "FontHeight": c.fontHeight = n
            case "LineHeight": c.lineHeight = n
            case "TextColor_R": c.text.r = n
            case "TextColor_G": c.text.g = n
            case "TextColor_B": c.text.b = n
            case "BgAlpha": c.bgAlpha = max(0, min(255, n))
            case "BgColor_R": c.bg.r = n
            case "BgColor_G": c.bg.g = n
            case "BgColor_B": c.bg.b = n
            case "FontFamily": if !val.isEmpty, val != "Microsoft YaHei UI" { c.fontFamily = val }
            case "DisplayMode": c.displayMode = n
            case "CountdownRepeat": c.countdownRepeat = n != 0
            case "HoverHide": c.hoverHide = n != 0
            case "HoverHideDelay": c.hoverHideDelay = max(0, min(60000, n))
            default: break
            }
        }
        return c
    }

    func save(origin: NSPoint?) {
        var out = ""
        let ox = origin.map { Int($0.x) } ?? x
        let oy = origin.map { Int($0.y) } ?? y
        out += "Window_X: \(ox)\nWindow_Y: \(oy)\nTODO_path: \(todoPath)\n"
        out += "FontHeight: \(fontHeight)\nLineHeight: \(lineHeight)\n"
        out += "TextColor_R: \(text.r)\nTextColor_G: \(text.g)\nTextColor_B: \(text.b)\n"
        out += "BgAlpha: \(bgAlpha)\nBgColor_R: \(bg.r)\nBgColor_G: \(bg.g)\nBgColor_B: \(bg.b)\n"
        out += "FontFamily: \(fontFamily)\nDisplayMode: \(displayMode)\nCountdownRepeat: \(countdownRepeat ? 1 : 0)\n"
        out += "HoverHide: \(hoverHide ? 1 : 0)\nHoverHideDelay: \(hoverHideDelay)\n"
        try? FileManager.default.createDirectory(at: supportDir, withIntermediateDirectories: true)
        try? out.write(to: configURL, atomically: true, encoding: .utf8)
    }
}

var cfg = Config.load()

// MARK: - TODO file

func decodeText(_ data: Data) -> String {
    if data.count >= 3, data[0] == 0xEF, data[1] == 0xBB, data[2] == 0xBF {
        return String(decoding: data.dropFirst(3), as: UTF8.self)
    }
    if data.count >= 2, data[0] == 0xFF, data[1] == 0xFE { return String(data: data, encoding: .utf16LittleEndian) ?? "" }
    if data.count >= 2, data[0] == 0xFE, data[1] == 0xFF { return String(data: data, encoding: .utf16BigEndian) ?? "" }
    if let s = String(data: data, encoding: .utf8) { return s }
    let gb = CFStringConvertEncodingToNSStringEncoding(CFStringEncoding(CFStringEncodings.GB_18030_2000.rawValue))
    return String(data: data, encoding: String.Encoding(rawValue: gb)) ?? String(decoding: data, as: UTF8.self)
}

func splitLines(_ s: String) -> [String] {
    s.replacingOccurrences(of: "\r\n", with: "\n").replacingOccurrences(of: "\r", with: "\n")
        .components(separatedBy: "\n")
}

func readTodoLines() -> [String]? {
    guard let d = FileManager.default.contents(atPath: cfg.todoPath) else { return nil }
    return splitLines(decodeText(d))
}

func writeTodoLines(_ lines: [String]) -> Bool {
    (try? lines.joined(separator: "\n").write(toFile: cfg.todoPath, atomically: true, encoding: .utf8)) != nil
}

func trimRight(_ s: String) -> String {
    var s = s
    while let c = s.last, c == " " || c == "\t" || c == "\r" { s.removeLast() }
    return s
}

/// Returns (text, completed) for a todo line, nil for heading/meta/blank lines.
func parseTodo(_ line: String) -> (text: String, done: Bool)? {
    let t = String(trimRight(line).drop(while: { $0 == " " || $0 == "\t" }))
    if t.isEmpty || t.first == "#" || t.first == ">" { return nil }
    var text = t
    if (t.first == "-" || t.first == "*"), t.count > 1, t[t.index(after: t.startIndex)] == " " { text = String(t.dropFirst(2)) }
    var done = false
    let r = trimRight(text)
    if r.hasSuffix("_OK") {
        done = true
        text = trimRight(String(r.dropLast(3)))
    }
    return (text, done)
}

func stripOK(_ line: String) -> String {
    let r = trimRight(line)
    return r.hasSuffix("_OK") ? trimRight(String(r.dropLast(3))) : r
}

func toggleTodo(at index: Int) -> Bool {
    guard var lines = readTodoLines(), index >= 0, index < lines.count, let st = parseTodo(lines[index]) else { return false }
    lines[index] = st.done ? stripOK(lines[index]) : stripOK(lines[index]) + "_OK"
    // 已完成的沉到未完成之后（占用原有待办行位置）
    var slots: [Int] = [], open: [String] = [], done: [String] = []
    for (i, l) in lines.enumerated() {
        guard let s = parseTodo(l) else { continue }
        slots.append(i)
        if s.done { done.append(l) } else { open.append(l) }
    }
    for (n, l) in (open + done).enumerated() { lines[slots[n]] = l }
    return writeTodoLines(lines)
}

func daysLeft() -> (week: Int, month: Int, year: Int) {
    let cal = Calendar.current, now = Date()
    let wd = cal.component(.weekday, from: now) - 1            // Sunday = 0
    let dayRange = cal.range(of: .day, in: .month, for: now)!
    let day = cal.component(.day, from: now)
    let yday = cal.ordinality(of: .day, in: .year, for: now)!
    let ydays = cal.range(of: .day, in: .year, for: now)!.count
    return ((7 - wd) % 7, dayRange.count - day, ydays - yday)
}

func processPlaceholders(_ s: String) -> String {
    guard s.contains("{{") else { return s }
    let now = Date()
    func fmt(_ f: String) -> String {
        let d = DateFormatter(); d.locale = Locale.current; d.dateFormat = f; return d.string(from: now)
    }
    let dl = daysLeft()
    let map: [String: String] = [
        "DATE": fmt("yyyy-MM-dd"), "TIME": fmt("HH:mm"), "WEEK": fmt("EEEE"), "MONTH": fmt("MMMM"), "YEAR": fmt("yyyy"),
        "DAYS_LEFT_WEEK": "\(dl.week)", "DAYS_LEFT_MONTH": "\(dl.month)", "DAYS_LEFT_YEAR": "\(dl.year)",
    ]
    let re = try! NSRegularExpression(pattern: "\\{\\{([A-Z_]+)\\}\\}")
    var result = s
    for m in re.matches(in: s, range: NSRange(s.startIndex..., in: s)).reversed() {
        let key = (s as NSString).substring(with: m.range(at: 1))
        if let v = map[key], let r = Range(m.range, in: result) { result.replaceSubrange(r, with: v) }
    }
    return result
}

func ensureDefaultTodo() {
    try? FileManager.default.createDirectory(at: supportDir, withIntermediateDirectories: true)
    if !FileManager.default.fileExists(atPath: cfg.todoPath) {
        let sample = "# {{TIME}}\n> {{DATE}}  {{WEEK}}\n- 项目：撰写结题报告\n- 论文：提升论文质量\n> 本周剩 {{DAYS_LEFT_WEEK}} · 本月剩 {{DAYS_LEFT_MONTH}} · 本年剩 {{DAYS_LEFT_YEAR}} 天\n"
        try? sample.write(toFile: cfg.todoPath, atomically: true, encoding: .utf8)
    }
}

// MARK: - Per-task timers（每项任务的倒计时 / 正计时）
// 保存在 ~/Library/Application Support/DesktopTip/timers，与 Windows 版格式相同：
// 每行 kind\tduration\tstart\taccum\tfired\t任务文本；kind = down(倒计时) / up(正计时)，
// start 为本次开始计时的 Unix 秒（0 = 暂停中），accum 为此前累计的秒数。

let timersURL = supportDir.appendingPathComponent("timers")

func nowEpoch() -> Int { Int(Date().timeIntervalSince1970) }

struct TaskTimer {
    var countdown = true
    var duration = 0
    var start = 0
    var accum = 0
    var fired = false
    var key = ""
    var elapsed: Int { max(0, accum + (start > 0 ? nowEpoch() - start : 0)) }
    var running: Bool { start > 0 }
}

func loadTaskTimers() -> [TaskTimer] {
    guard let d = FileManager.default.contents(atPath: timersURL.path) else { return [] }
    var out: [TaskTimer] = []
    for line in splitLines(decodeText(d)) {
        let p = line.components(separatedBy: "\t")
        guard p.count >= 6, let du = Int(p[1]), let st = Int(p[2]), let ac = Int(p[3]) else { continue }
        let key = p[5...].joined(separator: "\t")
        if key.isEmpty { continue }
        out.append(TaskTimer(countdown: p[0] == "down", duration: du, start: st, accum: ac, fired: p[4] == "1", key: key))
    }
    return out
}

var taskTimers: [TaskTimer] = loadTaskTimers()

func saveTaskTimers() {
    let s = taskTimers.map { "\($0.countdown ? "down" : "up")\t\($0.duration)\t\($0.start)\t\($0.accum)\t\($0.fired ? 1 : 0)\t\($0.key)\n" }.joined()
    try? FileManager.default.createDirectory(at: supportDir, withIntermediateDirectories: true)
    try? s.write(to: timersURL, atomically: true, encoding: .utf8)
}

func timerIndex(_ key: String) -> Int? { taskTimers.firstIndex { $0.key == key } }
func timer(for key: String) -> TaskTimer? { timerIndex(key).map { taskTimers[$0] } }

func ensureTimer(_ key: String) -> Int {
    if let i = timerIndex(key) { return i }
    taskTimers.append(TaskTimer(key: key))
    return taskTimers.count - 1
}

func startTaskCountdown(_ key: String, _ seconds: Int) {
    let i = ensureTimer(key)
    taskTimers[i].countdown = true; taskTimers[i].duration = seconds
    taskTimers[i].accum = 0; taskTimers[i].start = nowEpoch(); taskTimers[i].fired = false
    saveTaskTimers()
}

func startTaskStopwatch(_ key: String) {
    let i = ensureTimer(key)
    taskTimers[i].countdown = false; taskTimers[i].duration = 0
    taskTimers[i].accum = 0; taskTimers[i].start = nowEpoch(); taskTimers[i].fired = false
    saveTaskTimers()
}

func pauseTaskTimer(at i: Int) {
    if taskTimers[i].start > 0 { taskTimers[i].accum = taskTimers[i].elapsed; taskTimers[i].start = 0 }
}

func toggleTaskTimerPause(_ key: String) {
    guard let i = timerIndex(key) else { return }
    if taskTimers[i].running { pauseTaskTimer(at: i) } else { taskTimers[i].start = nowEpoch() }
    saveTaskTimers()
}

func restartTaskTimer(_ key: String) {
    guard let i = timerIndex(key) else { return }
    taskTimers[i].accum = 0; taskTimers[i].start = nowEpoch(); taskTimers[i].fired = false
    saveTaskTimers()
}

func removeTaskTimer(_ key: String) {
    taskTimers.removeAll { $0.key == key }
    saveTaskTimers()
}

/// 「再等 N 分钟」：倒计时延长到“从现在起还剩 N 秒”，并继续计时
func snoozeTaskTimer(_ key: String, _ seconds: Int) {
    guard let i = timerIndex(key) else { return }
    let el = taskTimers[i].elapsed
    taskTimers[i].countdown = true; taskTimers[i].duration = el + seconds
    if taskTimers[i].start == 0 { taskTimers[i].start = nowEpoch() }
    taskTimers[i].fired = false
    saveTaskTimers()
}

func formatClock(_ s: Int) -> String {
    let s = max(0, s), h = s / 3600, m = (s % 3600) / 60, sec = s % 60
    return h > 0 ? String(format: "%d:%02d:%02d", h, m, sec) : String(format: "%02d:%02d", m, sec)
}

func formatShortDuration(_ s: Int) -> String {
    if s < 60 { return "\(s) 秒" }
    if s % 60 != 0 { return "\(s / 60) 分 \(s % 60) 秒" }
    let m = s / 60
    if m < 60 { return "\(m) 分钟" }
    return m % 60 == 0 ? "\(m / 60) 小时" : "\(m / 60) 小时 \(m % 60) 分钟"
}

/// 分钟数，或 MM:SS / HH:MM:SS
func parseDurationInput(_ v: String) -> Int? {
    let parts = v.replacingOccurrences(of: "：", with: ":").split(separator: ":", omittingEmptySubsequences: false)
        .map { Int($0.trimmingCharacters(in: .whitespaces)) }
    guard parts.allSatisfy({ $0 != nil && $0! >= 0 }) else { return nil }
    let p = parts.map { $0! }
    var secs: Int?
    switch p.count {
    case 1: secs = p[0] * 60
    case 2: secs = p[0] * 60 + p[1]
    case 3: secs = p[0] * 3600 + p[1] * 60 + p[2]
    default: break
    }
    if let s = secs, s > 0 { return s }
    return nil
}

func todoKey(_ line: String) -> (key: String, done: Bool)? {
    parseTodo(line).map { ($0.text.trimmingCharacters(in: .whitespaces), $0.done) }
}

/// 按任务文本切换完成状态（hint 只是行号提示，文件被改过也能找到正确的行）
func toggleTodo(key: String, hint: Int) -> Bool {
    guard let lines = readTodoLines() else { return false }
    var idx = -1
    if hint >= 0, hint < lines.count, todoKey(lines[hint])?.key == key { idx = hint }
    else if let i = lines.firstIndex(where: { todoKey($0)?.key == key }) { idx = i }
    return idx >= 0 && toggleTodo(at: idx)
}

// MARK: - Animation / alert state

let completeAnimSec = 0.70      // 完成动画
let settleAnimSec = 0.32        // 已完成项沉底的滑动
let shakeSec = 0.75             // 单次抖动
let shakeRepeatSec = 6.0        // 未处理时每 6 秒再抖一次
let shakeMaxTimes = 6

final class UIState {
    var alertActive = false
    var alertTitle = "", alertDetail = "", alertTaskKey = ""
    var alertStart = Date()
    var shaking = false
    var shakeStart = Date()
    var shakeTimes = 0
    var animKey: String?
    var animHint = -1
    var animStart = Date()
    var settling = false
    var settleStart = Date()
    var settleFrom: [String: CGFloat] = [:]
    var lastRowY: [String: CGFloat] = [:]
    var justCompletedKey = ""
}
let ui = UIState()

enum BannerAction { case done, snooze, dismiss }

func clamp01(_ v: Double) -> Double { min(1, max(0, v)) }
func easeOutCubic(_ t: Double) -> Double { let u = 1 - clamp01(t); return 1 - u * u * u }
func easeInOutCubic(_ t: Double) -> Double { let t = clamp01(t); return t < 0.5 ? 4 * t * t * t : 1 - pow(-2 * t + 2, 3) / 2 }
func easeOutBack(_ t: Double) -> Double {
    let t = clamp01(t), c1 = 1.70158, c3 = c1 + 1
    return 1 + c3 * pow(t - 1, 3) + c1 * pow(t - 1, 2)
}
func rgb(_ r: CGFloat, _ g: CGFloat, _ b: CGFloat, _ a: CGFloat = 1) -> NSColor {
    NSColor(srgbRed: r / 255, green: g / 255, blue: b / 255, alpha: a)
}
func lerpColor(_ a: NSColor, _ b: NSColor, _ t: Double) -> NSColor {
    let t = CGFloat(clamp01(t))
    guard let x = a.usingColorSpace(.sRGB), let y = b.usingColorSpace(.sRGB) else { return b }
    return NSColor(srgbRed: x.redComponent + (y.redComponent - x.redComponent) * t,
                   green: x.greenComponent + (y.greenComponent - x.greenComponent) * t,
                   blue: x.blueComponent + (y.blueComponent - x.blueComponent) * t,
                   alpha: x.alphaComponent + (y.alphaComponent - x.alphaComponent) * t)
}

let accentDone = rgb(52, 211, 153)    // 完成：翡翠绿
let accentGold = rgb(253, 224, 71)
let coral = rgb(255, 107, 107)        // 到点：珊瑚红
let amber = rgb(251, 191, 36)         // 快到点：琥珀
let sky = rgb(125, 211, 252)          // 倒计时：天蓝
let violet = rgb(196, 181, 253)       // 正计时：淡紫

// MARK: - Fonts

func makeFont(_ family: String, _ size: CGFloat, light: Bool = false) -> NSFont {
    let fm = NSFontManager.shared
    if let f = fm.font(withFamily: family, traits: [], weight: light ? 3 : 5, size: size) { return f }
    return NSFont.systemFont(ofSize: size, weight: light ? .light : .regular)
}

func makeBoldFont(_ family: String, _ size: CGFloat) -> NSFont {
    if let f = NSFontManager.shared.font(withFamily: family, traits: .boldFontMask, weight: 9, size: size) { return f }
    return NSFont.systemFont(ofSize: size, weight: .semibold)
}

/// 对勾（按路径长度比例 t 逐步描绘）；box 为复选框在翻转坐标中的原点
func drawTick(_ x: CGFloat, _ y: CGFloat, _ d: CGFloat, _ t: Double, _ color: NSColor, _ width: CGFloat) {
    guard t > 0 else { return }
    let p0 = NSPoint(x: x + d * 0.23, y: y + d * 0.53), p1 = NSPoint(x: x + d * 0.43, y: y + d * 0.72)
    let p2 = NSPoint(x: x + d * 0.78, y: y + d * 0.28)
    let l1 = hypot(p1.x - p0.x, p1.y - p0.y), l2 = hypot(p2.x - p1.x, p2.y - p1.y)
    let len = (l1 + l2) * CGFloat(clamp01(t))
    let path = NSBezierPath()
    path.lineWidth = width; path.lineCapStyle = .round; path.lineJoinStyle = .round
    path.move(to: p0)
    if len <= l1 {
        let k = len / l1
        path.line(to: NSPoint(x: p0.x + (p1.x - p0.x) * k, y: p0.y + (p1.y - p0.y) * k))
    } else {
        let k = (len - l1) / l2
        path.line(to: p1)
        path.line(to: NSPoint(x: p1.x + (p2.x - p1.x) * k, y: p1.y + (p2.y - p1.y) * k))
    }
    color.setStroke(); path.stroke()
}

// MARK: - Card view

enum RowType { case spacer, heading, meta, todo }
struct Row {
    var type: RowType
    var text = ""
    var w: CGFloat = 0, h: CGFloat = 0, y: CGFloat = 0
    var tw: CGFloat = 0          // 纯文字宽度（删除线动画用）
    var source = -1
    var done = false
    var key = ""                 // 待办原始文本（计时器的键）
}

struct HitRect { var rect: NSRect; var source: Int; var key: String; var done: Bool }

final class CardView: NSView {
    static let margin: (CGFloat) -> CGFloat = { $0 * 2.1 }
    var rows: [Row] = []
    var dividerY: CGFloat = -1
    var cardSize = NSSize(width: 100, height: 60)
    var hitRects: [HitRect] = []          // 复选框
    var rowRects: [HitRect] = []          // 整行（右键设置计时）
    var pillRects: [HitRect] = []         // 计时胶囊（单击暂停/继续）
    var bannerButtons: [(rect: NSRect, action: BannerAction)] = []
    var lastSignature = ""
    weak var controller: AppController?

    // 布局结果（计时胶囊列 / 提醒横幅）
    var pillColW: CGFloat = 0
    var bannerH: CGFloat = 0, bannerY: CGFloat = 0, bHeadBlock: CGFloat = 0, bTitleH: CGFloat = 0, bDetailH: CGFloat = 0
    var btnSpec: [(action: BannerAction, title: String)] = []
    var btnW: [CGFloat] = []

    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { false }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    var base: CGFloat { CGFloat(cfg.fontHeight > 8 ? cfg.fontHeight : 24) }
    var M: CGFloat { CardView.margin(base) }

    // 尺寸常量（随字号缩放）
    var pillH: CGFloat { base * 1.08 }
    var ringD: CGFloat { pillH * 0.56 }
    var pillPadX: CGFloat { base * 0.40 }
    var pillInnerGap: CGFloat { base * 0.30 }
    var pillGap: CGFloat { base * 0.75 }
    var bPad: CGFloat { base * 0.62 }
    var iconD: CGFloat { base * 1.5 }
    var btnH: CGFloat { base * 1.25 }
    var btnPadX: CGFloat { base * 0.72 }
    var btnGap: CGFloat { base * 0.36 }
    var timerFont: NSFont { NSFont.monospacedDigitSystemFont(ofSize: base * 0.66, weight: .regular) }
    var bTitleFont: NSFont { makeBoldFont(cfg.fontFamily, base * 0.88) }
    var bDetailFont: NSFont { makeFont(cfg.fontFamily, base * 0.64) }
    var btnFont: NSFont { makeBoldFont(cfg.fontFamily, base * 0.58) }

    func attrs(_ font: NSFont, color: NSColor, strike: Bool = false) -> [NSAttributedString.Key: Any] {
        var a: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: color]
        if strike { a[.strikethroughStyle] = NSUnderlineStyle.single.rawValue }
        return a
    }

    func textSize(_ s: String, _ f: NSFont) -> NSSize { NSAttributedString(string: s, attributes: [.font: f]).size() }

    /// Rebuilds rows and layout; returns the full window size.
    func relayout() -> NSSize {
        let base = self.base
        let fileLines = readTodoLines()
        var raw = fileLines ?? ["Error: Unable to open file!"]
        if fileLines == nil { raw = ["Error: Unable to open file!"] }
        let ok = fileLines != nil

        var rs: [Row] = []
        for (i, line) in raw.enumerated() {
            let t = String(trimRight(line).drop(while: { $0 == " " || $0 == "\t" }))
            var r = Row(type: .spacer)
            r.source = ok ? i : -1
            func body(_ s: String) -> String { processPlaceholders(String(s.dropFirst(s.dropFirst().first == " " ? 2 : 1))) }
            if t.isEmpty { r.type = .spacer }
            else if t.first == "#" { r.type = .heading; r.text = body(t) }
            else if t.first == ">" { r.type = .meta; r.text = body(t) }
            else if let st = parseTodo(line) {
                r.type = ok ? .todo : .meta
                r.text = processPlaceholders(st.text.isEmpty && !ok ? line : st.text)
                r.done = st.done
                r.key = st.text.trimmingCharacters(in: .whitespaces)
            }
            rs.append(r)
        }
        // 已完成排在后面（与文件内的排序一致，容错手工编辑）
        let todos = rs.filter { $0.type == .todo }.enumerated().sorted { a, b in
            a.element.done != b.element.done ? !a.element.done : a.offset < b.offset
        }.map { $0.element }
        var k = 0
        for i in rs.indices where rs[i].type == .todo { rs[i] = todos[k]; k += 1 }

        let headSz = base * 2.0, metaSz = base * 0.64
        let fHead = makeFont(cfg.fontFamily, headSz, light: true)
        let fText = makeFont(cfg.fontFamily, base)
        let fMeta = makeFont(cfg.fontFamily, metaSz)

        let PADX = (base * 1.15).rounded(.towardZero), PADTOP = (base * 0.95).rounded(.towardZero), PADBOT = (base * 1.05).rounded(.towardZero)
        let checkD = max(11, (base * 0.58).rounded(.towardZero)), checkGap = (base * 0.52).rounded(.towardZero)
        let divGap = (base * 0.80).rounded(.towardZero), metaGap = (base * 0.28).rounded(.towardZero), headGap = (base * 0.12).rounded(.towardZero)
        let todoExtra = CGFloat(max(0, cfg.lineHeight - cfg.fontHeight))
        let todoGap = (base * 0.45).rounded(.towardZero) + todoExtra

        // 计时胶囊：同一列右对齐，宽度按模板测量，数字变化时不抖动
        var pw: CGFloat = 0
        for r in rs where r.type == .todo {
            guard let t = timer(for: r.key) else { continue }
            let el = t.elapsed
            let span = t.countdown ? max(t.duration, el - t.duration) : el
            let tw = ceil(textSize(span >= 3600 ? "+88:88:88" : "+88:88", timerFont).width)
            pw = max(pw, pillPadX * 2 + ringD + pillInnerGap + tw)
        }
        pillColW = pw

        var contentW = base * 4
        for i in rs.indices {
            if rs[i].type == .spacer { rs[i].h = base * 0.45; continue }
            let f = rs[i].type == .heading ? fHead : rs[i].type == .meta ? fMeta : fText
            let sz = textSize(rs[i].text, f)
            rs[i].h = ceil(sz.height)
            rs[i].tw = ceil(sz.width)
            rs[i].w = ceil(sz.width) + (rs[i].type == .todo ? checkD + checkGap : 0)
            if rs[i].type == .todo, pillColW > 0, timer(for: rs[i].key) != nil { rs[i].w += pillGap + pillColW }
            contentW = max(contentW, rs[i].w)
        }

        // 提醒横幅
        bannerH = 0; btnW = []
        btnSpec = controller?.bannerButtonSpec() ?? []
        if ui.alertActive {
            let ts = textSize(ui.alertTitle, bTitleFont), ds = textSize(ui.alertDetail, bDetailFont)
            bTitleH = ceil(ts.height); bDetailH = ceil(ds.height)
            var btnsW: CGFloat = 0
            for b in btnSpec {
                let w = ceil(textSize(b.title, btnFont).width) + btnPadX * 2
                btnW.append(w); btnsW += w
            }
            btnsW += btnGap * CGFloat(max(0, btnSpec.count - 1))
            let need = max(bPad + iconD + bPad * 0.8 + max(ts.width, ds.width) + bPad, bPad * 2 + btnsW)
            bHeadBlock = max(iconD, bTitleH + base * 0.12 + bDetailH)
            bannerH = bPad + bHeadBlock + base * 0.6 + btnH + bPad
            contentW = max(contentW, ceil(need))
        }

        var y = PADTOP, divY: CGFloat = -1, placed = false, seen = ui.alertActive, lastGap: CGFloat = 0
        bannerY = PADTOP
        if ui.alertActive { y += bannerH + base * 0.85 }
        for i in rs.indices {
            if rs[i].type == .spacer { y += rs[i].h; lastGap = 0; continue }
            if rs[i].type == .todo, !placed {
                if seen { y += divGap; divY = y; y += 1 + divGap }
                placed = true
            }
            rs[i].y = y
            switch rs[i].type {
            case .heading: y += rs[i].h + headGap; lastGap = headGap
            case .meta: y += rs[i].h + metaGap; lastGap = metaGap
            default: y += max(rs[i].h, checkD) + todoGap; lastGap = todoGap
            }
            seen = true
        }
        y -= lastGap
        var cardW = contentW + PADX * 2, cardH = y + PADBOT
        if let scr = NSScreen.main { cardW = min(cardW, scr.frame.width - 20) }
        cardW = ceil(cardW); cardH = ceil(cardH)
        rows = rs; dividerY = divY; cardSize = NSSize(width: cardW, height: cardH)
        return NSSize(width: cardW + 2 * M, height: cardH + 2 * M)
    }

    override func draw(_ dirtyRect: NSRect) {
        guard let ctx = NSGraphicsContext.current?.cgContext else { return }
        let base = self.base, M = self.M
        let now = Date()
        let alertT = ui.alertActive ? now.timeIntervalSince(ui.alertStart) : 0
        let pulse = ui.alertActive ? 0.5 + 0.5 * sin(alertT * 2 * .pi / 1.4) : 0
        let radius = base * 0.7
        let cardRect = NSRect(x: M, y: M, width: cardSize.width, height: cardSize.height)
        let cardPath = NSBezierPath(roundedRect: cardRect, xRadius: radius, yRadius: radius)

        // 柔和阴影（只画卡片外侧，避免半透明卡片中心发灰）
        ctx.saveGState()
        let clip = NSBezierPath(rect: bounds)
        clip.append(cardPath)
        clip.windingRule = .evenOdd
        clip.addClip()
        let sh = NSShadow()
        sh.shadowBlurRadius = min(56, max(8, base * 0.95))
        sh.shadowOffset = NSSize(width: 0, height: -base * 0.35)
        sh.shadowColor = NSColor.black.withAlphaComponent(0.5)
        sh.set()
        NSColor.black.setFill()
        cardPath.fill()
        ctx.restoreGState()

        cfg.bgColor.withAlphaComponent(CGFloat(cfg.bgAlpha) / 255).setFill()
        cardPath.fill()
        NSColor.white.withAlphaComponent(0.1).setStroke()
        cardPath.lineWidth = 1
        cardPath.stroke()

        // 提醒中：卡片边缘呼吸光
        if ui.alertActive {
            for k in stride(from: 3, through: 1, by: -1) {
                coral.withAlphaComponent(CGFloat((22 + 34 * pulse) * Double(4 - k) / 3 / 255)).setStroke()
                cardPath.lineWidth = CGFloat(k) * 2.2
                cardPath.stroke()
            }
            rgb(255, 128, 112, CGFloat((110 + 120 * pulse) / 255)).setStroke()
            cardPath.lineWidth = 1.6
            cardPath.stroke()
        }

        let tc = cfg.textColor
        func scaled(_ k: CGFloat, _ a: CGFloat) -> NSColor {
            NSColor(srgbRed: tc.redComponent * k, green: tc.greenComponent * k, blue: tc.blueComponent * k, alpha: a)
        }
        let ink = tc, checkc = tc.withAlphaComponent(0.92)
        let doneInk = scaled(0.55, 0.6), doneCheck = scaled(0.52, 0.65), doneFill = scaled(0.45, 0.14)
        let metaC = NSColor(srgbRed: 169 / 255, green: 175 / 255, blue: 189 / 255, alpha: 1)
        let fHead = makeFont(cfg.fontFamily, base * 2, light: true), fText = makeFont(cfg.fontFamily, base)
        let fMeta = makeFont(cfg.fontFamily, base * 0.64)
        let PADX = (base * 1.15).rounded(.towardZero)
        let checkD = max(11, (base * 0.58).rounded(.towardZero)), checkGap = (base * 0.52).rounded(.towardZero)
        let sdy = max(1, base * 0.045)

        func drawText(_ s: String, _ f: NSFont, _ x: CGFloat, _ y: CGFloat, _ c: NSColor, strike: Bool = false) {
            if s.isEmpty { return }
            NSAttributedString(string: s, attributes: attrs(f, color: NSColor.black.withAlphaComponent(0.35 * c.alphaComponent), strike: strike))
                .draw(at: NSPoint(x: x, y: y + sdy))
            NSAttributedString(string: s, attributes: attrs(f, color: c, strike: strike)).draw(at: NSPoint(x: x, y: y))
        }

        let bx = M + PADX
        let rightX = M + cardSize.width - PADX
        if dividerY >= 0 {
            NSColor.white.withAlphaComponent(0.12).setStroke()
            let p = NSBezierPath()
            p.move(to: NSPoint(x: bx, y: M + dividerY + 0.5)); p.line(to: NSPoint(x: rightX, y: M + dividerY + 0.5))
            p.lineWidth = 1; p.stroke()
        }

        // ---- 提醒横幅 ----
        bannerButtons.removeAll()
        if ui.alertActive {
            let e = easeOutCubic(alertT / 0.38)                     // 入场：下滑 + 淡入
            let A = CGFloat(e)
            let bw = rightX - bx
            let byT = M + bannerY - CGFloat(1 - e) * base * 0.6
            let br = base * 0.55
            for k in 1...3 {                                         // 外圈呼吸光
                let kk = CGFloat(k) * 1.6
                let gp = NSBezierPath(roundedRect: NSRect(x: bx - kk, y: byT - kk, width: bw + kk * 2, height: bannerH + kk * 2),
                                      xRadius: br + kk, yRadius: br + kk)
                rgb(255, 120, 100, CGFloat(e * (40 + 60 * pulse) / Double(k) / 255)).setStroke()
                gp.lineWidth = 1.6; gp.stroke()
            }
            let bp = NSBezierPath(roundedRect: NSRect(x: bx, y: byT, width: bw, height: bannerH), xRadius: br, yRadius: br)
            NSGradient(starting: rgb(255, 95, 109, 0.96 * A), ending: rgb(255, 166, 77, 0.96 * A))?.draw(in: bp, angle: 18)
            let hp = NSBezierPath(roundedRect: NSRect(x: bx + 1, y: byT + 1, width: bw - 2, height: bannerH - 2), xRadius: br - 1, yRadius: br - 1)
            NSColor.white.withAlphaComponent(0.25 * A).setStroke(); hp.lineWidth = 1; hp.stroke()

            // 闹钟图标（抖动时轻轻摇摆）
            let icx = bx + bPad + iconD / 2, icy = byT + bPad + bHeadBlock / 2
            var wig = 0.0
            if ui.shaking {
                let st = now.timeIntervalSince(ui.shakeStart)
                wig = 18 * sin(st * 2 * .pi * 6) * (1 - clamp01(st / shakeSec))
            }
            NSGraphicsContext.saveGraphicsState()
            let xf = NSAffineTransform()
            xf.translateX(by: icx, yBy: icy)
            xf.rotate(byDegrees: CGFloat(wig))
            xf.concat()
            NSColor.white.withAlphaComponent(0.24 * A).setFill()
            NSBezierPath(ovalIn: NSRect(x: -iconD / 2, y: -iconD / 2, width: iconD, height: iconD)).fill()
            let cr = iconD * 0.25, sw = max(1.4, base * 0.075), cyo = iconD * 0.05
            NSColor.white.withAlphaComponent(A).setStroke()
            func stroked(_ p: NSBezierPath) { p.lineWidth = sw; p.lineCapStyle = .round; p.stroke() }
            stroked(NSBezierPath(ovalIn: NSRect(x: -cr, y: cyo - cr, width: cr * 2, height: cr * 2)))
            let hands = NSBezierPath()
            hands.move(to: NSPoint(x: 0, y: cyo - cr * 0.55)); hands.line(to: NSPoint(x: 0, y: cyo))
            hands.line(to: NSPoint(x: cr * 0.42, y: cyo + cr * 0.22))
            stroked(hands)
            let er = cr * 0.48
            let earL = NSBezierPath(); earL.appendArc(withCenter: NSPoint(x: -cr * 0.85, y: cyo - cr * 0.95), radius: er, startAngle: 190, endAngle: 290, clockwise: false)
            let earR = NSBezierPath(); earR.appendArc(withCenter: NSPoint(x: cr * 0.85, y: cyo - cr * 0.95), radius: er, startAngle: 250, endAngle: 350, clockwise: false)
            stroked(earL); stroked(earR)
            let feet = NSBezierPath()
            feet.move(to: NSPoint(x: -cr * 0.62, y: cyo + cr * 0.86)); feet.line(to: NSPoint(x: -cr * 0.86, y: cyo + cr * 1.14))
            feet.move(to: NSPoint(x: cr * 0.62, y: cyo + cr * 0.86)); feet.line(to: NSPoint(x: cr * 0.86, y: cyo + cr * 1.14))
            stroked(feet)
            NSGraphicsContext.restoreGraphicsState()

            let tx = bx + bPad + iconD + bPad * 0.8
            let ty = byT + bPad + (bHeadBlock - (bTitleH + base * 0.12 + bDetailH)) / 2
            NSAttributedString(string: ui.alertTitle, attributes: attrs(bTitleFont, color: NSColor.white.withAlphaComponent(A))).draw(at: NSPoint(x: tx, y: ty))
            NSAttributedString(string: ui.alertDetail, attributes: attrs(bDetailFont, color: NSColor.white.withAlphaComponent(0.88 * A)))
                .draw(at: NSPoint(x: tx, y: ty + bTitleH + base * 0.12))

            // 按钮（右对齐；第一个为主按钮）
            let byB = byT + bannerH - bPad - btnH
            var cx = bx + bw - bPad
            for i in stride(from: btnSpec.count - 1, through: 0, by: -1) where i < btnW.count {
                cx -= btnW[i]
                let primary = i == 0
                let r = NSRect(x: cx, y: byB, width: btnW[i], height: btnH)
                (primary ? NSColor.white.withAlphaComponent(0.96 * A) : NSColor.white.withAlphaComponent(0.22 * A)).setFill()
                NSBezierPath(roundedRect: r, xRadius: btnH / 2, yRadius: btnH / 2).fill()
                let label = NSAttributedString(string: btnSpec[i].title,
                                               attributes: attrs(btnFont, color: primary ? rgb(225, 72, 82, A) : NSColor.white.withAlphaComponent(A)))
                let ls = label.size()
                label.draw(at: NSPoint(x: r.midX - ls.width / 2, y: r.midY - ls.height / 2))
                bannerButtons.append((r, btnSpec[i].action))
                cx -= btnGap
            }
        }

        // ---- 各行 ----
        hitRects.removeAll(); rowRects.removeAll(); pillRects.removeAll()
        var rowY: [String: CGFloat] = [:]
        let settleT = ui.settling ? easeOutCubic(now.timeIntervalSince(ui.settleStart) / settleAnimSec) : 1
        let animP = ui.animKey != nil ? clamp01(now.timeIntervalSince(ui.animStart) / completeAnimSec) : 0

        for r in rows {
            var ry = M + r.y
            switch r.type {
            case .spacer: continue
            case .heading: drawText(r.text, fHead, bx, ry, ink); continue
            case .meta: drawText(r.text, fMeta, bx, ry, metaC); continue
            case .todo: break
            }
            // 已完成项沉底时，从旧位置平滑滑到新位置
            if ui.settling, let from = ui.settleFrom[r.key] { ry = M + from + (r.y - from) * CGFloat(settleT) }
            if rowY[r.key] == nil { rowY[r.key] = ry - M }

            let rh = max(r.h, checkD)
            let cy = ry + rh / 2 - checkD / 2
            let stroke = max(1.25, base * 0.06)
            let box = NSRect(x: bx, y: cy, width: checkD, height: checkD)
            let textX = bx + checkD + checkGap
            let rowRect = NSRect(x: bx - base * 0.35, y: ry - base * 0.22, width: rightX - bx + base * 0.7, height: rh + base * 0.44)
            let animating = ui.animKey == r.key && !r.done
            let alerting = ui.alertActive && !ui.alertTaskKey.isEmpty && !r.done && r.key == ui.alertTaskKey

            if alerting || animating {
                (alerting ? coral.withAlphaComponent(CGFloat((26 + 34 * pulse) / 255))
                          : accentDone.withAlphaComponent(CGFloat(34 * sin(.pi * animP) / 255))).setFill()
                NSBezierPath(roundedRect: rowRect, xRadius: base * 0.35, yRadius: base * 0.35).fill()
            }

            if animating {
                // 完成动画：涟漪 + 光点 + 复选框弹出填充 + 对勾描绘 + 删除线划过
                let pf = easeOutBack(animP / 0.32)
                let pt = clamp01((animP - 0.16) / 0.30)
                let pr = clamp01(animP / 0.80)
                let ps = easeInOutCubic((animP - 0.28) / 0.55)
                let ccx = box.midX, ccy = box.midY
                let rr = checkD * (0.55 + 1.30 * CGFloat(easeOutCubic(pr)))
                let ring = NSBezierPath(ovalIn: NSRect(x: ccx - rr, y: ccy - rr, width: rr * 2, height: rr * 2))
                accentDone.withAlphaComponent(CGFloat(0.67 * (1 - pr))).setStroke()
                ring.lineWidth = max(1, stroke * CGFloat(1.5 - pr)); ring.stroke()
                for k in 0..<8 {
                    let ang = Double(k) * .pi / 4 + 0.39
                    let dist = Double(checkD) * (0.6 + 1.5 * easeOutCubic(pr))
                    let ds = checkD * 0.13 * CGFloat(1 - pr)
                    if ds < 0.3 { continue }
                    (k % 2 == 0 ? accentDone : accentGold).withAlphaComponent(CGFloat(0.92 * (1 - pr))).setFill()
                    let sx = ccx + CGFloat(cos(ang) * dist), sy = ccy + CGFloat(sin(ang) * dist)
                    NSBezierPath(ovalIn: NSRect(x: sx - ds, y: sy - ds, width: ds * 2, height: ds * 2)).fill()
                }
                lerpColor(checkc, accentDone, animP / 0.2).setStroke()
                let bp = NSBezierPath(rect: box.insetBy(dx: stroke / 2, dy: stroke / 2)); bp.lineWidth = stroke; bp.stroke()
                let fs = checkD * CGFloat(pf)
                if fs > 0.5 {
                    accentDone.setFill()
                    NSBezierPath(rect: NSRect(x: ccx - fs / 2, y: ccy - fs / 2, width: fs, height: fs)).fill()
                }
                drawTick(bx, cy, checkD, pt, .white, stroke + 0.35)
                let c = lerpColor(ink, doneInk, ps)
                drawText(r.text, fText, textX, ry, c)
                if ps > 0 {
                    let sl = NSBezierPath()
                    let sy = ry + r.h * 0.54
                    sl.move(to: NSPoint(x: textX, y: sy)); sl.line(to: NSPoint(x: textX + r.tw * CGFloat(ps), y: sy))
                    sl.lineWidth = max(1.2, base * 0.06); c.setStroke(); sl.stroke()
                }
            } else {
                if r.done { doneFill.setFill(); NSBezierPath(rect: box).fill() }
                (r.done ? doneCheck : checkc).setStroke()
                let bp = NSBezierPath(rect: box.insetBy(dx: stroke / 2, dy: stroke / 2)); bp.lineWidth = stroke; bp.stroke()
                if r.done { drawTick(bx, cy, checkD, 1, doneCheck, stroke + 0.35) }
                // 刚完成的一项：滑动期间强调色逐渐褪为已完成样式
                if r.done, ui.settling, r.key == ui.justCompletedKey {
                    let a = CGFloat(1 - settleT)
                    accentDone.withAlphaComponent(a).setFill(); NSBezierPath(rect: box).fill()
                    drawTick(bx, cy, checkD, 1, NSColor.white.withAlphaComponent(a), stroke + 0.35)
                }
                drawText(r.text, fText, textX, ry, r.done ? doneInk : ink, strike: r.done)
            }

            if r.source >= 0 {
                let inflate = max(4, base * 0.2)
                hitRects.append(HitRect(rect: box.insetBy(dx: -inflate, dy: -inflate), source: r.source, key: r.key, done: r.done))
                rowRects.append(HitRect(rect: rowRect, source: r.source, key: r.key, done: r.done))
            }

            // ---- 计时胶囊 ----
            if pillColW > 0, let t = timer(for: r.key) {
                let px = rightX - pillColW, py = ry + rh / 2 - pillH / 2
                let el = t.elapsed
                let expired = t.countdown && el >= t.duration
                var label = "", frac = 0.0, ringC = sky
                if t.countdown {
                    let rem = t.duration - el
                    if rem > 0 {
                        label = formatClock(rem)
                        frac = t.duration > 0 ? Double(rem) / Double(t.duration) : 0
                        ringC = (rem <= 60 || rem * 5 <= t.duration) ? amber : sky
                    } else {
                        label = "+" + formatClock(-rem)   // 超时多久
                        frac = 1; ringC = coral
                    }
                } else {
                    label = formatClock(el)
                    frac = Double(el % 60) / 60
                    ringC = violet
                }
                let fade: CGFloat = r.done ? 0.47 : (t.running ? 1 : 0.68)
                let pill = NSRect(x: px, y: py, width: pillColW, height: pillH)
                ((expired && !r.done) ? coral.withAlphaComponent(CGFloat(alerting ? (60 + 50 * pulse) / 255 : 0.2))
                                      : tc.withAlphaComponent(r.done ? 0.055 : 0.1)).setFill()
                NSBezierPath(roundedRect: pill, xRadius: pillH / 2, yRadius: pillH / 2).fill()

                let rcx = px + pillPadX + ringD / 2, rcy = py + pillH / 2
                let rw = max(1.5, ringD * 0.16), rd = ringD - rw
                let track = NSBezierPath(ovalIn: NSRect(x: rcx - rd / 2, y: rcy - rd / 2, width: rd, height: rd))
                tc.withAlphaComponent(0.28 * fade).setStroke(); track.lineWidth = rw; track.stroke()
                ringC.withAlphaComponent(fade).setStroke()
                if frac >= 0.999 {
                    track.stroke()
                } else if frac > 0.001 {
                    let arc = NSBezierPath()
                    arc.appendArc(withCenter: NSPoint(x: rcx, y: rcy), radius: rd / 2, startAngle: -90, endAngle: -90 + CGFloat(360 * frac), clockwise: false)
                    arc.lineWidth = rw; arc.lineCapStyle = .round; arc.stroke()
                }
                if !t.running {   // 暂停：中间两道竖线
                    ringC.withAlphaComponent(fade).setFill()
                    let bw2 = max(1.2, rw * 0.75), bh2 = ringD * 0.34
                    NSBezierPath(rect: NSRect(x: rcx - bw2 * 1.6, y: rcy - bh2 / 2, width: bw2, height: bh2)).fill()
                    NSBezierPath(rect: NSRect(x: rcx + bw2 * 0.6, y: rcy - bh2 / 2, width: bw2, height: bh2)).fill()
                }
                let lc = (expired && !r.done) ? rgb(255, 150, 140, fade) : tc.withAlphaComponent(fade)
                let ls = NSAttributedString(string: label, attributes: attrs(timerFont, color: lc))
                ls.draw(at: NSPoint(x: rcx + ringD / 2 + pillInnerGap, y: py + (pillH - ls.size().height) / 2))
                pillRects.append(HitRect(rect: pill, source: r.source, key: r.key, done: r.done))
            }
        }
        ui.lastRowY = rowY
    }

    override func mouseDown(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        if let b = bannerButtons.first(where: { $0.rect.contains(p) }) { controller?.bannerAction(b.action); return }
        if let h = pillRects.first(where: { $0.rect.contains(p) }) {
            toggleTaskTimerPause(h.key); controller?.refresh(force: true); return
        }
        if let h = hitRects.first(where: { $0.rect.contains(p) }) {
            controller?.beginToggle(h)
            return
        }
        if event.clickCount == 2 { controller?.editTodo(); return }
        window?.performDrag(with: event)
    }

    override func menu(for event: NSEvent) -> NSMenu? {
        controller?.buildMenu(taskAt: convert(event.locationInWindow, from: nil))
    }
}

// MARK: - Settings panel

final class SettingsController: NSObject {
    let panel: NSPanel
    weak var app: AppController?
    var fontSlider: NSSlider!, spaceSlider: NSSlider!, opacitySlider: NSSlider!
    var labels: [NSTextField] = []
    var textWell: NSColorWell!, cardWell: NSColorWell!
    var fontPopup: NSPopUpButton!

    init(app: AppController) {
        self.app = app
        panel = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 360, height: 340),
                        styleMask: [.titled, .closable, .utilityWindow], backing: .buffered, defer: false)
        panel.title = "DesktopTip 设置"
        panel.level = .floating
        panel.isReleasedWhenClosed = false
        super.init()
        let v = panel.contentView!
        func label(_ s: String, _ y: CGFloat) -> NSTextField {
            let t = NSTextField(labelWithString: s); t.frame = NSRect(x: 20, y: y, width: 220, height: 18); v.addSubview(t); return t
        }
        func value(_ y: CGFloat) -> NSTextField {
            let t = NSTextField(labelWithString: ""); t.alignment = .right
            t.frame = NSRect(x: 260, y: y, width: 80, height: 18); v.addSubview(t); labels.append(t); return t
        }
        func slider(_ y: CGFloat, _ lo: Double, _ hi: Double, _ val: Double) -> NSSlider {
            let s = NSSlider(value: val, minValue: lo, maxValue: hi, target: self, action: #selector(changed))
            s.frame = NSRect(x: 20, y: y, width: 320, height: 22); v.addSubview(s); return s
        }
        _ = label("字号  Font size", 306); _ = value(306)
        fontSlider = slider(282, 10, 60, Double(cfg.fontHeight))
        _ = label("行距  Spacing", 246); _ = value(246)
        spaceSlider = slider(222, 0, 90, Double(cfg.lineHeight))
        _ = label("卡片透明度  Opacity", 186); _ = value(186)
        opacitySlider = slider(162, 0, 255, Double(cfg.bgAlpha))
        _ = label("文字颜色  Text", 122)
        textWell = NSColorWell(frame: NSRect(x: 20, y: 90, width: 90, height: 28)); textWell.color = cfg.textColor
        textWell.target = self; textWell.action = #selector(changed); v.addSubview(textWell)
        _ = label("卡片颜色  Card", 122).frame.origin.x = 200
        cardWell = NSColorWell(frame: NSRect(x: 200, y: 90, width: 90, height: 28)); cardWell.color = cfg.bgColor
        cardWell.target = self; cardWell.action = #selector(changed); v.addSubview(cardWell)
        _ = label("字体  Font", 60)
        fontPopup = NSPopUpButton(frame: NSRect(x: 20, y: 24, width: 320, height: 28), pullsDown: false)
        fontPopup.addItems(withTitles: NSFontManager.shared.availableFontFamilies)
        fontPopup.selectItem(withTitle: cfg.fontFamily)
        fontPopup.target = self; fontPopup.action = #selector(changed); v.addSubview(fontPopup)
        updateLabels()
    }

    func updateLabels() {
        labels[0].stringValue = "\(Int(fontSlider.doubleValue)) px"
        labels[1].stringValue = "\(Int(spaceSlider.doubleValue))"
        labels[2].stringValue = "\(Int(opacitySlider.doubleValue / 255 * 100))%"
    }

    @objc func changed() {
        cfg.fontHeight = Int(fontSlider.doubleValue)
        cfg.lineHeight = Int(spaceSlider.doubleValue)
        cfg.bgAlpha = Int(opacitySlider.doubleValue)
        if let c = textWell.color.usingColorSpace(.sRGB) {
            cfg.text = (Int(round(c.redComponent * 255)), Int(round(c.greenComponent * 255)), Int(round(c.blueComponent * 255)))
        }
        if let c = cardWell.color.usingColorSpace(.sRGB) {
            cfg.bg = (Int(round(c.redComponent * 255)), Int(round(c.greenComponent * 255)), Int(round(c.blueComponent * 255)))
        }
        if let t = fontPopup.titleOfSelectedItem { cfg.fontFamily = t }
        updateLabels()
        app?.refresh(force: true)
        app?.saveConfig()
    }

    func show(near p: NSPoint) {
        panel.setFrameOrigin(NSPoint(x: p.x, y: max(40, p.y - panel.frame.height)))
        NSApp.activate(ignoringOtherApps: true)
        panel.makeKeyAndOrderFront(nil)
    }
}

// MARK: - App controller

final class AppController: NSObject, NSApplicationDelegate, NSWindowDelegate, NSMenuDelegate {
    var window: NSPanel!
    var card: CardView!
    var statusItem: NSStatusItem!
    var settings: SettingsController?
    var lastMTime: Date?

    // 动画（完成动画 / 行滑动 / 抖动 / 提醒横幅呼吸光）
    var animTimer: Timer?
    var animFrame = 0
    var shakeBaseTL = NSPoint.zero      // 抖动基准位置（窗口左上角）
    var menuTaskKey = ""

    // 置顶时鼠标悬停隐藏：鼠标移到卡片上时淡出并忽略鼠标事件（点击穿透到下面的窗口），
    // 移开后淡入恢复；按住 ⌘ 时保持显示、可正常拖动/勾选/右键。
    var hoverTimer: Timer?
    var hoverHidden = false
    var hoverStart: Date?        // 本次悬停开始时间（用于悬停延时）
    var menuTracking = false

    // reminder state
    var armed = false
    var due = Date()
    var remTitle = "", remDetail = ""
    var isCountdown = false
    var interval = 0

    func applicationDidFinishLaunching(_ n: Notification) {
        ensureDefaultTodo()
        if !FileManager.default.fileExists(atPath: cfg.todoPath) { cfg.todoPath = supportDir.appendingPathComponent("TODO.txt").path; ensureDefaultTodo() }

        card = CardView(frame: NSRect(x: 0, y: 0, width: 200, height: 100))
        card.controller = self
        window = NSPanel(contentRect: card.frame, styleMask: [.borderless, .nonactivatingPanel], backing: .buffered, defer: false)
        window.contentView = card
        window.isOpaque = false
        window.backgroundColor = .clear
        window.hasShadow = false
        window.isReleasedWhenClosed = false
        window.hidesOnDeactivate = false
        window.becomesKeyOnlyIfNeeded = true
        window.collectionBehavior = [.canJoinAllSpaces, .stationary, .ignoresCycle, .fullScreenAuxiliary]
        window.delegate = self

        applyMode()
        refresh(force: true)
        if cfg.hasPosition, let scr = NSScreen.main {
            // 配置里存的是 左上角、自屏幕顶部向下的坐标
            window.setFrameTopLeftPoint(NSPoint(x: CGFloat(cfg.x), y: scr.frame.maxY - CGFloat(cfg.y)))
        } else if let scr = NSScreen.main {
            window.setFrameTopLeftPoint(NSPoint(x: scr.visibleFrame.maxX - window.frame.width - 10, y: scr.visibleFrame.maxY - 10))
        }
        clampToScreens()
        window.orderFrontRegardless()

        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
        if let img = NSImage(systemSymbolName: "checklist", accessibilityDescription: "DesktopTip") {
            statusItem.button?.image = img
        } else { statusItem.button?.title = "✓" }
        let m = NSMenu(); m.delegate = self; statusItem.menu = m

        Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in self?.tick() }
        NotificationCenter.default.addObserver(self, selector: #selector(screensChanged),
                                               name: NSApplication.didChangeScreenParametersNotification, object: nil)
        NotificationCenter.default.addObserver(forName: NSMenu.didBeginTrackingNotification, object: nil, queue: .main) { [weak self] _ in self?.menuTracking = true }
        NotificationCenter.default.addObserver(forName: NSMenu.didEndTrackingNotification, object: nil, queue: .main) { [weak self] _ in self?.menuTracking = false }
    }

    @objc func screensChanged() { clampToScreens() }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { false }

    // Window level
    func applyMode() {
        if cfg.displayMode == 1 {
            window.level = .floating
        } else {
            window.level = NSWindow.Level(rawValue: Int(CGWindowLevelForKey(.desktopIconWindow)) + 1)
        }
        updateHoverHide()
    }

    // MARK: Hide on hover (always-on-top only)
    func updateHoverHide() {
        if cfg.hoverHide && cfg.displayMode == 1 {
            if hoverTimer == nil {
                let t = Timer(timeInterval: 0.03, repeats: true) { [weak self] _ in self?.hoverTick() }
                RunLoop.main.add(t, forMode: .common)   // 拖动/菜单跟踪期间也继续检测
                hoverTimer = t
            }
        } else {
            hoverTimer?.invalidate(); hoverTimer = nil
            hoverStart = nil
            setHoverHidden(false)
        }
    }

    func hoverTick() {
        // 到点提醒期间覆盖悬停隐藏：卡片保持完全显示、可点击
        if ui.alertActive { setHoverHidden(false); return }
        let inside = window.frame.contains(NSEvent.mouseLocation)
        let reveal = NSEvent.modifierFlags.contains(.command)
        // 卡片可见时按着左键（正在拖动卡片）不隐藏
        let dragging = !hoverHidden && (NSEvent.pressedMouseButtons & 1) != 0
        var want = inside && !reveal && !dragging && !menuTracking
        // 悬停延时：需连续悬停满 hoverHideDelay 才隐藏；已隐藏时鼠标仍在卡片内就保持隐藏
        if want && !hoverHidden {
            if hoverStart == nil { hoverStart = Date() }
            if Date().timeIntervalSince(hoverStart!) * 1000 < Double(cfg.hoverHideDelay) { want = false }
        } else if !want {
            hoverStart = nil
        }
        setHoverHidden(want)
    }

    func setHoverHidden(_ hidden: Bool) {
        guard hidden != hoverHidden else { return }
        hoverHidden = hidden
        window.ignoresMouseEvents = hidden
        NSAnimationContext.runAnimationGroup { ctx in
            ctx.duration = 0.12
            self.window.animator().alphaValue = hidden ? 0 : 1
        }
    }

    // Layout/refresh keeping the top-left corner fixed
    func refresh(force: Bool = false) {
        let sig = (try? FileManager.default.attributesOfItem(atPath: cfg.todoPath)[.modificationDate] as? Date) ?? nil
        let minute = Calendar.current.component(.minute, from: Date()) * 60 + Calendar.current.component(.second, from: Date())
        _ = minute
        if !force && sig == lastMTime && !contentHasPlaceholders { return }
        lastMTime = sig
        let size = card.relayout()
        let top = NSPoint(x: window.frame.minX, y: window.frame.maxY)
        let hadFrame = window.frame.width > 1
        window.setContentSize(size)
        if hadFrame { window.setFrameTopLeftPoint(top) }
        card.frame = NSRect(origin: .zero, size: size)
        card.needsDisplay = true
    }

    var contentHasPlaceholders: Bool {
        guard let d = FileManager.default.contents(atPath: cfg.todoPath) else { return true }
        return decodeText(d).contains("{{")
    }

    func clampToScreens() {
        let union = NSScreen.screens.map { $0.frame }.reduce(NSRect.null) { $0.union($1) }
        guard !union.isNull else { return }
        var f = window.frame
        f.origin.x = min(max(f.origin.x, union.minX), union.maxX - f.width)
        f.origin.y = min(max(f.origin.y, union.minY), union.maxY - f.height)
        if f != window.frame { window.setFrame(f, display: true) }
    }

    func tick() {
        checkReminder()
        checkTaskTimers()
        refresh(force: taskTimers.contains { $0.running })   // 有计时在走：每秒刷新
        if cfg.displayMode == 1 || ui.alertActive { window.orderFrontRegardless() }
    }

    // Position persistence (top-left, measured down from the top of the main screen)
    func windowDidMove(_ notification: Notification) {
        NSObject.cancelPreviousPerformRequests(withTarget: self, selector: #selector(saveConfig), object: nil)
        perform(#selector(saveConfig), with: nil, afterDelay: 0.4)
    }

    @objc func saveConfig() {
        guard !ui.alertActive, !ui.shaking, let scr = NSScreen.main else { return }
        cfg.save(origin: NSPoint(x: window.frame.minX, y: scr.frame.maxY - window.frame.maxY))
    }

    // MARK: Menu

    func menuNeedsUpdate(_ menu: NSMenu) {
        menu.removeAllItems()
        let built = buildMenu()
        for item in built.items { built.removeItem(item); menu.addItem(item) }
    }

    @discardableResult
    func add(_ menu: NSMenu, _ title: String, _ action: Selector?, _ tag: Int = 0, checked: Bool = false, enabled: Bool = true) -> NSMenuItem {
        let it = NSMenuItem(title: title, action: enabled ? action : nil, keyEquivalent: "")
        it.target = self; it.tag = tag; it.state = checked ? .on : .off
        menu.addItem(it); return it
    }

    func buildMenu(taskAt point: NSPoint? = nil) -> NSMenu {
        let m = NSMenu()
        // 在某条待办上右键：顶部出现「任务计时」子菜单
        menuTaskKey = ""
        if let point, let row = card.rowRects.first(where: { $0.rect.contains(point) }) {
            menuTaskKey = row.key
            let task = NSMenu()
            let cd = NSMenu()
            for mins in [5, 10, 15, 25, 30, 45, 60, 90] {
                let title = mins == 25 ? "25 分钟（番茄钟）" : mins % 60 == 0 ? "\(mins / 60) 小时" : "\(mins) 分钟"
                add(cd, title, #selector(taskCountdown(_:)), mins * 60)
            }
            cd.addItem(.separator())
            add(cd, "自定义…  Custom", #selector(taskCustomCountdown))
            let cdItem = NSMenuItem(title: "倒计时  Countdown", action: nil, keyEquivalent: ""); cdItem.submenu = cd; task.addItem(cdItem)
            add(task, "正计时  Stopwatch", #selector(taskStopwatch))
            if let t = timer(for: row.key) {
                task.addItem(.separator())
                add(task, t.running ? "暂停  Pause" : "继续  Resume", #selector(taskPause))
                add(task, "重新开始  Restart", #selector(taskRestart))
                add(task, "移除计时  Remove timer", #selector(taskRemove))
            }
            var name = processPlaceholders(row.key)
            if name.count > 16 { name = String(name.prefix(16)) + "…" }
            let ti = NSMenuItem(title: "任务计时：\(name)  Timer", action: nil, keyEquivalent: ""); ti.submenu = task; m.addItem(ti)
            m.addItem(.separator())
        }
        add(m, "设置…  Settings", #selector(showSettings))
        m.addItem(.separator())
        let cd = NSMenu()
        add(cd, "1 分钟", #selector(countdown(_:)), 60)
        add(cd, "5 分钟", #selector(countdown(_:)), 300)
        add(cd, "10 分钟", #selector(countdown(_:)), 600)
        add(cd, "1 小时", #selector(countdown(_:)), 3600)
        cd.addItem(.separator())
        add(cd, "重复计时  Repeat", #selector(toggleRepeat), checked: cfg.countdownRepeat)
        cd.addItem(.separator())
        add(cd, "自定义…  Custom", #selector(customCountdown))
        let cdItem = NSMenuItem(title: "倒计时  Countdown", action: nil, keyEquivalent: ""); cdItem.submenu = cd; m.addItem(cdItem)
        add(m, "设置闹钟…  Alarm", #selector(customAlarm))
        if armed {
            let left = max(0, Int(due.timeIntervalSinceNow))
            var s = "已设置：\(remTitle)（剩余 \(formatDuration(left))）"
            if isCountdown && cfg.countdownRepeat && interval > 0 { s += " · 重复" }
            add(m, s, nil, enabled: false)
            if !ui.alertActive { add(m, "取消倒计时/闹钟  Cancel", #selector(cancelReminder)) }
        }
        if ui.alertActive { add(m, "停止提醒  Stop alert", #selector(cancelReminder)) }
        m.addItem(.separator())
        let mode = NSMenu()
        add(mode, "附着桌面  Attach to desktop", #selector(setMode(_:)), 0, checked: cfg.displayMode == 0)
        add(mode, "始终置顶  Always on top", #selector(setMode(_:)), 1, checked: cfg.displayMode == 1)
        mode.addItem(.separator())
        add(mode, "置顶时鼠标悬停隐藏（按住 ⌘ 可操作）  Hide on hover when on top", #selector(toggleHoverHide), checked: cfg.hoverHide)
        let delay = NSMenu()
        for ms in [0, 500, 1000, 2000, 3000, 5000] {
            let title = ms == 0 ? "立即  Immediately" : String(format: "%g 秒  %gs", Double(ms) / 1000, Double(ms) / 1000)
            add(delay, title, #selector(setHoverDelay(_:)), ms, checked: cfg.hoverHideDelay == ms, enabled: cfg.hoverHide)
        }
        let di = NSMenuItem(title: "悬停多久后隐藏  Hover delay", action: nil, keyEquivalent: ""); di.submenu = delay
        di.isEnabled = cfg.hoverHide; mode.addItem(di)
        let mi = NSMenuItem(title: "显示模式  Display mode", action: nil, keyEquivalent: ""); mi.submenu = mode; m.addItem(mi)
        m.addItem(.separator())
        add(m, "编辑 TODO  Edit", #selector(editTodoAction))
        add(m, "刷新  Refresh", #selector(refreshAction))
        add(m, "更换 TODO 文件  Change file", #selector(changeFile))
        add(m, "在访达中显示  Reveal in Finder", #selector(reveal))
        m.addItem(.separator())
        add(m, "退出  Quit", #selector(quit))
        return m
    }

    @objc func showSettings() {
        if settings == nil { settings = SettingsController(app: self) }
        let p = NSEvent.mouseLocation
        settings?.show(near: p)
    }
    @objc func setMode(_ s: NSMenuItem) { cfg.displayMode = s.tag; applyMode(); saveConfig(); window.orderFrontRegardless() }
    @objc func toggleHoverHide() { cfg.hoverHide.toggle(); updateHoverHide(); saveConfig() }
    @objc func setHoverDelay(_ s: NSMenuItem) { cfg.hoverHideDelay = s.tag; hoverStart = nil; saveConfig() }
    @objc func refreshAction() { refresh(force: true) }
    @objc func editTodoAction() { editTodo() }
    func editTodo() { NSWorkspace.shared.open(URL(fileURLWithPath: cfg.todoPath)) }
    @objc func reveal() { NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: cfg.todoPath)]) }
    @objc func quit() { saveConfig(); NSApp.terminate(nil) }

    @objc func changeFile() {
        let p = NSOpenPanel()
        p.allowedContentTypes = [.plainText, .text]
        p.allowsMultipleSelection = false
        p.title = "选择 TODO 文件"
        NSApp.activate(ignoringOtherApps: true)
        if p.runModal() == .OK, let url = p.url {
            cfg.todoPath = url.path
            saveConfig(); refresh(force: true)
        }
    }

    // MARK: Reminders

    func formatDuration(_ s: Int) -> String {
        let h = s / 3600, m = (s % 3600) / 60, sec = s % 60
        if h > 0 { return String(format: "%d:%02d:%02d", h, m, sec) }
        return String(format: "%d:%02d", m, sec)
    }

    func info(_ title: String, _ msg: String? = nil) {
        let a = NSAlert(); a.messageText = title; if let msg { a.informativeText = msg }
        NSApp.activate(ignoringOtherApps: true); a.runModal()
    }

    func prompt(_ title: String, _ label: String, _ initial: String) -> String? {
        let a = NSAlert(); a.messageText = title; a.informativeText = label
        a.addButton(withTitle: "确定"); a.addButton(withTitle: "取消")
        let f = NSTextField(frame: NSRect(x: 0, y: 0, width: 240, height: 24)); f.stringValue = initial
        a.accessoryView = f; a.window.initialFirstResponder = f
        NSApp.activate(ignoringOtherApps: true)
        return a.runModal() == .alertFirstButtonReturn ? f.stringValue.trimmingCharacters(in: .whitespaces) : nil
    }

    func armCountdown(_ seconds: Int) {
        armed = true; due = Date().addingTimeInterval(TimeInterval(seconds))
        remTitle = "倒计时结束"; remDetail = "倒计时 \(formatDuration(seconds)) 已到"
        isCountdown = true; interval = seconds
        info("倒计时已开始：\(formatDuration(seconds))" + (cfg.countdownRepeat ? "（重复计时已开启）" : ""))
    }

    @objc func countdown(_ s: NSMenuItem) { armCountdown(s.tag) }
    @objc func toggleRepeat() { cfg.countdownRepeat.toggle(); saveConfig() }

    @objc func customCountdown() {
        guard let v = prompt("设置倒计时", "输入分钟数，或 MM:SS / HH:MM:SS", "10") else { return }
        if let secs = parseDurationInput(v) { armCountdown(secs) } else { info("请输入有效的倒计时时长。") }
    }

    @objc func customAlarm() {
        let df = DateFormatter(); df.dateFormat = "HH:mm"
        guard let v = prompt("设置闹钟", "输入闹钟时间 HH:MM 或 HH:MM:SS", df.string(from: Date().addingTimeInterval(300))) else { return }
        let p = v.split(separator: ":").map { Int($0) }
        guard (2...3).contains(p.count), p.allSatisfy({ $0 != nil }) else { info("请输入有效的闹钟时间。"); return }
        let (h, m, s) = (p[0]!, p[1]!, p.count == 3 ? p[2]! : 0)
        guard (0..<24).contains(h), (0..<60).contains(m), (0..<60).contains(s),
              var d = Calendar.current.date(bySettingHour: h, minute: m, second: s, of: Date()) else { info("请输入有效的闹钟时间。"); return }
        if d <= Date() { d = Calendar.current.date(byAdding: .day, value: 1, to: d)! }
        armed = true; due = d; isCountdown = false; interval = 0
        remTitle = "闹钟时间到"
        let f = DateFormatter(); f.dateFormat = "yyyy-MM-dd HH:mm:ss"
        remDetail = "设定时间：" + f.string(from: d)
        info("闹钟已设置：" + f.string(from: d))
    }

    @objc func cancelReminder() {
        if ui.alertActive { stopAlert() } else { armed = false; isCountdown = false; interval = 0 }
    }

    func checkReminder() {
        guard armed, Date() >= due else { return }
        let repeating = isCountdown && cfg.countdownRepeat && interval > 0
        func scheduleNext() {
            var next = due.addingTimeInterval(TimeInterval(interval))
            while next <= Date() { next = next.addingTimeInterval(TimeInterval(interval)) }
            due = next; remTitle = "倒计时结束"; remDetail = "重复倒计时 \(formatDuration(interval)) 已到"
        }
        if ui.alertActive { if repeating { scheduleNext() }; return }
        let t = remTitle, d = remDetail
        armed = false
        startAlert(t, d, key: "")
        if repeating { armed = true; scheduleNext() } else { isCountdown = false; interval = 0 }
    }

    // MARK: Per-task timer menu actions

    @objc func taskCountdown(_ s: NSMenuItem) { startTaskCountdown(menuTaskKey, s.tag); refresh(force: true) }
    @objc func taskCustomCountdown() {
        let key = menuTaskKey
        guard !key.isEmpty, let v = prompt("任务倒计时", "输入分钟数，或 MM:SS / HH:MM:SS", "25") else { return }
        if let secs = parseDurationInput(v) { startTaskCountdown(key, secs); refresh(force: true) } else { info("请输入有效的倒计时时长。") }
    }
    @objc func taskStopwatch() { startTaskStopwatch(menuTaskKey); refresh(force: true) }
    @objc func taskPause() { toggleTaskTimerPause(menuTaskKey); refresh(force: true) }
    @objc func taskRestart() { restartTaskTimer(menuTaskKey); refresh(force: true) }
    @objc func taskRemove() {
        let key = menuTaskKey
        removeTaskTimer(key)
        if ui.alertActive && ui.alertTaskKey == key { stopAlert() }
        refresh(force: true)
    }

    /// 每秒检查任务倒计时是否到点
    func checkTaskTimers() {
        let lines = readTodoLines()
        var open: [String: Bool] = [:]
        for l in lines ?? [] {
            guard let k = todoKey(l) else { continue }
            if !k.done { open[k.key] = true } else if open[k.key] == nil { open[k.key] = false }
        }
        var changed = false
        var due: [TaskTimer] = []
        for i in taskTimers.indices {
            let t = taskTimers[i]
            guard t.countdown, !t.fired, t.running, t.elapsed >= t.duration else { continue }
            taskTimers[i].fired = true; changed = true
            if lines != nil && open[t.key] != true { continue }   // 任务已完成或已删除：静默
            due.append(t)
        }
        if changed { saveTaskTimers() }
        for t in due { startAlert("时间到 · " + formatShortDuration(t.duration), processPlaceholders(t.key), key: t.key) }
    }

    func bannerButtonSpec() -> [(action: BannerAction, title: String)] {
        var v: [(action: BannerAction, title: String)] = []
        let task = !ui.alertTaskKey.isEmpty
        if task { v.append((.done, "完成")) }
        if task || !armed { v.append((.snooze, "+5 分钟")) }
        v.append((.dismiss, "知道了"))
        return v
    }

    func bannerAction(_ action: BannerAction) {
        let key = ui.alertTaskKey
        switch action {
        case .done where !key.isEmpty:
            stopAlert()
            if let h = card.hitRects.first(where: { $0.key == key && !$0.done }) { beginToggle(h) }   // 播放完成动画
            else if toggleTodo(key: key, hint: -1) { refresh(force: true) }
            return
        case .snooze:
            if !key.isEmpty { snoozeTaskTimer(key, 5 * 60) }
            else if !armed {
                armed = true; due = Date().addingTimeInterval(300); isCountdown = false; interval = 0
                remTitle = "倒计时结束"; remDetail = "稍后提醒（5 分钟）已到"
            }
        default: break
        }
        stopAlert()
    }

    // MARK: Alert（到点：卡片升到最前端 + 抖动 + 提醒横幅；替代原来的全屏弹跳提示）

    func startAlert(_ title: String, _ detail: String, key: String) {
        ui.alertTitle = title; ui.alertDetail = detail; ui.alertTaskKey = key
        ui.alertStart = Date(); ui.shakeTimes = 0
        ui.alertActive = true
        setHoverHidden(false)                 // 覆盖悬停隐藏
        window.level = .statusBar             // 附着桌面模式下也临时升到最前端
        refresh(force: true)
        window.orderFrontRegardless()
        startShake()
        ensureAnimTimer()
    }

    func stopAlert() {
        guard ui.alertActive else { return }
        stopShake()
        ui.alertActive = false; ui.alertTaskKey = ""
        applyMode()                           // 恢复原层级（附着桌面 / 始终置顶）
        refresh(force: true)
        window.orderFrontRegardless()
        saveConfig()
        ensureAnimTimer()
    }

    func startShake() {
        if (NSEvent.pressedMouseButtons & 1) != 0 { return }   // 正在拖动卡片
        if !ui.shaking { shakeBaseTL = NSPoint(x: window.frame.minX, y: window.frame.maxY) }
        ui.shaking = true; ui.shakeStart = Date(); ui.shakeTimes += 1
        NSSound(named: "Glass")?.play()
    }

    func stopShake() {
        guard ui.shaking else { return }
        ui.shaking = false
        window.setFrameTopLeftPoint(shakeBaseTL)
    }

    func tickShake() {
        guard ui.shaking else { return }
        if (NSEvent.pressedMouseButtons & 1) != 0 { ui.shaking = false; return }   // 用户开始拖动：不再拉回
        let t = Date().timeIntervalSince(ui.shakeStart)
        if t >= shakeSec { stopShake(); return }
        let amp = max(10, card.base * 0.6), k = 1 - t / shakeSec   // 衰减包络，收尾柔和
        let dx = amp * CGFloat(k * k * sin(2 * .pi * 7.5 * t))
        window.setFrameTopLeftPoint(NSPoint(x: shakeBaseTL.x + dx, y: shakeBaseTL.y))
    }

    // MARK: Completion animation

    var animating: Bool { ui.animKey != nil || ui.settling || ui.shaking || ui.alertActive }

    func ensureAnimTimer() {
        if animating {
            if animTimer == nil {
                let t = Timer(timeInterval: 1.0 / 60, repeats: true) { [weak self] _ in self?.tickAnimations() }
                RunLoop.main.add(t, forMode: .common)
                animTimer = t
            }
        } else {
            animTimer?.invalidate(); animTimer = nil
        }
    }

    func tickAnimations() {
        let now = Date()
        if ui.animKey != nil, now.timeIntervalSince(ui.animStart) >= completeAnimSec { commitCompletion() }
        if ui.settling, now.timeIntervalSince(ui.settleStart) >= settleAnimSec {
            ui.settling = false; ui.settleFrom = [:]; ui.justCompletedKey = ""
            card.needsDisplay = true
        }
        if ui.alertActive, !ui.shaking, ui.shakeTimes < shakeMaxTimes, now.timeIntervalSince(ui.shakeStart) >= shakeRepeatSec { startShake() }
        tickShake()
        animFrame += 1
        let fast = ui.animKey != nil || ui.settling || ui.shaking
        if fast || animFrame % 3 == 0 { card.needsDisplay = true }   // 只有呼吸光时 ~20fps 即可
        ensureAnimTimer()
    }

    func snapshotRowsForSettle() {
        ui.settleFrom = ui.lastRowY; ui.settling = true; ui.settleStart = Date()
    }

    func beginToggle(_ h: HitRect) {
        if ui.animKey != nil { commitCompletion() }   // 上一个动画未结束：直接落定
        if !h.done {
            ui.animKey = h.key; ui.animHint = h.source; ui.animStart = Date()
        } else {
            snapshotRowsForSettle(); ui.justCompletedKey = ""
            if !toggleTodo(key: h.key, hint: h.source) { ui.settling = false; ui.settleFrom = [:] }
            refresh(force: true)
        }
        card.needsDisplay = true
        ensureAnimTimer()
    }

    func commitCompletion() {
        guard let key = ui.animKey else { return }
        let hint = ui.animHint
        ui.animKey = nil
        snapshotRowsForSettle()
        if toggleTodo(key: key, hint: hint) {
            ui.justCompletedKey = key
            if let i = timerIndex(key) { pauseTaskTimer(at: i); taskTimers[i].fired = true; saveTaskTimers() }   // 完成即停表
            if ui.alertActive && ui.alertTaskKey == key { stopAlert() }
        } else {
            ui.settling = false; ui.settleFrom = [:]
        }
        refresh(force: true)
        ensureAnimTimer()
    }
}

// MARK: - Main

let app = NSApplication.shared
app.setActivationPolicy(.accessory)
let controller = AppController()
app.delegate = controller
app.run()
