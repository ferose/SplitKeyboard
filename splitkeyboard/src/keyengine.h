/*
 * This file is part of SplitKeyboard (a fork of CoreKeyboard).
 * An on-screen keyboard for X11.
 * Copyright 2019 CuboCore Group
 * Fork modifications Copyright 2026 Ferose
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see {http://www.gnu.org/licenses/}.
 */

#pragma once

#include <set>
#include <functional>

/* The modifier state machine, pulled out of the keyboard widget so it can be unit-tested
 * without Qt or X11. It owns the modifier config + live state and emits real key events
 * (keycode + up/down) through an injectable sink: the app wires the sink to
 * XTestFakeKeyEvent, the tests wire it to a recorder.
 *
 * Behavior (matches the on-screen keyboard):
 *  - Modifiers (Ctrl/Shift/Alt/Super) are armed one-shot on a quick tap (held for the next
 *    key, then auto-cleared; tap again to disarm) and momentary when physically held while
 *    another key is pressed. Taps accumulate so chords (Ctrl+Alt+Del) work.
 *  - Caps is a classic lock: each tap toggles it (a press+release of the Caps keycode, which
 *    X latches on the press edge) and flips capsOn for the renderer.
 *  - press()/release() take an *encoded* keymap value; realCode() decodes the 1xxx fat-key
 *    form. Pseudo-keys like Hide (777) are handled by the caller, not here. */
class KeyEngine {
public:
	using Sink = std::function<void(int keycode, bool down)>;

	explicit KeyEngine(Sink sink = {}) : mSink(std::move(sink)) {}
	void setSink(Sink sink) { mSink = std::move(sink); }

	/* Input config, filled from the keymap. */
	std::set<int> modifiers;        // Ctrl/Shift/Alt/Super keycodes
	int ctrl = 0, shift = 0, caps = 0;

	/* Live state, read by the renderer for highlighting and shifted labels. */
	std::set<int> modPress;         // armed one-shot modifiers
	std::set<int> heldMods;         // physically held, sticky-vs-momentary still pending
	std::set<int> usedMods;         // held *and* combined with a normal key (a real chord)
	bool capsOn = false;

	/* Decode an encoded keymap value (1xxx "fat" key) to its real X11 keycode. */
	static int realCode(int key);

	bool isModifier(int realKey) const { return modifiers.count(realKey) != 0; }
	bool modDown(int realKey) const;    // armed or physically held

	void press(int key);
	void release(int key);

private:
	Sink mSink;   /* not named `emit` -- that's a Qt keyword macro in the app build */
	void fire(int keycode, bool down) { if (mSink) mSink(keycode, down); }
};
