// DesktopTip for macOS — native AppKit port of the Win32 version.
// Same TODO.txt format as Windows and the same config keys, stored in ~/Library/Application Support/DesktopTip/config.
//
// TODO.txt 行首：# 大标题 / > 次要说明 / ## 分组（### 子分组 …）/ ! 独立倒计时 / - 或 * 或直接文字 = 待办；
//   单独一行 ## 结束所有分组，单独一行 ### 结束二级及更深的分组。
// 行尾标记（顺序任意）：_OK 已完成、_HIDE 隐藏、_FOLD 折叠分组、_SHOW 分组折叠/隐藏时仍显示、
//   @YYYY-MM-DD HH:MM:SS 截止时间、/30m 到点后按间隔重复（s/m/h/d）。
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
    var displayMode = 2          // 0 = 附着桌面, 1 = 始终置顶, 2 = 智能浮现（新安装默认）
    var surfaceLead = 10         // 智能浮现：截止前多少分钟浮现
    var surfaceEvery = 45        // 智能浮现：每隔多少分钟轻浮现一次（0 = 关闭）
    var countdownRepeat = false  // 新建独立倒计时时「重复」的默认值
    var hoverHide = true         // 始终置顶时鼠标悬停隐藏（按住 ⌘ 可操作）
    var showHidden = false       // 临时显示带 _HIDE 的条目（淡显）
    var dueShowAbsolute = false  // 截止时间胶囊显示截止时刻（true）还是剩余时长（false）
    var dueInputMode = 1         // 倒计时对话框默认页：0 = 截止时间，1 = 剩余时长
    var sinkCompleted = false    // 勾选完成后是否把已完成项沉到段末（默认原位不动）

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
            case "SurfaceLead": c.surfaceLead = max(1, n)
            case "SurfaceEvery": c.surfaceEvery = max(0, n)
            case "CountdownRepeat": c.countdownRepeat = n != 0
            case "HoverHide": c.hoverHide = n != 0
            case "ShowHidden": c.showHidden = n != 0
            case "DueDisplay": c.dueShowAbsolute = n != 0
            case "DueInputMode": c.dueInputMode = n == 0 ? 0 : 1
            case "SinkCompleted": c.sinkCompleted = n != 0
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
        out += "HoverHide: \(hoverHide ? 1 : 0)\nShowHidden: \(showHidden ? 1 : 0)\nSinkCompleted: \(sinkCompleted ? 1 : 0)\n"
        out += "DueDisplay: \(dueShowAbsolute ? 1 : 0)\nDueInputMode: \(dueInputMode)\n"
        out += "SurfaceLead: \(surfaceLead)\nSurfaceEvery: \(surfaceEvery)\n"
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

func trimmed(_ s: String) -> String { s.trimmingCharacters(in: .whitespaces) }

func nowEpoch() -> Int { Int(Date().timeIntervalSince1970) }

// MARK: - Line model（与 Windows 版一致）

enum LineKind { case blank, heading, meta, group, groupEnd, todo, countdown }

struct LineInfo {
    var kind: LineKind = .blank
    var lead = ""          // 缩进 + 行首符号
    var text = ""          // 正文（去掉行首符号与行尾标记）
    var ok = false, hide = false, fold = false, show = false
    var level = 0          // 分组层级：## = 1，### = 2 …
    var hasDue = false
    var due = 0            // 截止时间（Unix 秒）
    var repeatSec = 0      // 重复间隔（秒）
}

let keyCDPrefix = "\u{1}C"      // 独立倒计时的键前缀
let keyGroupPrefix = "\u{1}G"   // 分组的键前缀
func isCountdownKey(_ k: String) -> Bool { k.hasPrefix(keyCDPrefix) }
func isGroupKey(_ k: String) -> Bool { k.hasPrefix(keyGroupPrefix) }
func keyDisplayName(_ k: String) -> String { (isCountdownKey(k) || isGroupKey(k)) ? String(k.dropFirst(2)) : k }

let dueRegex = try! NSRegularExpression(pattern: "^(\\d{4})[-/.](\\d{1,2})[-/.](\\d{1,2})(?:[ T\\t]+(\\d{1,2})[:：](\\d{2})(?:[:：](\\d{2}))?)?$")

/// 「YYYY-MM-DD」「YYYY-MM-DD HH:MM」「YYYY-MM-DD HH:MM:SS」；只写日期视为当天 23:59:59
func parseDueText(_ s: String) -> Int? {
    let s = trimmed(s)
    guard let m = dueRegex.firstMatch(in: s, range: NSRange(s.startIndex..., in: s)) else { return nil }
    func g(_ i: Int) -> Int? {
        let r = m.range(at: i)
        guard r.location != NSNotFound, let rr = Range(r, in: s) else { return nil }
        return Int(s[rr])
    }
    guard let y = g(1), let mo = g(2), let d = g(3) else { return nil }
    var h = 23, mi = 59, sec = 59
    if let hh = g(4), let mm = g(5) { h = hh; mi = mm; sec = g(6) ?? 0 }
    guard (1971...2999).contains(y), (1...12).contains(mo), (1...31).contains(d), h < 24, mi < 60, sec < 60 else { return nil }
    let comps = DateComponents(year: y, month: mo, day: d, hour: h, minute: mi, second: sec)
    guard let date = Calendar.current.date(from: comps) else { return nil }
    if Calendar.current.component(.day, from: date) != d { return nil }   // 2 月 30 日之类
    return Int(date.timeIntervalSince1970)
}

func makeFormatter(_ f: String) -> DateFormatter {
    let d = DateFormatter(); d.locale = Locale(identifier: "en_US_POSIX"); d.dateFormat = f; return d
}
let dueFormatter = makeFormatter("yyyy-MM-dd HH:mm:ss")
func formatDueText(_ due: Int) -> String { dueFormatter.string(from: Date(timeIntervalSince1970: TimeInterval(due))) }

func formatRepeatText(_ r: Int) -> String {
    if r % 86400 == 0 { return "\(r / 86400)d" }
    if r % 3600 == 0 { return "\(r / 3600)h" }
    if r % 60 == 0 { return "\(r / 60)m" }
    return "\(r)s"
}

func stripSuffix(_ s: inout String, _ tok: String) -> Bool {
    s = trimRight(s)
    guard s.hasSuffix(tok) else { return false }
    s = trimRight(String(s.dropLast(tok.count)))
    return true
}

/// 行尾「@日期 [时间]」；@ 前须为空白或位于正文开头
func stripDue(_ s: inout String) -> Int? {
    s = trimRight(s)
    guard let at = s.lastIndex(of: "@") else { return nil }
    if at > s.startIndex {
        let prev = s[s.index(before: at)]
        if prev != " " && prev != "\t" { return nil }
    }
    guard let due = parseDueText(String(s[s.index(after: at)...])) else { return nil }
    s = trimRight(String(s[..<at]))
    return due
}

/// 行尾「/30m」之类的重复间隔
func stripRepeat(_ s: inout String) -> Int? {
    s = trimRight(s)
    let sp = s.lastIndex(where: { $0 == " " || $0 == "\t" })
    let tok = sp.map { String(s[s.index(after: $0)...]) } ?? s
    guard tok.count >= 3, tok.first == "/", let u = tok.last else { return nil }
    let mul = u == "s" ? 1 : u == "m" ? 60 : u == "h" ? 3600 : u == "d" ? 86400 : 0
    guard mul > 0, let n = Int(tok.dropFirst().dropLast()), n > 0, n <= 1_000_000 else { return nil }
    s = sp.map { trimRight(String(s[..<$0])) } ?? ""
    return n * mul
}

func stripFlags(_ body: inout String, _ L: inout LineInfo, ok allowOk: Bool, due allowDue: Bool, fold allowFold: Bool) {
    var changed = true
    while changed {
        changed = false
        if allowOk, stripSuffix(&body, "_OK") { L.ok = true; changed = true; continue }
        if stripSuffix(&body, "_HIDE") { L.hide = true; changed = true; continue }
        if allowOk, stripSuffix(&body, "_SHOW") { L.show = true; changed = true; continue }
        if allowFold, stripSuffix(&body, "_FOLD") { L.fold = true; changed = true; continue }
        if allowDue, !L.hasDue {
            var tmp = body
            let rep = stripRepeat(&tmp)
            if let d = stripDue(&tmp) {
                L.hasDue = true; L.due = d; L.repeatSec = rep ?? 0
                body = tmp; changed = true; continue
            }
        }
    }
}

func parseLine(_ raw: String) -> LineInfo {
    var L = LineInfo()
    let s = trimRight(raw)
    let indent = String(s.prefix(while: { $0 == " " || $0 == "\t" }))
    let t = String(s.dropFirst(indent.count))
    guard let first = t.first else { return L }

    if t.hasPrefix("##") {
        let hashes = t.prefix(while: { $0 == "#" }).count
        var body = trimmed(String(t.dropFirst(hashes)))
        L.level = hashes - 1
        L.lead = indent + String(repeating: "#", count: hashes) + " "
        if body.isEmpty { L.kind = .groupEnd; return L }
        L.kind = .group
        stripFlags(&body, &L, ok: false, due: false, fold: true)
        L.text = trimmed(body)
        return L
    }
    if first == "#" || first == ">" {
        L.kind = first == "#" ? .heading : .meta
        let n = (t.dropFirst().first == " ") ? 2 : 1
        L.lead = indent + String(t.prefix(n))
        var body = String(t.dropFirst(n))
        stripFlags(&body, &L, ok: false, due: false, fold: false)
        L.text = body
        return L
    }
    if first == "!" {
        var C = LineInfo()
        var body = trimmed(String(t.dropFirst()))
        stripFlags(&body, &C, ok: true, due: true, fold: false)
        if C.hasDue {
            C.kind = .countdown; C.lead = indent + "! "; C.text = trimmed(body)
            return C
        }
        // 没有截止时间的 ! 行仍按普通待办处理（兼容旧文件）
    }
    L.kind = .todo
    var body: String
    if (first == "-" || first == "*"), t.count > 1, t[t.index(after: t.startIndex)] == " " {
        L.lead = indent + String(t.prefix(2)); body = String(t.dropFirst(2))
    } else {
        L.lead = indent; body = t
    }
    stripFlags(&body, &L, ok: true, due: true, fold: false)
    L.text = body
    return L
}

func formatLine(_ L: LineInfo) -> String {
    var s = L.lead + L.text
    switch L.kind {
    case .blank: return ""
    case .groupEnd: return String(repeating: "#", count: max(2, L.level + 1))
    case .heading, .meta:
        if L.hide { s += " _HIDE" }
        return s
    case .group:
        if L.hide { s += " _HIDE" }
        if L.fold { s += " _FOLD" }
        return s
    default: break
    }
    var marker = false
    if L.hasDue { s += " @" + formatDueText(L.due); marker = true }
    if L.hasDue && L.repeatSec > 0 { s += " /" + formatRepeatText(L.repeatSec) }
    if L.hide { s += " _HIDE"; marker = true }
    if L.show { s += " _SHOW"; marker = true }
    if L.ok { s += marker ? " _OK" : "_OK" }
    return s
}

