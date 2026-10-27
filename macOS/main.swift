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

// MARK: - Fonts

func makeFont(_ family: String, _ size: CGFloat, light: Bool = false) -> NSFont {
    let fm = NSFontManager.shared
    if let f = fm.font(withFamily: family, traits: [], weight: light ? 3 : 5, size: size) { return f }
    return NSFont.systemFont(ofSize: size, weight: light ? .light : .regular)
}

// MARK: - Card view

enum RowType { case spacer, heading, meta, todo }
struct Row {
    var type: RowType
    var text = ""
    var w: CGFloat = 0, h: CGFloat = 0, y: CGFloat = 0
    var source = -1
    var done = false
}

final class CardView: NSView {
    static let margin: (CGFloat) -> CGFloat = { $0 * 2.1 }
    var rows: [Row] = []
    var dividerY: CGFloat = -1
    var cardSize = NSSize(width: 100, height: 60)
    var hitRects: [(rect: NSRect, source: Int)] = []
    var lastSignature = ""
    weak var controller: AppController?

    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { false }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    var base: CGFloat { CGFloat(cfg.fontHeight > 8 ? cfg.fontHeight : 24) }
    var M: CGFloat { CardView.margin(base) }

    func attrs(_ font: NSFont, color: NSColor, strike: Bool = false) -> [NSAttributedString.Key: Any] {
        var a: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: color]
        if strike { a[.strikethroughStyle] = NSUnderlineStyle.single.rawValue }
        return a
    }

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

        var contentW = base * 4
        for i in rs.indices {
            if rs[i].type == .spacer { rs[i].h = base * 0.45; continue }
            let f = rs[i].type == .heading ? fHead : rs[i].type == .meta ? fMeta : fText
            let sz = NSAttributedString(string: rs[i].text, attributes: [.font: f]).size()
            rs[i].h = ceil(sz.height)
            rs[i].w = ceil(sz.width) + (rs[i].type == .todo ? checkD + checkGap : 0)
            contentW = max(contentW, rs[i].w)
        }
        var y = PADTOP, divY: CGFloat = -1, placed = false, seen = false, lastGap: CGFloat = 0
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
            NSAttributedString(string: s, attributes: attrs(f, color: NSColor.black.withAlphaComponent(0.35), strike: strike))
                .draw(at: NSPoint(x: x, y: y + sdy))
            NSAttributedString(string: s, attributes: attrs(f, color: c, strike: strike)).draw(at: NSPoint(x: x, y: y))
        }

        let bx = M + PADX
        if dividerY >= 0 {
            NSColor.white.withAlphaComponent(0.12).setStroke()
            let p = NSBezierPath()
            p.move(to: NSPoint(x: bx, y: M + dividerY + 0.5)); p.line(to: NSPoint(x: M + cardSize.width - PADX, y: M + dividerY + 0.5))
            p.lineWidth = 1; p.stroke()
        }

        hitRects.removeAll()
        for r in rows {
            let ry = M + r.y
            switch r.type {
            case .spacer: continue
            case .heading: drawText(r.text, fHead, bx, ry, ink)
            case .meta: drawText(r.text, fMeta, bx, ry, metaC)
            case .todo:
                let rh = max(r.h, checkD)
                let cy = ry + rh / 2 - checkD / 2
                let stroke = max(1.25, base * 0.06)
                let box = NSRect(x: bx, y: cy, width: checkD, height: checkD)
                if r.done { doneFill.setFill(); NSBezierPath(rect: box).fill() }
                (r.done ? doneCheck : checkc).setStroke()
                let bp = NSBezierPath(rect: box.insetBy(dx: stroke / 2, dy: stroke / 2)); bp.lineWidth = stroke; bp.stroke()
                if r.done {
                    let tp = NSBezierPath()
                    tp.lineWidth = stroke + 0.35; tp.lineCapStyle = .round; tp.lineJoinStyle = .round
                    tp.move(to: NSPoint(x: bx + checkD * 0.23, y: cy + checkD * 0.53))
                    tp.line(to: NSPoint(x: bx + checkD * 0.43, y: cy + checkD * 0.72))
                    tp.line(to: NSPoint(x: bx + checkD * 0.78, y: cy + checkD * 0.28))
                    tp.stroke()
                }
                if r.source >= 0 {
                    let inflate = max(4, base * 0.2)
                    hitRects.append((box.insetBy(dx: -inflate, dy: -inflate), r.source))
                }
                drawText(r.text, fText, bx + checkD + checkGap, ry, r.done ? doneInk : ink, strike: r.done)
            }
        }
    }

    func hit(_ p: NSPoint) -> Int? { hitRects.first { $0.rect.contains(p) }?.source }

    override func mouseDown(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        if let src = hit(p) {
            if toggleTodo(at: src) { controller?.refresh(force: true) }
            return
        }
        if event.clickCount == 2 { controller?.editTodo(); return }
        window?.performDrag(with: event)
    }

    override func menu(for event: NSEvent) -> NSMenu? { controller?.buildMenu() }
}

