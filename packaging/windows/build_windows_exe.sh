#!/usr/bin/env bash
#
# Build a single-file Windows executable of LookinFiles: the portable zip of
# build_windows_zip.sh embedded in a small launcher (launcher.c) that
# extracts it once into %LOCALAPPDATA%\lookinfiles-runtime\ and starts it.
# Run it in the MSYS2 UCRT64 shell.
# Usage: ./packaging/windows/build_windows_exe.sh [output_directory]
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

OUTPUT_ARG="${1:-${ROOT_DIR}/build}"
mkdir -p "${OUTPUT_ARG}"
OUTPUT_DIR="$(cd "${OUTPUT_ARG}" && pwd)"

VERSION=$(sed -n 's/^#define APP_VERSION *"\(.*\)"/\1/p' "${ROOT_DIR}/src/app_paths.h")
DIST_NAME="lookinfiles-${VERSION}-windows-x86_64"
WORK_DIR="${ROOT_DIR}/build/windows-exe"
ZIP_FILE="${ROOT_DIR}/build/${DIST_NAME}.zip"
EXE_FILE="${OUTPUT_DIR}/${DIST_NAME}.exe"

bash "${SCRIPT_DIR}/build_windows_zip.sh" "${ROOT_DIR}/build"

# Each build extracts into its own folder: the version plus a hash of the zip
HASH=$(sha256sum "${ZIP_FILE}" | cut -c1-12)
PAYLOAD_ID="${VERSION}-${HASH}"

echo "==> Embedding ${ZIP_FILE} in the launcher..."
rm -rf "${WORK_DIR}"
mkdir -p "${WORK_DIR}"
cp "${ZIP_FILE}" "${WORK_DIR}/payload.zip"
printf '#include "launcher.h"\nPAYLOAD_RESOURCE RCDATA "payload.zip"\n' > "${WORK_DIR}/launcher.rc"

(
    cd "${WORK_DIR}"
    "${WINDRES:-windres}" -I "${SCRIPT_DIR}" -O coff launcher.rc -o launcher_res.o
    "${CC:-gcc}" -std=gnu99 -O2 -Wall -Wextra -municode -mwindows \
        -I "${SCRIPT_DIR}" \
        -DPAYLOAD_ID="\"${PAYLOAD_ID}\"" -DDIST_NAME="\"${DIST_NAME}\"" \
        "${SCRIPT_DIR}/launcher.c" launcher_res.o \
        -o "${EXE_FILE}" -lshell32 -lole32 -luuid
)
rm -f "${WORK_DIR}/payload.zip"

echo "==> Single-file Windows executable created successfully:"
echo "    ${EXE_FILE}"
