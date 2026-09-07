// SPDX-License-Identifier: GPL-3.0-or-later
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Shell from 'gi://Shell';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {focusedAppId} from './focus.js';

const IFACE = `<node><interface name="org.freedesktop.Spacenav.Focus1">
<property name="AppId" type="s" access="read"/>
<property name="Available" type="b" access="read"/>
</interface></node>`;

export default class SpaceNavFocus extends Extension {
    get AppId() { return this._appId; }
    get Available() { return this._available; }

    enable() {
        this._appId = '';
        this._available = true;
        this._signals = [];
        this._tracker = Shell.WindowTracker.get_default();
        this._object = Gio.DBusExportedObject.wrapJSObject(IFACE, this);
        this._object.export(Gio.DBus.session, '/org/freedesktop/Spacenav/Focus');
        for (const [source, signal] of [
            [global.display, 'notify::focus-window'],
            [this._tracker, 'notify::focus-app'],
            [Main.overview, 'showing'],
            [Main.overview, 'hidden'],
            [Main.sessionMode, 'updated'],
        ]) {
            this._signals.push([source, source.connect(signal, () => this._publish())]);
        }
        this._publish();
    }

    _publish() {
        this._appId = focusedAppId(global.display.focus_window, this._tracker,
            Main.sessionMode.isLocked || Main.overview.visible);
        this._object.emit_property_changed('AppId', new GLib.Variant('s', this._appId));
        this._object.emit_property_changed('Available', new GLib.Variant('b', true));
    }

    disable() {
        for (const [source, id] of this._signals ?? [])
            source.disconnect(id);
        this._signals = [];
        this._available = false;
        this._appId = '';
        this._object?.emit_property_changed('Available', new GLib.Variant('b', false));
        this._object?.flush();
        this._object?.unexport();
        this._object = null;
        this._tracker = null;
    }
}
