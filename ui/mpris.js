// MPRIS media players on the session bus, matched to recording sources.
// Signal-driven: no polling.

import Gio from 'gi://Gio';
import GLib from 'gi://GLib';

const PREFIX = 'org.mpris.MediaPlayer2.';
const PATH = '/org/mpris/MediaPlayer2';
const PLAYER_IFACE = 'org.mpris.MediaPlayer2.Player';
const MAX_ANCESTORS = 32;
const IGNORED_WORDS = new Set(['com', 'org', 'net', 'io', 'app', 'client', 'desktop', 'instance']);

function words(text) {
    return (text ?? '').toLowerCase().split(/[^a-z0-9]+/).filter(w => w.length >= 3 && !IGNORED_WORDS.has(w) && !/^instance\d+$/.test(w));
}

function parentOf(pid) {
    try {
        const [, bytes] = GLib.file_get_contents(`/proc/${pid}/stat`);
        const stat = new TextDecoder().decode(bytes);
        // Field 4, after the parenthesised command name.
        return Number(stat.slice(stat.lastIndexOf(')') + 2).split(' ')[1]);
    } catch {
        return 0;
    }
}

function ancestors(pid) {
    const out = [];
    for (let p = pid; p > 1 && out.length < MAX_ANCESTORS; p = parentOf(p))
        out.push(p);
    return out;
}

class Player {
    constructor(busName, proxy, onChanged) {
        this.busName = busName;
        this.proxy = proxy;
        this.pid = 0;
        this.identity = '';
        this.desktopEntry = '';
        this._signal = proxy.connect('g-properties-changed', onChanged);
    }

    destroy() {
        this.proxy.disconnect(this._signal);
    }

    get(name) {
        return this.proxy.get_cached_property(name)?.recursiveUnpack();
    }

    get playing() {
        return this.get('PlaybackStatus') === 'Playing';
    }

    get title() {
        const meta = this.get('Metadata') ?? {};
        const artist = [].concat(meta['xesam:artist'] ?? []).join(', ');
        const title = meta['xesam:title'] ?? '';
        return artist && title ? `${title} – ${artist}` : title;
    }

    call(method) {
        this.proxy.call(method, null, Gio.DBusCallFlags.NONE, -1, null, null);
    }

    get keys() {
        return new Set([...words(this.identity), ...words(this.desktopEntry),
            ...words(this.busName.slice(PREFIX.length).split('.')[0])]);
    }
}

export class Mpris {
    constructor(onChanged) {
        this._onChanged = onChanged;
        this._players = new Map();
        this._bus = Gio.bus_get_sync(Gio.BusType.SESSION, null);
        this._bus.signal_subscribe('org.freedesktop.DBus', 'org.freedesktop.DBus', 'NameOwnerChanged',
            '/org/freedesktop/DBus', null, Gio.DBusSignalFlags.NONE, (...args) => {
                const [name, oldOwner, newOwner] = args[5].deepUnpack();
                if (!name.startsWith(PREFIX))
                    return;
                if (oldOwner)
                    this._remove(name);
                if (newOwner)
                    this._add(name);
            });
        this._dbusCall('ListNames', null, reply => {
            for (const name of reply.deepUnpack()[0])
                if (name.startsWith(PREFIX))
                    this._add(name);
        });
    }

    // Player of a source: same process tree first, then by name.
    playerFor(source) {
        if (!source || this._players.size === 0)
            return null;
        const players = [...this._players.values()];
        const pid = source.process_id;
        if (pid) {
            const tree = ancestors(pid);
            const byPid = players.filter(p => p.pid && tree.includes(p.pid));
            if (byPid.length)
                return byPid.find(p => p.playing) ?? byPid[0];
        }
        const sourceKeys = new Set([...words(source.name), ...words(source.binary), ...words(source.app_id)]);
        const byName = players.filter(p => [...p.keys].some(k => sourceKeys.has(k)));
        return byName.find(p => p.playing) ?? byName[0] ?? null;
    }

    _dbusCall(method, args, callback) {
        this._bus.call('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus', method,
            args, null, Gio.DBusCallFlags.NONE, -1, null, (bus, result) => {
                try {
                    callback(bus.call_finish(result));
                } catch {
                    // Name left meanwhile.
                }
            });
    }

    _add(busName) {
        Gio.DBusProxy.new(this._bus, Gio.DBusProxyFlags.NONE, null, busName, PATH, PLAYER_IFACE, null,
            (_source, result) => {
                let proxy;
                try {
                    proxy = Gio.DBusProxy.new_finish(result);
                } catch {
                    return;
                }
                this._remove(busName);
                const player = new Player(busName, proxy, () => this._onChanged());
                this._players.set(busName, player);
                this._dbusCall('GetConnectionUnixProcessID', new GLib.Variant('(s)', [busName]), reply => {
                    player.pid = reply.deepUnpack()[0];
                    this._onChanged();
                });
                this._bus.call(busName, PATH, 'org.freedesktop.DBus.Properties', 'GetAll',
                    new GLib.Variant('(s)', ['org.mpris.MediaPlayer2']), null, Gio.DBusCallFlags.NONE, -1,
                    null, (bus, res) => {
                        try {
                            const props = bus.call_finish(res).recursiveUnpack()[0];
                            player.identity = props.Identity ?? '';
                            player.desktopEntry = props.DesktopEntry ?? '';
                        } catch {
                            return;
                        }
                        this._onChanged();
                    });
                this._onChanged();
            });
    }

    _remove(busName) {
        const player = this._players.get(busName);
        if (!player)
            return;
        player.destroy();
        this._players.delete(busName);
        this._onChanged();
    }
}
