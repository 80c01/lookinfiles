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
     mingw-w64-ucrt-x86_64-gtksourceview5 \
     mingw-w64-ucrt-x86_64-poppler \
     mingw-w64-ucrt-x86_64-sqlite3 \
     mingw-w64-ucrt-x86_64-libarchive \
     make \
     zip
   ```

*(Note: VTE is optional and disabled on Windows as it requires POSIX pseudo-terminals).*

## 2. Compiling

From the root of the repository in the MSYS2 UCRT64 shell:

```bash
make
```

This will produce `lookinfiles.exe`.

## 3. Creating a Portable Windows Zip

To package a standalone portable distribution containing `lookinfiles.exe` along with all required runtime DLLs, GLib schemas, and Adwaita icons:

```bash
make windows-zip
```

The resulting archive will be placed in `build/lookinfiles-<version>-windows-x86_64.zip`.
Unzip and run `lookinfiles.exe` on any Windows 10/11 system.
