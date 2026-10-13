import Adw from 'gi://Adw?version=1';
import GObject from 'gi://GObject';
import GLib from 'gi://GLib';
import Gtk from 'gi://Gtk?version=4.0';
import Woodshed from 'gi://Woodshed?version=0.1';

import {Mpris} from './mpris.js';

const State = Woodshed.State;
const DISPLAY_NAMES = ['wave', 'notes'];
const SPEED_PRESETS = [0.5, 0.67, 0.75, 1.0];
const SPEED_STEP = 0.05;
const STREAM_WAIT_MS = 3000;

function formatSpeed(speed) {
    return `${Math.round(speed * 100)}%`;
}

function signed(value) {
    return value > 0 ? `+${value}` : value < 0 ? `−${-value}` : '0';
}

function formatPitch(semitones, cents) {
    const fine = Math.round(cents);
    if (fine === 0)
        return signed(semitones);
    return semitones === 0 ? `${signed(fine)}¢` : `${signed(semitones)} ${signed(fine)}¢`;
}

function stepButton(iconName, tooltip, callback) {
    const button = new Gtk.Button({
        icon_name: iconName,
        tooltip_text: tooltip,
        valign: Gtk.Align.CENTER,
        focus_on_click: false,
        css_classes: ['flat', 'circular', 'step'],
    });
    button.connect('clicked', callback);
    return button;
}

function captioned(widget, caption) {
    const box = new Gtk.Box({orientation: Gtk.Orientation.VERTICAL, valign: Gtk.Align.CENTER});
    box.append(widget);
    box.append(new Gtk.Label({label: caption, css_classes: ['caption', 'dim-label']}));
    return box;
}

function formatTime(seconds) {
    const minutes = Math.floor(seconds / 60);
    const rest = (seconds - minutes * 60).toFixed(1).padStart(4, '0');
    return `${minutes}:${rest}`;
}

function circularButton(iconName, tooltip, extraClasses = []) {
    return new Gtk.Button({
        icon_name: iconName,
        tooltip_text: tooltip,
        valign: Gtk.Align.CENTER,
        focus_on_click: false,
        css_classes: ['circular', ...extraClasses],
    });
}

