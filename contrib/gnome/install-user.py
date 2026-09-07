#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Install the GNOME 50 focus adapter for the current user (never run as root)."""
import os
from pathlib import Path
import shutil
from gi.repository import Gio
if os.geteuid() == 0:
    raise SystemExit('Run this installer as your desktop user, not root.')
base = Path(__file__).resolve().parent
uuid = 'spacenav-focus@jl1990'
shutil.copytree(base / uuid, Path.home() / '.local/share/gnome-shell/extensions' / uuid, dirs_exist_ok=True)
autostart = Path.home() / '.config/autostart/spnav-focus-gnome.desktop'
autostart.parent.mkdir(parents=True, exist_ok=True)
autostart.write_text('''[Desktop Entry]
Type=Application
Name=SpaceNav application profiles
Comment=Forward GNOME focus changes to the SpaceNav daemon
Exec=/usr/local/bin/spnav-focus-gnome
OnlyShowIn=GNOME;
NoDisplay=true
''')
settings = Gio.Settings.new('org.gnome.shell')
enabled = settings.get_strv('enabled-extensions')
if uuid not in enabled:
    settings.set_strv('enabled-extensions', enabled + [uuid])
Gio.Settings.sync()
print('Installed and selected for the next GNOME login. A new extension may require logout/login.')
