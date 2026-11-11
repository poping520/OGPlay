#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo=$(CDPATH= cd -- "$root/../../../.." && pwd)
: "${ANDROID_JAR:=$repo/.local/toolchain/android-19/android.jar}"
: "${D8_JAR:=$repo/.local/toolchain/build-tools-29.0.2/d8.jar}"
: "${AAPT:=aapt}"
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
mkdir -p "$build/classes" "$build/dex" "$build/res/raw"
cp "$repo/tests/fixtures/video/h264-silent.mp4" "$build/res/raw/fixture.mp4"
javac --release 8 -cp "$ANDROID_JAR" -d "$build/classes" "$root"/*.java
java -cp "$D8_JAR" com.android.tools.r8.D8 --min-api 19 --lib "$ANDROID_JAR" --output "$build/dex" "$build/classes"/org/ogplay/fixture/window/*.class
"$AAPT" package -f -M "$root/AndroidManifest.xml" -I "$ANDROID_JAR" -S "$build/res" -F "$build/base.apk"
python3 - "$build" "$root/../rom_window.apk" <<'PY'
import pathlib
import sys
import zipfile

build = pathlib.Path(sys.argv[1])
with zipfile.ZipFile(build / 'base.apk') as source, zipfile.ZipFile(sys.argv[2], 'w') as target:
    for name in sorted(source.namelist()):
        target.writestr(zipfile.ZipInfo(name), source.read(name))
    target.writestr(zipfile.ZipInfo('classes.dex'), (build / 'dex/classes.dex').read_bytes())
PY
