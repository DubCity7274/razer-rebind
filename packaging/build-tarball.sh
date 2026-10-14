#!/usr/bin/env bash
# Build the pre-compiled tarball for other distributions from the committed
# HEAD. It is built on Debian 12 so it runs on that and anything newer (glibc
# 2.36, Qt 6.4 or later). The archive lands in dist/.
#
#   ./packaging/build-tarball.sh           build in a clean Debian 12 container (podman)
#   ./packaging/build-tarball.sh --direct  build on this machine (used by CI)
set -euo pipefail

cd "$(dirname "$0")/.."
version=$(sed -n 's/^project(razer-rebind VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
deps="build-essential cmake pkg-config qt6-base-dev libevdev-dev git ca-certificates"

build_direct() {
    local work name stage
    work=$(mktemp -d)
    git archive HEAD | tar -x -C "$work"
    cmake -S "$work" -B "$work/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
    cmake --build "$work/build" --parallel

    name="razer-rebind-$version-linux-$(uname -m)"
    stage="$work/$name"
    mkdir -p "$stage"
    install -m755 -s "$work/build/razer-rebind" "$stage/"
    install -m644 "$work/build/razer-rebind.service" "$work/data/razer-rebind.desktop" \
        "$work/data/70-razer-rebind.rules" "$work/data/razer-rebind.svg" "$work/README.md" "$work/LICENSE" "$stage/"
    install -m755 "$work/packaging/tarball/install.sh" "$work/packaging/tarball/uninstall.sh" "$stage/"
    mkdir -p dist
    tar -C "$work" -czf "dist/$name.tar.gz" --owner=0 --group=0 "$name"
    rm -rf "$work"
}

case "${1:-}" in
--direct)
    build_direct
    ;;
"")
    podman run --rm -v "$PWD:/src:Z" -w /src -e DEBIAN_FRONTEND=noninteractive docker.io/library/debian:12 bash -euc "
        apt-get update -qq >/dev/null
        apt-get install -y -qq --no-install-recommends $deps >/dev/null
        git config --global --add safe.directory /src
        ./packaging/build-tarball.sh --direct"
    ;;
*)
    echo "usage: $0 [--direct]" >&2
    exit 2
    ;;
esac

echo
echo "Built:"
ls -1 dist/*.tar.gz
