#!/usr/bin/env bash
# Build del frontend macOS (Swift/AppKit) + assemblaggio del bundle .app.
#
# Passi:
#   1) CMake compila il MOTORE e la sua C ABI (target motore_c e dipendenze) in una
#      libreria statica, riusando core/ (nessun sorgente C++ duplicato).
#   2) swiftc compila i sorgenti Swift, importando il modulo C `CMotore` e linkando
#      le librerie statiche del core + i framework AppKit/ApplicationServices/CoreGraphics.
#   3) Si assembla SmartOneHandWriter.app con Info.plist e i dati in Contents/Resources/data.
#   4) (Opzionale) firma con l'identità in $CODESIGN_ID.
#
# Prerequisiti: macOS con Xcode Command Line Tools (swiftc, cmake). Eseguire su un Mac.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
BUILD="$REPO/build"
APP="$BUILD/SmartOneHandWriter.app"

echo "==> [1/4] CMake: build del MOTORE + C ABI (motore_c)"
cmake -S "$REPO" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD" --target motore_c -j

echo "==> [2/4] swiftc: build del frontend Swift"
mkdir -p "$BUILD"
swiftc -O \
    -I "$HERE/CMotore" \
    -Xcc -I"$REPO/core/include" \
    "$HERE"/Sources/*.swift \
    -L "$BUILD/core" -lmotore_c -lmotore_core -lsohw_core -lonehand_core -lc++ \
    -framework AppKit -framework ApplicationServices -framework CoreGraphics \
    -o "$BUILD/SmartOneHandWriter"

echo "==> [3/4] Assemblaggio del bundle .app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources/data"
cp "$HERE/Info.plist" "$APP/Contents/Info.plist"
cp "$BUILD/SmartOneHandWriter" "$APP/Contents/MacOS/SmartOneHandWriter"
# Dati accanto all'app (wordlist, modello bigrammi se presente, conf dei tasti).
for f in wordlist_it.txt it.bigrams.bin tasti.conf config.json; do
    [ -f "$REPO/data/$f" ] && cp "$REPO/data/$f" "$APP/Contents/Resources/data/$f" || true
done

echo "==> [4/4] Firma"
if [ -n "${CODESIGN_ID:-}" ]; then
    codesign --force --deep --options runtime --sign "$CODESIGN_ID" "$APP"
    echo "    firmato con: $CODESIGN_ID"
    echo "    (per la distribuzione: notarizzare con 'xcrun notarytool submit')"
else
    # Firma ad-hoc: sufficiente per l'uso locale; per l'Accessibilità è meglio una
    # identità Developer ID stabile (la firma ad-hoc cambia a ogni rebuild).
    codesign --force --deep --sign - "$APP" || true
    echo "    firma ad-hoc (imposta CODESIGN_ID='Developer ID Application: ...' per firmare)"
fi

echo "==> Fatto: $APP"
echo "    Avvia con: open '$APP'  (poi concedi Accessibilità e premi Play)"
