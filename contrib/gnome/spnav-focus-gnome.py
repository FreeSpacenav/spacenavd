#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Forward GNOME compositor focus to spacenavd; runs in the user's session."""
import ctypes
import signal

class SpnavClient:
    def __init__(self):
        self.lib = ctypes.CDLL('libspnav.so.0')
        self.lib.spnav_set_focus.argtypes = [ctypes.c_char_p]
        self.connected = False

    def publish(self, app_id):
        if not self.connected:
            if self.lib.spnav_open() < 0:
                raise OSError('spacenavd is unavailable')
            self.connected = True
            if self.lib.spnav_evmask(0) < 0:
                raise OSError('Cannot disable input events')
        if self.lib.spnav_set_focus(app_id.encode('utf-8')) < 0:
            raise OSError('Focus update rejected')

    def close(self):
        if self.connected:
            self.lib.spnav_close()
            self.connected = False

class Forwarder:
    def __init__(self, client):
        self.client = client

    def update(self, available, app_id):
        if not available:
            self.client.close()
            return
        try:
            self.client.publish(app_id)
        except OSError:
            self.client.close()


def main():
    from gi.repository import Gio, GLib
    # A broken daemon connection must be retried, not terminate on SIGPIPE.
    signal.signal(signal.SIGPIPE, signal.SIG_IGN)
    client = SpnavClient()
    forwarder = Forwarder(client)
    proxy = Gio.DBusProxy.new_for_bus_sync(
        Gio.BusType.SESSION, Gio.DBusProxyFlags.DO_NOT_AUTO_START, None,
        'org.gnome.Shell', '/org/freedesktop/Spacenav/Focus',
        'org.freedesktop.Spacenav.Focus1', None)
    def properties_changed(obj, changed, invalidated):
        available = obj.get_cached_property('Available')
        app_id = obj.get_cached_property('AppId')
        forwarder.update(bool(available and available.unpack()), app_id.unpack() if app_id else '')
    def refresh():
        # Also checks liveness: unloading the extension or restarting the daemon
        # must not leave stale focus. All blocking I/O is outside GNOME Shell.
        try:
            reply = proxy.call_sync('org.freedesktop.DBus.Properties.GetAll',
                GLib.Variant('(s)', ('org.freedesktop.Spacenav.Focus1',)),
                Gio.DBusCallFlags.NO_AUTO_START, 500, None)
            values = reply.unpack()[0]
            forwarder.update(values.get('Available', False), values.get('AppId', ''))
        except GLib.Error:
            forwarder.update(False, '')
        return GLib.SOURCE_CONTINUE
    proxy.connect('g-properties-changed', properties_changed)
    loop = GLib.MainLoop()
    GLib.unix_signal_add(GLib.PRIORITY_DEFAULT, signal.SIGTERM, lambda: (loop.quit(), False)[1])
    GLib.unix_signal_add(GLib.PRIORITY_DEFAULT, signal.SIGINT, lambda: (loop.quit(), False)[1])
    GLib.timeout_add_seconds(2, refresh)
    refresh()
    try:
        loop.run()
    finally:
        client.close()

if __name__ == '__main__':
    main()
