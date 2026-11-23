# Building and Packaging LookinFiles for Windows

LookinFiles can be built natively for Windows using **MSYS2** with the **UCRT64** environment.

## 1. Prerequisites (MSYS2 UCRT64)

1. Download and install [MSYS2](https://www.msys2.org/).
2. Open the **MSYS2 UCRT64** shell (not MSYS or MINGW32).
3. Update the package database:
   ```bash
   pacman -Syu
   ```
4. Install the required build tools and libraries:
   ```bash
   pacman -S --needed \
     mingw-w64-ucrt-x86_64-gcc \
     mingw-w64-ucrt-x86_64-pkg-config \
     mingw-w64-ucrt-x86_64-gtk4 \
     mingw-w64-ucrt-x86_64-libgit2 \
     mingw-w64-ucrt-x86_64-gtksourceview5 \
     mingw-w64-ucrt-x86_64-poppler \
     mingw-w64-ucrt-x86_64-sqlite3 \
     mingw-w64-ucrt-x86_64-libarchive \
     mingw-w64-ucrt-x86_64-zstd \
     mingw-w64-ucrt-x86_64-libjpeg-turbo \
     mingw-w64-ucrt-x86_64-gst-plugins-base \
     mingw-w64-ucrt-x86_64-libssh \
     diffutils \
     make \
     unzip \
     zip
   ```

*(Note: VTE does not exist on Windows; the terminal panel (`Ctrl+T`) uses the
built-in terminal instead (`src/terminal/term_widget.c`) on a Windows pseudo console
(ConPTY), which needs Windows 10 version 1809 or later. The shell is
PowerShell 7 (`pwsh.exe`) when installed, else Windows PowerShell, else
`cmd.exe`; set `SHELL` to a Windows program to choose another one.)*

*(Note: WebKitGTK does not exist on Windows either; browser tabs
(`Ctrl+Shift+B`) use Microsoft WebView2 instead (`src/preview/web_engine_win.c`).
Get its SDK once with:*

```bash
sh packaging/windows/fetch_webview2.sh   # into deps/webview2 (make WEBVIEW2_DIR=... for elsewhere)
```

*`make` then builds with `HAVE_WEBVIEW2` (`make deps` says so), and
`make windows-zip` puts `WebView2Loader.dll` next to `lookinfiles.exe`. At run
time the WebView2 Runtime is needed: it comes with Windows 11 and up-to-date
Windows 10 (else install the Evergreen Runtime from Microsoft). Without the
SDK or the runtime, web addresses open in the default browser.)*

## 2. Compiling

From the root of the repository in the MSYS2 UCRT64 shell:

```bash
make
```

This will produce `lookinfiles.exe` (and `lookinfiles-relay.exe` when built with
`mingw-w64-ucrt-x86_64-libssh`, a small console-subsystem helper used by remote
terminals inside ConPTY).

## 3. Creating a Portable Windows Zip

To package a standalone portable distribution containing `lookinfiles.exe` (and `lookinfiles-relay.exe` when built with libssh) along with all required runtime DLLs, GLib schemas, and Adwaita icons:

```bash
make windows-zip
```

The resulting archive will be placed in `build/lookinfiles-<version>-windows-x86_64.zip`.
Unzip and run `lookinfiles.exe` on any Windows 10/11 system.

## 4. Creating a Single-File Executable

To get the whole application as one `.exe` (nothing to unzip):

```bash
make windows-exe
```

This builds the portable zip above, then embeds it in a small launcher
(`launcher.c`) as `build/lookinfiles-<version>-windows-x86_64.exe`.

- **First run**: the launcher extracts the zip with the `tar.exe` of Windows
  into `%LOCALAPPDATA%\lookinfiles-runtime\<version>-<hash>\` (a few
  seconds), then starts `lookinfiles.exe` from there.
- **Next runs**: it starts the extracted copy at once, with the same
  arguments, and returns its exit code.
- **New build**: the hash of the zip changes, so a new folder is extracted
  and the folders of older builds are removed (except one still running).
- Settings stay in `%LOCALAPPDATA%\lookinfiles\`; deleting
  `lookinfiles-runtime` only forces a new extraction.

`windres` and `gcc` come with `mingw-w64-ucrt-x86_64-gcc` (set `WINDRES` or
`CC` to use others).
