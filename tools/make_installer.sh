#!/usr/bin/env bash
# Builds the Windows installer (installer/gunsmith3d.nsi, NSIS) from a Windows build of the game.
#   tools/make_installer.sh <path to gunsmith3d.exe> [output folder]
# Needs makensis (apt install nsis). Writes Gunsmith3D-<version>-setup.exe.
set -euo pipefail
exe="$1"; out="${2:-.}"
root="$(cd "$(dirname "$0")/.." && pwd)"
v=$(sed -n 's/^project(Gunsmith3D VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")
stage_name=$(sed -n 's/^set(GUNSMITH_STAGE "\([a-z]*\)".*/\1/p' "$root/CMakeLists.txt")
full="$v${stage_name:+-$stage_name}"
st="$(mktemp -d)"
trap 'rm -rf "$st"' EXIT
cp "$exe" "$st/gunsmith3d.exe"
cp "$root/README.md" "$root/CHANGELOG.md" "$root/BACKLOG.md" "$root/assets/logo/gunsmith3d.ico" "$st/"
cp -r "$root/licenses" "$st/licenses"
cp -r "$root/dist/user" "$st/user"
# what the uninstaller removes from the sample files (only those: anything a player added stays)
( cd "$st/user" && find . -type f | sed 's|^\./||' | while read -r f; do printf '  Delete "$INSTDIR\\user\\%s"\n' "${f//\//\\}"; done
  find . -mindepth 1 -type d | sort -r | sed 's|^\./||' | while read -r d; do printf '  RMDir "$INSTDIR\\user\\%s"\n' "${d//\//\\}"; done ) > "$st/uninstall_samples.nsh"
mkdir -p "$out"
outabs="$(cd "$out" && pwd)/Gunsmith3D-$full-setup.exe"
makensis -V2 -DVERSION="$full" -DVERNUM="$v.0" -DSTAGE="$st" -DOUT="$outabs" "$root/installer/gunsmith3d.nsi"
echo "$outabs"
