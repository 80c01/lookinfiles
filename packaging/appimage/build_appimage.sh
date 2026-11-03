#!/usr/bin/env bash
#
# Build an AppImage for LookinFiles using linuxdeploy and linuxdeploy-plugin-gtk.
# Usage: ./packaging/appimage/build_appimage.sh [output_directory]
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUTPUT_DIR="${1:-${ROOT_DIR}/build}"
APP_DIR="${ROOT_DIR}/build/AppDir"

echo "==> Building LookinFiles..."
cd "${ROOT_DIR}"
make -j"$(nproc 2>/dev/null || echo 2)"

echo "==> Installing into AppDir: ${APP_DIR}..."
rm -rf "${APP_DIR}"
mkdir -p "${APP_DIR}"
make install DESTDIR="${APP_DIR}" PREFIX=/usr

mkdir -p "${OUTPUT_DIR}"

TOOLS_DIR="${ROOT_DIR}/build/tools"
mkdir -p "${TOOLS_DIR}"

LINUXDEPLOY="${TOOLS_DIR}/linuxdeploy-x86_64.AppImage"
GTK_PLUGIN="${TOOLS_DIR}/linuxdeploy-plugin-gtk.sh"

if [ ! -f "${LINUXDEPLOY}" ]; then
    echo "==> Fetching linuxdeploy..."
    curl -fsSL -o "${LINUXDEPLOY}" \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
    chmod +x "${LINUXDEPLOY}"
fi

if [ ! -f "${GTK_PLUGIN}" ]; then
    echo "==> Fetching linuxdeploy-plugin-gtk..."
    curl -fsSL -o "${GTK_PLUGIN}" \
        "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh"
    chmod +x "${GTK_PLUGIN}"
fi

echo "==> Packaging AppImage with linuxdeploy..."
cd "${OUTPUT_DIR}"

# Export required environment variables for linuxdeploy-plugin-gtk
export DEPLOY_GTK_VERSION="4"
export OUTPUT="${OUTPUT_DIR}/LookinFiles-x86_64.AppImage"

# Run linuxdeploy with GTK plugin
PATH="${TOOLS_DIR}:${PATH}" "${LINUXDEPLOY}" \
    --appdir "${APP_DIR}" \
    --desktop-file "${APP_DIR}/usr/share/applications/io.github._80c01.LookinFiles.desktop" \
    --icon-file "${APP_DIR}/usr/share/icons/hicolor/scalable/apps/io.github._80c01.LookinFiles.svg" \
    --plugin gtk \
    --output appimage

echo "==> AppImage created successfully: ${OUTPUT}"
