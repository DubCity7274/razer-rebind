#!/usr/bin/env bash
# Build a .deb for one Debian or Ubuntu release from the committed HEAD.
# The package lands in dist/.
#
#   ./packaging/build-deb.sh debian:12             build in a clean container (podman);
#                                                  also debian:13, ubuntu:24.04, ubuntu:26.04
#   ./packaging/build-deb.sh --direct debian12     build on this machine, which must already
#                                                  have the build dependencies (used by CI)
set -euo pipefail

cd "$(dirname "$0")/.."
deps="build-essential cmake pkg-config qt6-base-dev libevdev-dev dpkg-dev file git ca-certificates"

build_direct() {
    local distro=$1 work
    work=$(mktemp -d)
    git archive HEAD | tar -x -C "$work"
    cmake -S "$work" -B "$work/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
        -DRAZER_REBIND_INSTALL_DOCS=ON -DRAZER_REBIND_DEB_DISTRO="$distro"
    cmake --build "$work/build" --parallel
    (cd "$work/build" && cpack -G DEB)
    mkdir -p dist
    cp "$work"/build/razer-rebind_*.deb dist/
    rm -rf "$work"
}

case "${1:-}" in
--direct)
    build_direct "${2:?usage: $0 --direct <distro tag, e.g. debian12>}"
    ;;
*:*)
    image=$1
    distro=${image/:/}   # debian:12 -> debian12
    podman run --rm -v "$PWD:/src:Z" -w /src -e DEBIAN_FRONTEND=noninteractive "docker.io/library/$image" bash -euc "
        apt-get update -qq >/dev/null
        apt-get install -y -qq --no-install-recommends $deps >/dev/null
        git config --global --add safe.directory /src
        ./packaging/build-deb.sh --direct $distro"
    ;;
*)
    echo "usage: $0 <image, e.g. debian:12> | --direct <distro tag>" >&2
    exit 2
    ;;
esac

echo
echo "Built:"
ls -1 dist/*.deb
