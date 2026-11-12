// SPDX-FileCopyrightText: 2026 kebabroyal
// SPDX-License-Identifier: GPL-3.0-or-later

import Adw from 'gi://Adw?version=1';
import Gdk from 'gi://Gdk?version=4.0';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Gtk from 'gi://Gtk?version=4.0';
import Gettext from 'gettext';
import System from 'system';

import {WoodshedWindow} from './window.js';

// The dev launcher sets io.github.kebabroyal.Woodshed.Devel.
const APP_ID = GLib.getenv('WOODSHED_APP_ID') ?? 'io.github.kebabroyal.Woodshed';

Gettext.bindtextdomain('woodshed', GLib.getenv('WOODSHED_LOCALE_DIR') ?? '/usr/share/locale');
Gettext.textdomain('woodshed');

const app = new Adw.Application({
    application_id: APP_ID,
    flags: Gio.ApplicationFlags.DEFAULT_FLAGS,
});

app.connect('startup', () => {
    Gtk.Window.set_default_icon_name(app.application_id);
    const css = new Gtk.CssProvider();
    css.load_from_file(Gio.File.new_for_uri(
        GLib.uri_resolve_relative(import.meta.url, 'style.css', GLib.UriFlags.NONE)));
    Gtk.StyleContext.add_provider_for_display(Gdk.Display.get_default(), css,
        Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION);
});

app.connect('activate', () => {
    let window = app.get_active_window();
    if (!window) {
        window = new WoodshedWindow({application: app});
        if (APP_ID.endsWith('.Devel'))
            window.add_css_class('devel');
    }
    window.present();
});

System.exit(await app.runAsync([System.programInvocationName, ...System.programArgs]));