export const WoodshedWindow = GObject.registerClass(
class WoodshedWindow extends Adw.ApplicationWindow {
    constructor(params) {
        super({
            title: 'Woodshed',
            default_width: 1280,
            default_height: 720,
            width_request: 360,
            height_request: 480,
            ...params,
        });

        this.engine = new Woodshed.Engine();

        const header = new Adw.HeaderBar({title_widget: this._buildSourcePicker()});
        header.pack_start(this._buildRemote());
        header.pack_end(this._buildDisplaySwitch());
        this._stack = new Gtk.Stack({transition_type: Gtk.StackTransitionType.CROSSFADE});
        this._stack.add_named(new Adw.StatusPage({
            icon_name: 'media-record-symbolic',
            title: 'Nothing recorded',
            description: 'Play something, then press Record (R).',
        }), 'empty');
        this._waveform = new Woodshed.Waveform({engine: this.engine, vexpand: true});
        this._waveform.bind_property_full('display', this._displaySwitch, 'active-name',
            GObject.BindingFlags.BIDIRECTIONAL | GObject.BindingFlags.SYNC_CREATE,
            (_binding, display) => [true, DISPLAY_NAMES[display]],
            (_binding, name) => [true, DISPLAY_NAMES.indexOf(name)]);
        this._stack.add_named(this._waveform, 'take');

        this._toasts = new Adw.ToastOverlay({child: this._stack});
        const view = new Adw.ToolbarView({content: this._toasts});
        view.add_top_bar(header);
        view.add_bottom_bar(this._buildTransport());
        this.content = view;

        this._addShortcuts();
        for (const prop of ['state', 'duration', 'position', 'loop-start', 'loop-end', 'looping', 'source'])
            this.engine.connect(`notify::${prop}`, () => this._sync());
        this.engine.connect('error', (_engine, message) =>
            this._toasts.add_toast(new Adw.Toast({title: message})));
        this.engine.connect('notify::file', () => this._syncTitle());
        this.connect('close-request', () => {
            this.engine.flush();
            return false;
        });
        this._sync();
        this.engine.open_last();
    }

    // Play, pause and skip in the app being recorded, through MPRIS.
    _buildRemote() {
        const button = (iconName, method) => {
            const b = new Gtk.Button({icon_name: iconName, valign: Gtk.Align.CENTER, css_classes: ['flat', 'circular']});
            b.connect('clicked', () => this._remotePlayer?.call(method));
            return b;
        };
        this._remotePrevious = button('media-skip-backward-symbolic', 'Previous');
        this._remotePlay = button('media-playback-start-symbolic', 'PlayPause');
        this._remoteNext = button('media-skip-forward-symbolic', 'Next');
        this._remoteIcon = new Gtk.Image({margin_start: 6, margin_end: 4});
        this._remote = new Gtk.Box({spacing: 2, visible: false, css_classes: ['remote']});
        this._remote.append(this._remoteIcon);
        for (const b of [this._remotePrevious, this._remotePlay, this._remoteNext])
            this._remote.append(b);

        this._mpris = new Mpris(() => this._syncRemote());
        this.engine.connect('notify::source', () => this._syncRemote());
        // Record plays the app; stopping pauses it, so it won't play over the loop.
        // The source is resolved before the state changes, so the player is current here.
        this._wasRecording = false;
        this.engine.connect('notify::state', () => {
            const recording = this.engine.state === State.RECORDING;
            if (recording === this._wasRecording)
                return;
            this._wasRecording = recording;
            const player = this._remotePlayer;
            if (player && player.playing !== recording)
                player.call(recording ? 'Play' : 'Pause');
        });
        this.engine.sources.connect('items-changed', () => this._syncRemote());
        return this._remote;
    }

    _isListed(source) {
        const sources = this.engine.sources;
        for (let i = 0; i < sources.get_n_items(); i++) {
            if (sources.get_item(i) === source)
                return true;
        }
        return false;
    }

    _sameAppListed(source) {
        const sources = this.engine.sources;
        for (let i = 0; i < sources.get_n_items(); i++) {
            const item = sources.get_item(i);
            if (item.kind === Woodshed.SourceKind.APP && item.name === source.name)
                return true;
        }
        return false;
    }

    // Some apps (Chrome) drop their stream when paused: play first, record once it is back.
    _toggleRecord() {
        if (this._streamWait) {
            this._cancelStreamWait();
            return;
        }
        const engine = this.engine;
        const source = engine.source;
        const player = this._remotePlayer;
        if (engine.state === State.RECORDING || !player || !source ||
            this._isListed(source) || this._sameAppListed(source)) {
            engine.toggle_record();
            return;
        }
        player.call('Play');
        this._recordButton.add_css_class('waiting');
        const ready = () => {
            if (!this._sameAppListed(source))
                return;
            this._cancelStreamWait();
            engine.record_start();
        };
        this._streamWait = {
            signal: engine.sources.connect('items-changed', ready),
            timeout: GLib.timeout_add(GLib.PRIORITY_DEFAULT, STREAM_WAIT_MS, () => {
                this._streamWait.timeout = 0;
                this._cancelStreamWait();
                this._toasts.add_toast(new Adw.Toast({title: `${source.name} did not start playing`}));
                return GLib.SOURCE_REMOVE;
            }),
        };
    }

    _cancelStreamWait() {
        const wait = this._streamWait;
        if (!wait)
            return;
        this.engine.sources.disconnect(wait.signal);
        if (wait.timeout)
            GLib.source_remove(wait.timeout);
        this._streamWait = null;
        this._recordButton.remove_css_class('waiting');
    }

    // The chosen app, or the one Record would pick.
    _remoteSource() {
        const isApp = s => s?.kind === Woodshed.SourceKind.APP;
        if (this.engine.source)
            return isApp(this.engine.source) ? this.engine.source : null;
        const sources = this.engine.sources;
        for (let i = 0; i < sources.get_n_items(); i++) {
            if (isApp(sources.get_item(i)))
                return sources.get_item(i);
        }
        return null;
    }

    _syncRemote() {
        const source = this._remoteSource();
        const player = this._mpris.playerFor(source);
        this._remotePlayer = player;
        this._remote.visible = player !== null;
        if (!player)
            return;
        const name = player.identity || source.name;
        this._remoteIcon.icon_name = source.icon_name;
        this._remoteIcon.tooltip_text = `Controls ${name}`;
        const playing = player.playing;
        const title = player.title;
        this._remotePlay.icon_name = playing ? 'media-playback-pause-symbolic' : 'media-playback-start-symbolic';
        this._remotePlay.tooltip_text = `${playing ? 'Pause' : 'Play'} ${name}${title ? `\n${title}` : ''}`;
        this._remotePlay.sensitive = (playing ? player.get('CanPause') : player.get('CanPlay')) ?? true;
        this._remotePrevious.tooltip_text = `Previous in ${name}`;
        this._remotePrevious.sensitive = player.get('CanGoPrevious') ?? false;
        this._remoteNext.tooltip_text = `Next in ${name}`;
        this._remoteNext.sensitive = player.get('CanGoNext') ?? false;
    }

    _buildDisplaySwitch() {
        this._displaySwitch = new Adw.ToggleGroup({valign: Gtk.Align.CENTER, css_classes: ['round']});
        this._displaySwitch.add(new Adw.Toggle({name: 'wave', label: 'Wave', tooltip: 'Waveform (S)'}));
        this._displaySwitch.add(new Adw.Toggle({name: 'notes', label: 'Notes', tooltip: 'Sonogram (S)'}));
        return this._displaySwitch;
    }

    _buildSourcePicker() {
        const list = new Gtk.ListBox({
            selection_mode: Gtk.SelectionMode.NONE,
            css_classes: ['navigation-sidebar'],
            width_request: 320,
        });
        list.bind_model(this.engine.sources, source => this._sourceRow(source));
        list.connect('row-activated', (_list, row) => {
            this.engine.source = this.engine.sources.get_item(row.get_index());
            this._sourcePopover.popdown();
        });

        this._sourcePopover = new Gtk.Popover({
            child: new Gtk.ScrolledWindow({
                child: list,
                propagate_natural_height: true,
                propagate_natural_width: true,
                max_content_height: 480,
                hscrollbar_policy: Gtk.PolicyType.NEVER,
            }),
        });
        this._sourceIcon = new Gtk.Image();
        this._sourceLabel = new Gtk.Label({ellipsize: 3, max_width_chars: 28});
        const box = new Gtk.Box({spacing: 8});
        box.append(this._sourceIcon);
        box.append(this._sourceLabel);
        box.append(new Gtk.Image({icon_name: 'pan-down-symbolic'}));
        this._sourceButton = new Gtk.MenuButton({
            child: box,
            popover: this._sourcePopover,
            tooltip_text: 'Record from',
            css_classes: ['flat', 'source-button'],
        });
        return this._sourceButton;
    }

    _sourceRow(source) {
        const name = new Gtk.Label({xalign: 0, ellipsize: 3, max_width_chars: 40, css_classes: ['heading']});
        const detail = new Gtk.Label({xalign: 0, ellipsize: 3, max_width_chars: 48,
            css_classes: ['dim-label', 'caption']});
        const text = new Gtk.Box({orientation: Gtk.Orientation.VERTICAL, valign: Gtk.Align.CENTER, hexpand: true});
        text.append(name);
        text.append(detail);
        const playing = new Gtk.Image({icon_name: 'media-playback-start-symbolic', tooltip_text: 'Playing'});
        const chosen = new Gtk.Image({icon_name: 'object-select-symbolic'});

        const row = new Gtk.Box({spacing: 12, css_classes: ['source-row']});
        const icon = new Gtk.Image({pixel_size: 32});
        row.append(icon);
        row.append(text);
        row.append(playing);
        row.append(chosen);

        const update = () => {
            icon.icon_name = source.icon_name;
            name.label = source.name;
            detail.label = source.detail;
            detail.visible = source.detail !== '' && source.detail !== source.name;
            playing.visible = source.playing;
            chosen.visible = this.engine.source === source;
        };
        for (const prop of ['name', 'detail', 'icon-name', 'playing'])
            source.connect(`notify::${prop}`, update);
        this.engine.connect('notify::source', update);
        update();
        return row;
    }

    _buildTransport() {
        this._playButton = circularButton('media-playback-start-symbolic', 'Play (Space)');
        this._playButton.connect('clicked', () => this.engine.toggle_play());

        this._recordButton = circularButton('media-record-symbolic', 'Record (R)',
            ['record', 'destructive-action']);
        this._recordButton.connect('clicked', () => this._toggleRecord());

        this._loopButton = new Gtk.ToggleButton({
            icon_name: 'media-playlist-repeat-symbolic',
            tooltip_text: 'Loop (L)',
            valign: Gtk.Align.CENTER,
            focus_on_click: false,
            css_classes: ['circular'],
        });
        this.engine.bind_property('looping', this._loopButton, 'active',
            GObject.BindingFlags.BIDIRECTIONAL | GObject.BindingFlags.SYNC_CREATE);

        const buttons = new Gtk.Box({spacing: 24});
        buttons.append(this._playButton);
        buttons.append(this._recordButton);
        buttons.append(this._loopButton);

        this._timeLabel = new Gtk.Label({css_classes: ['numeric', 'title-4'], xalign: 0});
        this._loopLabel = new Gtk.Label({css_classes: ['numeric', 'dim-label'], xalign: 0});
        const info = new Gtk.Box({
            orientation: Gtk.Orientation.VERTICAL,
            valign: Gtk.Align.CENTER,
            width_request: 200,
        });
        info.append(this._timeLabel);
        info.append(this._loopLabel);

        this._adjustments = this._buildAdjustments();
        this._transport = new Gtk.CenterBox({
            start_widget: info,
            center_widget: buttons,
            end_widget: this._adjustments,
            css_classes: ['transport'],
        });
        // Narrow windows: speed and pitch get their own row.
        this._adjustmentRow = new Gtk.CenterBox({visible: false, css_classes: ['adjustment-row']});
        const narrow = new Adw.Breakpoint({condition: Adw.BreakpointCondition.parse('max-width: 1150px')});
        narrow.connect('apply', () => this._placeAdjustments(true));
        narrow.connect('unapply', () => this._placeAdjustments(false));
        this.add_breakpoint(narrow);

        const bar = new Gtk.Box({orientation: Gtk.Orientation.VERTICAL});
        bar.append(this._adjustmentRow);
        bar.append(this._transport);
        return bar;
    }

    _placeAdjustments(narrow) {
        this._transport.end_widget = null;
        this._adjustmentRow.center_widget = null;
        if (narrow)
            this._adjustmentRow.center_widget = this._adjustments;
        else
            this._transport.end_widget = this._adjustments;
        this._adjustmentRow.visible = narrow;
        this._adjustments.halign = narrow ? Gtk.Align.CENTER : Gtk.Align.END;
    }

    _buildAdjustments() {
        const box = new Gtk.Box({spacing: 16, halign: Gtk.Align.END});
        box.append(this._buildSpeed());
        box.append(this._buildPitch());
        return box;
    }

    _buildSpeed() {
        const presets = new Gtk.Box({spacing: 6, homogeneous: true});
        for (const speed of SPEED_PRESETS) {
            const button = new Gtk.Button({label: formatSpeed(speed), css_classes: ['speed-preset']});
            button.connect('clicked', () => {
                this.engine.speed = speed;
                this._speedPopover.popdown();
            });
            presets.append(button);
        }
        const scale = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL,
            Woodshed.ENGINE_MIN_SPEED, Woodshed.ENGINE_MAX_SPEED, SPEED_STEP);
        scale.round_digits = 2;
        for (const speed of [0.5, 1.0])
            scale.add_mark(speed, Gtk.PositionType.BOTTOM, formatSpeed(speed));
        this.engine.bind_property('speed', scale.adjustment, 'value',
            GObject.BindingFlags.BIDIRECTIONAL | GObject.BindingFlags.SYNC_CREATE);
        const popoverBox = new Gtk.Box({orientation: Gtk.Orientation.VERTICAL, spacing: 12, width_request: 320});
        popoverBox.append(presets);
        popoverBox.append(scale);
        this._speedPopover = new Gtk.Popover({child: popoverBox});

        this._speedButton = new Gtk.MenuButton({
            popover: this._speedPopover,
            tooltip_text: 'Speed (− / +)',
            valign: Gtk.Align.CENTER,
            css_classes: ['flat', 'value-button', 'numeric'],
        });

        const box = new Gtk.Box({spacing: 4});
        box.append(stepButton('list-remove-symbolic', 'Slower (−)', () => this._nudgeSpeed(-SPEED_STEP)));
        box.append(captioned(this._speedButton, 'Speed'));
        box.append(stepButton('list-add-symbolic', 'Faster (+)', () => this._nudgeSpeed(SPEED_STEP)));
        this.engine.connect('notify::speed', () => this._syncSpeed());
        this._syncSpeed();
        return box;
    }

    _buildPitch() {
        const fine = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL,
            -Woodshed.ENGINE_MAX_FINE_TUNE, Woodshed.ENGINE_MAX_FINE_TUNE, 1);
        fine.round_digits = 0;
        fine.add_mark(0, Gtk.PositionType.BOTTOM, '0');
        this.engine.bind_property('fine-tune', fine.adjustment, 'value',
            GObject.BindingFlags.BIDIRECTIONAL | GObject.BindingFlags.SYNC_CREATE);

        this._matchButton = new Gtk.Button({css_classes: ['pill']});
        this._matchButton.connect('clicked', () => this.engine.match_a440());
        const reset = new Gtk.Button({label: 'Reset', css_classes: ['pill']});
        reset.connect('clicked', () => {
            this.engine.transpose = 0;
            this.engine.fine_tune = 0;
        });
        const actions = new Gtk.Box({spacing: 6, homogeneous: true});
        actions.append(this._matchButton);
        actions.append(reset);

        const content = new Gtk.Box({orientation: Gtk.Orientation.VERTICAL, spacing: 12, width_request: 320});
        content.append(new Gtk.Label({label: 'Fine tune (cents)', xalign: 0, css_classes: ['heading']}));
        content.append(fine);
        content.append(actions);
        this._pitchButton = new Gtk.MenuButton({
            popover: new Gtk.Popover({child: content}),
            tooltip_text: 'Transpose (Page Up / Page Down)',
            valign: Gtk.Align.CENTER,
            css_classes: ['flat', 'value-button', 'numeric'],
        });

        const box = new Gtk.Box({spacing: 4});
        box.append(stepButton('list-remove-symbolic', 'Semitone down (Page Down)', () => this.engine.transpose--));
        box.append(captioned(this._pitchButton, 'Pitch'));
        box.append(stepButton('list-add-symbolic', 'Semitone up (Page Up)', () => this.engine.transpose++));
        for (const prop of ['transpose', 'fine-tune', 'tuning'])
            this.engine.connect(`notify::${prop}`, () => this._syncPitch());
        this._syncPitch();
        return box;
    }

    _syncPitch() {
        const {transpose, fine_tune: fine, tuning} = this.engine;
        this._pitchButton.label = formatPitch(transpose, fine);
        if (transpose !== 0 || Math.round(fine) !== 0)
            this._pitchButton.add_css_class('accent');
        else
            this._pitchButton.remove_css_class('accent');
        const hz = 440 * 2 ** (tuning / 1200);
        this._matchButton.label = tuning === 0 ? 'Match A440' : `Match A440 (now ${hz.toFixed(1)} Hz)`;
        this._matchButton.sensitive = tuning !== 0;
    }

    _nudgeSpeed(delta) {
        const speed = Math.round((this.engine.speed + delta) / SPEED_STEP) * SPEED_STEP;
        this.engine.speed = Math.round(speed * 100) / 100;
    }

    _syncSpeed() {
        const speed = this.engine.speed;
        this._speedButton.label = formatSpeed(speed);
        if (speed === 1.0)
            this._speedButton.remove_css_class('accent');
        else
            this._speedButton.add_css_class('accent');
    }

    _addShortcuts() {
        const shortcuts = new Gtk.ShortcutController({propagation_phase: Gtk.PropagationPhase.CAPTURE});
        const add = (accel, callback) => shortcuts.add_shortcut(new Gtk.Shortcut({
            trigger: Gtk.ShortcutTrigger.parse_string(accel),
            action: Gtk.CallbackAction.new(() => {
                callback();
                return true;
            }),
        }));
        add('r', () => this._toggleRecord());
        add('space', () => this.engine.toggle_play());
        add('l', () => (this.engine.looping = !this.engine.looping));
        add('Escape', () => this.engine.clear_loop());
        add('0', () => this._waveform.zoom_to_fit());
        add('Page_Up', () => this.engine.transpose++);
        add('Page_Down', () => this.engine.transpose--);
        for (const key of ['minus', 'KP_Subtract'])
            add(key, () => this._nudgeSpeed(-SPEED_STEP));
        for (const key of ['plus', 'equal', 'KP_Add'])
            add(key, () => this._nudgeSpeed(SPEED_STEP));
        add('s', () => (this._waveform.display = this._waveform.display === Woodshed.Display.WAVEFORM
            ? Woodshed.Display.SPECTROGRAM : Woodshed.Display.WAVEFORM));
        this.add_controller(shortcuts);
    }

    _syncTitle() {
        const file = this.engine.file;
        this.title = file ? `${GLib.path_get_basename(file).replace(/\.[^.]+$/, '')} – Woodshed` : 'Woodshed';
    }

    _sync() {
        const engine = this.engine;
        const recording = engine.state === State.RECORDING;
        const hasTake = engine.duration > 0;

        this._stack.visible_child_name = hasTake || recording ? 'take' : 'empty';

        this._recordButton.icon_name = recording ? 'media-playback-stop-symbolic' : 'media-record-symbolic';
        this._recordButton.tooltip_text = recording ? 'Stop (R)' : 'Record (R)';
        const playing = engine.state === State.PLAYING;
        this._playButton.icon_name = playing ? 'media-playback-pause-symbolic' : 'media-playback-start-symbolic';
        this._playButton.tooltip_text = playing ? 'Pause (Space)' : 'Play (Space)';
        this._playButton.sensitive = hasTake && !recording;
        this._loopButton.sensitive = hasTake && !recording;
        this._sourceButton.sensitive = !recording;

        const source = engine.source;
        this._sourceIcon.icon_name = source?.icon_name ?? 'media-playlist-shuffle-symbolic';
        this._sourceLabel.label = source?.name ?? 'Playing app';

        this._timeLabel.label = recording
            ? `● ${formatTime(engine.duration)}`
            : hasTake ? `${formatTime(engine.position)} / ${formatTime(engine.duration)}` : '';
        this._loopLabel.label = !hasTake || recording ? ''
            : engine.has_loop ? `${formatTime(engine.loop_start)} – ${formatTime(engine.loop_end)}`
                : 'Drag to select';
    }
});
