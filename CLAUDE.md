# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository shape

A workspace of independent Pebble smartwatch projects. Each top-level directory with a `wscript` is its own Pebble project (its own `package.json`, UUID, and `build/` output). There is no shared code between projects.

- `watchface-c/` — C watchapp (Pebble SDK). Time/date `TextLayer`s with a custom bundled font (`Jersey10`, declared twice in `package.json` as `FONT_JERSEY_56`/`FONT_JERSEY_24`), plus a battery bar drawn in a layer update proc. Builds for all platforms (aplite → gabbro), so use `PBL_IF_COLOR_ELSE` / `PBL_IF_ROUND_ELSE` / `PBL_IF_BW_ELSE` for platform differences. Note `watchapp.watchface` is `false`.
- `watchface/` — Pebble **Alloy** project (`"projectType": "moddable"`): the UI is embedded JavaScript running on Moddable XS (`src/embeddedjs/main.js`, drawing with `commodetto/Poco` on `watch`'s `minutechange` event). `src/c/mdbl.c` is only C glue that starts the Moddable machine. Alloy supports only `emery` and `gabbro`.
- `standup-reminder/` — C watchapp **plus background worker** (`worker_src/c/`, entry point is `int main(void)` with `<pebble_worker.h>`). The worker does the timing and can't vibrate, so it hands reminders to the app via `worker_launch_app()`. App and worker share `src/common/keys.h`. Clay settings use `@rebble/clay` (the original `pebble-clay` 1.0.4 is a Pebble package without flint/gabbro binaries and fails the build). No aplite (no health). Design and decisions: `stand-up-reminder-plan.md`.
- `src/pkjs/index.js` in each project is phone-side PebbleKit JS (bundled by `wscript`, entry file is fixed at `src/pkjs/index.js`).
- `stand-up-reminder-plan.md` — an approved design for a not-yet-created `standup-reminder/` sibling project (watchapp + background worker + Clay settings). Read it before working on that feature; it records decisions the user already made.

The `wscript` files are the stock SDK template: they compile `src/c/**/*.c`, automatically build a background worker if `worker_src/` exists, and bundle `src/pkjs/**`, `src/common/**/*.js`.

## Environment

The toolchain comes from a Nix flake (`flake.nix`, using `pebble-dev/pebble.nix`) loaded via direnv (`.envrc`, which also sets `PEBBLE_EMULATOR=emery`). Run commands inside it with `direnv exec . <cmd>` (or `nix develop`).

The flake wraps the `pebble` CLI so `HOME` is set to `<repo>/.local`; the SDK (version pinned in `flake.nix`, currently 4.33.1) is auto-installed there on first shell entry, under `.local/.pebble-sdk/`. Don't install the SDK globally or expect `~/.pebble-sdk`. Pixi was removed (commit f7955c6) — ignore the leftover `.pixi/` dir and the `pixi.lock` line in `.gitattributes`.

## Commands

Run from inside a project directory (e.g. `cd watchface-c`):

```sh
direnv exec . pebble build                       # build for all targetPlatforms
direnv exec . pebble install --emulator emery    # install to emulator
direnv exec . pebble install --phone <ip>        # install to a paired phone
direnv exec . pebble logs --emulator emery       # view app/worker logs
```

From the repo root, `./build_and_install.sh [relative/file/path]` builds and installs to `$PEBBLE_EMULATOR`. It picks the project from the first path component of the argument; with no argument it picks the only project, or the one whose `package.json` was modified most recently. It ends by `exec`-ing an interactive bash (it is designed for the Zed "Build and Install" task in `.zed/tasks.json`), so don't run it from a non-interactive agent shell — use `pebble build`/`pebble install` directly instead.

There are no tests or linters; verification means building and running in the emulator.

**Emulator gotcha:** the dev shell exports `LD_LIBRARY_PATH` pointing at an `alsa-lib` built for a newer glibc than `qemu-pebble`/SDL3, so emulator commands fail with `Failed loading SDL3 library` (even with `--vnc`, since audio uses SDL). Workaround: `direnv exec . env -u LD_LIBRARY_PATH pebble install --emulator emery` (same for `logs`, `screenshot`, `emu-*`, `send-app-message`). Each CLI connection resyncs the emulator clock to host time, so run `emu-set-time` *after* other commands.

## Editor / clangd

`.clangd` hardcodes absolute paths and per-project platform defines (`watchface-c` → basalt 144×168, `watchface` → emery 200×228), and includes headers from each project's `build/` dir. Run `pebble build` once before expecting clangd to resolve `pebble.h` and generated `RESOURCE_ID_*` / message-key symbols.
