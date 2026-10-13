# Woodshed

Record what an app plays, then slow it down and loop it to transcribe it.
Made for jazz piano solos; works for anything.

- Records any app's audio (PipeWire), or the whole system, or a mic
- Waveform and a pitch view with piano keys
- Drag to select, seamless loops
- Slower or faster without changing pitch (Rubber Band)
- Transpose by semitones or cents; match a record to A440
- Plays and pauses the source app for you (MPRIS)
- Saves every take as FLAC with its settings

## Build

Needs Meson, GTK 4, libadwaita, GJS, gobject-introspection, PipeWire,
Rubber Band, FFTW (float), libsndfile and nlohmann-json. Catch2 v2 for tests.

```
meson setup build
meson compile -C build
meson test -C build
meson devenv -C build woodshed
```

Release tarball: `meson dist -C build`. Flatpak: see `build-aux/flatpak/`.

## License

GPL-2.0-or-later.
