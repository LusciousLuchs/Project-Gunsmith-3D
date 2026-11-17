#!/usr/bin/env bash
# Packs the native Linux build into Gunsmith3D-Linux.tar.gz (the file installer/install-linux.sh downloads).
#   tools/make_linux_package.sh <path to the Linux gunsmith3d> [output folder]
# Build it with: cmake -S . -B build-linux -DGUNSMITH_NATIVE_APP=ON -DGUNSMITH_LINUX_RELEASE=ON -DCMAKE_BUILD_TYPE=Release
set -euo pipefail
exe="$1"; out="${2:-.}"
root="$(cd "$(dirname "$0")/.." && pwd)"
v=$(sed -n 's/^project(Gunsmith3D VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")
stage=$(sed -n 's/^set(GUNSMITH_STAGE "\([a-z]*\)".*/\1/p' "$root/CMakeLists.txt")
st="$(mktemp -d)"
trap 'rm -rf "$st"' EXIT
d="$st/Gunsmith3D"
mkdir -p "$d"
cp "$exe" "$d/gunsmith3d"
strip "$d/gunsmith3d" 2>/dev/null || true
chmod +x "$d/gunsmith3d"
cp "$root/README.md" "$root/CHANGELOG.md" "$root/BACKLOG.md" "$root/assets/logo/gunsmith3d.png" "$d/"
cp "$root/installer/install-linux.sh" "$d/install-linux.sh"
chmod +x "$d/install-linux.sh"
cp -r "$root/licenses" "$d/licenses"
cp -r "$root/dist/user" "$d/user"
echo "$v${stage:+-$stage}" > "$d/VERSION"
mkdir -p "$out"
tar -czf "$out/Gunsmith3D-Linux.tar.gz" -C "$st" Gunsmith3D
echo "$(cd "$out" && pwd)/Gunsmith3D-Linux.tar.gz"