/// 条目键：任务 = 正文；独立倒计时 / 分组带内部前缀
func itemKey(_ L: LineInfo) -> String {
    switch L.kind {
    case .todo: return trimmed(L.text)
    case .countdown: return keyCDPrefix + trimmed(L.text)
    case .group: return keyGroupPrefix + trimmed(L.text)
    default: return ""
    }
}

/// 只在同一段内（分组标题 / 分组结束 / # 大标题 都开启新段）把已完成的待办排到未完成之后
func sortByCompletion(_ lines: inout [String]) {
    var slots: [Int] = [], open: [String] = [], done: [String] = []
    func flush() {
        for (n, l) in (open + done).enumerated() { lines[slots[n]] = l }
        slots = []; open = []; done = []
    }
    for i in lines.indices {
        let L = parseLine(lines[i])
        if L.kind == .group || L.kind == .groupEnd || L.kind == .heading { flush(); continue }
        guard L.kind == .todo else { continue }
        slots.append(i)
        if L.ok { done.append(lines[i]) } else { open.append(lines[i]) }
    }
    flush()
}

/// 按键找到条目所在行；hint 为行号提示
func findItemLine(_ lines: [String], _ hint: Int, _ key: String) -> Int? {
    guard !key.isEmpty else { return nil }
    if hint >= 0, hint < lines.count, itemKey(parseLine(lines[hint])) == key { return hint }
    return lines.firstIndex { itemKey(parseLine($0)) == key }
}

func toggleTodo(at index: Int) -> Bool {
    guard var lines = readTodoLines(), index >= 0, index < lines.count else { return false }
    var L = parseLine(lines[index])
    guard L.kind == .todo else { return false }
    L.ok.toggle()
    lines[index] = formatLine(L)
    if cfg.sinkCompleted { sortByCompletion(&lines) }   // 默认不移动：已完成项留在原位
    return writeTodoLines(lines)
}

/// 按任务文本切换完成状态（hint 只是行号提示，文件被改过也能找到正确的行）
func toggleTodo(key: String, hint: Int) -> Bool {
    guard let lines = readTodoLines(), let idx = findItemLine(lines, hint, key), parseLine(lines[idx]).kind == .todo else { return false }
    return toggleTodo(at: idx)
}

/// 修改条目所在的行；fn 返回 false 表示删除该行
@discardableResult
func editItemLine(_ hint: Int, _ key: String, _ fn: (inout LineInfo) -> Bool) -> Bool {
    guard var lines = readTodoLines(), let idx = findItemLine(lines, hint, key) else { return false }
    var L = parseLine(lines[idx])
    if fn(&L) { lines[idx] = formatLine(L) } else { lines.remove(at: idx) }
    return writeTodoLines(lines)
}

/// 新建独立倒计时：放在已有倒计时之后；没有的话放在第一条待办 / 分组之前；都没有就追加到末尾
func insertCountdownLine(_ name: String, _ due: Int, _ rep: Int) -> Bool {
    guard var lines = readTodoLines() else { return false }
    var C = LineInfo()
    C.kind = .countdown; C.lead = "! "; C.text = name; C.hasDue = true; C.due = due; C.repeatSec = rep
    var lastCd = -1, firstList = -1
    for (i, l) in lines.enumerated() {
        let L = parseLine(l)
        if L.kind == .group { if firstList < 0 { firstList = i }; break }
        if L.kind == .countdown { lastCd = i }
        if L.kind == .todo && firstList < 0 { firstList = i }
    }
    var pos: Int
    if lastCd >= 0 { pos = lastCd + 1 } else if firstList >= 0 { pos = firstList } else {
        pos = lines.count
        while pos > 0 && trimmed(lines[pos - 1]).isEmpty { pos -= 1 }
    }
    lines.insert(formatLine(C), at: pos)
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
        let sample = "# {{TIME}}\n> {{DATE}}  {{WEEK}}\n## 工作\n- 项目：撰写结题报告\n- 论文：提升论文质量\n## 生活 _FOLD\n- 买菜\n##\n> 本周剩 {{DAYS_LEFT_WEEK}} · 本月剩 {{DAYS_LEFT_MONTH}} · 本年剩 {{DAYS_LEFT_YEAR}} 天\n"
        try? sample.write(toFile: cfg.todoPath, atomically: true, encoding: .utf8)
    }
}

// MARK: - Stopwatch（每项任务的正计时）
// 保存在 ~/Library/Application Support/DesktopTip/timers，与 Windows 版格式相同：
// 每行 kind\tduration\tstart\taccum\tfired\t任务文本；kind = up(正计时)。
// 旧版的 down（倒计时）记录在启动时换算成截止时间写进 TODO 文件。

let timersURL = supportDir.appendingPathComponent("timers")
let duesURL = supportDir.appendingPathComponent("deadlines")

