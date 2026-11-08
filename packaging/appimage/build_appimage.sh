#!/usr/bin/env bash
#
# Build an AppImage for LookinFiles using linuxdeploy and linuxdeploy-plugin-gtk.
# Usage: ./packaging/appimage/build_appimage.sh [output_directory]
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# Resolve output directory to an absolute path so relative inputs (e.g. artifacts/) work
OUTPUT_ARG="${1:-${ROOT_DIR}/build}"
mkdir -p "${OUTPUT_ARG}"
OUTPUT_DIR="$(cd "${OUTPUT_ARG}" && pwd)"

APP_DIR="${ROOT_DIR}/build/AppDir"
APP_ID="io.github._80c01.LookinFiles"
VERSION=$(sh "${ROOT_DIR}/tools/version.sh")

# Allow AppImages to run in CI / containers without FUSE
export APPIMAGE_EXTRACT_AND_RUN=1

echo "==> Building LookinFiles v${VERSION}..."
cd "${ROOT_DIR}"
make -j"$(nproc 2>/dev/null || echo 2)" VERSION="${VERSION}"

echo "==> Installing into AppDir: ${APP_DIR}..."
rm -rf "${APP_DIR}"
mkdir -p "${APP_DIR}"
make install DESTDIR="${APP_DIR}" PREFIX=/usr VERSION="${VERSION}"

# Ensure 256x256 PNG icon exists and place it at AppDir root (.DirIcon + AppID.png)
ICON_PNG="${APP_DIR}/usr/share/icons/hicolor/256x256/apps/${APP_ID}.png"
if [ ! -f "${ICON_PNG}" ]; then
    mkdir -p "$(dirname "${ICON_PNG}")"
    if command -v gdk-pixbuf-thumbnailer >/dev/null 2>&1; then
        gdk-pixbuf-thumbnailer -s 256 "${ROOT_DIR}/data/icons/hicolor/scalable/apps/${APP_ID}.svg" "${ICON_PNG}"
    elif command -v rsvg-convert >/dev/null 2>&1; then
        rsvg-convert -w 256 -h 256 "${ROOT_DIR}/data/icons/hicolor/scalable/apps/${APP_ID}.svg" -o "${ICON_PNG}"
    fi
fi

if [ -f "${ICON_PNG}" ]; then
    cp "${ICON_PNG}" "${APP_DIR}/${APP_ID}.png"
    cp "${ICON_PNG}" "${APP_DIR}/.DirIcon"
fi

# Compatibility symlink for appimagetool's legacy AppStream expectation
META_DIR="${APP_DIR}/usr/share/metainfo"
if [ -f "${META_DIR}/${APP_ID}.metainfo.xml" ]; then
    ln -sf "${APP_ID}.metainfo.xml" "${META_DIR}/${APP_ID}.appdata.xml"
fi

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
export LDAI_OUTPUT="${OUTPUT_DIR}/LookinFiles-${VERSION}-x86_64.AppImage"

# appimagetool's own AppStream check fetches every URL in the metainfo, so a
# slow or failing github.com breaks the build. Validate offline instead.
export LDAI_NO_APPSTREAM=1
if command -v appstreamcli >/dev/null 2>&1; then
    echo "==> Validating AppStream metadata (offline)..."
    appstreamcli validate --no-net "${META_DIR}/${APP_ID}.metainfo.xml"
fi

# Run linuxdeploy with GTK plugin
ICON_ARG=()
if [ -f "${APP_DIR}/${APP_ID}.png" ]; then
    ICON_ARG=(--icon-file "${APP_DIR}/${APP_ID}.png")
else
    ICON_ARG=(--icon-file "${APP_DIR}/usr/share/icons/hicolor/scalable/apps/${APP_ID}.svg")
fi

PATH="${TOOLS_DIR}:${PATH}" "${LINUXDEPLOY}" \
    --appdir "${APP_DIR}" \
    --desktop-file "${APP_DIR}/usr/share/applications/${APP_ID}.desktop" \
    "${ICON_ARG[@]}" \
    --plugin gtk \
    --output appimage

echo "==> AppImage created successfully: ${LDAI_OUTPUT}"
