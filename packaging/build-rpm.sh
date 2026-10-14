#!/usr/bin/env bash
# Build the RPM in a clean Fedora container with podman, from the committed
# HEAD. Packages land in dist/. Set FEDORA=43 etc. to target another release.
set -euo pipefail

cd "$(dirname "$0")/.."
fedora=${FEDORA:-44}
version=$(sed -n 's/^Version: *//p' packaging/razer-rebind.spec)

if ! git diff --quiet HEAD; then
    echo "note: uncommitted changes are not included (the build uses HEAD)" >&2
fi

mkdir -p dist
git archive --prefix="razer-rebind-$version/" -o "dist/razer-rebind-$version.tar.gz" HEAD

podman run --rm -v "$PWD:/src:Z" -w /src "registry.fedoraproject.org/fedora:$fedora" bash -euc '
    dnf -y -q install rpm-build "dnf-command(builddep)" >/dev/null
    dnf -y -q builddep packaging/razer-rebind.spec >/dev/null
    rpmbuild -ba packaging/razer-rebind.spec \
        --define "_sourcedir /src/dist" \
        --define "_rpmdir /src/dist" \
        --define "_srcrpmdir /src/dist" \
        --define "_builddir /tmp/rpmbuild" \
        --define "_build_name_fmt %%{NAME}-%%{VERSION}-%%{RELEASE}.%%{ARCH}.rpm"
'

echo
echo "Built:"
ls -1 dist/*.rpm
