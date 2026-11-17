#!/usr/bin/env bash
# Gunsmith3D for Linux: installs, updates, launches and uninstalls the native build, for your own user (no sudo).
#
#   Install (or update):  curl -fsSL https://github.com/LusciousLuchs/Project-Gunsmith-3D/releases/latest/download/install-linux.sh | bash
#   Play:                 gunsmith3d   (or from your desktop's app menu)
#   Update now:           gunsmith3d --update
#   Uninstall:            gunsmith3d --uninstall
#
# Where things go:
#   the game        ~/.local/share/gunsmith3d       (its version in VERSION there)
#   the command     ~/.local/bin/gunsmith3d
#   the menu entry  ~/.local/share/applications/gunsmith3d.desktop (and its icon)
#   your files      Documents/OAB Foundry/Gunsmith  (designs, maps, settings: never touched by any of this)
#
# Each time you start it, it looks for a newer version first (a few seconds at most) and updates itself, like the
# Windows version. Updates aren't optional; offline, the version you have starts.
set -euo pipefail

REPOS="LusciousLuchs/Gunsmith3D-Releases LusciousLuchs/Project-Gunsmith-3D"   # (the downloads page, then the code's own)
ASSET="Gunsmith3D-Linux.tar.gz"
API="${GUNSMITH_GITHUB_API:-https://api.github.com}"   # (testing: another server standing in for GitHub)
WEB="${GUNSMITH_GITHUB_WEB:-https://github.com}"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
DIR="$DATA/gunsmith3d"
BIN="$HOME/.local/bin"
APPS="$DATA/applications"
ICONS="$DATA/icons/hicolor/256x256/apps"

say() { printf '%s\n' "$*" >&2; }
need() { command -v "$1" >/dev/null 2>&1 || { say "Gunsmith3D needs '$1' (install it with your package manager)."; exit 1; }; }
notify() { command -v notify-send >/dev/null 2>&1 && notify-send -a Gunsmith3D "Gunsmith3D" "$1" 2>/dev/null || true; say "$1"; }
installed() { cat "$DIR/VERSION" 2>/dev/null || echo ""; }
newer() { [ "$1" != "$2" ] && [ "$(printf '%s\n%s\n' "$1" "$2" | sed 's/-.*//' | sort -V | tail -n1)" = "$(printf '%s' "$1" | sed 's/-.*//')" ]; }

# the newest release among the repositories: prints "<repo> <tag>" (nothing if none can be reached)
latest() {
    local best="" bestRepo="" r tag
    for r in $REPOS; do
        tag=$(curl -fsSL --max-time "${1:-15}" -H "Accept: application/vnd.github+json" "$API/repos/$r/releases/latest" 2>/dev/null |
              sed -n 's/.*"tag_name": *"\([^"]*\)".*/\1/p' | head -n1) || true
        [ -z "$tag" ] && continue
        if [ -z "$best" ] || newer "${tag#v}" "${best#v}"; then best="$tag"; bestRepo="$r"; fi
    done
    [ -n "$best" ] && printf '%s %s\n' "$bestRepo" "$best"
}

# downloads a release and puts it in place: the game's own files replaced, the sample files only added if missing
fetch() {
    local repo="$1" tag="$2" tmp
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' RETURN
    say "Downloading Gunsmith3D ${tag#v}..."
    curl -fL --progress-bar -o "$tmp/g.tar.gz" "$WEB/$repo/releases/download/$tag/$ASSET"
    tar -xzf "$tmp/g.tar.gz" -C "$tmp"
    local src="$tmp/Gunsmith3D"
    [ -x "$src/gunsmith3d" ] || { say "The download didn't hold the game."; return 1; }
    mkdir -p "$DIR"
    if [ -d "$src/user" ]; then mkdir -p "$DIR/user"; cp -rn "$src/user/." "$DIR/user/" 2>/dev/null || true; rm -rf "$src/user"; fi
    cp -rf "$src/." "$DIR/"
    printf '%s\n' "${tag#v}" > "$DIR/VERSION"
}

