#!/usr/bin/env bash
#
# Build a Debian package (.deb) for LookinFiles.
# Usage: ./packaging/debian/build_deb.sh [output_directory]
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUTPUT_DIR="${1:-${ROOT_DIR}/build}"

VERSION=$(sed -n 's/^\#define APP_VERSION *"\(.*\)"/\1/p' "${ROOT_DIR}/src/app_paths.h")
ARCH=$(dpkg --print-architecture 2>/dev/null || uname -m)
case "${ARCH}" in
    x86_64) ARCH="amd64" ;;
    aarch64) ARCH="arm64" ;;
esac

echo "==> Building LookinFiles v${VERSION} (${ARCH})..."
cd "${ROOT_DIR}"
make -j"$(nproc 2>/dev/null || echo 2)"

PKG_DIR="${ROOT_DIR}/build/pkg-deb"
rm -rf "${PKG_DIR}"
mkdir -p "${PKG_DIR}"

make install DESTDIR="${PKG_DIR}" PREFIX=/usr

mkdir -p "${PKG_DIR}/DEBIAN"
mkdir -p "${PKG_DIR}/usr/share/doc/lookinfiles"

install -Dm644 "${ROOT_DIR}/LICENSE" "${PKG_DIR}/usr/share/doc/lookinfiles/copyright"
install -Dm644 "${SCRIPT_DIR}/changelog" "${PKG_DIR}/usr/share/doc/lookinfiles/changelog.Debian"
gzip -9 -n "${PKG_DIR}/usr/share/doc/lookinfiles/changelog.Debian"

INSTALLED_SIZE=$(du -ks "${PKG_DIR}" | cut -f1)

# The libgit2 package name carries its soname (libgit2-1.9, libgit2-1.5...):
# take the one the binary was linked with
LIBGIT2_SO=$(ldd "${ROOT_DIR}/lookinfiles" | awk '/libgit2/ {print $3; exit}')
LIBGIT2_PKG=$(dpkg -S "${LIBGIT2_SO}" 2>/dev/null | head -n1 | cut -d: -f1)
LIBGIT2_PKG=${LIBGIT2_PKG:-libgit2-1.9}

cat <<EOF > "${PKG_DIR}/DEBIAN/control"
Package: lookinfiles
Version: ${VERSION}-1
Section: utils
Priority: optional
Architecture: ${ARCH}
Installed-Size: ${INSTALLED_SIZE}
Maintainer: 80c01 <noreply@github.com>
Depends: libgtk-4-1 (>= 4.12.0), libglib2.0-0 (>= 2.74.0), libc6 (>= 2.34), ${LIBGIT2_PKG}
Recommends: sshfs, libvte-2.91-gtk4-0, libgtksourceview-5-0, libpoppler-glib8, libsqlite3-0, libarchive13, libwebkitgtk-6.0-4
Description: Fast, keyboard-driven GTK 4 file explorer
 LookinFiles ("look in your files") is a keyboard-driven file explorer in C with
 GTK 4, featuring a rich preview panel, text editors in Notepad, Emacs or Vim mode,
 embedded terminal panes, search in files, Git status, and workspaces.
EOF

mkdir -p "${OUTPUT_DIR}"
DEB_FILE="${OUTPUT_DIR}/lookinfiles_${VERSION}-1_${ARCH}.deb"

echo "==> Packaging .deb with dpkg-deb..."
dpkg-deb --build --root-owner-group "${PKG_DIR}" "${DEB_FILE}"

echo "==> Package successfully created: ${DEB_FILE}"
