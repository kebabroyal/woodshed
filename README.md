# Woodshed

<img src="data/icons/hicolor/scalable/apps/io.github.kebabroyal.Woodshed.svg" width="128" height="128" alt="">

Record what an app plays, then slow it down and loop it to transcribe it.
Made for jazz piano solos; works for anything.

- Records any app's audio (PipeWire), or the whole system, or a mic
- Waveform and a sonogram with piano keys
- Drag to select, seamless loops
- Slower or faster without changing pitch (Rubber Band)
- Transpose by semitones or cents; match a record to A440
- Plays and pauses the source app for you (MPRIS)
- Saves every take as FLAC with its settings, in Music/Woodshed

## Building

### GNOME Builder

Open the folder in Builder and press Run. It builds the Flatpak manifest in
`build-aux/flatpak/`.

### Flatpak

Needs `flatpak-builder` and the GNOME 50 SDK from Flathub:

```
flatpak install flathub org.gnome.Sdk//50 org.gnome.Platform//50
flatpak-builder --user --install --force-clean flatpak-build build-aux/flatpak/io.github.kebabroyal.Woodshed.json
flatpak run io.github.kebabroyal.Woodshed
```

The first build also builds FFTW, Rubber Band and nlohmann-json (a few minutes).

### From source

Dependencies on Fedora:

```
sudo dnf install meson gcc-c++ gettext desktop-file-utils appstream \
  gjs libadwaita gtk4-devel gobject-introspection-devel pipewire-devel \
  rubberband-devel fftw-devel libsndfile-devel json-devel catch2-devel
```

Catch2 must be v2. Skip it with `-Dtests=false`.

Build, test and run without installing:

```
meson setup build
meson compile -C build
meson test -C build
build/src/woodshed
```

`build/src/woodshed` runs as "Woodshed (dev)" (`io.github.kebabroyal.Woodshed.Devel`, striped
header) beside an installed copy. JavaScript runs from `src/`: edits need no rebuild.
To add it to the app menu: `meson compile -C build install-desktop`.

Install system-wide:

```
meson setup build --prefix=/usr
meson install -C build
```

## Layout

| Path | Contents |
| --- | --- |
| `src/` | GJS + libadwaita interface |
| `lib/` | `libwoodshed`: GObject C API, introspected as `Woodshed-0.1` |
| `lib/core/` | C++20 audio core: capture, playback, stretching, analysis. No GLib |
| `tests/` | Core unit tests (Catch2) |
| `data/` | Desktop file, metainfo, icons |
| `po/` | Translations |
| `build-aux/flatpak/` | Flatpak manifest |

## Translations

Add the language code to `po/LINGUAS`, then:

```
meson compile -C build woodshed-pot
msginit -i po/woodshed.pot -o po/LANG.po -l LANG
```

## Release

```
meson dist -C build
```

Writes `build/meson-dist/woodshed-VERSION.tar.xz` from the last commit. Bump the version in
`meson.build`, add a `<release>` to the metainfo and an entry to `NEWS` first.

## License

GPL-3.0-or-later. See `COPYING`.
