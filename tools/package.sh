#!/usr/bin/env bash
# Builds the Windows release package from a finished build (MSYS2 UCRT64):
#   package/FastShell-<version>-windows-x64.zip         (folder FastShell/ inside)
#   package/FastShell-<version>-windows-x64.zip.sha256
# Usage: bash tools/package.sh [build-dir] [output-dir]
set -euo pipefail

build=${1:-build}
out=${2:-package}
root=$(cd "$(dirname "$0")/.." && pwd)

version=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build/CMakeCache.txt")
[ -n "$version" ] || { echo "Cannot read the version from $build/CMakeCache.txt" >&2; exit 1; }
name="FastShell-$version-windows-x64.zip"
stage="$out/FastShell"

rm -rf "$out"
mkdir -p "$stage"
cp "$build/src/FastShell.exe" "$stage/"
windeployqt6 --release --no-translations --no-system-d3d-compiler --no-opengl-sw "$stage/FastShell.exe" > /dev/null

# The other DLLs from MSYS2 (libssh, OpenSSL, GCC runtime...), until none is missing.
copy_missing() {
    local added=0
    for dll in $(for f in "$stage"/*.exe "$stage"/*.dll "$stage"/*/*.dll; do
                     ldd "$f" 2>/dev/null | awk '/ucrt64\/bin\//{print $3}'
                 done | sort -u); do
        if [ ! -f "$stage/$(basename "$dll")" ]; then
            cp "$dll" "$stage/"
            added=1
        fi
    done
    return $((1 - added))
}
while copy_missing; do :; done

cp "$root/LICENSE" "$root/NOTICE" "$stage/"
cp "$root/install.ps1" "$stage/"   # also used to uninstall

(cd "$out" && powershell.exe -NoProfile -Command "Compress-Archive -Path FastShell -DestinationPath '$name' -Force")
(cd "$out" && sha256sum "$name" | sed 's/ \*/  /' > "$name.sha256")
echo "$out/$name"
cat "$out/$name.sha256"