# the command, the menu entry and the icon
links() {
    mkdir -p "$BIN" "$APPS" "$ICONS"
    cp -f "$DIR/install-linux.sh" "$DIR/.launcher.sh" 2>/dev/null || true
    chmod +x "$DIR/.launcher.sh" "$DIR/gunsmith3d" 2>/dev/null || true
    cat > "$BIN/gunsmith3d" <<EOF
#!/usr/bin/env bash
case "\${1:-}" in --update|--uninstall) exec bash "$DIR/.launcher.sh" "\$1" ;; esac
exec bash "$DIR/.launcher.sh" --launch "\$@"
EOF
    chmod +x "$BIN/gunsmith3d"
    [ -f "$DIR/gunsmith3d.png" ] && cp -f "$DIR/gunsmith3d.png" "$ICONS/gunsmith3d.png"
    cat > "$APPS/gunsmith3d.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Gunsmith3D
Comment=Design firearm mechanisms part by part, then test and shoot them
Exec=$BIN/gunsmith3d
Icon=gunsmith3d
Terminal=false
Categories=Game;Simulation;
StartupWMClass=Gunsmith3D $(installed)
EOF
    command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APPS" >/dev/null 2>&1 || true
}


case "${1:-}" in
--launch)
    shift
    if command -v curl >/dev/null 2>&1; then   # (updates aren't optional: always the newest)
        if found=$(latest 4) && [ -n "$found" ]; then
            repo=${found% *}; tag=${found#* }
            if newer "${tag#v}" "$(installed)"; then
                notify "Updating to ${tag#v}..."
                fetch "$repo" "$tag" && links || notify "The update didn't work; starting the version you have."
            fi
        fi
    fi
    cd "$DIR"
    exec ./gunsmith3d "$@"
    ;;
--update)
    need curl; need tar
    found=$(latest) || { say "Couldn't reach GitHub (offline?)."; exit 1; }
    repo=${found% *}; tag=${found#* }
    if newer "${tag#v}" "$(installed)"; then fetch "$repo" "$tag"; links; say "Updated to ${tag#v}."
    else say "Gunsmith3D $(installed) is the newest."; fi
    ;;
--uninstall)
    rm -rf "$DIR" "$BIN/gunsmith3d" "$APPS/gunsmith3d.desktop" "$ICONS/gunsmith3d.png"
    command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APPS" >/dev/null 2>&1 || true
    say "Gunsmith3D is uninstalled. Your designs, maps and settings are still in Documents/OAB Foundry/Gunsmith."
    ;;
""|--install)
    need curl; need tar
    [ "$(uname -m)" = "x86_64" ] || { say "Gunsmith3D's Linux build is for 64-bit PCs (x86_64) only."; exit 1; }
    if [ -n "${GUNSMITH_FROM:-}" ]; then   # (testing) install from a package on disk
        tmp=$(mktemp -d); tar -xzf "$GUNSMITH_FROM" -C "$tmp"; mkdir -p "$DIR"
        if [ -d "$tmp/Gunsmith3D/user" ]; then mkdir -p "$DIR/user"; cp -rn "$tmp/Gunsmith3D/user/." "$DIR/user/"; rm -rf "$tmp/Gunsmith3D/user"; fi
        cp -rf "$tmp/Gunsmith3D/." "$DIR/"; rm -rf "$tmp"
        [ -f "$DIR/VERSION" ] || echo "0" > "$DIR/VERSION"
    else
        found=$(latest) || { say "Couldn't reach GitHub (offline?)."; exit 1; }
        fetch "${found% *}" "${found#* }"
    fi
    links
    say ""
    say "Gunsmith3D $(installed) is installed. Start it from your app menu, or type: gunsmith3d"
    case ":$PATH:" in *":$BIN:"*) ;; *) say "(If the command isn't found, add $BIN to your PATH, or log out and back in.)";; esac
    ;;
*)
    say "Usage: install-linux.sh [--update | --uninstall]"; exit 1 ;;
esac
