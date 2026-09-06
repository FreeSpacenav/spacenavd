# GNOME Wayland application profiles

The GNOME 50 extension reports the compositor's focused application, including
native Wayland and XWayland windows. Window titles and keystrokes are not sent.
It uses Shell.WindowTracker's desktop application ID, falling back to WM_CLASS.
An empty ID means no app has focus (including the overview/locked session).

The extension exposes AppId and Available on the session bus at
org.gnome.Shell /org/freedesktop/Spacenav/Focus, interface
org.freedesktop.Spacenav.Focus1. A separate Python/PyGObject helper sends focus to
spacenavd through libspnav. Blocking socket work stays outside GNOME Shell.
The helper retries after a daemon restart; disappearance of the extension closes
its daemon connection and releases focus ownership.

Install the updated daemon and library first. Install spnav-focus-gnome.py as
/usr/local/bin/spnav-focus-gnome (executable), then run:

    python3 contrib/gnome/install-user.py

Run that command as your desktop user, never root. It installs the extension,
adds it to enabled extensions, and installs a GNOME session autostart entry.
A newly installed extension can require logout/login on Wayland. Do not restart
or replace the running GNOME Shell process. After login, verify:

    gnome-extensions info spacenav-focus@jl1990
    gdbus call --session --dest org.gnome.Shell \
      --object-path /org/freedesktop/Spacenav/Focus \
      --method org.freedesktop.DBus.Properties.GetAll org.freedesktop.Spacenav.Focus1

Configure profiles in /etc/spnavrc, for example:

    profile "Blender" class=blender
      sensitivity = 1.0
    end

Matching remains case-insensitive substring matching. Manual profile selection
wins over focus updates; selecting auto resumes the compositor's current ID.
Only one provider connection owns focus at a time. Existing app-ID registration
remains metadata. Access uses the same local socket permissions as configuration
clients; the daemon does not independently authenticate a client's focus claim.
This implementation, like existing global profiles, targets one desktop session.
Other Wayland compositors (KDE, Sway, Hyprland) need their own focus adapters.

Tests (no active GNOME session or hardware required):

    gjs -m tests/test_focus.js
    python3 tests/test_focus_bridge.py

Daemon profile tests cover provider priority, disconnect, blank focus, and manual
selection with both X11 build variants. Library tests cover acknowledged string
chunks. GNOME 50 activation and focus forwarding were verified after login. An application
needs a matching configured profile to display its profile name; otherwise the
display correctly shows Default. The Blender example above inherits global
mappings when no overrides are specified.

To disable: disable spacenav-focus@jl1990 in Extensions and remove
~/.config/autostart/spnav-focus-gnome.desktop. Stop the running helper to release
its connection immediately.

After building the sibling libspnav, run `timeout 10s python3
tests/test_focus_reconnect.py` to test real library/helper reconnection against
a fake daemon. SPNAV_TEST_LIBRARY can select a different built library.
