#!/bin/sh
# pacchetto_mac.sh - l'app per macOS dalla cartella di build.
#
# Uso: tools/pacchetto_mac.sh <cartella build> <SDL2.framework> <versione> <uscita>
#
# Fa <uscita>/Total Chaos AGA.app (eseguibile in Contents/MacOS, assets e
# tc_modifiche.bin in Contents/Resources, dove li cerca SDL_GetBasePath, e
# SDL2.framework in Contents/Frameworks) firmata ad hoc: senza firma un Mac
# con chip Apple non la apre. I dati installati dal launcher vanno in
# ~/Library/Application Support/TotalChaosAGA711Recomp (amiga_cartella_utente).
set -e
B="$1"; SDL="$2"; VER="$3"; OUT="$4"
APP="$OUT/Total Chaos AGA.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$APP/Contents/Frameworks"
cp "$B/TotalChaosAGA711Recomp" "$APP/Contents/MacOS/"
cp -R "$B/assets" "$APP/Contents/Resources/"
cp "$B/tc_modifiche.bin" "$APP/Contents/Resources/"
cp -R "$SDL" "$APP/Contents/Frameworks/"
cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key><string>TotalChaosAGA711Recomp</string>
    <key>CFBundleIdentifier</key><string>io.github.sirl0ll0.totalchaosaga711recomp</string>
    <key>CFBundleName</key><string>Total Chaos AGA</string>
    <key>CFBundleDisplayName</key><string>Total Chaos AGA</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>$VER</string>
    <key>CFBundleVersion</key><string>$VER</string>
    <key>LSMinimumSystemVersion</key><string>11.0</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>LSApplicationCategoryType</key><string>public.app-category.strategy-games</string>
</dict>
</plist>
EOF
codesign --force --deep --sign - "$APP"
codesign --verify --deep "$APP"
lipo -info "$APP/Contents/MacOS/TotalChaosAGA711Recomp"
otool -L "$APP/Contents/MacOS/TotalChaosAGA711Recomp"
