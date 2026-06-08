# SplitKeyboard

A split-layout fork of [CoreKeyboard](https://gitlab.com/cubocore/coreapps/corekeyboard)
— a Qt6/X11 on-screen keyboard — for wide displays, built for and tested on the Steam
Deck. The keys are packed into the **left and right quarters** of the screen, leaving the middle empty and
**click-through** so the desktop behind stays usable. It targets general desktop typing
(full symbol and F-key coverage), not mobile text entry.

It installs as a **separate** flatpak (`online.ferose.SplitKeyboard`) alongside the stock
`org.cubocore.CoreKeyboard`, leaving the original untouched.

Highlights:

- **Two-thumb split** with a see-through, click-through middle.
- **Single 7-row page** — every key shifts via the real Shift, so there's no symbol page to toggle.
- **Hybrid modifiers** — tap to arm one for the next key, or hold it to type a chord; Caps is a classic lock.
- **Multitouch** — two thumbs at once.
- **Ghosted panel** — semi-transparent, so you can see what's behind it.
- **Global hotkey** — `Super+Ctrl+K` toggles the keyboard from anywhere; the tray icon and
  re-launching the app do too.

## Layout

Each row is a **30-column grid**: 6 keys in the left quarter, 18 invisible spacer columns
(the empty middle), 6 keys in the right quarter — a **single 7-row page** where the physical
Shift does all shifting. The grid below is the human-readable form of
`splitkeyboard/resources/en_US.keymap` (the source of truth); `||` marks the center split.

```
      C1   |   C2  |   C3  |   C4  |   C5  |   C6   ||    C7  |   C8  |   C9  |  C10  |  C11  |  C12
R1:   F1   |   F2  |   F3  |   F4  |   F5  |   F6   ||    F7  |   F8  |   F9  |  F10  |  F11  |  F12
R2:   `    |   [   |   ]   |  Home |   Lt  |   Dn   ||    Up  |   Rt  |  End  |   -   |   =   |  Hide
R3:  Esc   |   1   |   2   |   3   |   4   |   5    ||    6   |   7   |   8   |   9   |   0   |  Bksp
R4:  Tab   |   q   |   w   |   e   |   r   |   t    ||    y   |   u   |   i   |   o   |   p   | Enter
R5:  Caps  |   a   |   s   |   d   |   f   |   g    ||    h   |   j   |   k   |   l   |   ;   |   '
R6: Shift  | PrtScr|   z   |   x   |   c   |   v    ||    b   |   n   |   m   |   ,   |   /   | Shift
R7:  Ctrl  | Super |  Alt  | Space | Space |  Del   ||    \   | Space | Space |   .   |  RAlt |  RCtl
```

- **QWERTY hand split** between T\|Y, G\|H, and V\|B (`b` is right-hand).
- **Arrows** form an inline `Lt Dn Up Rt` strip across R2, split by the center hole.
- All 32 ASCII symbols fit on this one page with **no duplicates** — 11 typed directly, 21 from Shift.

https://github.com/user-attachments/assets/e292f76d-704a-4495-a14c-72851088572b

## Install

A signed flatpak repo is hosted on GitHub Pages — install and auto-update with one command:

```sh
flatpak install --user https://ferose.github.io/SplitKeyboard/splitkeyboard.flatpakref
```

It pulls the `org.kde.Platform` runtime from Flathub automatically and updates with the rest
of your apps (`flatpak update`). Or open the [install page](https://ferose.github.io/SplitKeyboard/)
and click through. Then see [Run](#run) below.

## Build from source

Requires `org.flatpak.Builder` and `org.kde.Sdk//6.10`.

```sh
flatpak install -y flathub org.flatpak.Builder org.kde.Sdk//6.10
flatpak run org.flatpak.Builder --force-clean --user --install \
  --install-deps-from=flathub --ccache builddir \
  online.ferose.SplitKeyboard.yml
```

## Run

```sh
flatpak run online.ferose.SplitKeyboard -platform xcb
```

`-platform xcb` forces X11, which the input and window-masking code requires. Or launch
**SplitKeyboard** from the application menu.

> **X11 only.** The keystroke injection (`XTest`) and global hotkey (`XGrabKey`) are X11
> mechanisms. On a Wayland session the app runs through XWayland but **cannot type into
> native Wayland windows**, so use it on an X11 desktop — e.g. the Steam Deck's desktop
> mode (KDE Plasma X11).

## Configure

Settings live in a `settings.conf` under `[SplitKeyboard]`. For the flatpak that's the
sandbox-redirected config dir,
`~/.var/app/online.ferose.SplitKeyboard/config/SplitKeyboard/settings.conf` (a from-source
build uses `~/.config/SplitKeyboard/settings.conf`):

- `Mode=true` — full-width bar docked at the bottom.
- `Mode=false` + `WindowMode`/`WindowSize` — floating resizable window.

## Development

SplitKeyboard is a **vendored fork**: the source (originally CoreKeyboard) lives in
[`splitkeyboard/`](splitkeyboard/) (forked from commit `454e241`) and our changes are edited
straight into it — no patch files. Edit `splitkeyboard/src/` for behavior and
`splitkeyboard/resources/en_US.keymap` for the layout (keep the [Layout](#layout) grid in
sync), then rebuild the flatpak. It depends only on **Qt 6 and X11** — no external
libraries — and is built via the flatpak manifest. The `KeyEngine` modifier logic
is unit-tested via `./tests/run.sh`; everything else is validated hands-on. The source is
heavily commented — `splitkeyboard.cpp` documents the layout/mask engine and the keymap
encoding, and `keyengine.{h,cpp}` the modifier state machine.

## License

GPL-3.0-or-later — see [`LICENSE`](LICENSE). SplitKeyboard is a fork of CoreKeyboard
(© CuboCore Group); the original GPL headers are preserved throughout the source.
