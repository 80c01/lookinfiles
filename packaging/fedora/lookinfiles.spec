Name:           lookinfiles
Version:        0.6.0
Release:        1%{?dist}
Summary:        Fast, keyboard-driven GTK 4 file explorer with preview panel, editors and terminal

License:        MIT
URL:            https://github.com/80c01/lookinfiles
Source0:        %{url}/archive/refs/tags/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  gettext
BuildRequires:  pkgconfig
BuildRequires:  desktop-file-utils
BuildRequires:  libappstream-glib
BuildRequires:  pkgconfig(gtk4) >= 4.12
BuildRequires:  pkgconfig(libgit2) >= 1.1
BuildRequires:  pkgconfig(vte-2.91-gtk4)
BuildRequires:  pkgconfig(gtksourceview-5)
BuildRequires:  pkgconfig(poppler-glib)
BuildRequires:  pkgconfig(sqlite3)
BuildRequires:  pkgconfig(webkitgtk-6.0)
BuildRequires:  pkgconfig(libarchive)
BuildRequires:  pkgconfig(gstreamer-video-1.0)
BuildRequires:  pkgconfig(gstreamer-app-1.0)
BuildRequires:  pkgconfig(gstreamer-tag-1.0)

Recommends:     sshfs

%description
LookinFiles ("look in your files") is a keyboard-driven file explorer in C with
GTK 4, featuring a rich preview panel, text editors in Notepad, Emacs or Vim mode,
embedded terminal panes, search in files, Git status, and workspaces.

%prep
%autosetup

%build
%make_build VERSION=%{version}

%check
make test
desktop-file-validate %{buildroot}%{_datadir}/applications/io.github._80c01.LookinFiles.desktop
appstream-util validate-relax --nonet %{buildroot}%{_datadir}/metainfo/io.github._80c01.LookinFiles.metainfo.xml

%install
%make_install PREFIX=/usr VERSION=%{version}
%find_lang %{name}

%files -f %{name}.lang
%license LICENSE
%doc README.md CHANGELOG.md
%{_bindir}/lookinfiles
%{_datadir}/applications/io.github._80c01.LookinFiles.desktop
%{_datadir}/metainfo/io.github._80c01.LookinFiles.metainfo.xml
%{_datadir}/icons/hicolor/scalable/apps/io.github._80c01.LookinFiles.svg
%{_datadir}/icons/hicolor/256x256/apps/io.github._80c01.LookinFiles.png

%changelog
* Tue Nov 03 2026 80c01 <noreply@github.com> - 0.1.0-1
- Initial package release
