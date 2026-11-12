# To do

Open items before Woodshed is shared. Move them to the issue tracker once the repo is hosted.

## Before publishing

- [ ] Push to GitHub (`kebabroyal/Woodshed`).
- [ ] CI: build the Flatpak and run `meson test` on every push.
- [ ] Metainfo: screenshots, homepage, bug tracker, source URLs. Drop the `url-homepage-missing` override.
- [ ] Primary menu with Keyboard Shortcuts and About (`Adw.ShortcutsDialog`, `Adw.AboutDialog`).

## GNOME conventions

- [ ] GSettings schema: window size, maximized, last source.
- [ ] JS in a GResource, loaded by the launcher. Costs the no-rebuild dev loop.
- [ ] `-Dprofile=development` and a Devel Flatpak manifest (GNOME nightly), replacing the dev launcher's env var.
- [ ] Translatable engine errors: C++ exceptions surface as English toasts.
- [ ] User help in `help/` (Mallard).
- [ ] `CONTRIBUTING.md`, link to the GNOME Code of Conduct.
- [ ] Optional: interface in Blueprint or `.ui` files instead of built in JS.
