#!/bin/sh
# Prints the GitHub release text for a version: its CHANGELOG.md section and
# a guide to the downloads. Fails if CHANGELOG.md has no entry for it.
#   ./packaging/release-notes.sh 0.5.0
set -eu
version=$1
root=$(dirname "$0")/..

notes=$(awk -v v="$version" '
    /^## / { if (found) exit; found = ($2 == v) ; next }
    found' "$root/CHANGELOG.md")
if [ -z "$(printf '%s' "$notes" | tr -d '[:space:]')" ]; then
    echo "CHANGELOG.md has no entry for $version" >&2
    exit 1
fi

printf '%s\n' "$notes"
cat <<NOTES

### Downloads

| Your system | File |
|---|---|
| Fedora 44, Nobara | \`razer-rebind-$version-1.fc44.x86_64.rpm\` |
| Fedora 43 | \`razer-rebind-$version-1.fc43.x86_64.rpm\` |
| Debian 13 | \`razer-rebind_$version-1_debian13_amd64.deb\` |
| Debian 12 | \`razer-rebind_$version-1_debian12_amd64.deb\` |
| Ubuntu 26.04 | \`razer-rebind_$version-1_ubuntu26.04_amd64.deb\` |
| Ubuntu 24.04, Linux Mint 22, Pop!_OS 24.04 | \`razer-rebind_$version-1_ubuntu24.04_amd64.deb\` |
| Anything else | \`razer-rebind-$version-x86_64.AppImage\` or \`razer-rebind-$version-linux-x86_64.tar.gz\` |

All of them need the OpenRazer kernel modules. See the
[README](https://github.com/DubCity7274/razer-rebind#installation) for install
steps. \`SHA256SUMS\` lists the checksum of every file.
NOTES
