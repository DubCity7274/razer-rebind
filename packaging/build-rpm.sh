#!/usr/bin/env bash
# Build the RPM from the committed HEAD. Packages land in dist/.
#
#   ./packaging/build-rpm.sh           build in a clean Fedora container (podman);
#                                      FEDORA=43 ./packaging/build-rpm.sh for another release
#   ./packaging/build-rpm.sh --direct  build on this machine, which must already have
#                                      rpm-build and the spec's BuildRequires (used by CI)
set -euo pipefail

cd "$(dirname "$0")/.."
spec=packaging/razer-rebind.spec
version=$(sed -n 's/^Version: *//p' "$spec")

make_tarball() {
    if ! git diff --quiet HEAD; then
        echo "note: uncommitted changes are not included (the build uses HEAD)" >&2
    fi
    mkdir -p dist
    git archive --prefix="razer-rebind-$version/" -o "dist/razer-rebind-$version.tar.gz" HEAD
}

run_rpmbuild() {
    rpmbuild -ba "$spec" \
        --define "_sourcedir $PWD/dist" \
        --define "_rpmdir $PWD/dist" \
        --define "_srcrpmdir $PWD/dist" \
        --define "_builddir ${TMPDIR:-/tmp}/rpmbuild" \
        --define "_build_name_fmt %%{NAME}-%%{VERSION}-%%{RELEASE}.%%{ARCH}.rpm"
}

case "${1:-}" in
--direct)
    make_tarball
    run_rpmbuild
    ;;
--rpmbuild-only)
    # Internal: second half of the container build below.
    run_rpmbuild
    exit 0
    ;;
"")
    make_tarball
    podman run --rm -v "$PWD:/src:Z" -w /src "registry.fedoraproject.org/fedora:${FEDORA:-44}" bash -euc "
        dnf -y -q install rpm-build 'dnf-command(builddep)' >/dev/null
        dnf -y -q builddep $spec >/dev/null
        ./packaging/build-rpm.sh --rpmbuild-only"
    ;;
*)
    echo "usage: $0 [--direct]" >&2
    exit 2
    ;;
esac

echo
echo "Built:"
ls -1 dist/*.rpm
