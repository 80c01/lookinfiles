#!/usr/bin/env bash
#
# Build and package a portable Windows zip for LookinFiles.
# Usage: ./packaging/windows/build_windows_zip.sh [output_directory]
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

OUTPUT_ARG="${1:-${ROOT_DIR}/build}"
mkdir -p "${OUTPUT_ARG}"
OUTPUT_DIR="$(cd "${OUTPUT_ARG}" && pwd)"

VERSION=$(sh "${ROOT_DIR}/tools/version.sh")
DIST_NAME="lookinfiles-${VERSION}-windows-x86_64"
STAGE_DIR="${ROOT_DIR}/build/${DIST_NAME}"

echo "==> Building LookinFiles for Windows (v${VERSION})..."
cd "${ROOT_DIR}"
make -j"$(nproc 2>/dev/null || echo 2)" VERSION="${VERSION}"

EXE="lookinfiles.exe"
if [ ! -f "${EXE}" ] && [ -f "lookinfiles" ]; then
    EXE="lookinfiles"
fi

if [ ! -f "${EXE}" ]; then
    echo "Error: ${EXE} binary not found after build!" >&2
    exit 1
fi

echo "==> Staging portable folder: ${STAGE_DIR}..."
rm -rf "${STAGE_DIR}"
mkdir -p "${STAGE_DIR}"

cp "${EXE}" "${STAGE_DIR}/lookinfiles.exe"

# If running in MSYS2 / MinGW environment, bundle runtime dependencies
PREFIX="${MINGW_PREFIX:-}"
if [ -n "${PREFIX}" ] && [ -d "${PREFIX}/bin" ]; then
    echo "==> Copying DLLs and runtime assets from ${PREFIX}..."

    # Deploy core GLib schemas
    mkdir -p "${STAGE_DIR}/share/glib-2.0/schemas"
    if [ -d "${PREFIX}/share/glib-2.0/schemas" ]; then
        cp -r "${PREFIX}/share/glib-2.0/schemas/"* "${STAGE_DIR}/share/glib-2.0/schemas/"
        glib-compile-schemas "${STAGE_DIR}/share/glib-2.0/schemas" || true
    fi

    # Deploy icons
    mkdir -p "${STAGE_DIR}/share/icons"
    for icon_dir in "${PREFIX}/share/icons/Adwaita" "${PREFIX}/share/icons/hicolor"; do
        if [ -d "${icon_dir}" ]; then
            cp -r "${icon_dir}" "${STAGE_DIR}/share/icons/"
        fi
    done

    # Deploy gdk-pixbuf loaders if present
    if [ -d "${PREFIX}/lib/gdk-pixbuf-2.0" ]; then
        mkdir -p "${STAGE_DIR}/lib"
        cp -r "${PREFIX}/lib/gdk-pixbuf-2.0" "${STAGE_DIR}/lib/"
    fi

    # Collect DLLs recursively using ldd/ntldd
    echo "==> Collecting dependent DLLs..."
    copied=1
    while [ "${copied}" -ne 0 ]; do
        copied=0
        for pe in "${STAGE_DIR}"/*.exe "${STAGE_DIR}"/*.dll; do
            [ -f "${pe}" ] || continue
            # Find DLL dependencies
            for dll_raw in $(ldd "${pe}" 2>/dev/null | grep -i "${PREFIX}" | awk '{print $3}' || ntldd -R "${pe}" 2>/dev/null | grep -i "${PREFIX}" | awk '{print $3}' || true); do
                [ -n "${dll_raw}" ] || continue
                dll_name="$(cygpath -u "${dll_raw}" 2>/dev/null || echo "${dll_raw}")"
                [ -f "${dll_name}" ] || continue
                base_dll="$(basename "${dll_name}")"
                if [ ! -f "${STAGE_DIR}/${base_dll}" ]; then
                    cp "${dll_name}" "${STAGE_DIR}/"
                    copied=$((copied + 1))
                fi
            done
        done
    done
fi

# Include documentation and licenses
cp "${ROOT_DIR}/README.md" "${STAGE_DIR}/"
cp "${ROOT_DIR}/LICENSE" "${STAGE_DIR}/"

ZIP_FILE="${OUTPUT_DIR}/${DIST_NAME}.zip"
rm -f "${ZIP_FILE}"
echo "==> Creating zip archive: ${ZIP_FILE}..."
(cd "${ROOT_DIR}/build" && zip -r -9 "${ZIP_FILE}" "${DIST_NAME}")

echo "==> Windows package created successfully:"
echo "    ${ZIP_FILE}"
