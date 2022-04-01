spacenavd
=========

About
-----
Spacenavd is a free software user-space driver (daemon), for 6-dof input
devices, like 3Dconnexion's space-mice. It's compatible with the original 3dxsrv
proprietary daemon provided by 3Dconnexion, and works as a drop-in replacement
with any program that was written for the 3Dconnexion driver, but also provides
an improved communication mechanism for programs designed specifically to work
with spacenavd.

For more info on the spacenav project, visit: http://spacenav.sourceforge.net

License
-------
Copyright (C) 2007-2022 John Tsiombikas <nuclear@member.fsf.org>

This program is free software. Feel free to copy, modify and/or redistribute it
under the terms of the GNU General Public License version 3, or at your option,
any later version published by the Free Software Foundation. See COPYING for
details.

Dependencies
------------
In order to compile the spacenavd daemon, you'll need the following:
 - GNU C Compiler
 - GNU make
 - Xlib (libX11, optional)
 - XInput2 (libXi, optional)
 - Xtest (libXtst, optional)

You can compile the daemon without Xlib, but it won't be compatible with
applications that where written for the original proprietary 3Dconnexion driver
(e.g. maya, houdini, etc). The 3dxsrv compatibility interface needs to
communicate with clients through the X window system. Programs designed to work
with the alternative spacenavd-specific interface however (e.g. blender) will
work fine even when spacenavd is built without X11 support.

Installation
------------
If you have the dependencies installed, just run `./configure` and then `make`
to compile the daemon, and then `make install`, to install it.The default
installation prefix is `/usr/local`. If you wish to install somewhere else, you
may pass `--prefix=/whatever` to the configure script.

Running spacenavd
-----------------
Spacenavd is designed to run during startup as a system daemon.

If your system uses SysV init, then you may run `setup_init` as root, to install
the spacenavd init script, and have spacenavd start automatically during
startup. To start the daemon right after installing it, without having to reboot
your system, just type `/etc/init.d/spacenavd start` as root.

If your system uses BSD init or some other init system, then you'll have to
follow your init documentation to set this up yourself. You may be able to
use the provided `init_script` file as a starting point.

For systems running systemd, there is a spacenavd.service file under
`contrib/systemd`. Follow your system documentation for how to use it.

Configuration
-------------
The spacenavd daemon reads a number of options from `/etc/spnavrc`. If
that file doesn't exist, then it will use default values for everything. An
example configuration file is included in the doc subdirectory, which you may
copy to `/etc` and tweak.

You may use the graphical spnavcfg program to interactively set and tweak any
of the configuration options.

Troubleshooting
---------------
If you're having trouble running spacenavd, read the up to date FAQ on the
spacenav website: http://spacenav.sourceforge.net/faq.html

If you're not sure if spacenavd is set up correctly and works with your device,
a good first step is to try and run the "simple" example program which comes
with libspnav. It builds into two variants: `simple_af_unix` and `simple_x11`,
which is helpful for testing both supported communication protocols. If either
or both fail to work, there's something wrong with your setup.

If you're still having trouble, send a description of your problem to the
spacenav-users mailing list: spacenav-users@lists.sourceforge.net along with a
copy of your /var/log/spnavd.log and any other relevant information.

If you have encountered a bug, please file a bug report in our bug tracker:
https://github.com/FreeSpacenav/spacenavd/issues
