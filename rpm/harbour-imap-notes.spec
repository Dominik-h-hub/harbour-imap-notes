Name:       harbour-imap-notes

Summary:    IMAP Notes
Version:    0.1
Release:    1
License:    EPL-2.0
URL:        https://github.com/Dominik-h-hub/harbour-imap-notes
Source0:    %{name}-%{version}.tar.bz2
Requires:   sailfishsilica-qt5 >= 0.10.9
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Sql)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  desktop-file-utils

%description
Notes client that synchronises notes with an IMAP mail account, using the
same on-wire format as the native iOS Notes app over IMAP.


%prep
%setup -q -n %{name}-%{version}

%build
# The top-level harbour-imap-notes.pro is a SUBDIRS project that builds
# both the app and the background-sync daemon in one pass.  This avoids
# the mb2 shadow-build problem where "cd daemon" fails because the build
# directory is separate from the source tree.
%qmake5
%make_build


%install
%qmake5_install


desktop-file-install --delete-original \
    --dir %{buildroot}%{_datadir}/applications \
    %{buildroot}%{_datadir}/applications/*.desktop

%post
# Reload systemd --user units. The daemon is enabled per-user via:
#   systemctl --user enable --now harbour-imap-notes-daemon
# We don't enable it automatically because not every user wants background
# sync. The /usr/lib/systemd/user unit is installed regardless.
:

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_bindir}/%{name}-daemon
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
/usr/lib/systemd/user/harbour-imap-notes-daemon.service