// MARK: - Alert overlay (countdown / alarm reached)

final class AlertView: NSView {
    var title = "", detail = ""
    var x: CGFloat = 200, y: CGFloat = 200, vx: CGFloat = 260, vy: CGFloat = 190
    var font = cfg.fontFamily
    var onStop: (() -> Void)?
    var last = Date(), nextFont = Date()
    var timer: Timer?
    var start = Date()
    let families = NSFontManager.shared.availableFontFamilies

    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }
    override func keyDown(with event: NSEvent) { onStop?() }
    override func mouseDown(with event: NSEvent) { onStop?() }

    func begin() {
        vx = CGFloat.random(in: 220...380) * (Bool.random() ? 1 : -1)
        vy = CGFloat.random(in: 160...300) * (Bool.random() ? 1 : -1)
        x = bounds.midX - 200; y = bounds.midY - 100
        timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 60, repeats: true) { [weak self] _ in self?.tick() }
        RunLoop.main.add(timer!, forMode: .common)
    }
    func end() { timer?.invalidate(); timer = nil }

    func cardSize() -> NSSize {
        let t = NSAttributedString(string: title, attributes: [.font: makeFont(font, 56, light: true)]).size()
        let d = NSAttributedString(string: detail, attributes: [.font: makeFont(font, 26)]).size()
        return NSSize(width: max(t.width, d.width) + 90, height: t.height + d.height + 70)
    }

    func tick() {
        let now = Date(), dt = CGFloat(now.timeIntervalSince(last)); last = now
        let s = cardSize()
        x += vx * dt; y += vy * dt
        if x < 0 { x = 0; vx = abs(vx) }; if x + s.width > bounds.width { x = bounds.width - s.width; vx = -abs(vx) }
        if y < 0 { y = 0; vy = abs(vy) }; if y + s.height > bounds.height { y = bounds.height - s.height; vy = -abs(vy) }
        if now >= nextFont {
            font = families.randomElement() ?? cfg.fontFamily
            nextFont = now.addingTimeInterval(0.45)
        }
        needsDisplay = true
    }

    override func draw(_ dirtyRect: NSRect) {
        NSColor.black.withAlphaComponent(0.35).setFill(); bounds.fill()
        let s = cardSize()
        let rect = NSRect(x: x, y: y, width: s.width, height: s.height)
        let hue = CGFloat((Date().timeIntervalSince(start) * 0.4).truncatingRemainder(dividingBy: 1))
        let p = NSBezierPath(roundedRect: rect, xRadius: 28, yRadius: 28)
        NSColor(hue: hue, saturation: 0.55, brightness: 0.25, alpha: 0.93).setFill(); p.fill()
        NSColor(hue: hue, saturation: 0.6, brightness: 1, alpha: 1).setStroke(); p.lineWidth = 3; p.stroke()
        let ta: [NSAttributedString.Key: Any] = [.font: makeFont(font, 56, light: true), .foregroundColor: NSColor.white]
        let da: [NSAttributedString.Key: Any] = [.font: makeFont(font, 26), .foregroundColor: NSColor(white: 0.85, alpha: 1)]
        let tsz = NSAttributedString(string: title, attributes: ta).size()
        NSAttributedString(string: title, attributes: ta).draw(at: NSPoint(x: rect.minX + 45, y: rect.minY + 30))
        NSAttributedString(string: detail, attributes: da).draw(at: NSPoint(x: rect.minX + 45, y: rect.minY + 30 + tsz.height + 10))
    }
}

