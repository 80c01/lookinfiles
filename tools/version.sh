#!/bin/sh
# Print the project version:
# 1. Environment variable $VERSION if already set
# 2. $GITHUB_REF_NAME when running on a tag in GitHub Actions (e.g. v0.4.2 -> 0.4.2)
# 3. Exact git tag if HEAD points to one (v0.4.2 -> 0.4.2)
# 4. Git describe for dev/branch builds (v0.4.2-2-gba23999 -> 0.4.2.2.gba23999)
# 5. Fallback defined in src/core/app_paths.h
set -eu

if [ -n "${VERSION:-}" ]; then
    echo "${VERSION}"
    exit 0
fi

if [ -n "${GITHUB_REF_NAME:-}" ] && echo "${GITHUB_REF_NAME}" | grep -q '^v[0-9]'; then
    echo "${GITHUB_REF_NAME#v}"
    exit 0
fi

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

V=$(git -C "${ROOT_DIR}" describe --tags --exact-match 2>/dev/null | sed 's/^v//' || true)
if [ -n "${V}" ]; then
    echo "${V}"
    exit 0
fi

V=$(git -C "${ROOT_DIR}" describe --tags --match "v[0-9]*" 2>/dev/null | sed -e 's/^v//' -e 's/-\([0-9]\+\)-g/.\1.g/' || true)
if [ -n "${V}" ]; then
    echo "${V}"
    exit 0
fi

V=$(sed -n 's/^\#define APP_VERSION *"\(.*\)"/\1/p' "${ROOT_DIR}/src/app_paths.h" 2>/dev/null || true)
if [ -n "${V}" ]; then
    echo "${V}"
    exit 0
fi

echo "0.6.0"
