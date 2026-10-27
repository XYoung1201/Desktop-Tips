#!/bin/bash
# Builds DesktopTip.app (native macOS). Usage: ./build.sh   →  macOS/build/DesktopTip.app
set -euo pipefail
cd "$(dirname "$0")"

APP=build/DesktopTip.app
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

# Universal binary (Apple Silicon + Intel); falls back to the host arch if the Intel toolchain is unavailable
swiftc -O -swift-version 5 -target arm64-apple-macos13.0 main.swift -o build/DesktopTip-arm64
if swiftc -O -swift-version 5 -target x86_64-apple-macos13.0 main.swift -o build/DesktopTip-x86_64 2>/dev/null; then
  lipo -create build/DesktopTip-arm64 build/DesktopTip-x86_64 -output "$APP/Contents/MacOS/DesktopTip"
else
  echo "x86_64 slice unavailable here; building arm64 only"
  cp build/DesktopTip-arm64 "$APP/Contents/MacOS/DesktopTip"
fi
rm -f build/DesktopTip-arm64 build/DesktopTip-x86_64

# Icon from ../Tip.png
if [ -f ../Tip.png ]; then
  ICONSET=build/AppIcon.iconset
  rm -rf "$ICONSET"; mkdir -p "$ICONSET"
  for s in 16 32 128 256 512; do
    sips -z $s $s ../Tip.png --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
    sips -z $((s*2)) $((s*2)) ../Tip.png --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
  done
  iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"
  rm -rf "$ICONSET"
fi

cat > "$APP/Contents/Info.plist" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>DesktopTip</string>
  <key>CFBundleDisplayName</key><string>DesktopTip</string>
  <key>CFBundleIdentifier</key><string>top.hogn.DesktopTip</string>
  <key>CFBundleExecutable</key><string>DesktopTip</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>LSUIElement</key><true/>
  <key>NSHighResolutionCapable</key><true/>
</dict></plist>
EOF

codesign --force --deep --sign - "$APP" >/dev/null 2>&1 || true
echo "Built $APP"