final class AlertWindow: NSWindow {
    override var canBecomeKey: Bool { true }
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
    var alertWindow: AlertWindow?
    var alertView: AlertView?
    var alertSound: Timer?
    var lastMTime: Date?

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
        refresh()
        if cfg.displayMode == 1 { window.orderFrontRegardless() }
    }

    // Position persistence (top-left, measured down from the top of the main screen)
    func windowDidMove(_ notification: Notification) {
        NSObject.cancelPreviousPerformRequests(withTarget: self, selector: #selector(saveConfig), object: nil)
        perform(#selector(saveConfig), with: nil, afterDelay: 0.4)
    }

    @objc func saveConfig() {
        guard alertWindow == nil, let scr = NSScreen.main else { return }
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

    func buildMenu() -> NSMenu {
        let m = NSMenu()
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
            if alertWindow == nil { add(m, "取消倒计时/闹钟  Cancel", #selector(cancelReminder)) }
        }
        if alertWindow != nil { add(m, "停止提醒  Stop alert", #selector(cancelReminder)) }
        m.addItem(.separator())
        let mode = NSMenu()
        add(mode, "附着桌面  Attach to desktop", #selector(setMode(_:)), 0, checked: cfg.displayMode == 0)
        add(mode, "始终置顶  Always on top", #selector(setMode(_:)), 1, checked: cfg.displayMode == 1)
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
        let parts = v.split(separator: ":", omittingEmptySubsequences: false).map { Int($0) }
        var secs: Int?
        if parts.allSatisfy({ $0 != nil && $0! >= 0 }) {
            let p = parts.map { $0! }
            switch p.count {
            case 1: secs = p[0] * 60
            case 2: secs = p[0] * 60 + p[1]
            case 3: secs = p[0] * 3600 + p[1] * 60 + p[2]
            default: break
            }
        }
        if let secs, secs > 0 { armCountdown(secs) } else { info("请输入有效的倒计时时长。") }
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
        if alertWindow != nil { stopAlert() } else { armed = false; isCountdown = false; interval = 0 }
    }

    func checkReminder() {
        guard armed, Date() >= due else { return }
        let repeating = isCountdown && cfg.countdownRepeat && interval > 0
        func scheduleNext() {
            var next = due.addingTimeInterval(TimeInterval(interval))
            while next <= Date() { next = next.addingTimeInterval(TimeInterval(interval)) }
            due = next; remTitle = "倒计时结束"; remDetail = "重复倒计时 \(formatDuration(interval)) 已到"
        }
        if alertWindow != nil { if repeating { scheduleNext() }; return }
        let t = remTitle, d = remDetail
        armed = false
        startAlert(t, d)
        if repeating { armed = true; scheduleNext() } else { isCountdown = false; interval = 0 }
    }

    func startAlert(_ title: String, _ detail: String) {
        guard let scr = NSScreen.main else { return }
        let w = AlertWindow(contentRect: scr.frame, styleMask: [.borderless], backing: .buffered, defer: false)
        w.isOpaque = false; w.backgroundColor = .clear; w.hasShadow = false
        w.level = .screenSaver
        w.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]
        let v = AlertView(frame: NSRect(origin: .zero, size: scr.frame.size))
        v.title = title; v.detail = detail
        v.onStop = { [weak self] in self?.stopAlert() }
        w.contentView = v
        alertWindow = w; alertView = v
        NSApp.activate(ignoringOtherApps: true)
        w.makeKeyAndOrderFront(nil)
        w.makeFirstResponder(v)
        v.begin()
        NSSound(named: "Glass")?.play()
        alertSound = Timer.scheduledTimer(withTimeInterval: 1.5, repeats: true) { _ in NSSound(named: "Glass")?.play() }
    }

    func stopAlert() {
        alertSound?.invalidate(); alertSound = nil
        alertView?.end()
        alertWindow?.orderOut(nil)
        alertWindow = nil; alertView = nil
        window.orderFrontRegardless()
    }
}

// MARK: - Main

let app = NSApplication.shared
app.setActivationPolicy(.accessory)
let controller = AppController()
app.delegate = controller
app.run()