struct TaskTimer {
    var countdown = false
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

func timerIndex(_ key: String) -> Int? { taskTimers.firstIndex { $0.key == key && !$0.countdown } }
func timer(for key: String) -> TaskTimer? { timerIndex(key).map { taskTimers[$0] } }

func startTaskStopwatch(_ key: String) {
    let i = timerIndex(key) ?? { taskTimers.append(TaskTimer(key: key)); return taskTimers.count - 1 }()
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

// MARK: - Deadline state（不写进 TODO 文件的部分）
// ~/Library/Application Support/DesktopTip/deadlines，每行：due\tsetAt\tfired\t条目键（与 Windows 相同）

struct DueMeta { var due: Int; var setAt: Int; var fired: Bool; var key: String; var seen = false }

func keyForFile(_ k: String) -> String {
    if isCountdownKey(k) { return "!C:" + k.dropFirst(2) }
    if isGroupKey(k) { return "!G:" + k.dropFirst(2) }
    return "T:" + k
}
func keyFromFile(_ s: String) -> String {
    if s.hasPrefix("!C:") { return keyCDPrefix + s.dropFirst(3) }
    if s.hasPrefix("!G:") { return keyGroupPrefix + s.dropFirst(3) }
    if s.hasPrefix("T:") { return String(s.dropFirst(2)) }
    return s
}

func loadDueMeta() -> [DueMeta] {
    guard let d = FileManager.default.contents(atPath: duesURL.path) else { return [] }
    var out: [DueMeta] = []
    for line in splitLines(decodeText(d)) {
        let p = line.components(separatedBy: "\t")
        guard p.count >= 4, let due = Int(p[0]), let setAt = Int(p[1]) else { continue }
        out.append(DueMeta(due: due, setAt: setAt, fired: p[2] == "1", key: keyFromFile(p[3...].joined(separator: "\t"))))
    }
    return out
}

var dueMeta: [DueMeta] = loadDueMeta()

func saveDueMeta() {
    let s = dueMeta.map { "\($0.due)\t\($0.setAt)\t\($0.fired ? 1 : 0)\t\(keyForFile($0.key))\n" }.joined()
    try? FileManager.default.createDirectory(at: supportDir, withIntermediateDirectories: true)
    try? s.write(to: duesURL, atomically: true, encoding: .utf8)
}

func dueMetaIndex(_ key: String, _ due: Int) -> Int? { dueMeta.firstIndex { $0.due == due && $0.key == key } }

/// 通过界面设定截止时间后调用：进度环从现在开始走，且可以再次提醒
func noteDueSet(_ key: String, _ due: Int) {
    if let i = dueMetaIndex(key, due) { dueMeta[i].setAt = nowEpoch(); dueMeta[i].fired = false }
    else { dueMeta.append(DueMeta(due: due, setAt: nowEpoch(), fired: false, key: key)) }
    saveDueMeta()
}

func dueSetAt(_ key: String, _ due: Int) -> Int { dueMetaIndex(key, due).map { dueMeta[$0].setAt } ?? 0 }

/// 旧版任务倒计时（timers 里的 down 记录）→ 换算成截止时间写进 TODO 文件
func migrateLegacyCountdowns() {
    guard taskTimers.contains(where: { $0.countdown }), var lines = readTodoLines() else { return }
    let now = nowEpoch()
    var changed = false
    for t in taskTimers where t.countdown && !t.fired {
        guard let idx = findItemLine(lines, -1, t.key) else { continue }
        var L = parseLine(lines[idx])
        guard L.kind == .todo, !L.hasDue, !L.ok else { continue }
        L.hasDue = true; L.due = now + max(0, t.duration - t.elapsed); L.repeatSec = 0
        lines[idx] = formatLine(L); changed = true
        noteDueSet(t.key, L.due)
    }
    if changed && !writeTodoLines(lines) { return }
    taskTimers.removeAll { $0.countdown }
    saveTaskTimers()
}

func setItemDue(_ hint: Int, _ key: String, _ due: Int, keepRepeat: Bool = true, repeatSec: Int = 0) {
    let ok = editItemLine(hint, key) { L in
        guard L.kind == .todo || L.kind == .countdown else { return true }
        L.hasDue = true; L.due = due
        if !keepRepeat { L.repeatSec = repeatSec }
        return true
    }
    if ok { noteDueSet(key, due) }
}

/// 在 base 上加 months 个自然月（月末自动收拢）
func addMonths(_ base: Int, _ months: Int) -> Int {
    guard months > 0, let d = Calendar.current.date(byAdding: .month, value: months, to: Date(timeIntervalSince1970: TimeInterval(base))) else { return base }
    return Int(d.timeIntervalSince1970)
}

/// 把 now → due 拆成 月 / 天 / 时 / 分 / 秒
func splitRemain(_ now: Int, _ due: Int) -> [Int] {
    guard due > now else { return [0, 0, 0, 0, 0] }
    var months = 0
    while months < 1200 && addMonths(now, months + 1) <= due { months += 1 }
    let rest = max(0, due - addMonths(now, months))
    return [months, rest / 86400, (rest % 86400) / 3600, (rest % 3600) / 60, rest % 60]
}

func formatClock(_ s: Int) -> String {
    let s = max(0, s), h = s / 3600, m = (s % 3600) / 60, sec = s % 60
    return h > 0 ? String(format: "%d:%02d:%02d", h, m, sec) : String(format: "%02d:%02d", m, sec)
}

/// 剩余时长：< 1 天用时钟格式，>= 1 天用「N天 hh:mm」
func formatRemain(_ s: Int) -> String {
    let s = abs(s)
    if s < 86400 { return formatClock(s) }
    let d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60
    return d < 100 ? String(format: "%d天 %02d:%02d", d, h, m) : "\(d)天"
}

/// 截止时刻：今天 / 明天 / 今年内 / 更远
func formatDueShort(_ due: Int) -> String {
    let cal = Calendar.current
    let d = Date(timeIntervalSince1970: TimeInterval(due)), now = Date()
    let hm = makeFormatter("HH:mm").string(from: d)
    if cal.isDateInToday(d) { return "今天 " + hm }
    if cal.isDateInTomorrow(d) { return "明天 " + hm }
    if cal.component(.year, from: d) == cal.component(.year, from: now) { return makeFormatter("MM-dd HH:mm").string(from: d) }
    return makeFormatter("yyyy-MM-dd").string(from: d)
}

// MARK: - Animation / alert state

let completeAnimSec = 0.70      // 完成动画
let settleAnimSec = 0.32        // 行位置变化时的滑动
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
extension NSColor {
    /// 在现有 alpha 上再乘一个系数（隐藏项淡显用）
    func faded(_ f: Double) -> NSColor { withAlphaComponent(alphaComponent * CGFloat(f)) }
}

let accentDone = rgb(52, 211, 153)    // 完成：翡翠绿
let accentGold = rgb(253, 224, 71)
let coral = rgb(255, 107, 107)        // 到点：珊瑚红
let amber = rgb(251, 191, 36)         // 快到点：琥珀
let sky = rgb(125, 211, 252)          // 倒计时：天蓝
let violet = rgb(196, 181, 253)       // 正计时：淡紫
func toneColor(_ t: Int) -> NSColor { t == 2 ? coral : t == 1 ? amber : t == 3 ? violet : sky }

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

/// 对勾（按路径长度比例 t 逐步描绘）；(x, y) 为复选框在翻转坐标中的原点
func drawTick(_ x: CGFloat, _ y: CGFloat, _ d: CGFloat, _ t: Double, _ color: NSColor, _ width: CGFloat) {
    guard t > 0 else { return }
    let p0 = NSPoint(x: x + d * 0.27, y: y + d * 0.52), p1 = NSPoint(x: x + d * 0.44, y: y + d * 0.68)
    let p2 = NSPoint(x: x + d * 0.74, y: y + d * 0.34)
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

func digitsTo8(_ s: String) -> String { String(s.map { $0.isASCII && $0.isNumber ? "8" : $0 }) }

// MARK: - Card view

enum RowType { case spacer, heading, meta, todo, countdown, group }
struct Row {
    var type: RowType
    var text = ""
    var w: CGFloat = 0, h: CGFloat = 0, y: CGFloat = 0
    var tw: CGFloat = 0          // 纯文字宽度（删除线动画用）
    var source = -1
    var done = false
    var key = ""                 // 条目键
    var seg = 0                  // 所在段（分组）
    var depth = 0                // 嵌套深度（缩进）
    var fade = 1.0               // 隐藏项淡显
    var hasDue = false
    var due = 0
    // 分组
    var folded = false
    var openCount = 0
    var nearestDue = 0
    var summary = ""
    // 计时胶囊
    var pill = false
    var pillDue = false
    var pillRunning = true
    var pillExpired = false
    var pillFrac = 0.0
    var pillTone = 0             // 0 常态 / 1 快到点 / 2 已到点 / 3 正计时
    var pillLabel = ""
}

struct HitRect { var rect: NSRect; var source: Int; var key: String; var done: Bool; var kind = 0 }

final class CardView: NSView {
    static let margin: (CGFloat) -> CGFloat = { $0 * 2.1 }
    var rows: [Row] = []
    var dividerY: CGFloat = -1
    var cardSize = NSSize(width: 100, height: 60)
    var hitRects: [HitRect] = []          // 复选框
    var rowRects: [HitRect] = []          // 整行（右键菜单）
    var pillRects: [HitRect] = []         // 计时胶囊
    var groupRects: [HitRect] = []        // 分组标题（单击折叠/展开）
    var bannerButtons: [(rect: NSRect, action: BannerAction)] = []
    var hasDuePill = false
    weak var controller: AppController?

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
    var checkD: CGFloat { max(12, (base * 0.62).rounded(.towardZero)) }
    var checkGap: CGFloat { (base * 0.50).rounded(.towardZero) }
    var indentStep: CGFloat { base * 0.95 }
    var pillH: CGFloat { base * 1.0 }
    var ringD: CGFloat { base * 0.62 }
    var pillPadX: CGFloat { base * 0.34 }
    var pillInnerGap: CGFloat { base * 0.28 }
    var pillGap: CGFloat { base * 0.75 }
    var bPad: CGFloat { base * 0.60 }
    var iconD: CGFloat { base * 1.30 }
    var btnH: CGFloat { base * 1.15 }
    var btnPadX: CGFloat { base * 0.70 }
    var btnGap: CGFloat { base * 0.30 }
    var timerFont: NSFont { NSFont.monospacedDigitSystemFont(ofSize: base * 0.64, weight: .regular) }
    var groupFont: NSFont { makeBoldFont(cfg.fontFamily, base * 0.66) }
    var bTitleFont: NSFont { makeBoldFont(cfg.fontFamily, base * 0.84) }
    var bDetailFont: NSFont { makeFont(cfg.fontFamily, base * 0.62) }
    var btnFont: NSFont { makeFont(cfg.fontFamily, base * 0.58) }

    func attrs(_ font: NSFont, color: NSColor, strike: Bool = false) -> [NSAttributedString.Key: Any] {
        var a: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: color]
        if strike { a[.strikethroughStyle] = NSUnderlineStyle.single.rawValue }
        return a
    }

    func textSize(_ s: String, _ f: NSFont) -> NSSize { NSAttributedString(string: s, attributes: [.font: f]).size() }

    func dueLabel(_ due: Int) -> String {
        let rem = due - nowEpoch()
        if cfg.dueShowAbsolute { return formatDueShort(due) }
        return rem > 0 ? formatRemain(rem) : "+" + formatRemain(-rem)
    }

    /// 读文件 → 多级分组 / 折叠 / 隐藏 / _SHOW → 行模型
    func buildRows(_ raw: [String], ok: Bool) -> [Row] {
        var rs: [Row] = []
        struct Frame { var level: Int; var row: Int; var hidden: Bool; var folded: Bool; var fade: Double }
        var stack: [Frame] = []
        var seg = 0
        func popTo(_ level: Int) { while let l = stack.last, l.level >= level { stack.removeLast() } }
        func ancestors() -> (hidden: Bool, folded: Bool, fade: Double, summaryRow: Int) {
            var h = false, f = false, fd = 1.0, sr = -1
            for fr in stack {
                if fr.hidden { h = true }
                if fr.folded { if !f && !h && fr.row >= 0 { sr = fr.row }; f = true }
                fd *= fr.fade
            }
            return (h, f, fd, sr)
        }
        for (i, line) in raw.enumerated() {
            var r = Row(type: .spacer)
            r.source = ok ? i : -1
            if !ok { r.type = .meta; r.text = line; rs.append(r); continue }
            let L = parseLine(line)
            if L.kind == .groupEnd { popTo(L.level); seg += 1; continue }
            if L.kind == .heading { stack.removeAll(); seg += 1 }
            if L.kind == .group {
                popTo(L.level)
                let a = ancestors()
                let blocked = (a.hidden && !cfg.showHidden) || a.folded
                seg += 1
                var fr = Frame(level: L.level, row: -1, hidden: L.hide, folded: L.fold, fade: L.hide ? 0.42 : 1)
                if !blocked && !(L.hide && !cfg.showHidden) {
                    r.type = .group
                    r.text = processPlaceholders(L.text)
                    if r.text.isEmpty { r.text = "未命名分组" }
                    r.key = itemKey(L); r.folded = L.fold
                    r.fade = a.fade * fr.fade; r.seg = seg; r.depth = stack.count
                    rs.append(r)
                    fr.row = rs.count - 1
                }
                stack.append(fr)
                continue
            }
            let a = ancestors()
            let blocked = (a.hidden && !cfg.showHidden) || a.folded
            let isItem = L.kind == .todo || L.kind == .countdown
            let selfHidden = L.hide && !cfg.showHidden
            let pinned = isItem && L.show && !selfHidden     // _SHOW：分组折叠或隐藏时仍显示
            if (blocked && !pinned) || selfHidden {
                if a.folded, a.summaryRow >= 0, isItem, !L.ok, !L.hide {
                    if L.kind == .todo { rs[a.summaryRow].openCount += 1 }
                    if L.hasDue, rs[a.summaryRow].nearestDue == 0 || L.due < rs[a.summaryRow].nearestDue { rs[a.summaryRow].nearestDue = L.due }
                }
                continue
            }
            r.seg = seg; r.depth = stack.count
            r.fade = (pinned && blocked ? 1 : a.fade) * (L.hide ? 0.42 : 1)
            switch L.kind {
            case .blank: r.type = .spacer
            case .heading: r.type = .heading; r.text = processPlaceholders(L.text); r.depth = 0
            case .meta: r.type = .meta; r.text = processPlaceholders(L.text)
            case .countdown:
                r.type = .countdown
                r.text = processPlaceholders(trimmed(L.text)); if r.text.isEmpty { r.text = "倒计时" }
                r.key = itemKey(L); r.done = L.ok; r.hasDue = true; r.due = L.due
            default:
                r.type = .todo
                r.text = processPlaceholders(L.text)
                r.key = itemKey(L); r.done = L.ok; r.hasDue = L.hasDue; r.due = L.due
            }
            rs.append(r)
        }
        // 开启「已完成项沉底」时：已完成排在后面（只在同一段内）
        if cfg.sinkCompleted {
            var bySeg: [Int: [Int]] = [:]
            for i in rs.indices where rs[i].type == .todo { bySeg[rs[i].seg, default: []].append(i) }
            for (_, slots) in bySeg {
                let sorted = slots.map { rs[$0] }.enumerated().sorted { a, b in
                    a.element.done != b.element.done ? !a.element.done : a.offset < b.offset
                }.map { $0.element }
                for (n, ix) in slots.enumerated() { rs[ix] = sorted[n] }
            }
        }
        return rs
    }

    /// Rebuilds rows and layout; returns the full window size.
    func relayout() -> NSSize {
        let base = self.base
        let fileLines = readTodoLines()
        let ok = fileLines != nil
        var rs = buildRows(fileLines ?? ["Error: Unable to open file!"], ok: ok)
        let now = nowEpoch()

        let fHead = makeFont(cfg.fontFamily, base * 2.0, light: true)
        let fText = makeFont(cfg.fontFamily, base)
        let fMeta = makeFont(cfg.fontFamily, base * 0.64)

        let PADX = (base * 1.15).rounded(.towardZero), PADTOP = (base * 0.95).rounded(.towardZero), PADBOT = (base * 1.05).rounded(.towardZero)
        let divGap = (base * 0.80).rounded(.towardZero), metaGap = (base * 0.28).rounded(.towardZero), headGap = (base * 0.12).rounded(.towardZero)
        let groupGapTop = (base * 0.62).rounded(.towardZero), groupGapBot = (base * 0.30).rounded(.towardZero)
        let todoExtra = CGFloat(max(0, cfg.lineHeight - cfg.fontHeight))
        let todoGap = (base * 0.45).rounded(.towardZero) + todoExtra

        // 计时胶囊内容：截止时间优先；任务完成后只保留正计时的最终用时
        var pw: CGFloat = 0
        hasDuePill = false
        for i in rs.indices {
            if rs[i].type == .group && rs[i].folded {
                var s = rs[i].openCount > 0 ? "\(rs[i].openCount) 项" : ""
                if rs[i].nearestDue > 0 { s += (s.isEmpty ? "" : "  ·  ") + dueLabel(rs[i].nearestDue); hasDuePill = true }
                rs[i].summary = s
                continue
            }
            guard rs[i].type == .todo || rs[i].type == .countdown else { continue }
            if rs[i].hasDue && !rs[i].done {
                let rem = rs[i].due - now
                let setAt = dueSetAt(rs[i].key, rs[i].due)
                let total = (setAt > 0 && rs[i].due > setAt) ? rs[i].due - setAt : 0
                rs[i].pill = true; rs[i].pillDue = true; rs[i].pillRunning = true
                rs[i].pillExpired = rem <= 0
                rs[i].pillFrac = rem <= 0 ? 1 : (total > 0 ? clamp01(Double(rem) / Double(total)) : 1)
                rs[i].pillTone = rem <= 0 ? 2 : ((rem <= 60 || (total > 0 && rem * 5 <= total)
                    || (cfg.displayMode == 2 && rem <= cfg.surfaceLead * 60)) ? 1 : 0)
                rs[i].pillLabel = dueLabel(rs[i].due)
                hasDuePill = true
            } else if rs[i].type == .todo, let t = timer(for: rs[i].key) {
                rs[i].pill = true; rs[i].pillDue = false; rs[i].pillRunning = t.running
                rs[i].pillFrac = Double(t.elapsed % 60) / 60
                rs[i].pillTone = 3
                rs[i].pillLabel = formatClock(t.elapsed)
            }
            if rs[i].pill {
                pw = max(pw, pillPadX * 2 + ringD + pillInnerGap + ceil(textSize(digitsTo8(rs[i].pillLabel), timerFont).width))
            }
        }
        pillColW = pw

        var contentW = base * 4
        for i in rs.indices {
            if rs[i].type == .spacer { rs[i].h = base * 0.45; continue }
            let f = rs[i].type == .heading ? fHead : rs[i].type == .meta ? fMeta : rs[i].type == .group ? groupFont : fText
            let sz = textSize(rs[i].text, f)
            rs[i].h = ceil(sz.height)
            rs[i].tw = ceil(sz.width)
            rs[i].w = ceil(sz.width) + CGFloat(rs[i].depth) * indentStep
            if [.todo, .countdown, .group].contains(rs[i].type) { rs[i].w += checkD + checkGap }
            if rs[i].pill { rs[i].w += pillGap + pillColW }
            if rs[i].type == .group, !rs[i].summary.isEmpty { rs[i].w += pillGap + ceil(textSize(digitsTo8(rs[i].summary), fMeta).width) }
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
            bHeadBlock = max(iconD, bTitleH + base * 0.10 + bDetailH)
            bannerH = bPad + bHeadBlock + base * 0.55 + btnH + bPad
            contentW = max(contentW, ceil(need))
        }

        var y = PADTOP, divY: CGFloat = -1, placed = false, seen = ui.alertActive, seenList = false, lastGap: CGFloat = 0
        bannerY = PADTOP
        if ui.alertActive { y += bannerH + base * 0.85 }
        for i in rs.indices {
            if rs[i].type == .spacer { y += rs[i].h; lastGap = 0; continue }
            let listRow = [.todo, .countdown, .group].contains(rs[i].type)
            if listRow && !placed {
                if seen { y += divGap; divY = y; y += 1 + divGap }
                placed = true
            } else if rs[i].type == .group && seenList {
                y += groupGapTop - lastGap * 0.5
            }
            rs[i].y = y
            switch rs[i].type {
            case .heading: y += rs[i].h + headGap; lastGap = headGap
            case .meta: y += rs[i].h + metaGap; lastGap = metaGap
            case .group: y += rs[i].h + groupGapBot; lastGap = groupGapBot
            default: y += max(rs[i].h, checkD) + todoGap; lastGap = todoGap
            }
            seen = true
            if listRow { seenList = true }
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
        sh.shadowColor = NSColor.black.withAlphaComponent(0.45)
        sh.set()
        NSColor.black.setFill()
        cardPath.fill()
        ctx.restoreGState()

        cfg.bgColor.withAlphaComponent(CGFloat(cfg.bgAlpha) / 255).setFill()
        cardPath.fill()
        // 顶部微光
        NSGradient(starting: NSColor.white.withAlphaComponent(0.06), ending: NSColor.white.withAlphaComponent(0))?
            .draw(in: cardPath, angle: 90)
        NSColor.white.withAlphaComponent(0.1).setStroke()
        cardPath.lineWidth = 1
        cardPath.stroke()

        // 提醒中：卡片边缘呼吸光
        if ui.alertActive {
            for k in stride(from: 3, through: 1, by: -1) {
                coral.withAlphaComponent(CGFloat((18 + 30 * pulse) * Double(4 - k) / 3 / 255)).setStroke()
                cardPath.lineWidth = CGFloat(k) * 2.2
                cardPath.stroke()
            }
            rgb(255, 128, 112, CGFloat((90 + 110 * pulse) / 255)).setStroke()
            cardPath.lineWidth = 1.4
            cardPath.stroke()
        }

        let tc = cfg.textColor
        func scaled(_ k: CGFloat, _ a: CGFloat) -> NSColor {
            NSColor(srgbRed: tc.redComponent * k, green: tc.greenComponent * k, blue: tc.blueComponent * k, alpha: a)
        }
        let ink = tc, checkc = tc.withAlphaComponent(0.78)
        let doneInk = scaled(0.62, 0.6), doneCheck = scaled(0.58, 0.6), doneFill = scaled(0.5, 0.16)
        let metaC = rgb(160, 167, 182)
        let groupInk = lerpColor(metaC, ink, 0.3)
        let fHead = makeFont(cfg.fontFamily, base * 2, light: true), fText = makeFont(cfg.fontFamily, base)
        let fMeta = makeFont(cfg.fontFamily, base * 0.64)
        let PADX = (base * 1.15).rounded(.towardZero)
        let checkD = self.checkD, checkGap = self.checkGap
        let sdy = max(1, base * 0.045)
        let stroke = max(1.2, base * 0.058)
        let nowE = nowEpoch()

        func drawText(_ s: String, _ f: NSFont, _ x: CGFloat, _ y: CGFloat, _ c: NSColor, strike: Bool = false) {
            if s.isEmpty { return }
            NSAttributedString(string: s, attributes: attrs(f, color: NSColor.black.withAlphaComponent(0.32 * c.alphaComponent), strike: strike))
                .draw(at: NSPoint(x: x, y: y + sdy))
            NSAttributedString(string: s, attributes: attrs(f, color: c, strike: strike)).draw(at: NSPoint(x: x, y: y))
        }
        func circle(_ cx: CGFloat, _ cy: CGFloat, _ r: CGFloat) -> NSBezierPath {
            NSBezierPath(ovalIn: NSRect(x: cx - r, y: cy - r, width: r * 2, height: r * 2))
        }

        let bx = M + PADX
        let rightX = M + cardSize.width - PADX
        if dividerY >= 0 {
            NSGradient(starting: NSColor.white.withAlphaComponent(0.14), ending: NSColor.white.withAlphaComponent(0))?
                .draw(in: NSRect(x: bx, y: M + dividerY, width: rightX - bx, height: 1), angle: 0)
        }

        // ---- 提醒横幅：卡片色调的玻璃面板 + 珊瑚色描边 ----
        bannerButtons.removeAll()
        if ui.alertActive {
            let e = easeOutCubic(alertT / 0.38)                     // 入场：下滑 + 淡入
            let A = CGFloat(e)
            let bw = rightX - bx
            let byT = M + bannerY - CGFloat(1 - e) * base * 0.6
            let br = base * 0.5
            let bp = NSBezierPath(roundedRect: NSRect(x: bx, y: byT, width: bw, height: bannerH), xRadius: br, yRadius: br)
            coral.withAlphaComponent(A * CGFloat(0.10 + 0.05 * pulse)).setFill(); bp.fill()
            rgb(255, 128, 112, A * CGFloat(0.45 + 0.25 * pulse)).setStroke(); bp.lineWidth = 1.2; bp.stroke()

            // 图标：细线圆环 + 指针（抖动时轻轻摇摆）
            let icx = bx + bPad + iconD / 2, icy = byT + bPad + bHeadBlock / 2
            var wig = 0.0
            if ui.shaking {
                let st = now.timeIntervalSince(ui.shakeStart)
                wig = 16 * sin(st * 2 * .pi * 6) * (1 - clamp01(st / shakeSec))
            }
            NSGraphicsContext.saveGraphicsState()
            let xf = NSAffineTransform()
            xf.translateX(by: icx, yBy: icy)
            xf.rotate(byDegrees: CGFloat(wig))
            xf.concat()
            coral.withAlphaComponent(0.18 * A).setFill()
            circle(0, 0, iconD / 2).fill()
            let cr = iconD * 0.26, sw = max(1.3, base * 0.065)
            rgb(255, 150, 140, A).setStroke()
            let ring = circle(0, 0, cr); ring.lineWidth = sw; ring.stroke()
            let hands = NSBezierPath()
            hands.move(to: NSPoint(x: 0, y: -cr * 0.58)); hands.line(to: .zero); hands.line(to: NSPoint(x: cr * 0.42, y: cr * 0.24))
            hands.lineWidth = sw; hands.lineCapStyle = .round; hands.lineJoinStyle = .round; hands.stroke()
            NSGraphicsContext.restoreGraphicsState()

            let tx = bx + bPad + iconD + bPad * 0.8
            let ty = byT + bPad + (bHeadBlock - (bTitleH + base * 0.10 + bDetailH)) / 2
            NSAttributedString(string: ui.alertTitle, attributes: attrs(bTitleFont, color: ink.withAlphaComponent(A))).draw(at: NSPoint(x: tx, y: ty))
            NSAttributedString(string: ui.alertDetail, attributes: attrs(bDetailFont, color: metaC.withAlphaComponent(0.95 * A)))
                .draw(at: NSPoint(x: tx, y: ty + bTitleH + base * 0.10))

            // 按钮（右对齐；第一个为主按钮）
            let byB = byT + bannerH - bPad - btnH
            var cx = bx + bw - bPad
            for i in stride(from: btnSpec.count - 1, through: 0, by: -1) where i < btnW.count {
                cx -= btnW[i]
                let primary = i == 0
                let r = NSRect(x: cx, y: byB, width: btnW[i], height: btnH)
                (primary ? coral.withAlphaComponent(0.92 * A) : tc.withAlphaComponent(0.10 * A)).setFill()
                NSBezierPath(roundedRect: r, xRadius: btnH / 2, yRadius: btnH / 2).fill()
                let label = NSAttributedString(string: btnSpec[i].title,
                                               attributes: attrs(btnFont, color: primary ? NSColor.white.withAlphaComponent(A) : ink.withAlphaComponent(0.9 * A)))
                let ls = label.size()
                label.draw(at: NSPoint(x: r.midX - ls.width / 2, y: r.midY - ls.height / 2))
                bannerButtons.append((r, btnSpec[i].action))
                cx -= btnGap
            }
        }

        // ---- 各行 ----
        hitRects.removeAll(); rowRects.removeAll(); pillRects.removeAll(); groupRects.removeAll()
        var rowY: [String: CGFloat] = [:]
        let settleT = ui.settling ? easeOutCubic(now.timeIntervalSince(ui.settleStart) / settleAnimSec) : 1
        let animP = ui.animKey != nil ? clamp01(now.timeIntervalSince(ui.animStart) / completeAnimSec) : 0

        for r in rows {
            var ry = M + r.y
            let fd = r.fade
            switch r.type {
            case .spacer: continue
            case .heading: drawText(r.text, fHead, bx, ry, ink.faded(fd)); continue
            case .meta: drawText(r.text, fMeta, bx + CGFloat(r.depth) * indentStep, ry, metaC.faded(fd)); continue
            default: break
            }
            // 行位置变化（完成沉底 / 折叠展开 / 隐藏）时，从旧位置平滑滑到新位置
            if ui.settling, !r.key.isEmpty, let from = ui.settleFrom[r.key] { ry = M + from + (r.y - from) * CGFloat(settleT) }
            if !r.key.isEmpty, rowY[r.key] == nil { rowY[r.key] = ry - M }

            let rh = max(r.h, checkD)
            let cy = ry + rh / 2 - checkD / 2
            let ix = bx + CGFloat(r.depth) * indentStep          // 本行左缘（按嵌套深度缩进）
            let box = NSRect(x: ix, y: cy, width: checkD, height: checkD)
            let textX = ix + checkD + checkGap
            let rowRect = NSRect(x: ix - base * 0.35, y: ry - base * 0.22, width: rightX - ix + base * 0.7, height: rh + base * 0.44)

            // ---- 分组标题：细线箭头 + 名称；折叠时右侧显示数量与最近的截止时间 ----
            if r.type == .group {
                let cs = checkD * 0.30, ccx = box.midX, gcy = ry + r.h / 2
                let chev = NSBezierPath()
                if r.folded {
                    chev.move(to: NSPoint(x: ccx - cs * 0.45, y: gcy - cs)); chev.line(to: NSPoint(x: ccx + cs * 0.55, y: gcy)); chev.line(to: NSPoint(x: ccx - cs * 0.45, y: gcy + cs))
                } else {
                    chev.move(to: NSPoint(x: ccx - cs, y: gcy - cs * 0.45)); chev.line(to: NSPoint(x: ccx, y: gcy + cs * 0.55)); chev.line(to: NSPoint(x: ccx + cs, y: gcy - cs * 0.45))
                }
                chev.lineWidth = max(1.2, stroke * 0.95); chev.lineCapStyle = .round; chev.lineJoinStyle = .round
                groupInk.faded(fd).setStroke(); chev.stroke()
                drawText(r.text, groupFont, textX, ry, groupInk.faded(fd))
                if !r.summary.isEmpty {
                    let ss = textSize(r.summary, fMeta)
                    let sc = (r.nearestDue > 0 && r.nearestDue <= nowE) ? coral : metaC
                    drawText(r.summary, fMeta, rightX - ss.width, ry + (r.h - ss.height) / 2, sc.faded(fd * 0.9))
                }
                groupRects.append(HitRect(rect: rowRect, source: r.source, key: r.key, done: false))
                rowRects.append(HitRect(rect: rowRect, source: r.source, key: r.key, done: false))
                continue
            }

            let animating = r.type == .todo && ui.animKey == r.key && !r.done
            let alerting = ui.alertActive && !ui.alertTaskKey.isEmpty && !r.done && r.key == ui.alertTaskKey

            if alerting || animating {
                (alerting ? coral.withAlphaComponent(CGFloat((22 + 30 * pulse) / 255))
                          : accentDone.withAlphaComponent(CGFloat(30 * sin(.pi * animP) / 255))).setFill()
                NSBezierPath(roundedRect: rowRect, xRadius: base * 0.35, yRadius: base * 0.35).fill()
            }

            let cr = checkD / 2 - stroke / 2
            if r.type == .countdown {
                // 独立倒计时：细线小钟代替复选框
                (r.done ? doneCheck : toneColor(r.pillTone)).faded(fd).setStroke()
                let ring = circle(box.midX, box.midY, cr); ring.lineWidth = stroke; ring.stroke()
                let hands = NSBezierPath()
                hands.move(to: NSPoint(x: box.midX, y: box.midY - cr * 0.55)); hands.line(to: NSPoint(x: box.midX, y: box.midY))
                hands.line(to: NSPoint(x: box.midX + cr * 0.42, y: box.midY + cr * 0.20))
                hands.lineWidth = stroke; hands.lineCapStyle = .round; hands.lineJoinStyle = .round; hands.stroke()
                drawText(r.text, fText, textX, ry, (r.done ? doneInk : ink).faded(fd))
            } else if animating {
                // 完成动画：涟漪 + 光点 + 圆形复选框弹出填充 + 对勾描绘 + 删除线划过
                let pf = easeOutBack(animP / 0.32)
                let pt = clamp01((animP - 0.16) / 0.30)
                let pr = clamp01(animP / 0.80)
                let ps = easeInOutCubic((animP - 0.28) / 0.55)
                let ccx = box.midX, ccy = box.midY
                let rr = checkD * (0.55 + 1.25 * CGFloat(easeOutCubic(pr)))
                let ripple = circle(ccx, ccy, rr)
                accentDone.withAlphaComponent(CGFloat(0.63 * (1 - pr))).setStroke()
                ripple.lineWidth = max(1, stroke * CGFloat(1.5 - pr)); ripple.stroke()
                for k in 0..<8 {
                    let ang = Double(k) * .pi / 4 + 0.39
                    let dist = Double(checkD) * (0.6 + 1.5 * easeOutCubic(pr))
                    let ds = checkD * 0.11 * CGFloat(1 - pr)
                    if ds < 0.3 { continue }
                    (k % 2 == 0 ? accentDone : accentGold).withAlphaComponent(CGFloat(0.88 * (1 - pr))).setFill()
                    circle(ccx + CGFloat(cos(ang) * dist), ccy + CGFloat(sin(ang) * dist), ds).fill()
                }
                lerpColor(checkc, accentDone, animP / 0.2).setStroke()
                let bp = circle(ccx, ccy, cr); bp.lineWidth = stroke; bp.stroke()
                let fs = checkD * CGFloat(pf)
                if fs > 0.5 { accentDone.setFill(); circle(ccx, ccy, fs / 2).fill() }
                drawTick(ix, cy, checkD, pt, .white, stroke + 0.3)
                let c = lerpColor(ink, doneInk, ps)
                drawText(r.text, fText, textX, ry, c)
                if ps > 0 {
                    let sl = NSBezierPath()
                    let sy = ry + r.h * 0.54
                    sl.move(to: NSPoint(x: textX, y: sy)); sl.line(to: NSPoint(x: textX + r.tw * CGFloat(ps), y: sy))
                    sl.lineWidth = max(1.2, base * 0.055); c.setStroke(); sl.stroke()
                }
            } else {
                if r.done { doneFill.faded(fd).setFill(); circle(box.midX, box.midY, checkD / 2).fill() }
                (r.done ? doneCheck : checkc).faded(fd).setStroke()
                let bp = circle(box.midX, box.midY, cr); bp.lineWidth = stroke; bp.stroke()
                if r.done { drawTick(ix, cy, checkD, 1, doneCheck.faded(fd), stroke + 0.3) }
                // 刚完成的一项：强调色逐渐褪为已完成样式
                if r.done, ui.settling, r.key == ui.justCompletedKey {
                    let a = CGFloat(1 - settleT)
                    accentDone.withAlphaComponent(a).setFill(); circle(box.midX, box.midY, checkD / 2).fill()
                    drawTick(ix, cy, checkD, 1, NSColor.white.withAlphaComponent(a), stroke + 0.3)
                }
                drawText(r.text, fText, textX, ry, (r.done ? doneInk : ink).faded(fd), strike: r.done)
            }

            if r.source >= 0 {
                rowRects.append(HitRect(rect: rowRect, source: r.source, key: r.key, done: r.done))
                if r.type == .todo {
                    let inflate = max(4, base * 0.2)
                    hitRects.append(HitRect(rect: box.insetBy(dx: -inflate, dy: -inflate), source: r.source, key: r.key, done: r.done))
                }
            }

            // ---- 计时胶囊：细进度环 + 数字；只有到点 / 提醒时才有底色 ----
            if r.pill, pillColW > 0 {
                let px = rightX - pillColW, py = ry + rh / 2 - pillH / 2
                let ringC = toneColor(r.pillTone)
                let fade = CGFloat((r.done ? 0.45 : (r.pillRunning ? 1 : 0.65)) * fd)
                let pill = NSRect(x: px, y: py, width: pillColW, height: pillH)
                if r.pillExpired && !r.done {
                    coral.withAlphaComponent(CGFloat((alerting ? (46 + 40 * pulse) : 36) / 255 * fd)).setFill()
                    NSBezierPath(roundedRect: pill, xRadius: pillH / 2, yRadius: pillH / 2).fill()
                }
                let rcx = px + pillPadX + ringD / 2, rcy = py + pillH / 2
                let rw = max(1.3, ringD * 0.13), rd = ringD - rw
                let track = circle(rcx, rcy, rd / 2)
                tc.withAlphaComponent(0.20 * fade).setStroke(); track.lineWidth = rw; track.stroke()
                ringC.withAlphaComponent(fade).setStroke()
                if r.pillFrac >= 0.999 {
                    track.stroke()
                } else if r.pillFrac > 0.001 {
                    let arc = NSBezierPath()
                    arc.appendArc(withCenter: NSPoint(x: rcx, y: rcy), radius: rd / 2, startAngle: -90, endAngle: -90 + CGFloat(360 * r.pillFrac), clockwise: false)
                    arc.lineWidth = rw; arc.lineCapStyle = .round; arc.stroke()
                }
                if !r.pillRunning {   // 暂停：中间两道竖线
                    ringC.withAlphaComponent(fade).setFill()
                    let bw2 = max(1.2, rw * 0.75), bh2 = ringD * 0.34
                    NSBezierPath(rect: NSRect(x: rcx - bw2 * 1.6, y: rcy - bh2 / 2, width: bw2, height: bh2)).fill()
                    NSBezierPath(rect: NSRect(x: rcx + bw2 * 0.6, y: rcy - bh2 / 2, width: bw2, height: bh2)).fill()
                }
                let lc = (r.pillExpired && !r.done) ? rgb(255, 160, 150, fade) : tc.withAlphaComponent(0.88 * fade)
                let ls = NSAttributedString(string: r.pillLabel, attributes: attrs(timerFont, color: lc))
                ls.draw(at: NSPoint(x: rcx + ringD / 2 + pillInnerGap, y: py + (pillH - ls.size().height) / 2))
                pillRects.append(HitRect(rect: pill, source: r.source, key: r.key, done: r.done, kind: r.pillDue ? 1 : 0))
            }
        }
        ui.lastRowY = rowY
    }

    override func mouseDown(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        if let b = bannerButtons.first(where: { $0.rect.contains(p) }) { controller?.bannerAction(b.action); return }
        if let h = pillRects.first(where: { $0.rect.contains(p) }) {
            if h.kind == 1 { cfg.dueShowAbsolute.toggle(); controller?.saveConfig() }   // 截止时间：切换「剩余 / 截止时刻」
            else { toggleTaskTimerPause(h.key) }                                         // 正计时：暂停 / 继续
            controller?.refresh(force: true); return
        }
        if let h = hitRects.first(where: { $0.rect.contains(p) }) { controller?.beginToggle(h); return }
        if let h = groupRects.first(where: { $0.rect.contains(p) }) { controller?.toggleFold(h); return }
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
        panel.title = "外观"
        panel.level = .floating
        panel.isReleasedWhenClosed = false
        super.init()
        let v = panel.contentView!
        func label(_ s: String, _ y: CGFloat) -> NSTextField {
            let t = NSTextField(labelWithString: s); t.frame = NSRect(x: 20, y: y, width: 220, height: 18)
            t.textColor = .secondaryLabelColor; v.addSubview(t); return t
        }
        func value(_ y: CGFloat) -> NSTextField {
            let t = NSTextField(labelWithString: ""); t.alignment = .right
            t.font = NSFont.monospacedDigitSystemFont(ofSize: 13, weight: .medium)
            t.frame = NSRect(x: 260, y: y, width: 80, height: 18); v.addSubview(t); labels.append(t); return t
        }
        func slider(_ y: CGFloat, _ lo: Double, _ hi: Double, _ val: Double) -> NSSlider {
            let s = NSSlider(value: val, minValue: lo, maxValue: hi, target: self, action: #selector(changed))
            s.frame = NSRect(x: 20, y: y, width: 320, height: 22); v.addSubview(s); return s
        }
        _ = label("字号", 306); _ = value(306)
        fontSlider = slider(282, 10, 60, Double(cfg.fontHeight))
        _ = label("行距", 246); _ = value(246)
        spaceSlider = slider(222, 0, 90, Double(cfg.lineHeight))
        _ = label("卡片不透明度", 186); _ = value(186)
        opacitySlider = slider(162, 0, 255, Double(cfg.bgAlpha))
        _ = label("文字颜色", 122)
        textWell = NSColorWell(frame: NSRect(x: 20, y: 90, width: 90, height: 28)); textWell.color = cfg.textColor
        textWell.target = self; textWell.action = #selector(changed); v.addSubview(textWell)
        _ = label("卡片颜色", 122).frame.origin.x = 200
        cardWell = NSColorWell(frame: NSRect(x: 200, y: 90, width: 90, height: 28)); cardWell.color = cfg.bgColor
        cardWell.target = self; cardWell.action = #selector(changed); v.addSubview(cardWell)
        _ = label("字体", 60)
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

// MARK: - Countdown dialog
// 两种设定方式可随时切换、互相换算：「截止时间」（年月日 时分秒）与「剩余时长」（月 天 时 分 秒）。
// 确定后一律换算成截止时间（@YYYY-MM-DD HH:MM:SS）写进 TODO 文件。

final class FlippedView: NSView { override var isFlipped: Bool { true } }

final class DueDialog: NSObject, NSTextFieldDelegate {
    let alert = NSAlert()
    var nameField: NSTextField?
    let seg = NSSegmentedControl(labels: ["截止时间", "剩余时长"], trackingMode: .selectOne, target: nil, action: nil)
    let picker = NSDatePicker()
    var rel: [NSTextField] = []
    var relLabels: [NSTextField] = []
    let preview = NSTextField(labelWithString: "")
    let repeatBox = NSButton(checkboxWithTitle: "到点后按同样的间隔重复", target: nil, action: nil)
    var fixedRepeat = 0
    var mode = 1

    init(heading: String, sub: String, askName: Bool, name: String, due: Int, repeatSec: Int, repeatChecked: Bool, mode: Int) {
        super.init()
        self.mode = mode
        fixedRepeat = repeatSec
        alert.messageText = heading
        if !sub.isEmpty { alert.informativeText = sub }
        alert.addButton(withTitle: "确定"); alert.addButton(withTitle: "取消")

        let W: CGFloat = 340
        let v = FlippedView(frame: NSRect(x: 0, y: 0, width: W, height: 10))
        var y: CGFloat = 4
        if askName {
            let l = NSTextField(labelWithString: "名称（可留空）"); l.textColor = .secondaryLabelColor
            l.frame = NSRect(x: 0, y: y, width: W, height: 18); v.addSubview(l)
            let f = NSTextField(frame: NSRect(x: 0, y: y + 20, width: W, height: 24)); f.stringValue = name
            v.addSubview(f); nameField = f
            y += 56
        }
        seg.frame = NSRect(x: 0, y: y, width: 200, height: 24)
        seg.target = self; seg.action = #selector(modeChanged)
        v.addSubview(seg)
        y += 36
        picker.datePickerStyle = .textFieldAndStepper
        picker.datePickerElements = [.yearMonthDay, .hourMinuteSecond]
        picker.dateValue = Date(timeIntervalSince1970: TimeInterval(due))
        picker.frame = NSRect(x: 0, y: y, width: 240, height: 24)
        picker.target = self; picker.action = #selector(changed)
        v.addSubview(picker)
        let units = ["月", "天", "时", "分", "秒"]
        for i in 0..<5 {
            let x = CGFloat(i) * 68
            let f = NSTextField(frame: NSRect(x: x, y: y, width: 44, height: 24))
            f.alignment = .right; f.delegate = self
            f.font = NSFont.monospacedDigitSystemFont(ofSize: 13, weight: .regular)
            v.addSubview(f); rel.append(f)
            let l = NSTextField(labelWithString: units[i]); l.frame = NSRect(x: x + 47, y: y + 3, width: 20, height: 18)
            v.addSubview(l); relLabels.append(l)
        }
        y += 36
        preview.frame = NSRect(x: 0, y: y, width: W, height: 18)
        preview.textColor = .secondaryLabelColor
        preview.font = NSFont.systemFont(ofSize: 12)
        preview.lineBreakMode = .byTruncatingTail
        v.addSubview(preview)
        y += 28
        repeatBox.frame = NSRect(x: 0, y: y, width: W, height: 20)
        repeatBox.state = repeatChecked ? .on : .off
        repeatBox.target = self; repeatBox.action = #selector(changed)
        v.addSubview(repeatBox)
        y += 26
        v.setFrameSize(NSSize(width: W, height: y))
        alert.accessoryView = v

        setRel(due)
        showMode()
        updatePreview()
        alert.window.initialFirstResponder = nameField ?? (mode == 0 ? picker : rel[3])
    }

    func relDue() -> Int {
        let now = nowEpoch()
        let v = rel.map { Int($0.stringValue.filter { $0.isASCII && $0.isNumber }.prefix(9)) ?? 0 }
        return addMonths(now, min(v[0], 1200)) + v[1] * 86400 + v[2] * 3600 + v[3] * 60 + v[4]
    }
    func setRel(_ due: Int) {
        for (i, n) in splitRemain(nowEpoch(), due).enumerated() { rel[i].stringValue = "\(n)" }
    }
    func current() -> Int { mode == 0 ? Int(picker.dateValue.timeIntervalSince1970) : relDue() }

    func showMode() {
        seg.selectedSegment = mode
        picker.isHidden = mode != 0
        for f in rel { f.isHidden = mode != 1 }
        for l in relLabels { l.isHidden = mode != 1 }
    }

    func updatePreview() {
        let now = nowEpoch(), due = current()
        var t: String
        if due <= now { t = "这个时间已经过去了，请设一个将来的时刻" }
        else if mode == 0 { t = "还剩 " + formatRemain(due - now) }
        else { t = "将于 " + formatDueText(due) + " 到点" }
        if due > now, repeatBox.state == .on {
            t += "，之后每隔 " + formatRemain(fixedRepeat > 0 ? fixedRepeat : due - now) + " 重复"
        }
        preview.stringValue = t
    }

    @objc func modeChanged() {
        let nm = seg.selectedSegment
        guard nm != mode else { return }
        var due = current()
        if due <= nowEpoch() { due = nowEpoch() + 60 }
        mode = nm
        if nm == 0 { picker.dateValue = Date(timeIntervalSince1970: TimeInterval(due)) } else { setRel(due) }
        showMode(); updatePreview()
    }
    @objc func changed() { updatePreview() }
    func controlTextDidChange(_ obj: Notification) { updatePreview() }

    func run() -> (name: String, due: Int, repeatSec: Int)? {
        NSApp.activate(ignoringOtherApps: true)
        let t = Timer(timeInterval: 1, repeats: true) { [weak self] _ in if self?.mode == 0 { self?.updatePreview() } }
        RunLoop.main.add(t, forMode: .modalPanel)
        defer { t.invalidate() }
        while true {
            guard alert.runModal() == .alertFirstButtonReturn else { return nil }
            let now = nowEpoch(), due = current()
            if due <= now {
                let a = NSAlert(); a.messageText = "请设一个将来的时刻。"; a.runModal()
                continue
            }
            let rep = repeatBox.state == .on ? (fixedRepeat > 0 ? fixedRepeat : due - now) : 0
            return (trimmed(nameField?.stringValue ?? ""), due, rep)
        }
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
    var menuHint = -1

    // 置顶时鼠标悬停隐藏：鼠标移到卡片上时淡出并忽略鼠标事件（点击穿透到下面的窗口），
    // 移开后淡入恢复；按住 ⌘ 时保持显示、可正常拖动/勾选/右键。
    var hoverTimer: Timer?
    var hoverHidden = false
    var hoverAlphaTarget: CGFloat = 1
    // 智能浮现（显示模式 2）：平时附着桌面；临近截止或定时轻浮现时以“半透明 + 鼠标穿透”浮到最上层，
    // 悬停更淡、按住 ⌘ 才变实可操作；前台全屏时不浮现（到点提醒除外）。
    var peekActive = false
    var peekGlance = false
    var peekUntil = 0
    var lastGlance = 0
    var nextOpenDue = 0
    var hasOpenItems = false
    let peekAlpha: CGFloat = 0.7
    let peekHoverAlpha: CGFloat = 0.18
    let peekGlanceSec = 8
    var menuTracking = false

    func applicationDidFinishLaunching(_ n: Notification) {
        ensureDefaultTodo()
        if !FileManager.default.fileExists(atPath: cfg.todoPath) { cfg.todoPath = supportDir.appendingPathComponent("TODO.txt").path; ensureDefaultTodo() }
        migrateLegacyCountdowns()

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
        statusItem.button?.image = AppController.menuBarIcon()
        let m = NSMenu(); m.delegate = self; statusItem.menu = m

        Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in self?.tick() }
        NotificationCenter.default.addObserver(self, selector: #selector(screensChanged),
                                               name: NSApplication.didChangeScreenParametersNotification, object: nil)
        NotificationCenter.default.addObserver(forName: NSMenu.didBeginTrackingNotification, object: nil, queue: .main) { [weak self] _ in self?.menuTracking = true }
        NotificationCenter.default.addObserver(forName: NSMenu.didEndTrackingNotification, object: nil, queue: .main) { [weak self] _ in self?.menuTracking = false }
    }

    /// 菜单栏图标：细线圆环 + 对勾 + 一小段进度弧（模板图，随系统明暗自动着色）
    static func menuBarIcon() -> NSImage {
        let img = NSImage(size: NSSize(width: 18, height: 18), flipped: false) { _ in
            let c = NSPoint(x: 9, y: 9)
            let ring = NSBezierPath()
            ring.appendArc(withCenter: c, radius: 6.6, startAngle: 120, endAngle: 420, clockwise: false)
            ring.lineWidth = 1.5; ring.lineCapStyle = .round
            NSColor.black.withAlphaComponent(0.45).setStroke(); ring.stroke()
            let arc = NSBezierPath()
            arc.appendArc(withCenter: c, radius: 6.6, startAngle: 90, endAngle: 120, clockwise: false)
            arc.lineWidth = 1.5; arc.lineCapStyle = .round
            NSColor.black.setStroke(); arc.stroke()
            let tick = NSBezierPath()
            tick.move(to: NSPoint(x: 6.0, y: 9.2)); tick.line(to: NSPoint(x: 8.2, y: 7.0)); tick.line(to: NSPoint(x: 12.2, y: 11.2))
            tick.lineWidth = 1.6; tick.lineCapStyle = .round; tick.lineJoinStyle = .round
            tick.stroke()
            return true
        }
        img.isTemplate = true
        return img
    }

    @objc func screensChanged() { clampToScreens() }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { false }

    // Window level
    func applyMode() {
        if cfg.displayMode == 1 || (cfg.displayMode == 2 && peekActive) {
            window.level = .floating
        } else {
            window.level = NSWindow.Level(rawValue: Int(CGWindowLevelForKey(.desktopIconWindow)) + 1)
        }
        updateHoverHide()
    }

    // MARK: Hide on hover (always-on-top only)
    func updateHoverHide() {
        if (cfg.hoverHide && cfg.displayMode == 1) || peekActive {
            if hoverTimer == nil {
                let t = Timer(timeInterval: 0.03, repeats: true) { [weak self] _ in self?.hoverTick() }
                RunLoop.main.add(t, forMode: .common)   // 拖动/菜单跟踪期间也继续检测
                hoverTimer = t
            }
        } else {
            hoverTimer?.invalidate(); hoverTimer = nil
            setHoverHidden(false)
        }
    }

    func hoverTick() {
        // 到点提醒期间覆盖悬停隐藏：卡片保持完全显示、可点击
        if ui.alertActive { setHoverHidden(false); return }
        let inside = window.frame.contains(NSEvent.mouseLocation)
        let reveal = NSEvent.modifierFlags.contains(.command)
        if peekActive {   // 轻浮层：默认半透明且穿透；悬停更淡；按住 ⌘ 才变实可操作
            let busy = (!hoverHidden && (NSEvent.pressedMouseButtons & 1) != 0) || menuTracking
            let interactive = reveal || busy
            setHoverState(clickThrough: !interactive, alpha: interactive ? 1 : (inside ? peekHoverAlpha : peekAlpha))
            return
        }
        // 卡片可见时按着左键（正在拖动卡片）不隐藏
        let dragging = !hoverHidden && (NSEvent.pressedMouseButtons & 1) != 0
        setHoverHidden(inside && !reveal && !dragging && !menuTracking)
    }

    func setHoverHidden(_ hidden: Bool) { setHoverState(clickThrough: hidden, alpha: hidden ? 0 : 1) }

    func setHoverState(clickThrough: Bool, alpha: CGFloat) {
        if clickThrough != hoverHidden {
            hoverHidden = clickThrough
            window.ignoresMouseEvents = clickThrough
        }
        guard alpha != hoverAlphaTarget else { return }
        let soft = alpha > 0 && alpha < 1
        hoverAlphaTarget = alpha
        NSAnimationContext.runAnimationGroup { ctx in
            ctx.duration = soft ? 0.3 : 0.12
            self.window.animator().alphaValue = alpha
        }
    }

    // 前台是否为全屏程序（此时不做轻浮现）
    var userIsBusy: Bool {
        let o = NSApp.currentSystemPresentationOptions
        return o.contains(.fullScreen) || o.contains(.hideMenuBar)
    }

    // 每秒随截止时间检查调用：决定是否以轻浮层浮到最上面
    func updatePeek() {
        guard cfg.displayMode == 2, !ui.alertActive else {
            if cfg.displayMode != 2 { peekActive = false; peekGlance = false }
            return
        }
        let now = nowEpoch()
        if lastGlance == 0 { lastGlance = now }   // 启动后先等一个完整间隔
        let busy = userIsBusy
        let upcoming = nextOpenDue > 0 && nextOpenDue - now <= cfg.surfaceLead * 60
        var glance = false
        if peekActive && peekGlance && now < peekUntil { glance = true }
        else if !peekActive && cfg.surfaceEvery > 0 && hasOpenItems && now - lastGlance >= cfg.surfaceEvery * 60 {
            lastGlance = now
            if !busy { glance = true; peekUntil = now + peekGlanceSec }
        }
        var want = !busy && (upcoming || glance)
        if peekActive && (menuTracking || (!hoverHidden && (NSEvent.pressedMouseButtons & 1) != 0)) { want = true }
        if want && !peekActive {
            peekActive = true
            peekGlance = !upcoming
            window.alphaValue = 0; hoverAlphaTarget = 0   // 从透明淡入到轻浮层
            applyMode()
            hoverTick()
            window.orderFrontRegardless()
        } else if peekActive {
            if upcoming { peekGlance = false }
            if !want { peekActive = false; peekGlance = false; applyMode() }
        }
    }

    // Layout/refresh keeping the top-left corner fixed
    func refresh(force: Bool = false) {
        let sig = (try? FileManager.default.attributesOfItem(atPath: cfg.todoPath)[.modificationDate] as? Date) ?? nil
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
        checkDeadlines()
        refresh(force: taskTimers.contains { $0.running && !$0.countdown } || card.hasDuePill)   // 有计时在走：每秒刷新
        if cfg.displayMode == 1 || ui.alertActive || peekActive { window.orderFrontRegardless() }
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

    func submenu(_ m: NSMenu, _ title: String, _ sub: NSMenu) {
        let it = NSMenuItem(title: title, action: nil, keyEquivalent: ""); it.submenu = sub; m.addItem(it)
    }

    func buildMenu(taskAt point: NSPoint? = nil) -> NSMenu {
        let m = NSMenu()
        // 在某个条目上右键：顶部出现针对该条目的操作
        menuTaskKey = ""; menuHint = -1
        if let point, let row = card.rowRects.first(where: { $0.rect.contains(point) }) {
            menuTaskKey = row.key; menuHint = row.source
            var L = LineInfo()
            if let lines = readTodoLines(), let idx = findItemLine(lines, row.source, row.key) { L = parseLine(lines[idx]); menuHint = idx }
            var name = processPlaceholders(keyDisplayName(row.key))
            if name.count > 18 { name = String(name.prefix(18)) + "…" }
            if name.isEmpty { name = isGroupKey(row.key) ? "未命名分组" : "倒计时" }
            add(m, name, nil, enabled: false)
            if isGroupKey(row.key) {
                add(m, L.fold ? "展开分组" : "折叠分组", #selector(itemFold))
                add(m, L.hide ? "取消隐藏分组" : "隐藏分组", #selector(itemHide))
            } else {
                let isCd = isCountdownKey(row.key)
                let quick = NSMenu()
                for mins in [5, 10, 15, 25, 30, 45, 60, 90] {
                    add(quick, mins % 60 == 0 ? "\(mins / 60) 小时后" : "\(mins) 分钟后", #selector(itemQuick(_:)), mins * 60)
                }
                if isCd {
                    add(m, "修改倒计时…", #selector(itemSetDue))
                    submenu(m, "快速设定", quick)
                    add(m, "删除倒计时", #selector(itemDelete))
                } else {
                    add(m, L.hasDue ? "修改截止时间…" : "设置截止时间…", #selector(itemSetDue))
                    submenu(m, "快速倒计时", quick)
                    if L.hasDue { add(m, "清除截止时间", #selector(itemClearDue)) }
                    m.addItem(.separator())
                    if let t = timer(for: row.key) {
                        add(m, t.running ? "暂停正计时" : "继续正计时", #selector(taskPause))
                        add(m, "重新开始正计时", #selector(taskRestart))
                        add(m, "移除正计时", #selector(taskRemove))
                    } else {
                        add(m, "开始正计时", #selector(taskStopwatch))
                    }
                }
                m.addItem(.separator())
                add(m, "分组折叠 / 隐藏时仍显示", #selector(itemShow), checked: L.show)
                add(m, L.hide ? "取消隐藏" : "隐藏", #selector(itemHide))
            }
            m.addItem(.separator())
        }
        add(m, "新建倒计时…", #selector(newCountdown))
        if ui.alertActive { add(m, "停止提醒", #selector(stopAlertAction)) }
        add(m, "显示隐藏的条目", #selector(toggleShowHidden), checked: cfg.showHidden)
        add(m, "已完成项沉底", #selector(toggleSink), checked: cfg.sinkCompleted)
        m.addItem(.separator())
        let mode = NSMenu()
        add(mode, "智能浮现（推荐）", #selector(setMode(_:)), 2, checked: cfg.displayMode == 2)
        add(mode, "附着桌面", #selector(setMode(_:)), 0, checked: cfg.displayMode == 0)
        add(mode, "始终置顶", #selector(setMode(_:)), 1, checked: cfg.displayMode == 1)
        mode.addItem(.separator())
        let lead = NSMenu()
        for v in [5, 10, 15, 30] { add(lead, "提前 \(v) 分钟", #selector(setSurfaceLead(_:)), v, checked: cfg.surfaceLead == v) }
        let every = NSMenu()
        for v in [30, 45, 60, 0] { add(every, v > 0 ? "每 \(v) 分钟" : "关闭", #selector(setSurfaceEvery(_:)), v, checked: cfg.surfaceEvery == v) }
        if cfg.displayMode == 2 {
            submenu(mode, "临近截止时浮现", lead)
            submenu(mode, "定时轻浮现", every)
        }
        add(mode, "置顶时悬停隐藏（按住 ⌘ 可操作）", #selector(toggleHoverHide), checked: cfg.hoverHide, enabled: cfg.displayMode == 1)
        submenu(m, "显示模式", mode)
        add(m, "外观设置…", #selector(showSettings))
        m.addItem(.separator())
        add(m, "编辑 TODO", #selector(editTodoAction))
        add(m, "刷新", #selector(refreshAction))
        add(m, "更换 TODO 文件…", #selector(changeFile))
        add(m, "在访达中显示", #selector(reveal))
        m.addItem(.separator())
        add(m, "退出", #selector(quit))
        return m
    }

    @objc func showSettings() {
        if settings == nil { settings = SettingsController(app: self) }
        settings?.show(near: NSEvent.mouseLocation)
    }
    @objc func setMode(_ s: NSMenuItem) {
        cfg.displayMode = s.tag
        peekActive = false; peekGlance = false; lastGlance = nowEpoch()
        applyMode(); saveConfig(); window.orderFrontRegardless()
        updatePeek()
    }
    @objc func setSurfaceLead(_ s: NSMenuItem) { cfg.surfaceLead = s.tag; saveConfig(); updatePeek(); refresh(force: true) }
    @objc func setSurfaceEvery(_ s: NSMenuItem) { cfg.surfaceEvery = s.tag; lastGlance = nowEpoch(); saveConfig() }
    @objc func toggleHoverHide() { cfg.hoverHide.toggle(); updateHoverHide(); saveConfig() }
    @objc func toggleShowHidden() { animateLayout { cfg.showHidden.toggle() }; saveConfig() }
    @objc func toggleSink() { cfg.sinkCompleted.toggle(); saveConfig(); refresh(force: true) }
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

    /// 行位置会变化的操作（折叠 / 隐藏）：先记下各行位置，改完后平滑滑到新位置
    func animateLayout(_ change: () -> Void) {
        snapshotRowsForSettle()
        change()
        refresh(force: true)
        ensureAnimTimer()
    }

    func toggleFold(_ h: HitRect) { animateLayout { editItemLine(h.source, h.key) { L in if L.kind == .group { L.fold.toggle() }; return true } } }

    // MARK: Item actions（任务 / 独立倒计时 / 分组）

    func stopAlertIfFor(_ key: String) { if ui.alertActive && ui.alertTaskKey == key { stopAlert() } }

    @objc func itemQuick(_ s: NSMenuItem) {
        setItemDue(menuHint, menuTaskKey, nowEpoch() + s.tag)
        stopAlertIfFor(menuTaskKey); refresh(force: true)
    }
    @objc func itemClearDue() {
        editItemLine(menuHint, menuTaskKey) { L in
            if L.kind == .countdown { return false }   // 独立倒计时没有截止时间就没有意义：直接删除
            L.hasDue = false; L.repeatSec = 0; return true
        }
        stopAlertIfFor(menuTaskKey); refresh(force: true)
    }
    @objc func itemDelete() {
        editItemLine(menuHint, menuTaskKey) { _ in false }
        stopAlertIfFor(menuTaskKey); refresh(force: true)
    }
    @objc func itemHide() { let k = menuTaskKey, h = menuHint; animateLayout { editItemLine(h, k) { L in L.hide.toggle(); return true } } }
    @objc func itemShow() {
        let k = menuTaskKey, h = menuHint
        animateLayout { editItemLine(h, k) { L in if L.kind == .todo || L.kind == .countdown { L.show.toggle() }; return true } }
    }
    @objc func itemFold() { let k = menuTaskKey, h = menuHint; animateLayout { editItemLine(h, k) { L in if L.kind == .group { L.fold.toggle() }; return true } } }

    @objc func itemSetDue() {
        let key = menuTaskKey
        guard let lines = readTodoLines(), let idx = findItemLine(lines, menuHint, key) else { return }
        let L = parseLine(lines[idx])
        let now = nowEpoch(), isCd = L.kind == .countdown
        let dlg = DueDialog(heading: isCd ? "修改倒计时" : (L.hasDue ? "修改截止时间" : "设置截止时间"),
                            sub: isCd ? "" : processPlaceholders(trimmed(L.text)),
                            askName: isCd, name: L.text,
                            due: (L.hasDue && L.due > now) ? L.due : now + 25 * 60,
                            repeatSec: L.repeatSec, repeatChecked: L.repeatSec > 0, mode: cfg.dueInputMode)
        guard let r = dlg.run() else { return }
        cfg.dueInputMode = dlg.mode
        var newKey = key
        let ok = editItemLine(idx, key) { E in
            guard E.kind == .todo || E.kind == .countdown else { return true }
            if E.kind == .countdown { E.text = r.name }
            E.hasDue = true; E.due = r.due; E.repeatSec = r.repeatSec
            newKey = itemKey(E)
            return true
        }
        if ok { noteDueSet(newKey, r.due) }
        stopAlertIfFor(key)
        saveConfig(); refresh(force: true)
    }

    /// 新建独立倒计时（不挂在任务上），同样写进 TODO 文件
    @objc func newCountdown() {
        let dlg = DueDialog(heading: "新建倒计时", sub: "会以「! 名称 @截止时间」的形式写进 TODO 文件",
                            askName: true, name: "", due: nowEpoch() + 10 * 60,
                            repeatSec: 0, repeatChecked: cfg.countdownRepeat, mode: cfg.dueInputMode)
        guard let r = dlg.run() else { return }
        cfg.dueInputMode = dlg.mode
        cfg.countdownRepeat = r.repeatSec > 0
        if insertCountdownLine(r.name, r.due, r.repeatSec) { noteDueSet(keyCDPrefix + r.name, r.due) }
        else { let a = NSAlert(); a.messageText = "无法写入 TODO 文件。"; a.runModal() }
        saveConfig(); refresh(force: true)
    }

    @objc func stopAlertAction() { dismissDueItem(ui.alertTaskKey); stopAlert() }

    // MARK: Stopwatch menu actions

    @objc func taskStopwatch() { startTaskStopwatch(menuTaskKey); refresh(force: true) }
    @objc func taskPause() { toggleTaskTimerPause(menuTaskKey); refresh(force: true) }
    @objc func taskRestart() { restartTaskTimer(menuTaskKey); refresh(force: true) }
    @objc func taskRemove() { removeTaskTimer(menuTaskKey); refresh(force: true) }

    // MARK: Deadlines

    /// 每秒检查截止时间是否已到（任务与独立倒计时共用）
    func checkDeadlines() {
        guard var lines = readTodoLines() else { return }
        let now = nowEpoch()
        for i in dueMeta.indices { dueMeta[i].seen = false }
        var metaChanged = false, fileChanged = false
        var fires: [(key: String, title: String, detail: String)] = []
        func meta(_ key: String, _ due: Int) -> Int {
            if let i = dueMetaIndex(key, due) { dueMeta[i].seen = true; return i }
            dueMeta.append(DueMeta(due: due, setAt: now, fired: false, key: key, seen: true))
            metaChanged = true
            return dueMeta.count - 1
        }
        var nextDue = 0, openItems = false
        for i in lines.indices {
            var L = parseLine(lines[i])
            if (L.kind == .todo || L.kind == .countdown) && !L.ok && !L.hide { openItems = true }
            guard (L.kind == .todo || L.kind == .countdown), L.hasDue else { continue }
            let key = itemKey(L)
            let mi = meta(key, L.due)
            if !L.ok && L.due > now && (nextDue == 0 || L.due < nextDue) { nextDue = L.due }
            if L.ok || dueMeta[mi].fired || now < L.due { continue }
            dueMeta[mi].fired = true; metaChanged = true
            let name = processPlaceholders(trimmed(L.text))
            var f: (key: String, title: String, detail: String)
            if L.kind == .countdown { f = (key, name.isEmpty ? "倒计时结束" : name, "倒计时结束 · " + formatDueText(L.due)) }
            else { f = (key, "截止时间到", name) }
            if L.repeatSec > 0 {   // 重复：截止时间顺延到下一个未来时刻
                var nd = L.due
                while nd <= now { nd += L.repeatSec }
                L.due = nd
                lines[i] = formatLine(L); fileChanged = true
                let mj = meta(key, nd)
                dueMeta[mj].setAt = nd - L.repeatSec; dueMeta[mj].fired = false
                f.detail += " · 下次 " + formatDueShort(nd)
            }
            fires.append(f)
        }
        let before = dueMeta.count
        dueMeta.removeAll { !$0.seen }
        if dueMeta.count != before { metaChanged = true }
        if fileChanged { _ = writeTodoLines(lines) }
        if metaChanged { saveDueMeta() }
        nextOpenDue = nextDue; hasOpenItems = openItems
        for f in fires { startAlert(f.title, f.detail, key: f.key) }
        updatePeek()
    }

    func bannerButtonSpec() -> [(action: BannerAction, title: String)] {
        var v: [(action: BannerAction, title: String)] = []
        if !ui.alertTaskKey.isEmpty && !isCountdownKey(ui.alertTaskKey) { v.append((.done, "完成")) }
        v.append((.snooze, "+5 分钟"))
        v.append((.dismiss, "知道了"))
        return v
    }

    /// 「知道了」：不重复的独立倒计时到点后从文件里移除；任务保留截止时间（显示为已超时）
    func dismissDueItem(_ key: String) {
        guard isCountdownKey(key), var lines = readTodoLines(), let idx = findItemLine(lines, -1, key) else { return }
        let L = parseLine(lines[idx])
        if L.kind == .countdown && L.repeatSec == 0 && L.due <= nowEpoch() {
            lines.remove(at: idx); _ = writeTodoLines(lines)
        }
    }

    func bannerAction(_ action: BannerAction) {
        let key = ui.alertTaskKey
        switch action {
        case .done where !key.isEmpty && !isCountdownKey(key):
            stopAlert()
            if let h = card.hitRects.first(where: { $0.key == key && !$0.done }) { beginToggle(h) }   // 播放完成动画
            else if toggleTodo(key: key, hint: -1) { refresh(force: true) }
            return
        case .snooze:
            if !key.isEmpty { setItemDue(-1, key, nowEpoch() + 300) }   // 截止时间改为 5 分钟后
        case .dismiss:
            dismissDueItem(key)
        default: break
        }
        stopAlert()
    }

    // MARK: Alert（到点：卡片升到最前端 + 抖动 + 提醒横幅）

    func startAlert(_ title: String, _ detail: String, key: String) {
        ui.alertTitle = title; ui.alertDetail = detail; ui.alertTaskKey = key
        ui.alertStart = Date(); ui.shakeTimes = 0
        ui.alertActive = true
        peekActive = false; peekGlance = false   // 醒目提醒接管轻浮层
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
