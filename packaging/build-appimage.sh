#!/usr/bin/env bash
# Build the AppImage from the committed HEAD with linuxdeploy, bundling Qt
# (including the X11 and Wayland platform plugins) and libevdev. It is built on
# Debian 12 so it runs on that and anything newer. The AppImage lands in dist/.
#
#   ./packaging/build-appimage.sh           build in a clean Debian 12 container (podman)
#   ./packaging/build-appimage.sh --direct  build on this machine (used by CI)
set -euo pipefail

cd "$(dirname "$0")/.."
version=$(sed -n 's/^project(razer-rebind VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
deps="build-essential cmake pkg-config qt6-base-dev qt6-base-dev-tools qt6-qpa-plugins qt6-wayland
      libevdev-dev git ca-certificates curl file"

# Pinned tool releases, verified by checksum before they run.
linuxdeploy_url=https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage
linuxdeploy_sha256=c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
plugin_qt_url=https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage
plugin_qt_sha256=15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724

fetch() {
    local url=$1 sha=$2 dest=$3
    curl -fsSL -o "$dest" "$url"
    echo "$sha  $dest" | sha256sum -c --quiet
    chmod +x "$dest"
}

build_direct() {
    local work tools
    work=$(mktemp -d)
    tools="$work/tools"
    mkdir -p "$tools"
    fetch "$linuxdeploy_url" "$linuxdeploy_sha256" "$tools/linuxdeploy-x86_64.AppImage"
    fetch "$plugin_qt_url" "$plugin_qt_sha256" "$tools/linuxdeploy-plugin-qt-x86_64.AppImage"

    git archive HEAD | tar -x -C "$work"
    cmake -S "$work" -B "$work/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build "$work/build" --parallel
    DESTDIR="$work/AppDir" cmake --install "$work/build"

    # Containers and CI have no FUSE; the tools can unpack themselves instead.
    export APPIMAGE_EXTRACT_AND_RUN=1
    export QMAKE=/usr/lib/qt6/bin/qmake
    export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so"
    export EXTRA_QT_MODULES="waylandcompositor"
    export LINUXDEPLOY_OUTPUT_VERSION="$version"
    (cd "$work" && PATH="$tools:$PATH" linuxdeploy-x86_64.AppImage \
        --appdir AppDir \
        --desktop-file AppDir/usr/share/applications/razer-rebind.desktop \
        --icon-file AppDir/usr/share/icons/hicolor/scalable/apps/razer-rebind.svg \
        --plugin qt --output appimage)

    mkdir -p dist
    mv "$work"/*.AppImage "dist/razer-rebind-$version-x86_64.AppImage"
    rm -rf "$work"
}

case "${1:-}" in
--direct)
    build_direct
    ;;
"")
    podman run --rm -v "$PWD:/src:Z" -w /src -e DEBIAN_FRONTEND=noninteractive docker.io/library/debian:12 bash -euc "
        apt-get update -qq >/dev/null
        apt-get install -y -qq --no-install-recommends $(echo $deps) >/dev/null
        git config --global --add safe.directory /src
        ./packaging/build-appimage.sh --direct"
    ;;
*)
    echo "usage: $0 [--direct]" >&2
    exit 2
    ;;
esac

echo
echo "Built:"
ls -1 dist/*.AppImage
