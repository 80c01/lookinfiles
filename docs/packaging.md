# Packaging Guide

This guide documents the recipes and procedures for building distributable packages of LookinFiles across Linux distributions and universal formats.

All packaging recipes and scripts reside in the `packaging/` directory.

---

## Overview of Targets

| Target | Recipe / Location | Build Tool | Output |
| :--- | :--- | :--- | :--- |
| **Arch Linux** | `packaging/arch/PKGBUILD` | `makepkg` | `.pkg.tar.zst` |
| **Fedora** | `packaging/fedora/lookinfiles.spec` | `rpmbuild` | `.rpm` |
| **Debian / Ubuntu** | `packaging/debian/` | `dpkg-buildpackage` or `build_deb.sh` | `.deb` |
| **Flatpak** | `packaging/flatpak/io.github._80c01.LookinFiles.yml` | `flatpak-builder` | Flatpak bundle / Flathub |
| **AppImage** | `packaging/appimage/build_appimage.sh` | `linuxdeploy` + `linuxdeploy-plugin-gtk` | `.AppImage` |

---

## 1. Arch Linux (PKGBUILD)

The `PKGBUILD` file is ready for the Arch User Repository (AUR) or local compilation with `makepkg`.

### Building locally:
```bash
cd packaging/arch
makepkg -si
```

### Dependencies:
- **Runtime**: `gtk4`, `vte4`, `gtksourceview5`, `poppler-glib`, `sqlite`, `webkitgtk-6.0`, `libarchive`
- **Optional**: `git`, `sshfs`

---

## 2. Fedora / RHEL (RPM Spec)

The RPM spec file `packaging/fedora/lookinfiles.spec` follows Fedora Packaging Guidelines.

### Building locally:
```bash
# Prepare rpmbuild directory tree
rpmdev-setuptree

# Copy sources and spec
make dist
cp lookinfiles-*.tar.gz ~/rpmbuild/SOURCES/
cp packaging/fedora/lookinfiles.spec ~/rpmbuild/SPECS/

# Build binary and source RPM
rpmbuild -ba ~/rpmbuild/SPECS/lookinfiles.spec
```

The spec runs `desktop-file-validate` and `appstream-util validate-relax` during the `%check` step.

---

## 3. Flatpak (Flathub)

LookinFiles uses the GNOME runtime (`org.gnome.Platform//47` and `org.gnome.Sdk//47`).

### Building locally:
```bash
# Install Flatpak builder and GNOME 47 SDK
flatpak install flathub org.gnome.Platform//47 org.gnome.Sdk//47
sudo apt install flatpak-builder  # or your distro package manager

# Build and install locally
flatpak-builder --user --install --force-clean build-flatpak packaging/flatpak/io.github._80c01.LookinFiles.yml

# Run
flatpak run io.github._80c01.LookinFiles
```

### Flathub submission:
The manifest is structured to be published directly to [Flathub](https://flathub.org).

---

## 4. Debian / Ubuntu (.deb)

LookinFiles supports both standard `dpkg-buildpackage` and a standalone script `build_deb.sh`.

### Quick build with `build_deb.sh`:
```bash
./packaging/debian/build_deb.sh
# Creates build/lookinfiles_<version>-1_<arch>.deb
```

### Standard Debian build (`dpkg-buildpackage`):
```bash
ln -s packaging/debian debian
dpkg-buildpackage -us -uc -b
rm debian
```

---

## 5. AppImage

The AppImage is bundled using `linuxdeploy` and `linuxdeploy-plugin-gtk`.

### Building locally:
```bash
./packaging/appimage/build_appimage.sh
# Outputs: build/LookinFiles-x86_64.AppImage
```

---

## Continuous Integration & Release Automation

A GitHub Actions workflow (`.github/workflows/packages.yml`) automatically builds all targets on release tags (`v*`) and attaches the resulting packages to the release.
