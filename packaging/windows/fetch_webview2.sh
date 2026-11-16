#!/bin/sh
# Download the Microsoft WebView2 SDK (NuGet package) into deps/webview2, for
# the browser tabs on Windows (src/preview/web_engine_win.c). make then finds
# build/native/include/WebView2.h there and builds with HAVE_WEBVIEW2;
# make windows-zip copies build/native/x64/WebView2Loader.dll next to the
# executable. Usage: sh packaging/windows/fetch_webview2.sh [directory]
set -eu

VERSION=1.0.4258.31
SHA256=56f7f4b8bf9aee4b8efefbbdd4f67d5f74ebd1b100ed0806da71bf76af481aa9
URL="https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/${VERSION}/microsoft.web.webview2.${VERSION}.nupkg"

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="${1:-${ROOT_DIR}/deps/webview2}"

if [ -f "${DEST}/build/native/include/WebView2.h" ]; then
    echo "WebView2 SDK already in ${DEST}"
    exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

echo "==> Downloading WebView2 SDK ${VERSION}..."
curl -fsSL -o "${TMP}/webview2.zip" "${URL}"

echo "==> Checking its SHA-256..."
echo "${SHA256}  ${TMP}/webview2.zip" | sha256sum -c -

mkdir -p "${DEST}"
unzip -q -o "${TMP}/webview2.zip" -d "${DEST}"
echo "==> WebView2 SDK in ${DEST} (run make again)"
