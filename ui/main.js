import Adw from 'gi://Adw?version=1';
import Gdk from 'gi://Gdk?version=4.0';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Gtk from 'gi://Gtk?version=4.0';
import System from 'system';

import {WoodshedWindow} from './window.js';

const app = new Adw.Application({
    application_id: 'io.github.kebabroyal.Woodshed',
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
    const window = app.get_active_window() ?? new WoodshedWindow({application: app});
    window.present();
});

System.exit(await app.runAsync([System.programInvocationName, ...System.programArgs]));
